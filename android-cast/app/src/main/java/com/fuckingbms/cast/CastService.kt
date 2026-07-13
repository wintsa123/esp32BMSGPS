package com.fuckingbms.cast

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Base64
import android.view.WindowManager
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.security.SecureRandom
import kotlin.concurrent.thread

class CastService : Service() {
    private var projection: MediaProjection? = null
    private var reader: ImageReader? = null
    @Volatile private var running = false
    @Volatile private var latest: ByteArray? = null
    @Volatile private var encoder: FrameEncoder? = null
    private var socket: CastSocket? = null
    private val projectionCallback = object : MediaProjection.Callback() {
        override fun onStop() {
            running = false
            socket?.close()
            socket = null
            reader?.close()
            reader = null
            stopSelf()
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) return START_NOT_STICKY
        startForeground(1, notification())
        val info = CastInfo(intent.getIntExtra("width", 0), intent.getIntExtra("height", 0), intent.getIntExtra("rotation", 0), intent.getIntExtra("block", 16))
        val result = intent.getIntExtra("result", 0); val grant = intent.getParcelableExtra<Intent>("grant") ?: return START_NOT_STICKY
        val host = intent.getStringExtra("host") ?: return START_NOT_STICKY
        running = true; encoder = FrameEncoder(info.width, info.height, info.maxBlockSide)
        projection = (getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager).getMediaProjection(result, grant)
        projection!!.registerCallback(projectionCallback, Handler(Looper.getMainLooper()))
        val metrics = (getSystemService(WINDOW_SERVICE) as WindowManager).defaultDisplay
        val width = metrics.width.coerceAtLeast(1); val height = metrics.height.coerceAtLeast(1)
        reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 2).also { imageReader ->
            projection!!.createVirtualDisplay("BMS Cast", width, height, resources.displayMetrics.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, imageReader.surface, null, null)
            imageReader.setOnImageAvailableListener({ consumeImage(it, info.width, info.height) }, null)
        }
        thread(name = "bms-cast", isDaemon = true) { transmit(host, info.rotation) }
        return START_NOT_STICKY
    }

    private fun consumeImage(source: ImageReader, targetW: Int, targetH: Int) {
        val image = source.acquireLatestImage() ?: return
        try {
            val plane = image.planes[0]; val buffer = plane.buffer; val pixels = ByteArray(targetW * targetH * 2)
            val scale = minOf(targetW.toFloat() / image.width, targetH.toFloat() / image.height)
            val contentW = (image.width * scale).toInt(); val contentH = (image.height * scale).toInt(); val left = (targetW - contentW) / 2; val top = (targetH - contentH) / 2
            for (y in 0 until contentH) for (x in 0 until contentW) {
                val sx = (x / scale).toInt().coerceAtMost(image.width - 1); val sy = (y / scale).toInt().coerceAtMost(image.height - 1)
                val offset = sy * plane.rowStride + sx * plane.pixelStride
                val r = buffer.get(offset).toInt() and 0xff; val g = buffer.get(offset + 1).toInt() and 0xff; val b = buffer.get(offset + 2).toInt() and 0xff
                val rgb565 = ((r shr 3) shl 11) or ((g shr 2) shl 5) or (b shr 3); val out = ((top + y) * targetW + left + x) * 2
                pixels[out] = (rgb565 shr 8).toByte(); pixels[out + 1] = rgb565.toByte()
            }
            latest = pixels
        } finally { image.close() }
    }

    private fun transmit(host: String, rotation: Int) {
        var failure: Exception? = null
        for (attempt in 1..3) {
            if (!running) break
            try {
                report("正在连接设备（$attempt/3）")
                socket = CastSocket(host)
                report("设备已连接，正在传输画面")
                var sequence = 1
                while (running) {
                    val pixels = latest
                    if (pixels != null) {
                        val packets = encoder!!.encode(sequence, rotation, pixels)
                        packets.forEach { socket!!.send(it) }
                        if (socket!!.readAck() != sequence) throw IOException("设备未确认画面")
                        encoder!!.acknowledge(pixels)
                        sequence++
                    }
                    Thread.sleep(500)
                }
                return
            } catch (e: Exception) {
                failure = e
                socket?.close()
                socket = null
                if (attempt < 3 && running) {
                    report("连接失败：${e.message ?: e.javaClass.simpleName}；1 秒后重试")
                    Thread.sleep(1_000)
                }
            }
        }
        if (running && failure != null) report("投屏连接失败（已重试 3 次）：${failure.message ?: failure.javaClass.simpleName}")
        stopSelf()
    }
    private fun report(message: String) { sendBroadcast(Intent(ACTION_STATUS).setPackage(packageName).putExtra(EXTRA_STATUS, message)) }
    override fun onDestroy() {
        running = false
        socket?.close()
        socket = null
        reader?.close()
        reader = null
        projection?.unregisterCallback(projectionCallback)
        projection?.stop()
        projection = null
        super.onDestroy()
    }
    override fun onBind(intent: Intent?): IBinder? = null
    private fun notification(): Notification { val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager; val channel = NotificationChannel("cast", "BMS 投屏", NotificationManager.IMPORTANCE_LOW); manager.createNotificationChannel(channel); return Notification.Builder(this, "cast").setContentTitle("BMS 投屏进行中").setSmallIcon(android.R.drawable.presence_video_online).build() }
    companion object { const val ACTION_STATUS = "com.fuckingbms.cast.STATUS"; const val EXTRA_STATUS = "status"; fun intent(context: Context, result: Int, grant: Intent, host: String, info: CastInfo) = Intent(context, CastService::class.java).apply { putExtra("result", result); putExtra("grant", grant); putExtra("host", host); putExtra("width", info.width); putExtra("height", info.height); putExtra("rotation", info.rotation); putExtra("block", info.maxBlockSide) } }
}

private class CastSocket(host: String) {
    private val endpoint = URI("http://$host")
    private val socket = Socket().apply {
        connect(InetSocketAddress(requireNotNull(endpoint.host), if (endpoint.port >= 0) endpoint.port else 80), 4_000)
        soTimeout = 4_000
    }; private val output = BufferedOutputStream(socket.getOutputStream()); private val input = BufferedInputStream(socket.getInputStream()); private val random = SecureRandom()
    init { val key = Base64.encodeToString(ByteArray(16).also { random.nextBytes(it) }, Base64.NO_WRAP); output.write("GET /cast HTTP/1.1\r\nHost: $host\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: $key\r\n\r\n".toByteArray()); output.flush(); val response = generateSequence { readLine() }.takeWhile { it.isNotEmpty() }.toList(); check(response.firstOrNull()?.contains(" 101 ") == true) }
    fun send(payload: ByteArray) { val mask = ByteArray(4).also { random.nextBytes(it) }; output.write(0x82); when { payload.size < 126 -> output.write(0x80 or payload.size); else -> { output.write(0x80 or 126); output.write(payload.size ushr 8); output.write(payload.size) } }; output.write(mask); payload.forEachIndexed { i, b -> output.write(b.toInt() xor mask[i % 4].toInt()) }; output.flush() }
    fun readAck(): Int? { val first = input.read(); val second = input.read(); if (first < 0 || second < 0) return null; var length = second and 0x7f; if (length == 126) length = (input.read() shl 8) or input.read(); if (length !in 1..512) return null; val data = ByteArray(length); var offset = 0; while (offset < length) { val read = input.read(data, offset, length - offset); if (read < 0) return null; offset += read }; return CastProtocol.ackSequence(data) }
    private fun readLine(): String { val bytes = ArrayList<Byte>(); while (true) { val value = input.read(); if (value < 0 || value == '\n'.code) return bytes.toByteArray().toString(Charsets.ISO_8859_1).trimEnd('\r'); bytes += value.toByte() } }
    fun close() { socket.close() }
}
