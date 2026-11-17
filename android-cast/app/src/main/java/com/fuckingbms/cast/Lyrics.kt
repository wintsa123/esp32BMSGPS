package com.fuckingbms.cast

internal object Lyrics {
    data class Line(val timeMs: Long, val text: String)
    private val stamp = Regex("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?]")

    fun parse(text: String): List<Line> {
        var offset = Regex("\\[offset:([+-]?\\d+)]", RegexOption.IGNORE_CASE).find(text)?.groupValues?.get(1)?.toLongOrNull() ?: 0L
        offset = offset.coerceIn(-60000, 60000)
        return text.take(200000).lineSequence().flatMap { raw ->
            val words = raw.replace(stamp, "").trim().take(200)
            stamp.findAll(raw).mapNotNull { match ->
                val minutes = match.groupValues[1].toLongOrNull() ?: return@mapNotNull null
                val seconds = match.groupValues[2].toLongOrNull() ?: return@mapNotNull null
                if (seconds > 59) return@mapNotNull null
                val fraction = match.groupValues[3].padEnd(3, '0').take(3).toLongOrNull() ?: 0
                Line((minutes * 60000 + seconds * 1000 + fraction + offset).coerceAtLeast(0), words)
            }
        }.take(5000).sortedBy { it.timeMs }.toList()
    }

    fun current(lines: List<Line>, positionMs: Long): String = lines.lastOrNull { it.timeMs <= positionMs }?.text.orEmpty()
}
