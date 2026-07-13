package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CastProtocolTest {
    @Test fun frameBeginContainsRotation() { val p = CastProtocol.frameBegin(7, 3); assertEquals(7, p.size); assertEquals(3, p[6].toInt()) }
    @Test fun blocksRespectEdgesAndBaseline() {
        val encoder = FrameEncoder(17, 17); val pixels = ByteArray(17 * 17 * 2)
        assertEquals(6, encoder.encode(1, 0, pixels).size) // begin + 4 blocks + end
        encoder.acknowledge(pixels)
        assertEquals(2, encoder.encode(2, 0, pixels).size)
        pixels[pixels.lastIndex] = 1
        assertEquals(3, encoder.encode(3, 0, pixels).size)
    }
    @Test fun parsesOnlyValidAck() { assertEquals(42, CastProtocol.ackSequence(byteArrayOf(0x81.toByte(), 0, 0, 0, 42))); assertNull(CastProtocol.ackSequence(byteArrayOf(0x81.toByte()))) }
}
