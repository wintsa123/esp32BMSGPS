package com.fuckingbms.cast

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DeviceBleSessionTest {
    @Test fun fragmentsRequestsAndReassemblesResponses() {
        val payload = ByteArray(50) { it.toByte() }
        val fragments = bleRequestFragments(payload, 23)

        assertEquals(3, fragments.size)
        assertEquals(1, fragments.first()[0].toInt())
        assertEquals(2, fragments.last()[0].toInt())

        val assembler = BleResponseAssembler()
        assertNull(assembler.accept(byteArrayOf(1) + "hello ".toByteArray()))
        assertArrayEquals("hello world".toByteArray(), assembler.accept(byteArrayOf(2) + "world".toByteArray()))
    }
    @Test fun acceptsFullSettingsManifestAndRejectsOversize() {
        val payload = ByteArray(6144) { 65 }
        val assembler = BleResponseAssembler()
        assertNull(assembler.accept(byteArrayOf(1) + payload.copyOfRange(0, 3072)))
        assertArrayEquals(payload, assembler.accept(byteArrayOf(2) + payload.copyOfRange(3072, 6144)))
        org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            assembler.accept(byteArrayOf(3) + ByteArray(8193))
        }
    }
}
