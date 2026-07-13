package com.fuckingbms.cast

import java.nio.ByteBuffer
import java.nio.ByteOrder

object CastProtocol {
    const val VERSION = 1
    const val MAX_BLOCK_SIDE = 16
    const val FRAME_BEGIN: Byte = 1
    const val RGB565_BLOCK: Byte = 2
    const val FRAME_END: Byte = 3
    const val HEARTBEAT: Byte = 4
    const val ACK: Byte = 0x81.toByte()

    fun frameBegin(sequence: Int, rotation: Int): ByteArray {
        require(rotation in 0..3)
        return ByteBuffer.allocate(7).order(ByteOrder.BIG_ENDIAN)
            .put(FRAME_BEGIN).put(VERSION.toByte()).putInt(sequence).put(rotation.toByte()).array()
    }
    fun frameEnd(sequence: Int): ByteArray = ByteBuffer.allocate(5).order(ByteOrder.BIG_ENDIAN)
        .put(FRAME_END).putInt(sequence).array()
    fun block(x: Int, y: Int, width: Int, height: Int, rgb565: ByteArray): ByteArray {
        require(width in 1..MAX_BLOCK_SIDE && height in 1..MAX_BLOCK_SIDE && rgb565.size == width * height * 2)
        require(x >= 0 && y >= 0)
        return ByteBuffer.allocate(7 + rgb565.size).order(ByteOrder.BIG_ENDIAN).put(RGB565_BLOCK)
            .putShort(x.toShort()).putShort(y.toShort()).put(width.toByte()).put(height.toByte()).put(rgb565).array()
    }

    fun ackSequence(packet: ByteArray): Int? = if (packet.size == 5 && packet[0] == ACK)
        ByteBuffer.wrap(packet, 1, 4).order(ByteOrder.BIG_ENDIAN).int else null
}
