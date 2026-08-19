package com.fuckingbms.cast

import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

internal data class DeviceStatus(
    val version: String,
    val speed: String,
    val speedUnit: String,
    val bms: String,
    val packVoltageMv: Int?,
    val currentDeciAmps: Int?,
    val socPercent: Int?,
    val localBatteryMv: Int?,
    val bmsInfo: String,
    val protections: List<String>,
    val warnings: List<String>,
    val temperaturesC: List<Int>,
    val minCellVoltageMv: Int?,
    val averageCellVoltageMv: Int?,
    val maxCellVoltageMv: Int?,
    val deltaCellVoltageMv: Int?,
    val totalCapacityMah: Int?,
    val remainingCapacityMah: Int?,
    val runningTimeSeconds: Int?,
    val cycleCapacityMah: Int?,
    val wifi: String,
    val setupApEnabled: Boolean,
    val capacityMah: Int?,
    val capacityState: String,
    val controllerOnline: Boolean,
    val controllerSpeedDeci: Int?,
    val controllerRpm: Int?,
    val controllerGear: Int?,
    val controllerPowerW: Int?,
    val controllerTempC: Int?,
    val motorTempC: Int?,
) {
    companion object {
        fun parse(json: String): DeviceStatus = JSONObject(json).let { value ->
            DeviceStatus(
                version = value.optString("version", "--"),
                speed = value.optString("speed", "--"),
                speedUnit = value.optString("speed_unit", "km/h"),
                bms = value.optString("bms", "offline"),
                packVoltageMv = value.optIntOrNull("pack_voltage_mv"),
                currentDeciAmps = value.optIntOrNull("current_deci_amps"),
                socPercent = value.optIntOrNull("soc_percent"),
                localBatteryMv = value.optIntOrNull("local_battery_mv"),
                bmsInfo = value.optString("bms_info", "--"),
                protections = value.optStringList("bms_protections"),
                warnings = value.optStringList("bms_warnings"),
                temperaturesC = value.optIntList("bms_temperatures_c"),
                minCellVoltageMv = value.optIntOrNull("min_cell_voltage_mv"),
                averageCellVoltageMv = value.optIntOrNull("average_cell_voltage_mv"),
                maxCellVoltageMv = value.optIntOrNull("max_cell_voltage_mv"),
                deltaCellVoltageMv = value.optIntOrNull("delta_cell_voltage_mv"),
                totalCapacityMah = value.optIntOrNull("total_capacity_mah"),
                remainingCapacityMah = value.optIntOrNull("capacity_remaining_mah"),
                runningTimeSeconds = value.optIntOrNull("bms_running_time_seconds"),
                cycleCapacityMah = value.optIntOrNull("bms_cycle_capacity_mah"),
                wifi = value.optString("wifi", "--"),
                setupApEnabled = value.optBoolean("setup_ap_enabled", false),
                capacityMah = value.optIntOrNull("bms_capacity_estimate_mah"),
                capacityState = value.optString("bms_capacity_estimate_state", "unsupported"),
                controllerOnline = value.optBoolean("controller_online", false),
                controllerSpeedDeci = value.optIntOrNull("controller_speed_deci_units"),
                controllerRpm = value.optIntOrNull("controller_rpm"),
                controllerGear = value.optIntOrNull("controller_gear"),
                controllerPowerW = value.optIntOrNull("controller_power_w"),
                controllerTempC = value.optIntOrNull("controller_temp_c"),
                motorTempC = value.optIntOrNull("motor_temp_c"),
            )
        }
    }
}

internal data class RideSnapshot(
    val packVoltageMv: Int?,
    val currentDeciAmps: Int?,
    val deltaCellVoltageMv: Int?,
    val socPercent: Int?,
    val temperaturesC: List<Int>,
) {
    companion object {
        fun parse(value: JSONObject?): RideSnapshot = RideSnapshot(
            packVoltageMv = value?.optIntOrNull("pack_voltage_mv"),
            currentDeciAmps = value?.optIntOrNull("current_deci_amps"),
            deltaCellVoltageMv = value?.optIntOrNull("delta_cell_voltage_mv"),
            socPercent = value?.optIntOrNull("soc_percent"),
            temperaturesC = value?.optIntList("temperatures_c").orEmpty(),
        )
    }
}

internal data class RideRecord(
    val current: Boolean,
    val maxCurrent: RideSnapshot,
    val maxDelta: RideSnapshot,
)

internal data class GpsPoint(
    val latitude: Double,
    val longitude: Double,
    val timestampSeconds: Long?,
)

internal data class HistorySession(
    val id: Long,
    val startSeconds: Long,
    val endSeconds: Long,
    val samples: Int,
    val capacitySamples: Int,
    val elapsedSeconds: Int,
    val calibrated: Boolean,
    val truncated: Boolean,
    val capacityReached: Boolean,
)

internal data class HistoryOverview(
    val ready: Boolean,
    val backend: String,
    val capacityBytes: Long,
    val sessions: List<HistorySession>,
)

internal data class HistorySample(
    val timestamp: Long,
    val elapsedSeconds: Int,
    val flags: Int,
    val latitudeE7: Int,
    val longitudeE7: Int,
    val packVoltageMv: Int,
    val currentDeciAmps: Int,
    val socPercent: Int,
    val temperaturesC: List<Int>,
)

internal data class HistoryFault(
    val timestamp: Long,
    val sessionId: Long,
    val activeMask: Int,
    val supportedMask: Int,
    val bmsType: Int,
)

internal data class HistoryPage<T>(val values: List<T>, val nextCursor: Long?)

internal data class DeviceConfig(
    val brightness: Int,
    val volume: Int,
    val displayRotation: String,
    val speedUnit: String,
    val speedSource: String,
    val activeSpeedSource: String,
    val controllerOnline: Boolean,
    val language: String,
    val setupApSsid: String,
    val setupApState: String,
    val bmsMac: String,
    val controllerMac: String,
    val bmsType: String,
) {
    companion object {
        fun parse(json: String): DeviceConfig = JSONObject(json).let { value ->
            DeviceConfig(
                brightness = value.optInt("brightness", 80),
                volume = value.optInt("volume", 50),
                displayRotation = value.optString("display_rotation", "landscape"),
                speedUnit = value.optString("speed_unit", "km/h"),
                speedSource = value.optString("speed_source", "gps"),
                activeSpeedSource = value.optString("active_speed_source", "gps"),
                controllerOnline = value.optBoolean("controller_online", false),
                language = value.optString("language", "zh"),
                setupApSsid = value.optString("setup_ap_ssid", "--"),
                setupApState = value.optString("setup_ap_state", "disabled"),
                bmsMac = value.optString("bms_mac", ""),
                controllerMac = value.optString("controller_mac", ""),
                bmsType = value.optString("bms_type", "ant"),
            )
        }
    }
}

internal fun interface DeviceTransport {
    fun request(method: String, path: String, body: JSONObject?): String
}

internal enum class DeviceConnectionMode { NONE, WIFI, BLE }

internal data class BmsCandidate(val name: String, val mac: String, val rssi: Int?)

internal data class BmsCandidates(val scanActive: Boolean, val values: List<BmsCandidate>)

internal data class DeviceCapabilities(
    val itemIds: Set<String>,
    val sectionIds: Set<String>,
) {
    fun supports(itemId: String) = itemId in itemIds
    fun hasSection(sectionId: String) = sectionId in sectionIds

    companion object {
        fun parse(json: String): DeviceCapabilities {
            val sections = JSONObject(json).optJSONArray("sections") ?: JSONArray()
            val sectionIds = buildSet {
                for (sectionIndex in 0 until sections.length()) {
                    sections.optJSONObject(sectionIndex)
                        ?.optString("id")
                        ?.takeIf { it.isNotBlank() }
                        ?.let(::add)
                }
            }
            val itemIds = buildSet {
                for (sectionIndex in 0 until sections.length()) {
                    val items = sections.optJSONObject(sectionIndex)?.optJSONArray("items") ?: continue
                    for (itemIndex in 0 until items.length()) {
                        items.optJSONObject(itemIndex)?.optString("id")?.takeIf { it.isNotBlank() }?.let(::add)
                    }
                }
            }
            return DeviceCapabilities(itemIds, sectionIds)
        }
    }
}

internal data class OtaProgress(
    val active: Boolean,
    val state: String,
    val percent: Int,
    val message: String,
) {
    companion object {
        fun parse(json: String): OtaProgress = JSONObject(json).let { value ->
            OtaProgress(
                active = value.optBoolean("active", false),
                state = value.optString("state", ""),
                percent = value.optInt("percent", 0).coerceIn(0, 100),
                message = value.optString("message", ""),
            )
        }
    }
}

internal object DeviceApi {
    const val MAX_FIRMWARE_BYTES = 1_500 * 1024

    fun httpTransport(host: String) = DeviceTransport { method, path, body ->
        when (method) {
            "GET" -> get(host, path)
            "POST" -> if (body == null) post(host, path) else postJson(host, path, body)
            else -> error("Unsupported HTTP method: $method")
        }
    }

    fun status(host: String) = status(httpTransport(host))

    fun status(transport: DeviceTransport) =
        DeviceStatus.parse(transport.request("GET", "/api/status", null))

    fun rideRecords(host: String) = rideRecords(httpTransport(host))

    fun rideRecords(transport: DeviceTransport): List<RideRecord> {
        val records = JSONObject(transport.request("GET", "/api/bms/ride-records", null)).optJSONArray("records") ?: JSONArray()
        return List(records.length()) { index ->
            val value = records.optJSONObject(index) ?: JSONObject()
            RideRecord(
                current = value.optBoolean("current", false),
                maxCurrent = RideSnapshot.parse(value.optJSONObject("max_current")),
                maxDelta = RideSnapshot.parse(value.optJSONObject("max_delta")),
            )
        }
    }

    fun gpsTrack(host: String): List<GpsPoint> {
        val points = JSONObject(get(host, "/api/gps/track")).optJSONArray("points") ?: JSONArray()
        return buildList {
            for (index in 0 until points.length()) {
                val item = points.optJSONObject(index) ?: continue
                val latitude = when {
                    item.has("lat_e7") -> item.optInt("lat_e7").toDouble() / 10_000_000.0
                    item.has("latitude") -> item.optDouble("latitude", Double.NaN)
                    else -> Double.NaN
                }
                val longitude = when {
                    item.has("lon_e7") -> item.optInt("lon_e7").toDouble() / 10_000_000.0
                    item.has("longitude") -> item.optDouble("longitude", Double.NaN)
                    else -> Double.NaN
                }
                if (latitude.isFinite() && longitude.isFinite() &&
                    latitude in -90.0..90.0 && longitude in -180.0..180.0) {
                    add(GpsPoint(latitude, longitude, item.optLongOrNull("timestamp_s")))
                }
            }
        }
    }

    fun historyOverview(host: String) = historyOverview(httpTransport(host))

    fun historyOverview(transport: DeviceTransport) =
        parseHistoryOverview(transport.request("GET", "/api/history/sessions", null))

    fun historySessions(host: String): List<HistorySession> = historyOverview(host).sessions

    fun parseHistoryOverview(json: String): HistoryOverview {
        val root = JSONObject(json)
        val values = root.optJSONArray("sessions") ?: JSONArray()
        return HistoryOverview(
            ready = root.optBoolean("ready", false),
            backend = root.optString("backend", "--"),
            capacityBytes = root.optLong("capacity_bytes", 0L),
            sessions = List(values.length()) { index ->
            val value = values.optJSONObject(index) ?: JSONObject()
            HistorySession(value.optLong("id"), value.optLong("start_s"), value.optLong("end_s"),
                value.optInt("samples"), value.optInt("capacity_samples"),
                value.optInt("elapsed_s"), value.optBoolean("calibrated"), value.optBoolean("truncated"),
                value.optBoolean("capacity_reached"))
            },
        )
    }

    fun historySamples(host: String, session: Long, from: Long = 0, to: Long = Long.MAX_VALUE, limit: Int = 200): List<HistorySample> {
        return historySamplesPage(host, session, from, to, limit).values
    }

    fun historySamplesPage(host: String, session: Long, from: Long = 0, to: Long = Long.MAX_VALUE,
                           limit: Int = 200, cursor: Long? = null): HistoryPage<HistorySample> {
        return historySamplesPage(httpTransport(host), session, from, to, limit, cursor)
    }

    fun historySamplesPage(transport: DeviceTransport, session: Long, from: Long = 0, to: Long = Long.MAX_VALUE,
                           limit: Int = 200, cursor: Long? = null): HistoryPage<HistorySample> {
        val path = "/api/history/samples?session=$session&from=$from&to=$to&limit=${limit.coerceIn(1, 500)}" +
            (cursor?.let { "&cursor=$it" } ?: "")
        return parseHistorySamplesPage(transport.request("GET", path, null))
    }

    fun parseHistorySamplesPage(json: String): HistoryPage<HistorySample> {
        val root = JSONObject(json)
        val values = root.optJSONArray("samples") ?: JSONArray()
        return HistoryPage(List(values.length()) { index ->
            val value = values.optJSONObject(index) ?: JSONObject()
            HistorySample(value.optLong("t"), value.optInt("elapsed_s"), value.optInt("flags"),
                value.optInt("lat_e7"), value.optInt("lon_e7"), value.optInt("pack_voltage_mv"),
                value.optInt("current_deci_amps"), value.optInt("soc_percent"), value.optIntList("temperatures_c"))
        }, root.optLongOrNull("next_cursor"))
    }

    fun historyFaults(host: String, from: Long = 0, to: Long = Long.MAX_VALUE, limit: Int = 200): List<HistoryFault> {
        return historyFaultsPage(host, from, to, limit).values
    }

    fun historyFaultsPage(host: String, from: Long = 0, to: Long = Long.MAX_VALUE,
                          limit: Int = 200, session: Long? = null, cursor: Long? = null): HistoryPage<HistoryFault> {
        return historyFaultsPage(httpTransport(host), from, to, limit, session, cursor)
    }

    fun historyFaultsPage(transport: DeviceTransport, from: Long = 0, to: Long = Long.MAX_VALUE,
                          limit: Int = 200, session: Long? = null, cursor: Long? = null): HistoryPage<HistoryFault> {
        val path = "/api/history/faults?from=$from&to=$to&limit=${limit.coerceIn(1, 500)}" +
            (session?.let { "&session=$it" } ?: "") +
            (cursor?.let { "&cursor=$it" } ?: "")
        return parseHistoryFaultsPage(transport.request("GET", path, null))
    }

    fun parseHistoryFaultsPage(json: String): HistoryPage<HistoryFault> {
        val root = JSONObject(json)
        val values = root.optJSONArray("faults") ?: JSONArray()
        return HistoryPage(List(values.length()) { index ->
            val value = values.optJSONObject(index) ?: JSONObject()
            HistoryFault(value.optLong("t"), value.optLong("session"), value.optInt("active_mask"),
                value.optInt("supported_mask"), value.optInt("bms_type"))
        }, root.optLongOrNull("next_cursor"))
    }

    fun config(host: String) = config(httpTransport(host))

    fun config(transport: DeviceTransport) =
        DeviceConfig.parse(transport.request("GET", "/api/config", null))

    fun capabilities(host: String) = capabilities(httpTransport(host))

    fun capabilities(transport: DeviceTransport) =
        DeviceCapabilities.parse(transport.request("GET", "/api/settings/manifest", null))

    fun bmsCandidates(host: String): BmsCandidates {
        val value = JSONObject(get(host, "/api/bms/candidates"))
        val candidates = value.optJSONArray("candidates") ?: JSONArray()
        return BmsCandidates(
            scanActive = value.optBoolean("scan_active", false),
            values = List(candidates.length()) { index ->
                val item = candidates.optJSONObject(index) ?: JSONObject()
                BmsCandidate(
                    name = item.optString("name", "BMS"),
                    mac = item.optString("mac", ""),
                    rssi = item.optIntOrNull("rssi"),
                )
            },
        )
    }

    fun startBmsScan(host: String) = post(host, "/api/bms/scan")

    fun bindBms(host: String, mac: String) = postJson(host, "/api/bms/bind", JSONObject().put("mac", mac))

    fun bindBms(transport: DeviceTransport, mac: String) =
        transport.request("POST", "/api/bms/bind", JSONObject().put("mac", mac))

    fun bindController(host: String, mac: String) =
        postJson(host, "/api/controller/bind", JSONObject().put("mac", mac))

    fun bindController(transport: DeviceTransport, mac: String) =
        transport.request("POST", "/api/controller/bind", JSONObject().put("mac", mac))

    fun saveConfig(host: String, values: JSONObject) = saveConfig(httpTransport(host), values)

    fun saveConfig(transport: DeviceTransport, values: JSONObject) =
        transport.request("POST", "/api/config", values)

    fun otaProgress(host: String) = OtaProgress.parse(get(host, "/api/ota/progress"))

    fun uploadFirmware(host: String, firmware: ByteArray, code: String, onProgress: (Int) -> Unit) {
        require(firmware.isNotEmpty() && firmware.size <= MAX_FIRMWARE_BYTES) { "固件文件大小无效" }
        require(code.matches(Regex("\\d{4}"))) { "固件验证码必须为四位数字" }
        request(
            host = host,
            path = "/api/ota",
            method = "POST",
            contentType = "application/octet-stream",
            headers = mapOf("X-Firmware-Code" to code),
            payload = firmware,
            onProgress = onProgress,
        )
    }

    private fun get(host: String, path: String) = request(host, path, "GET")

    private fun post(host: String, path: String) = request(host, path, "POST", payload = ByteArray(0))

    private fun postJson(host: String, path: String, value: JSONObject) = request(
        host = host,
        path = path,
        method = "POST",
        contentType = "application/json; charset=utf-8",
        payload = value.toString().toByteArray(Charsets.UTF_8),
    )

    private fun request(
        host: String,
        path: String,
        method: String,
        contentType: String? = null,
        headers: Map<String, String> = emptyMap(),
        payload: ByteArray? = null,
        onProgress: ((Int) -> Unit)? = null,
    ): String {
        val connection = (URL("http://$host$path").openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = 4_000
            readTimeout = 8_000
            useCaches = false
            contentType?.let { setRequestProperty("Content-Type", it) }
            headers.forEach { (key, value) -> setRequestProperty(key, value) }
            if (payload != null) {
                doOutput = true
                setFixedLengthStreamingMode(payload.size)
            }
        }
        return try {
            if (payload != null) {
                connection.outputStream.use { output ->
                    if (payload.isEmpty()) {
                        onProgress?.invoke(100)
                    } else {
                        var offset = 0
                        while (offset < payload.size) {
                            val count = minOf(16 * 1024, payload.size - offset)
                            output.write(payload, offset, count)
                            offset += count
                            onProgress?.invoke(offset * 100 / payload.size)
                        }
                    }
                }
            }
            val code = connection.responseCode
            val body = (if (code in 200..299 && code != HttpURLConnection.HTTP_NO_CONTENT) connection.inputStream else connection.errorStream)
                ?.bufferedReader()?.use { it.readText() }.orEmpty()
            if (code !in 200..299) error("设备返回 HTTP $code${body.takeIf { it.isNotBlank() }?.let { ": $it" }.orEmpty()}")
            body
        } finally {
            connection.disconnect()
        }
    }
}

private fun JSONObject.optIntOrNull(key: String): Int? =
    if (!has(key) || isNull(key)) null else optInt(key)

private fun JSONObject.optLongOrNull(key: String): Long? =
    if (!has(key) || isNull(key)) null else optLong(key)

private fun JSONObject.optStringList(key: String): List<String> {
    val values = optJSONArray(key) ?: return emptyList()
    return buildList {
        for (index in 0 until values.length()) {
            if (!values.isNull(index)) values.optString(index).takeIf { it.isNotBlank() }?.let(::add)
        }
    }
}

private fun JSONObject.optIntList(key: String): List<Int> {
    val values = optJSONArray(key) ?: return emptyList()
    return buildList {
        for (index in 0 until values.length()) {
            if (!values.isNull(index)) add(values.optInt(index))
        }
    }
}
