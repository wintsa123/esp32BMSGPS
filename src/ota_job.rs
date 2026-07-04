use crate::{
    app_state::{AppState, OtaState},
    ota_manifest::{ManifestError, OtaManifest, Sha256Digest, parse_manifest},
    settings::{FixedAscii, FixedTextError},
};

pub const MAX_OTA_URL_LEN: usize = 192;
pub const MAX_OTA_VERSION_LEN: usize = 32;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OtaUpdatePlan {
    pub latest: FixedAscii<MAX_OTA_VERSION_LEN>,
    pub firmware_url: FixedAscii<MAX_OTA_URL_LEN>,
    pub sha256: Sha256Digest,
    pub size: u32,
}

impl OtaUpdatePlan {
    pub fn from_manifest(manifest: OtaManifest<'_>) -> Result<Self, FixedTextError> {
        Ok(Self {
            latest: FixedAscii::try_from_str(manifest.latest)?,
            firmware_url: FixedAscii::try_from_str(manifest.firmware_url)?,
            sha256: manifest.sha256,
            size: manifest.size,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OtaJobCommand {
    FetchManifest { url: FixedAscii<MAX_OTA_URL_LEN> },
    DownloadFirmware(OtaUpdatePlan),
    SwitchAndReboot,
    None,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OtaJobError {
    Manifest(ManifestError),
    CurrentVersionUnsupported,
    Text(FixedTextError),
}

impl From<FixedTextError> for OtaJobError {
    fn from(value: FixedTextError) -> Self {
        Self::Text(value)
    }
}

impl From<ManifestError> for OtaJobError {
    fn from(value: ManifestError) -> Self {
        Self::Manifest(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OtaJob {
    pub phase: OtaState,
    pub plan: Option<OtaUpdatePlan>,
    start_after_check: bool,
}

impl OtaJob {
    pub const fn new() -> Self {
        Self {
            phase: OtaState::Idle,
            plan: None,
            start_after_check: false,
        }
    }

    pub fn request_check(
        &mut self,
        app_state: &mut AppState,
        manifest_url: &str,
    ) -> Result<OtaJobCommand, OtaJobError> {
        self.start_after_check = false;
        self.phase = OtaState::Checking;
        app_state.ota = OtaState::Checking;
        Ok(OtaJobCommand::FetchManifest {
            url: FixedAscii::try_from_str(manifest_url)?,
        })
    }

    pub fn request_start(
        &mut self,
        app_state: &mut AppState,
        manifest_url: &str,
    ) -> Result<OtaJobCommand, OtaJobError> {
        if self.plan.is_some() {
            return Ok(self.begin_download(app_state));
        }

        self.start_after_check = true;
        self.phase = OtaState::Checking;
        app_state.ota = OtaState::Checking;
        Ok(OtaJobCommand::FetchManifest {
            url: FixedAscii::try_from_str(manifest_url)?,
        })
    }

    pub fn on_manifest(
        &mut self,
        app_state: &mut AppState,
        current_version: &str,
        json: &str,
    ) -> Result<OtaJobCommand, OtaJobError> {
        let manifest = parse_manifest(json)?;

        if !manifest.supports_current(current_version) {
            self.fail(app_state);
            return Err(OtaJobError::CurrentVersionUnsupported);
        }

        if !manifest.update_available(current_version) {
            self.phase = OtaState::Idle;
            self.plan = None;
            self.start_after_check = false;
            app_state.ota = OtaState::Idle;
            return Ok(OtaJobCommand::None);
        }

        self.plan = Some(OtaUpdatePlan::from_manifest(manifest)?);
        if self.start_after_check {
            self.start_after_check = false;
            Ok(self.begin_download(app_state))
        } else {
            self.phase = OtaState::UpdateAvailable;
            app_state.ota = OtaState::UpdateAvailable;
            Ok(OtaJobCommand::None)
        }
    }

    pub fn mark_verifying(&mut self, app_state: &mut AppState) {
        self.phase = OtaState::Verifying;
        app_state.ota = OtaState::Verifying;
    }

    pub fn mark_verified(&mut self, app_state: &mut AppState) -> OtaJobCommand {
        self.phase = OtaState::ReadyToReboot;
        self.start_after_check = false;
        app_state.ota = OtaState::ReadyToReboot;
        OtaJobCommand::SwitchAndReboot
    }

    pub fn mark_failed(&mut self, app_state: &mut AppState) {
        self.fail(app_state);
    }

    fn begin_download(&mut self, app_state: &mut AppState) -> OtaJobCommand {
        self.phase = OtaState::Downloading;
        app_state.ota = OtaState::Downloading;
        match self.plan {
            Some(plan) => OtaJobCommand::DownloadFirmware(plan),
            None => OtaJobCommand::None,
        }
    }

    fn fail(&mut self, app_state: &mut AppState) {
        self.phase = OtaState::Failed;
        self.start_after_check = false;
        app_state.ota = OtaState::Failed;
    }
}

impl Default for OtaJob {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MANIFEST_URL: &str = "https://example.com/manifest.json";
    const UPDATE_MANIFEST: &str = r#"{
      "latest": "0.2.0",
      "min_supported": "0.1.0",
      "firmware_url": "https://example.com/firmware.bin",
      "sha256": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
      "size": 12,
      "notes": "test"
    }"#;

    #[test]
    fn check_fetches_manifest_and_stores_available_update() {
        let mut app_state = AppState::default();
        let mut job = OtaJob::new();

        assert_eq!(
            job.request_check(&mut app_state, MANIFEST_URL),
            Ok(OtaJobCommand::FetchManifest {
                url: FixedAscii::try_from_str(MANIFEST_URL).unwrap()
            })
        );
        assert_eq!(app_state.ota, OtaState::Checking);

        assert_eq!(
            job.on_manifest(&mut app_state, "0.1.0", UPDATE_MANIFEST),
            Ok(OtaJobCommand::None)
        );

        assert_eq!(app_state.ota, OtaState::UpdateAvailable);
        assert_eq!(job.phase, OtaState::UpdateAvailable);
        assert_eq!(job.plan.unwrap().latest.as_str(), "0.2.0");
    }

    #[test]
    fn start_after_check_downloads_when_update_exists() {
        let mut app_state = AppState::default();
        let mut job = OtaJob::new();

        assert!(matches!(
            job.request_start(&mut app_state, MANIFEST_URL),
            Ok(OtaJobCommand::FetchManifest { .. })
        ));

        let command = job
            .on_manifest(&mut app_state, "0.1.0", UPDATE_MANIFEST)
            .unwrap();
        let OtaJobCommand::DownloadFirmware(plan) = command else {
            panic!("expected download command");
        };

        assert_eq!(app_state.ota, OtaState::Downloading);
        assert_eq!(
            plan.firmware_url.as_str(),
            "https://example.com/firmware.bin"
        );

        job.mark_verifying(&mut app_state);
        assert_eq!(app_state.ota, OtaState::Verifying);
        assert_eq!(
            job.mark_verified(&mut app_state),
            OtaJobCommand::SwitchAndReboot
        );
        assert_eq!(app_state.ota, OtaState::ReadyToReboot);
    }

    #[test]
    fn same_version_returns_to_idle_without_download_plan() {
        let mut app_state = AppState::default();
        let mut job = OtaJob::new();

        job.request_check(&mut app_state, MANIFEST_URL).unwrap();

        assert_eq!(
            job.on_manifest(&mut app_state, "0.2.0", UPDATE_MANIFEST),
            Ok(OtaJobCommand::None)
        );
        assert_eq!(app_state.ota, OtaState::Idle);
        assert_eq!(job.plan, None);
    }

    #[test]
    fn unsupported_current_version_fails_job() {
        let mut app_state = AppState::default();
        let mut job = OtaJob::new();

        assert_eq!(
            job.on_manifest(&mut app_state, "0.0.9", UPDATE_MANIFEST),
            Err(OtaJobError::CurrentVersionUnsupported)
        );
        assert_eq!(app_state.ota, OtaState::Failed);
    }
}
