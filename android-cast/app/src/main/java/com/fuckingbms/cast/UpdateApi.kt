package com.fuckingbms.cast

import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** 在线更新源（Vercel 静态托管，与 vercel/public 下的清单一致）。 */
internal object UpdateApi {
    const val BASE_URL = "https://esp-bms-setting.vercel.app"
    private const val APK_MANIFEST_PATH = "/apk/latest.json"
    private const val FIRMWARE_MANIFEST_PATH = "/firmware/firmware.json"
    private const val CONNECT_TIMEOUT_MS = 8_000
    private const val READ_TIMEOUT_MS = 15_000

    fun checkApkUpdate(): ApkRelease? {
        val body = getJson(resolve(BASE_URL, APK_MANIFEST_PATH)) ?: return null
        return parseApkRelease(body)
    }

    fun parseApkRelease(body: String): ApkRelease? {
        val value = JSONObject(body)
        return ApkRelease(
            versionCode = value.optInt("versionCode", 0),
            versionName = value.optString("versionName", ""),
            url = value.optString("url", ""),
            size = value.optLong("size", 0L),
            note = value.optString("note", ""),
        ).takeIf { it.url.isNotBlank() }
    }

    fun fetchFirmwareList(): List<FirmwareRelease> {
        val body = getJson(resolve(BASE_URL, FIRMWARE_MANIFEST_PATH)) ?: return emptyList()
        return parseFirmwareList(body)
    }

    fun parseFirmwareList(body: String): List<FirmwareRelease> {
        val root = JSONObject(body)
        val values = root.optJSONArray("firmwares") ?: return emptyList()
        return buildList {
            for (index in 0 until values.length()) {
                val item = values.optJSONObject(index) ?: continue
                val profile = item.optString("profile", "").takeIf { it.isNotBlank() } ?: continue
                val url = item.optString("url", "").takeIf { it.isNotBlank() } ?: continue
                add(
                    FirmwareRelease(
                        profile = profile,
                        name = item.optString("name", profile),
                        chip = item.optString("chip", ""),
                        version = item.optString("version", ""),
                        url = url,
                        size = item.optLong("size", 0L),
                        code = item.optString("code", ""),
                        note = item.optString("note", ""),
                    )
                )
            }
        }
    }

    /** 下载文件到 dest；onProgress 回调 0..100 百分比。 */
    fun downloadFile(url: String, dest: File, onProgress: (Int) -> Unit) {
        val connection = (URL(resolve(BASE_URL, url)).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout = READ_TIMEOUT_MS
            useCaches = false
            setRequestProperty("Accept", "application/octet-stream")
        }
        try {
            val code = connection.responseCode
            if (code !in 200..299) {
                val message = connection.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty()
                throw IOException("下载失败：HTTP $code${message.takeIf { it.isNotBlank() }?.let { ": $it" }.orEmpty()}")
            }
            val total = connection.contentLengthLong
            val temporary = File(dest.parentFile, dest.name + ".tmp")
            try {
                connection.inputStream.use { input ->
                    temporary.outputStream().use { output ->
                        val buffer = ByteArray(64 * 1024)
                        var received = 0L
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            output.write(buffer, 0, count)
                            received += count
                            if (total > 0) {
                                onProgress((received * 100 / total).toInt().coerceIn(0, 100))
                            }
                        }
                    }
                }
                if (!temporary.renameTo(dest)) {
                    if (dest.exists()) dest.delete()
                    if (!temporary.renameTo(dest)) throw IOException("无法写入下载文件")
                }
                onProgress(100)
            } finally {
                temporary.delete()
            }
        } finally {
            connection.disconnect()
        }
    }

    private fun getJson(url: String): String? {
        val connection = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout = READ_TIMEOUT_MS
            useCaches = false
        }
        try {
            val code = connection.responseCode
            if (code !in 200..299) return null
            return connection.inputStream.bufferedReader().use { it.readText() }
        } catch (_: IOException) {
            return null
        } finally {
            connection.disconnect()
        }
    }

    private fun resolve(baseUrl: String, url: String): String =
        if (url.startsWith("http://") || url.startsWith("https://")) url else baseUrl.trimEnd('/') + "/" + url.trimStart('/')
}

internal data class ApkRelease(
    val versionCode: Int,
    val versionName: String,
    val url: String,
    val size: Long,
    val note: String,
)

internal data class FirmwareRelease(
    val profile: String,
    val name: String,
    val chip: String,
    val version: String,
    val url: String,
    val size: Long,
    val code: String,
    val note: String,
) {
    val description: String
        get() = buildString {
            append(name)
            if (version.isNotBlank()) append("  v").append(version)
            if (chip.isNotBlank()) append("  [").append(chip).append("]")
            if (size > 0) append("  ").append(size / 1024).append(" KB")
        }
}
