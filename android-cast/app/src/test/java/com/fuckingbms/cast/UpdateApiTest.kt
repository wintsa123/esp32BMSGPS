package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdateApiTest {
    @Test fun bothSourcesKeepTheirDownloadPrefixAndFailuresAreNotLatest() {
        val requested = mutableListOf<String>()
        val body = """{"versionCode":4,"versionName":"0.4.0","url":"/apk/app.apk"}"""
        for (source in UpdateApi.Source.entries) {
            val release = UpdateApi.checkApkUpdate(source) { url -> requested += url; body }!!
            assertEquals(source.base + "apk/latest.json", requested.last())
            assertEquals(source.base + "apk/app.apk", release.url)
        }
        requested.clear()
        val fallback = UpdateApi.checkApkUpdate { url ->
            requested += url
            if (url.startsWith(UpdateApi.BASE_URL)) throw java.io.IOException("offline")
            body
        }!!
        assertEquals(2, requested.size)
        assertTrue(fallback.url.startsWith(UpdateApi.Source.GITHUB.base))
        try { UpdateApi.checkApkUpdate { throw java.io.IOException("offline") }; throw AssertionError("network failure reported as current") }
        catch (_: java.io.IOException) { }
        for (value in listOf("javascript:x", "http://bad.test/app.apk", "https://user@bad.test/app.apk")) {
            org.junit.Assert.assertThrows(Exception::class.java) { UpdateApi.releaseUrl(value, UpdateApi.Source.GITHUB.base + "apk/latest.json") }
        }
    }

    @Test fun firmwareDownloadsUseSelectedSourceAndInvalidCatalogFails() {
        val body = """{"firmwares":[{"profile":"esp32","url":"/firmware/esp32.bin"}]}"""
        for (source in UpdateApi.Source.entries) {
            assertEquals(source.base + "firmware/esp32.bin", UpdateApi.fetchFirmwareList(source) { body }.single().url)
        }
        try { UpdateApi.fetchFirmwareList(UpdateApi.Source.CF) { "{}" }; throw AssertionError("invalid catalog accepted") }
        catch (_: java.io.IOException) { }
    }

    @Test fun parsesApkReleaseManifest() {
        val release = UpdateApi.parseApkRelease(
            """{"versionCode":2,"versionName":"0.2.0","url":"/apk/两轮智控.apk","size":3934388,"note":"最新版本，建议升级","publishedAt":"2026-08-22T00:00:00+08:00"}""",
        )
        assertEquals(2, release?.versionCode)
        assertEquals("0.2.0", release?.versionName)
        assertEquals("/apk/两轮智控.apk", release?.url)
        assertEquals(3934388L, release?.size)
        assertEquals("最新版本，建议升级", release?.note)
    }

    @Test fun rejectsApkManifestWithoutUrl() {
        assertNull(UpdateApi.parseApkRelease("""{"versionCode":2,"versionName":"0.2.0","url":""}"""))
    }

    @Test fun parsesOnlineFirmwareList() {
        val firmwares = UpdateApi.parseFirmwareList(
            """{"updatedAt":"2026-08-22T00:00:00+08:00","firmwares":[
                {"profile":"esp32","name":"ESP32 精简版","chip":"esp32","version":"dev2","url":"/firmware/esp32.bin","size":1821392,"code":"1234","note":"仪表：controller、fireblade"},
                {"profile":"esp32-full","name":"ESP32 完整版","chip":"esp32","version":"dev","url":"/firmware/esp32-full.bin","size":1933312,"code":"5678","note":""}
            ]}""",
        )
        assertEquals(2, firmwares.size)
        assertEquals("esp32", firmwares[0].profile)
        assertEquals("ESP32 精简版", firmwares[0].name)
        assertEquals("dev2", firmwares[0].version)
        assertEquals("1234", firmwares[0].code)
        assertTrue(firmwares[0].description.contains("ESP32 精简版"))
        assertTrue(firmwares[0].description.contains("vdev2"))
        assertTrue(firmwares[0].description.contains("1778 KB"))
        assertEquals("5678", firmwares[1].code)
    }

    @Test fun skipsFirmwareEntriesWithoutProfileOrUrl() {
        val firmwares = UpdateApi.parseFirmwareList(
            """{"firmwares":[
                {"name":"no profile","url":"/firmware/x.bin"},
                {"profile":"p","url":""},
                {"profile":"valid","url":"/firmware/valid.bin","code":"0000"}
            ]}""",
        )
        assertEquals(1, firmwares.size)
        assertEquals("valid", firmwares[0].profile)
        assertEquals("0000", firmwares[0].code)
    }

    @Test fun firmwareDescriptionFallsBackToProfileName() {
        val firmware = UpdateApi.parseFirmwareList(
            """{"firmwares":[{"profile":"custom","url":"/firmware/custom.bin"}]}""",
        ).first()
        assertEquals("custom", firmware.name)
    }
}
