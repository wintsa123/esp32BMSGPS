package com.fuckingbms.cast

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.io.File
import kotlin.math.*

internal object CompanionMaps {
    fun openConnection(context: Context, url: String): HttpURLConnection {
        val manager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val internet = manager.allNetworks.firstOrNull {
            manager.getNetworkCapabilities(it)?.let { c -> c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                c.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) } == true
        }
        val connection = (internet?.openConnection(URL(url)) ?: URL(url).openConnection()) as HttpURLConnection
        connection.connectTimeout = 12000; connection.readTimeout = 12000
        return connection
    }

    fun fetch(context: Context, url: String, maxBytes: Int = 2_000_000): ByteArray {
        val connection = openConnection(context,url)
        try {
            check(connection.responseCode == 200) { "地图服务暂时不可用 (${connection.responseCode})" }
            return connection.inputStream.use { input ->
                val out = java.io.ByteArrayOutputStream(); val buffer = ByteArray(8192)
                while (true) { val n = input.read(buffer); if (n < 0) break; check(out.size()+n <= maxBytes) { "地图响应过大" }; out.write(buffer,0,n) }
                out.toByteArray()
            }
        } finally { connection.disconnect() }
    }

    private val tileWorkers=java.util.concurrent.Executors.newFixedThreadPool(2)
    private val pendingTiles=mutableSetOf<String>()
    private val failedTiles=mutableMapOf<String,Long>()

    private fun encoded(value: String) = URLEncoder.encode(value, "UTF-8")
    private fun parsePoint(text: String): NavPoint {
        val pair = text.split(','); require(pair.size == 2) { "坐标格式错误" }
        val p = NavPoint(pair[1].toDouble(),pair[0].toDouble())
        require(p.lat.isFinite() && p.lon.isFinite() && p.lat in -85.0..85.0 && p.lon in -180.0..180.0) { "坐标超出范围" }
        return p
    }

    fun parseRoute(json: JSONObject, name: String): NavRoute {
        check(json.optString("status") == "1") {
            "骑行规划失败：${json.optString("info", "请检查 JS API Key 与安全密钥").replace(Regex("[a-fA-F0-9]{32}"),"[隐藏]").take(100)}"
        }
        val path = json.getJSONObject("route").getJSONArray("paths").getJSONObject(0)
        val points = mutableListOf<NavPoint>(); val steps = mutableListOf<NavStep>()
        val list = path.optJSONArray("steps") ?: error("路线缺少分段指引")
        require(list.length() <= 5000) { "路线过长" }
        for (i in 0 until list.length()) {
            val step = list.getJSONObject(i)
            step.optString("polyline").split(';').filter(String::isNotBlank).forEach {
                val p = NavGeo.wgs(parsePoint(it))
                if (points.lastOrNull() != p) points.add(p)
                require(points.size <= 60000) { "路线点过多" }
            }
            steps.add(NavStep((points.size-1).coerceAtLeast(0), step.optString("instruction", "沿路线行驶"), step.optString("road_name",step.optString("road"))))
        }
        require(points.size >= 2) { "路线没有有效坐标" }
        val seconds=path.getInt("duration")
        require(seconds >= 0) { "骑行时间无效" }
        return NavRoute(points, steps, seconds, name)
    }

    fun pixel(p: NavPoint, zoom: Int): Pair<Double,Double> {
        val size = 256.0 * (1 shl zoom)
        val rad = Math.toRadians(p.lat.coerceIn(-85.0,85.0))
        return (p.lon + 180) / 360 * size to (1 - ln(tan(rad) + 1 / cos(rad)) / PI) / 2 * size
    }

    fun tile(context: Context, key: String, x: Int, y: Int, zoom: Int): Bitmap? {
        if (key.isBlank()) return null
        val count = 1 shl zoom
        if (y !in 0 until count) return null
        val tx = ((x % count) + count) % count
        val directory = File(context.cacheDir, "companion-tiles").apply { mkdirs() }
        val file = File(directory, "$zoom-$tx-$y.png")
        if (file.isFile) BitmapFactory.decodeFile(file.path)?.let { return it }
        val id="$zoom-$tx-$y"
        synchronized(pendingTiles) {
            if (id in pendingTiles || pendingTiles.size >= 16 ||
                android.os.SystemClock.elapsedRealtime()-(failedTiles[id] ?: -60000) < 30000) return null
            pendingTiles.add(id)
        }
        val app=context.applicationContext
        tileWorkers.execute {
            val result=runCatching {
                val bytes=fetch(app,"https://t${(tx+y) and 7}.tianditu.gov.cn/DataServer?T=vec_w&x=$tx&y=$y&l=$zoom&tk=${encoded(key)}",300000)
                val bitmap=BitmapFactory.decodeByteArray(bytes,0,bytes.size) ?: error("瓦片解码失败")
                val valid=bitmap.width == 256 && bitmap.height == 256
                bitmap.recycle(); check(valid)
                val temporary=File(directory,"$id.tmp"); temporary.writeBytes(bytes)
                check(temporary.renameTo(file))
                directory.listFiles()?.filter { it.extension == "png" }?.sortedBy { it.lastModified() }?.let { old ->
                    old.take((old.size-160).coerceAtLeast(0)).forEach(File::delete)
                }
            }
            synchronized(pendingTiles) {
                pendingTiles.remove(id)
                if (result.isFailure) {
                    failedTiles[id]=android.os.SystemClock.elapsedRealtime()
                    if (failedTiles.size > 200) failedTiles.entries.iterator().let { it.next(); it.remove() }
                } else failedTiles.remove(id)
            }
        }
        return null
    }
}
