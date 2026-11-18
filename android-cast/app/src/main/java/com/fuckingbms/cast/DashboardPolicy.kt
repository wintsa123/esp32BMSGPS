package com.fuckingbms.cast

import java.net.URI

internal object DashboardPolicy {
    const val ORIGIN = "https://bms.local"

    fun castLaunchUrl(value: String?): String? = runCatching {
        val uri = URI(value ?: return null)
        require(uri.rawUserInfo == null && uri.port == -1 && uri.rawFragment == null)
        require((uri.scheme == "fuckingbms" && uri.host == "cast" && uri.path == "/v1") ||
            (uri.scheme == "https" && uri.host in listOf("fuckbms.com", "esp-bms-setting.vercel.app") && uri.path == "/cast"))
        val query = uri.rawQuery.orEmpty().split('&').associate { item ->
            val fields = item.split('=', limit = 2)
            fields[0] to java.net.URLDecoder.decode(fields.getOrElse(1) { "" }, "UTF-8")
        }
        val host = localHost(query["host"] ?: "192.168.4.1")
        val language = if (query["lang"] == "en") "en" else "zh"
        "$ORIGIN/?cast=1&host=$host&lang=$language"
    }.getOrNull()

    fun isDocument(url: String): Boolean = runCatching {
        val uri = URI(url)
        uri.scheme == "https" && uri.host == "bms.local" && uri.port == -1 && uri.userInfo == null
    }.getOrDefault(false)

    fun apiPath(path: String): Boolean = runCatching {
        val uri = URI(path)
        path.startsWith("/api/") && !uri.isAbsolute && uri.rawAuthority == null &&
            uri.fragment == null && uri.normalize().path == uri.path && !path.contains('%')
    }.getOrDefault(false)

    fun localHost(value: String): String {
        val uri = URI(if (value.startsWith("http://")) value else "http://$value")
        require(uri.scheme == "http" && uri.userInfo == null && uri.query == null && uri.fragment == null)
        require(uri.path.isNullOrEmpty() || uri.path == "/")
        val octets = uri.host?.split('.')?.map { it.toIntOrNull() ?: -1 } ?: emptyList()
        require(octets.size == 4 && octets.all { it in 0..255 })
        require(octets[0] == 10 || (octets[0] == 192 && octets[1] == 168) ||
            (octets[0] == 172 && octets[1] in 16..31) || (octets[0] == 169 && octets[1] == 254))
        require(uri.port == -1 || uri.port in 1..65535)
        return octets.joinToString(".") + if (uri.port == -1) "" else ":${uri.port}"
    }
}
