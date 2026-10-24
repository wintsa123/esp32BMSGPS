package com.fuckingbms.cast

import java.nio.ByteBuffer
import java.nio.ByteOrder

object CastProtocol {
    const val VERSION = 4
    const val MAX_FRAME_BYTES = 262_144
    const val MAX_INBOUND_FRAME_BYTES = 64
    const val DEFAULT_JPEG_QUALITY = 80
    const val FALLBACK_JPEG_QUALITY = 80
    const val TARGET_FPS = 20
    const val FRAME_HEADER_BYTES = 7
    const val JPEG_FRAME: Byte = 1
    const val INPUT_TOUCH: Byte = 2
    const val EXIT_CAST: Byte = 3
    const val HEARTBEAT: Byte = 4
    const val ACK: Byte = 0x81.toByte()
    const val INPUT_FRAME_BYTES = 7
    const val INPUT_PHASE_DOWN = 0
    const val INPUT_PHASE_MOVE = 1
    const val INPUT_PHASE_UP = 2
    const val EXIT_FRAME_BYTES = 2

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

    /** Decodes a device -> phone touch event; null for anything else on the wire. */
    fun inputEvent(packet: ByteArray): TouchEvent? {
        if (packet.size != INPUT_FRAME_BYTES || packet[0] != INPUT_TOUCH ||
            packet[1] != VERSION.toByte()
        ) return null
        val phase = packet[2].toInt()
        if (phase !in INPUT_PHASE_DOWN..INPUT_PHASE_UP) return null
        return TouchEvent(
            phase = phase,
            x = ((packet[3].toInt() and 0xff) shl 8) or (packet[4].toInt() and 0xff),
            y = ((packet[5].toInt() and 0xff) shl 8) or (packet[6].toInt() and 0xff),
        )
    }
    /** True when the device asked the phone to stop casting (user long-pressed the panel). */
    fun exitRequest(packet: ByteArray): Boolean = packet.size == EXIT_FRAME_BYTES &&
        packet[0] == EXIT_CAST && packet[1] == VERSION.toByte()
}

/** Panel-space touch sample reported by the device. */
data class TouchEvent(val phase: Int, val x: Int, val y: Int)

/** What a device -> phone WebSocket frame turned out to be. */
internal sealed interface CastInbound {
    /** Frame acknowledgement for the given JPEG sequence. */
    data class Ack(val sequence: Int) : CastInbound

    /** The device ended the cast session; the phone must stop streaming and not retry. */
    object DeviceExit : CastInbound
}
