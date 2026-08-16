package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class CastCapabilitiesTest {
    @Test fun parsesDeviceCapabilitiesAndChoosesLogicalDirection() {
        val info = CastInfo.parse(
            """
                {
                  "protocol_version": 3,
                  "physical_width": 320,
                  "physical_height": 480,
                  "codec": "jpeg",
                  "jpeg_quality": 80,
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
        assertEquals(262_144, info.maxFrameBytes)
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
          "jpeg_quality": 80,
          "target_fps": 20,
          "max_frame_bytes": 262144,
          "orientations": [
            {"rotation": 0, "width": 320, "height": 480},
            {"rotation": 1, "width": 480, "height": 320}
          ]
        }
    """.trimIndent()
}
