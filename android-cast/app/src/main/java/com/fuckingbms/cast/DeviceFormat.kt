package com.fuckingbms.cast

internal fun formatMillivolts(value: Int?): String = value?.let { "%.2f V".format(it / 1000.0) } ?: "--"

internal fun formatDeciAmps(value: Int?): String = value?.let { "%.1f A".format(it / 10.0) } ?: "--"

internal fun formatCapacity(value: Int?, state: String): String = when {
    value != null && state == "ready" -> "%.1f Ah".format(value / 1000.0)
    state == "estimating" -> "估算中"
    else -> "--"
}

internal fun formatSnapshot(value: RideSnapshot): String {
    val temperatures = value.temperaturesC.joinToString("/") { "${it}C" }.ifBlank { "--" }
    val delta = value.deltaCellVoltageMv?.let { "$it mV" } ?: "--"
    val soc = value.socPercent?.let { "$it%" } ?: "--"
    return "${formatMillivolts(value.packVoltageMv)}  ·  ${formatDeciAmps(value.currentDeciAmps)}  ·  Δ $delta  ·  $soc  ·  T $temperatures"
}

internal fun formatCellRange(status: DeviceStatus): String {
    val values = listOf(status.minCellVoltageMv, status.averageCellVoltageMv, status.maxCellVoltageMv)
    return if (values.any { it != null }) {
        "${values[0]?.let { "$it mV" } ?: "--"} / " +
            "${values[1]?.let { "$it mV" } ?: "--"} / " +
            "${values[2]?.let { "$it mV" } ?: "--"}  (Δ ${status.deltaCellVoltageMv?.let { "$it mV" } ?: "--"})"
    } else {
        "--"
    }
}

internal fun formatCapacityValue(value: Int?): String = value?.let { "%.1f Ah".format(it / 1000.0) } ?: "--"

internal fun formatDuration(seconds: Int?): String = seconds?.let {
    val hours = it / 3600
    val minutes = it / 60 % 60
    "%02dh %02dm".format(hours, minutes)
} ?: "--"
