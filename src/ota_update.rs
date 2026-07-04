use sha2::{Digest, Sha256};

use crate::ota_manifest::Sha256Digest;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OtaWriteError {
    TooMuchData,
    WriterFailed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OtaFinishError {
    SizeMismatch,
    HashMismatch,
}

pub trait OtaImageWriter {
    fn write_chunk(&mut self, offset: u32, chunk: &[u8]) -> Result<(), OtaWriteError>;
}

pub struct OtaDownload<W> {
    writer: W,
    expected_size: u32,
    expected_sha256: Sha256Digest,
    written: u32,
    hasher: Sha256,
}

impl<W: OtaImageWriter> OtaDownload<W> {
    pub fn new(writer: W, expected_size: u32, expected_sha256: Sha256Digest) -> Self {
        Self {
            writer,
            expected_size,
            expected_sha256,
            written: 0,
            hasher: Sha256::new(),
        }
    }

    pub fn push_chunk(&mut self, chunk: &[u8]) -> Result<(), OtaWriteError> {
        let next_written = self
            .written
            .checked_add(chunk.len() as u32)
            .ok_or(OtaWriteError::TooMuchData)?;
        if next_written > self.expected_size {
            return Err(OtaWriteError::TooMuchData);
        }

        self.writer.write_chunk(self.written, chunk)?;
        self.hasher.update(chunk);
        self.written = next_written;
        Ok(())
    }

    pub fn finish(self) -> Result<W, OtaFinishError> {
        if self.written != self.expected_size {
            return Err(OtaFinishError::SizeMismatch);
        }

        let digest = self.hasher.finalize();
        if digest.as_slice() != self.expected_sha256.bytes {
            return Err(OtaFinishError::HashMismatch);
        }

        Ok(self.writer)
    }

    pub const fn written(&self) -> u32 {
        self.written
    }

    pub const fn expected_size(&self) -> u32 {
        self.expected_size
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug)]
    struct BufferWriter {
        bytes: [u8; 64],
        len: usize,
    }

    impl BufferWriter {
        fn new() -> Self {
            Self {
                bytes: [0; 64],
                len: 0,
            }
        }
    }

    impl OtaImageWriter for BufferWriter {
        fn write_chunk(&mut self, offset: u32, chunk: &[u8]) -> Result<(), OtaWriteError> {
            let offset = offset as usize;
            let end = offset + chunk.len();
            if end > self.bytes.len() || offset != self.len {
                return Err(OtaWriteError::WriterFailed);
            }
            self.bytes[offset..end].copy_from_slice(chunk);
            self.len = end;
            Ok(())
        }
    }

    fn digest(bytes: &[u8]) -> Sha256Digest {
        let mut out = [0_u8; 32];
        out.copy_from_slice(&Sha256::digest(bytes));
        Sha256Digest { bytes: out }
    }

    #[test]
    fn streams_chunks_and_verifies_sha256() {
        let expected = digest(b"firmware-image");
        let mut download = OtaDownload::new(BufferWriter::new(), 14, expected);

        download.push_chunk(b"firmware").unwrap();
        download.push_chunk(b"-image").unwrap();
        assert_eq!(download.written(), 14);

        let writer = download.finish().unwrap();
        assert_eq!(&writer.bytes[..writer.len], b"firmware-image");
    }

    #[test]
    fn rejects_oversize_or_bad_hash() {
        let expected = digest(b"abc");
        let mut download = OtaDownload::new(BufferWriter::new(), 3, expected);

        assert_eq!(
            download.push_chunk(b"abcd"),
            Err(OtaWriteError::TooMuchData)
        );

        let wrong = digest(b"wrong");
        let mut download = OtaDownload::new(BufferWriter::new(), 3, wrong);
        download.push_chunk(b"abc").unwrap();
        assert_eq!(download.finish().unwrap_err(), OtaFinishError::HashMismatch);
    }

    #[test]
    fn rejects_incomplete_download() {
        let expected = digest(b"abc");
        let mut download = OtaDownload::new(BufferWriter::new(), 3, expected);

        download.push_chunk(b"ab").unwrap();

        assert_eq!(download.finish().unwrap_err(), OtaFinishError::SizeMismatch);
    }
}
