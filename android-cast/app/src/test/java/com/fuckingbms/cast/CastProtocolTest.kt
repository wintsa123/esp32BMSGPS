package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CastProtocolTest {
    @Test fun parsesDeviceExitRequest() {
        assertTrue(
            CastProtocol.exitRequest(
                byteArrayOf(CastProtocol.EXIT_CAST, CastProtocol.VERSION.toByte()),
            ),
        )
        assertFalse(
            CastProtocol.exitRequest(
                byteArrayOf(CastProtocol.EXIT_CAST, (CastProtocol.VERSION - 1).toByte()),
            ),
        )
        assertFalse(CastProtocol.exitRequest(byteArrayOf(CastProtocol.EXIT_CAST)))
    }

    @Test fun jpegFrameContainsHeaderAndPayload() {
        val packet = CastProtocol.jpegFrame(7, 3, byteArrayOf(0xff.toByte(), 0xd8.toByte()))
        assertEquals(9, packet.size)
        assertEquals(CastProtocol.JPEG_FRAME, packet[0])
        assertEquals(CastProtocol.VERSION, packet[1].toInt())
        assertEquals(3, packet[6].toInt())
        assertEquals(0xff.toByte(), packet[7])
        assertEquals(0xd8.toByte(), packet[8])
    }

    @Test fun maskedWebSocketLengthSupportsLargeJpeg() {
        assertEquals(
            listOf(0xff, 0, 0, 0, 0, 0, 1, 0x11, 0x70),
            CastProtocol.maskedPayloadLength(70_000).map { it.toInt() and 0xff },
        )
    }

    @Test fun parsesOnlyValidAck() {
        assertEquals(42, CastProtocol.ackSequence(byteArrayOf(0x81.toByte(), 0, 0, 0, 42)))
        assertNull(CastProtocol.ackSequence(byteArrayOf(0x81.toByte())))
    }

    @Test fun parsesTouchInputEvent() {
        val event = CastProtocol.inputEvent(
            byteArrayOf(
                CastProtocol.INPUT_TOUCH,
                CastProtocol.VERSION.toByte(),
                CastProtocol.INPUT_PHASE_MOVE.toByte(),
                0x01,
                0x2c,
                0x00,
                0x40,
            ),
        )
        assertEquals(CastProtocol.INPUT_PHASE_MOVE, event?.phase)
        assertEquals(300, event?.x)
        assertEquals(64, event?.y)
    }

    @Test fun rejectsMalformedInputEvents() {
        assertNull(CastProtocol.inputEvent(byteArrayOf(0x81.toByte(), 0, 0, 0, 42)))
        assertNull(
            CastProtocol.inputEvent(
                byteArrayOf(
                    CastProtocol.INPUT_TOUCH,
                    CastProtocol.VERSION.toByte(),
                    9,
                    0,
                    0,
                    0,
                    0,
                ),
            ),
        )
        assertNull(
            CastProtocol.inputEvent(
                byteArrayOf(
                    CastProtocol.INPUT_TOUCH,
                    (CastProtocol.VERSION - 1).toByte(),
                    CastProtocol.INPUT_PHASE_DOWN.toByte(),
                    0,
                    0,
                    0,
                    0,
                ),
            ),
        )
    }
}
