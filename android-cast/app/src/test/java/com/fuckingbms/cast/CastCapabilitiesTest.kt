package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class CastCapabilitiesTest {
    @Test fun cropRectMapsSystemInsetsAndFallsBackForInvalidValues() {
        assertEquals(
            CaptureRect(0, 100, 1000, 900),
            cropRectFor(1000, 1000, 1000, 1000, CaptureInsets(0, 100, 0, 100)),
        )
        assertEquals(
            CaptureRect(0, 0, 1000, 1000),
            cropRectFor(1000, 1000, 1000, 1000, CaptureInsets(600, 0, 500, 0)),
        )
    }

    @Test fun parsesDeviceCapabilitiesAndChoosesLogicalDirection() {
        val info = CastInfo.parse(
            """
                {
                  "protocol_version": 3,
                  "physical_width": 320,
                  "physical_height": 480,
                  "codec": "jpeg",
                  "jpeg_quality": 60,
                  "target_fps": 20,
                  "max_frame_bytes": 262144,
                  "orientations": [
                    {"rotation": 0, "width": 320, "height": 480},
                    {"rotation": 1, "width": 480, "height": 320}
                  ],
                  "active": false
                }
            """.trimIndent(),
        )

        assertEquals(CastTarget(1, 480, 320), info.targetFor(2400, 1080))
        assertEquals(CastTarget(0, 320, 480), info.targetFor(1080, 2400))
        assertEquals(60, info.jpegQuality)
        assertEquals(CastProtocol.DEFAULT_JPEG_QUALITY, info.jpegQuality)
        assertEquals(262_144, info.maxFrameBytes)
    }

    @Test fun captureSizeCoversTargetAtSourceAspectRatio() {
        assertEquals(712 to 320, captureSizeFor(2400, 1080, CastTarget(1, 480, 320)))
        assertEquals(320 to 712, captureSizeFor(1080, 2400, CastTarget(0, 320, 480)))
        assertEquals(270 to 480, captureSizeFor(1080, 1920, CastTarget(0, 270, 480)))
    }

    @Test fun rejectsV2Capabilities() {
        assertThrows(IllegalArgumentException::class.java) {
            CastInfo.parse(validJson().replace("\"protocol_version\": 3", "\"protocol_version\": 2"))
        }
    }

    @Test fun rejectsNonJpegV3Capabilities() {
        assertThrows(IllegalArgumentException::class.java) {
            CastInfo.parse(validJson().replace("\"codec\": \"jpeg\"", "\"codec\": \"rgb565\""))
        }
    }

    private fun validJson() = """
        {
          "protocol_version": 3,
          "physical_width": 320,
          "physical_height": 480,
          "codec": "jpeg",
          "jpeg_quality": 60,
          "target_fps": 20,
          "max_frame_bytes": 262144,
          "orientations": [
            {"rotation": 0, "width": 320, "height": 480},
            {"rotation": 1, "width": 480, "height": 320}
          ]
        }
    """.trimIndent()
}
