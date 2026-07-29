package com.fuckingbms.cast

import java.util.UUID

object PhoneMediaProtocol {
    const val VERSION: Byte = 1
    const val TITLE_MAX_BYTES = 96
    const val STATE_READY = 1
    const val STATE_ACTIVE = 1 shl 1
    const val STATE_PLAYING = 1 shl 2

    const val COMMAND_PREVIOUS: Byte = 1
    const val COMMAND_NEXT: Byte = 2
    const val COMMAND_VOLUME_DOWN: Byte = 3
    const val COMMAND_VOLUME_UP: Byte = 4

    val SERVICE_UUID: UUID = UUID.fromString("5f9b7f60-9f16-4edf-a2e8-472a8aa1b201")
    val COMMAND_UUID: UUID = UUID.fromString("5f9b7f60-9f16-4edf-a2e8-472a8aa1b202")
    val STATE_UUID: UUID = UUID.fromString("5f9b7f60-9f16-4edf-a2e8-472a8aa1b203")

    fun statePayload(ready: Boolean,
                     active: Boolean,
                     playing: Boolean,
                     title: String,
                     maxAttributeBytes: Int): ByteArray {
        var flags = if (ready) STATE_READY else 0
        if (active) flags = flags or STATE_ACTIVE
        if (playing) flags = flags or STATE_PLAYING
        val maxTitleBytes = minOf(TITLE_MAX_BYTES, (maxAttributeBytes - 2).coerceAtLeast(0))
        val titleBytes = truncateUtf8(title, maxTitleBytes)
        return ByteArray(titleBytes.size + 2).also {
            it[0] = VERSION
            it[1] = flags.toByte()
            titleBytes.copyInto(it, 2)
        }
    }

    fun truncateUtf8(text: String, maxBytes: Int): ByteArray {
        val bytes = text.encodeToByteArray()
        if (bytes.size <= maxBytes) return bytes
        var length = maxBytes.coerceAtLeast(0)
        while (length > 0 && (bytes[length].toInt() and 0xc0) == 0x80) {
            length--
        }
        return bytes.copyOf(length)
    }
}
