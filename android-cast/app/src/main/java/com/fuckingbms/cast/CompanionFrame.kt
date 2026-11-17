package com.fuckingbms.cast

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.pow

internal object CompanionFrame {
    val palette = intArrayOf(0xff080a0e.toInt(), 0xff162029.toInt(), 0xff303c49.toInt(), 0xff556578.toInt(),
        0xff8091a5.toInt(), 0xffa9b4c8.toInt(), 0xffd5e0ee.toInt(), Color.WHITE, 0xff00e5ff.toInt(),
        0xff0066ff.toInt(), 0xff74d6b5.toInt(), 0xff20e87b.toInt(), 0xffffc857.toInt(),
        0xffff6b6b.toInt(), 0xff4a90d9.toInt(), 0xff5bbf5b.toInt())
    private val ids = AtomicInteger(1)

    fun encode(pixels: IntArray): ByteArray {
        val result = ByteArrayOutputStream()
        val cache = HashMap<Int, Int>()
        var previous = -1
        var run = 0
        fun flush() {
            if (run >= 16) { result.write(0xf0 or previous); result.write(run and 255); result.write(run shr 8) }
            else if (run > 0) result.write(((run - 1) shl 4) or previous)
        }
        for (color in pixels) {
            val index = cache.getOrPut(color) {
                palette.indices.minBy { k ->
                    val c = palette[k]
                    val r = ((color shr 16) and 255) - ((c shr 16) and 255)
                    val g = ((color shr 8) and 255) - ((c shr 8) and 255)
                    val b = (color and 255) - (c and 255)
                    r * r * 2 + g * g * 4 + b * b * 3
                }
            }
            if (index != previous || run == 65535) {
                flush()
                previous = index
                run = 0
            }
            run++
        }
        flush()
        return result.toByteArray()
    }

    fun send(transport: DeviceTransport, slot: Int, bitmap: Bitmap, cancelled: () -> Boolean, betweenChunks: () -> Unit = {}) {
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val encoded = encode(pixels)
        val id = ids.getAndIncrement() and Int.MAX_VALUE
        fun post(operation: String, body: JSONObject = JSONObject()) {
            check(!cancelled()) { "frame replaced" }
            transport.request("POST", "/api/companion", body.put("op", operation).put("slot", slot).put("frame", id))
        }
        post("begin", JSONObject().put("width", bitmap.width).put("height", bitmap.height))
        val hex = "0123456789abcdef"
        encoded.asList().chunked(128).forEachIndexed { index, bytes ->
            val text = buildString(bytes.size * 2) { bytes.forEach { b -> val v = b.toInt() and 255; append(hex[v shr 4]); append(hex[v and 15]) } }
            post("chunk", JSONObject().put("offset", index * 128).put("data", text))
            betweenChunks()
        }
        post("commit")
    }

    fun music(width: Int, height: Int, music: CompanionMusic.State): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(palette[0])
        val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL) }
        fun text(value: String, y: Float, size: Float, color: Int) {
            p.textSize = size; p.color = color
            val limited = android.text.TextUtils.ellipsize(value, android.text.TextPaint(p), (width - 40).toFloat(), android.text.TextUtils.TruncateAt.END).toString()
            canvas.drawText(limited, (width - p.measureText(limited)) / 2, y, p)
        }
        text("MUSIC", 30f, 15f, palette[8])
        text(music.title.ifBlank { "等待手机播放" }, height * .32f, if (width >= 400) 26f else 23f, palette[7])
        text(music.artist, height * .32f + 32, 16f, palette[5])
        p.color = palette[2]; canvas.drawRoundRect(30f, height * .57f, width - 30f, height * .57f + 2, 1f, 1f, p)
        text(music.line.ifBlank { if (music.title.isBlank()) "打开播放器，音乐会自动同步" else "纯粹聆听" }, height * .77f, 19f, palette[8])
        text(if (music.playing) "正在播放" else "已暂停", height - 14f, 12f, palette[4])
        return bitmap
    }
}
