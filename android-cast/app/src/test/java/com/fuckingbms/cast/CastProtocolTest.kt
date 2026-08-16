package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CastProtocolTest {
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
}
