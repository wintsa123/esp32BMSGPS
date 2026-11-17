package com.fuckingbms.cast

import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Two independent mirrors; resolved download URLs always retain the selected source. */
internal object UpdateApi {
    const val BASE_URL = "https://fuckbms.com/"
    enum class Source(val id: String, val label: String, val base: String) {
        CF("cf", "Cloudflare", BASE_URL),
        GITHUB("github", "GitHub", "https://raw.githubusercontent.com/wintsa123/espBmsSetting/updates/");
        companion object { fun from(value: String?) = entries.firstOrNull { it.id == value } }
    }
    private const val APK_MANIFEST_PATH = "/apk/latest.json"
    private const val FIRMWARE_MANIFEST_PATH = "/firmware/firmware.json"
    private const val CONNECT_TIMEOUT_MS = 8_000
    private const val READ_TIMEOUT_MS = 15_000

    fun checkApkUpdate(source: Source? = null, fetch: (String) -> String = ::getJson): ApkRelease? =
        catalog(APK_MANIFEST_PATH, source, fetch) { body, url ->
            val release = parseApkRelease(body) ?: throw IOException("无效的 App 更新清单")
            release.copy(url = releaseUrl(release.url, url))
        }

    private fun <T> catalog(path: String, source: Source?, fetch: (String) -> String, parse: (String, String) -> T): T {
        val errors = mutableListOf<String>()
        for (candidate in source?.let { listOf(it) } ?: Source.entries) {
            val url = resolve(candidate.base, path)
            try { return parse(fetch(url), url) }
            catch (reason: Exception) { errors += "${candidate.label}: ${reason.message}" }
        }
        throw IOException(errors.joinToString("; "))
    }

    fun releaseUrl(value: String, manifestUrl: String): String {
        val relative = if (value.startsWith("/") && !value.startsWith("//")) "..$value" else value
        val uri = java.net.URI(URL(URL(manifestUrl), relative).toString())
        require(uri.scheme == "https" && uri.host != null && uri.rawUserInfo == null && uri.rawFragment == null) { "下载地址必须使用 HTTPS" }
        return uri.toASCIIString()
    }

    fun parseApkRelease(body: String): ApkRelease? {
        val value = JSONObject(body)
        return ApkRelease(
            versionCode = value.optInt("versionCode", 0),
            versionName = value.optString("versionName", ""),
            url = value.optString("url", ""),
            size = value.optLong("size", 0L),
            note = value.optString("note", ""),
        ).takeIf { it.url.isNotBlank() && it.versionCode > 0 && it.versionName.isNotBlank() }
    }

    fun fetchFirmwareList(source: Source? = null, fetch: (String) -> String = ::getJson): List<FirmwareRelease> =
        catalog(FIRMWARE_MANIFEST_PATH, source, fetch) { body, url ->
            require(JSONObject(body).optJSONArray("firmwares") != null) { "无效的固件更新清单" }
            parseFirmwareList(body).map { it.copy(url = releaseUrl(it.url, url)) }
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

    private fun getJson(url: String): String {
        val connection = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout = READ_TIMEOUT_MS
            useCaches = false
        }
        try {
            val code = connection.responseCode
            if (code !in 200..299) throw IOException("HTTP $code")
            return connection.inputStream.bufferedReader().use { it.readText() }
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
