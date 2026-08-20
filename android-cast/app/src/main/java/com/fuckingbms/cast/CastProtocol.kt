package com.fuckingbms.cast

import java.nio.ByteBuffer
import java.nio.ByteOrder

object CastProtocol {
    const val VERSION = 3
    const val MAX_FRAME_BYTES = 262_144
    const val DEFAULT_JPEG_QUALITY = 80
    const val FALLBACK_JPEG_QUALITY = 80
    const val TARGET_FPS = 20
    const val FRAME_HEADER_BYTES = 7
    const val JPEG_FRAME: Byte = 1
    const val HEARTBEAT: Byte = 4
    const val ACK: Byte = 0x81.toByte()

    fun jpegFrame(sequence: Int, rotation: Int, jpeg: ByteArray): ByteArray {
        require(rotation in 0..3)
        require(jpeg.isNotEmpty() && jpeg.size <= MAX_FRAME_BYTES)
        return ByteBuffer.allocate(FRAME_HEADER_BYTES + jpeg.size).order(ByteOrder.BIG_ENDIAN)
            .put(JPEG_FRAME).put(VERSION.toByte()).putInt(sequence).put(rotation.toByte()).put(jpeg).array()
    }

    fun maskedPayloadLength(payloadSize: Int): ByteArray {
        require(payloadSize >= 0)
        return when {
            payloadSize < 126 -> byteArrayOf((0x80 or payloadSize).toByte())
            payloadSize <= 0xffff -> byteArrayOf(
                (0x80 or 126).toByte(),
                (payloadSize ushr 8).toByte(),
                payloadSize.toByte(),
            )
            else -> ByteBuffer.allocate(9).order(ByteOrder.BIG_ENDIAN)
                .put((0x80 or 127).toByte()).putLong(payloadSize.toLong()).array()
        }
    }

    fun ackSequence(packet: ByteArray): Int? = if (packet.size == 5 && packet[0] == ACK)
        ByteBuffer.wrap(packet, 1, 4).order(ByteOrder.BIG_ENDIAN).int else null
}
