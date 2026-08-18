package com.fuckingbms.cast

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.Rect
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.util.Base64
import android.util.Log
import android.view.Display
import android.view.WindowManager
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.nio.ByteBuffer
import java.security.SecureRandom
import kotlin.concurrent.thread
import kotlin.math.ceil

private const val CAST_METRICS_LOG_WINDOW_NANOS = 5_000_000_000L

class CastService : Service() {
    private var projection: MediaProjection? = null
    private var reader: ImageReader? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var captureThread: HandlerThread? = null
    private var captureHandler: Handler? = null
    private var displayManager: DisplayManager? = null
    private var castInfo: CastInfo? = null
    private var captureWidth = 0
    private var captureHeight = 0
    @Volatile private var running = false
    @Volatile private var latest: CapturedFrame? = null
    private var socket: CastSocket? = null
    private var failed = false
    private var captureSequence = 0L
    private var lastCaptureAtNanos = 0L
    private var sourceBitmap: Bitmap? = null
    private var sourcePixelBuffer: ByteBuffer? = null
    private var targetBitmap: Bitmap? = null
    private var targetCanvas: Canvas? = null
    private val jpegBuffer = ByteArrayOutputStream(64 * 1024)
    private val scalePaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) = Unit
        override fun onDisplayRemoved(displayId: Int) = Unit

        override fun onDisplayChanged(displayId: Int) {
            if (displayId == Display.DEFAULT_DISPLAY) captureHandler?.post { configureCapture() }
        }
    }
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
        val info = intent.castInfo() ?: return START_NOT_STICKY
        val result = intent.getIntExtra("result", 0); val grant = intent.getParcelableExtra<Intent>("grant") ?: return START_NOT_STICKY
        val host = intent.getStringExtra("host") ?: return START_NOT_STICKY
        running = true; failed = false; latest = null; captureSequence = 0L; castInfo = info
        report(STATE_CONNECTING)
        projection = (getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager).getMediaProjection(result, grant)
        projection!!.registerCallback(projectionCallback, Handler(Looper.getMainLooper()))
        captureThread = HandlerThread("bms-cast-capture").also { it.start() }
        captureHandler = Handler(requireNotNull(captureThread).looper)
        displayManager = getSystemService(DISPLAY_SERVICE) as DisplayManager
        displayManager!!.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        captureHandler!!.post { configureCapture() }
        thread(name = "bms-cast", isDaemon = true) { transmit(host) }
        return START_NOT_STICKY
    }

    @Suppress("DEPRECATION")
    private fun captureSize(): Pair<Int, Int> {
        val display = (getSystemService(WINDOW_SERVICE) as WindowManager).defaultDisplay
        return display.width.coerceAtLeast(1) to display.height.coerceAtLeast(1)
    }

    private fun configureCapture() {
        if (!running) return
        val info = castInfo ?: return
        val (sourceWidth, sourceHeight) = captureSize()
        val target = info.targetFor(sourceWidth, sourceHeight)
        val (width, height) = captureSizeFor(sourceWidth, sourceHeight, target)
        if (width == captureWidth && height == captureHeight && reader != null) return
        val next = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 2)
        next.setOnImageAvailableListener({ consumeImage(it, info) }, captureHandler)
        val display = virtualDisplay
        if (display == null) {
            virtualDisplay = projection?.createVirtualDisplay(
                "BMS Cast",
                width,
                height,
                resources.displayMetrics.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                next.surface,
                null,
                null,
            )
            if (virtualDisplay == null) {
                next.close()
                failed = true
                running = false
                report(STATE_FAILED, "无法创建屏幕录制画面")
                stopSelf()
                return
            }
        } else {
            display.resize(width, height, resources.displayMetrics.densityDpi)
            display.setSurface(next.surface)
        }
        val previous = reader
        reader = next
        captureWidth = width
        captureHeight = height
        latest = null
        previous?.setOnImageAvailableListener(null, null)
        previous?.close()
    }

    private fun consumeImage(source: ImageReader, info: CastInfo) {
        if (source !== reader) return
        val image = try {
            source.acquireLatestImage()
        } catch (_: IllegalStateException) {
            null
        } ?: return
        try {
            val target = info.targetFor(image.width, image.height)
            val capturedAtNanos = SystemClock.elapsedRealtimeNanos()
            val minimumIntervalNanos = 1_000_000_000L / info.targetFps
            if (capturedAtNanos - lastCaptureAtNanos < minimumIntervalNanos) return
            lastCaptureAtNanos = capturedAtNanos

            val plane = image.planes[0]
            if (plane.pixelStride != 4 || plane.rowStride < image.width * 4 || plane.rowStride % 4 != 0) return
            val paddedWidth = plane.rowStride / plane.pixelStride
            val requiredBytesLong = plane.rowStride.toLong() * image.height
            val minimumBytesLong = plane.rowStride.toLong() * (image.height - 1) + image.width * 4L
            if (requiredBytesLong > Int.MAX_VALUE) return
            val requiredBytes = requiredBytesLong.toInt()
            val planePixels = plane.buffer.duplicate().apply { rewind() }
            if (planePixels.remaining().toLong() < minimumBytesLong) return

            val sourceFrame = bitmap(sourceBitmap, paddedWidth, image.height).also { sourceBitmap = it }
            val sourcePixels = if (planePixels.remaining() >= requiredBytes) {
                planePixels.apply { limit(position() + requiredBytes) }
            } else {
                val staging = sourcePixelBuffer?.takeIf { it.capacity() >= requiredBytes }
                    ?: ByteBuffer.allocateDirect(requiredBytes).also { sourcePixelBuffer = it }
                staging.clear()
                staging.put(planePixels)
                while (staging.position() < requiredBytes) staging.put(0.toByte())
                staging.flip()
                staging
            }
            sourceFrame.copyPixelsFromBuffer(sourcePixels)

            val output = bitmap(targetBitmap, target.width, target.height)
            if (output !== targetBitmap) {
                targetBitmap = output
                targetCanvas = Canvas(output)
            }
            targetCanvas!!.drawBitmap(
                sourceFrame,
                Rect(0, 0, image.width, image.height),
                Rect(0, 0, target.width, target.height),
                scalePaint,
            )
            val encodeStartedAtNanos = SystemClock.elapsedRealtimeNanos()
            val jpeg = compressJpeg(output, info) ?: return
            latest = CapturedFrame(
                id = ++captureSequence,
                target = target,
                jpeg = jpeg,
                capturedAtNanos = capturedAtNanos,
                encodeNanos = SystemClock.elapsedRealtimeNanos() - encodeStartedAtNanos,
            )
        } catch (_: IllegalStateException) {
            // A rotation can close the old ImageReader while its callback is in flight.
            return
        } finally { image.close() }
    }

    private fun bitmap(current: Bitmap?, width: Int, height: Int): Bitmap =
        current?.takeIf { it.width == width && it.height == height }
            ?: Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)

    private fun compressJpeg(bitmap: Bitmap, info: CastInfo): ByteArray? {
        jpegBuffer.reset()
        if (!bitmap.compress(Bitmap.CompressFormat.JPEG, info.jpegQuality, jpegBuffer)) return null
        if (jpegBuffer.size() <= info.maxFrameBytes) return jpegBuffer.toByteArray()
        if (info.jpegQuality <= CastProtocol.FALLBACK_JPEG_QUALITY) return null
        jpegBuffer.reset()
        if (!bitmap.compress(Bitmap.CompressFormat.JPEG, CastProtocol.FALLBACK_JPEG_QUALITY, jpegBuffer) ||
            jpegBuffer.size() > info.maxFrameBytes
        ) return null
        return jpegBuffer.toByteArray()
    }

    private fun transmit(host: String) {
        var failure: Exception? = null
        for (attempt in 1..3) {
            if (!running) break
            try {
                report(if (attempt == 1) STATE_CONNECTING else STATE_RETRYING)
                socket = CastSocket(host)
                report(STATE_STREAMING)
                var sequence = 1
                var sentFrameId = 0L
                var heartbeatAt = System.currentTimeMillis() + 2_000
                val metrics = CastSenderMetrics()
                while (running) {
                    val frame = latest
                    if (frame != null && frame.id != sentFrameId) {
                        val sentAtNanos = SystemClock.elapsedRealtimeNanos()
                        socket!!.send(CastProtocol.jpegFrame(sequence, frame.target.rotation, frame.jpeg))
                        if (socket!!.readAck() != sequence) {
                            throw IOException("设备未确认画面 $sequence")
                        }
                        metrics.record(
                            frame = frame,
                            previousFrameId = sentFrameId,
                            sentAtNanos = sentAtNanos,
                            acknowledgedAtNanos = SystemClock.elapsedRealtimeNanos(),
                        )
                        sentFrameId = frame.id
                        sequence = if (sequence == Int.MAX_VALUE) 1 else sequence + 1
                    } else {
                        if (System.currentTimeMillis() >= heartbeatAt) {
                            socket!!.send(byteArrayOf(CastProtocol.HEARTBEAT))
                            heartbeatAt = System.currentTimeMillis() + 2_000
                        }
                        Thread.sleep(10)
                    }
                }
                return
            } catch (e: Exception) {
                failure = e
                socket?.close()
                socket = null
                if (attempt < 3 && running) {
                    report(STATE_RETRYING)
                    Thread.sleep(1_000)
                }
            }
        }
        if (running && failure != null) {
            failed = true
            report(STATE_FAILED, "投屏连接失败（已重试 3 次）：${failure.message ?: failure.javaClass.simpleName}")
        }
        stopSelf()
    }
    private fun report(state: String, detail: String? = null) {
        currentState = state
        sendBroadcast(Intent(ACTION_STATUS).setPackage(packageName).putExtra(EXTRA_STATE, state).putExtra(EXTRA_DETAIL, detail))
    }
    override fun onDestroy() {
        running = false
        socket?.close()
        socket = null
        displayManager?.unregisterDisplayListener(displayListener)
        displayManager = null
        reader?.close()
        reader = null
        virtualDisplay?.release()
        virtualDisplay = null
        captureHandler?.removeCallbacksAndMessages(null)
        captureHandler = null
        captureThread?.quitSafely()
        captureThread = null
        castInfo = null
        projection?.unregisterCallback(projectionCallback)
        projection?.stop()
        projection = null
        if (!failed) report(STATE_STOPPED)
        super.onDestroy()
    }
    override fun onBind(intent: Intent?): IBinder? = null
    private fun notification(): Notification { val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager; val channel = NotificationChannel("cast", "两轮智控", NotificationManager.IMPORTANCE_LOW); manager.createNotificationChannel(channel); return Notification.Builder(this, "cast").setContentTitle("两轮智控投屏中").setSmallIcon(android.R.drawable.presence_video_online).build() }
    companion object {
        const val ACTION_STATUS = "com.fuckingbms.cast.STATUS"
        const val EXTRA_STATE = "state"
        const val EXTRA_DETAIL = "detail"
        const val STATE_CONNECTING = "connecting"
        const val STATE_RETRYING = "retrying"
        const val STATE_STREAMING = "streaming"
        const val STATE_FAILED = "failed"
        const val STATE_STOPPED = "stopped"
        @Volatile var currentState = STATE_STOPPED
            private set

        fun intent(context: Context, result: Int, grant: Intent, host: String, info: CastInfo) = Intent(context, CastService::class.java).apply {
            putExtra("result", result)
            putExtra("grant", grant)
            putExtra("host", host)
            putExtra("physical_width", info.physicalWidth)
            putExtra("physical_height", info.physicalHeight)
            putExtra("codec", info.codec)
            putExtra("jpeg_quality", info.jpegQuality)
            putExtra("target_fps", info.targetFps)
            putExtra("max_frame_bytes", info.maxFrameBytes)
            putExtra("targets", info.orientations.flatMap { listOf(it.rotation, it.width, it.height) }.toIntArray())
        }
    }
}

internal fun captureSizeFor(sourceWidth: Int, sourceHeight: Int, target: CastTarget): Pair<Int, Int> {
    require(sourceWidth > 0 && sourceHeight > 0)
    val scale = maxOf(
        target.width.toDouble() / sourceWidth,
        target.height.toDouble() / sourceHeight,
    )
    return ceil(sourceWidth * scale).toInt() to ceil(sourceHeight * scale).toInt()
}

private data class CapturedFrame(
    val id: Long,
    val target: CastTarget,
    val jpeg: ByteArray,
    val capturedAtNanos: Long,
    val encodeNanos: Long,
)

private class CastSenderMetrics {
    private var startedAtNanos = SystemClock.elapsedRealtimeNanos()
    private var sentFrames = 0L
    private var coalescedFrames = 0L
    private var totalEncodeNanos = 0L
    private var totalAckNanos = 0L
    private var totalFrameAgeNanos = 0L
    private var maxFrameAgeNanos = 0L

    fun record(
        frame: CapturedFrame,
        previousFrameId: Long,
        sentAtNanos: Long,
        acknowledgedAtNanos: Long,
    ) {
        val ackNanos = (acknowledgedAtNanos - sentAtNanos).coerceAtLeast(0L)
        val frameAgeNanos = (acknowledgedAtNanos - frame.capturedAtNanos).coerceAtLeast(0L)
        ++sentFrames
        coalescedFrames += (frame.id - previousFrameId - 1L).coerceAtLeast(0L)
        totalEncodeNanos += frame.encodeNanos.coerceAtLeast(0L)
        totalAckNanos += ackNanos
        totalFrameAgeNanos += frameAgeNanos
        maxFrameAgeNanos = maxOf(maxFrameAgeNanos, frameAgeNanos)

        if (acknowledgedAtNanos - startedAtNanos < CAST_METRICS_LOG_WINDOW_NANOS) return
        Log.i(
            "CastService",
            "[cast] sent=$sentFrames coalesced=$coalescedFrames " +
                "avg_encode_ms=${totalEncodeNanos.toDouble() / sentFrames / 1_000_000.0} " +
                "avg_send_ack_ms=${totalAckNanos.toDouble() / sentFrames / 1_000_000.0} " +
                "avg_ack_age_ms=${totalFrameAgeNanos.toDouble() / sentFrames / 1_000_000.0} " +
                "max_ack_age_ms=${maxFrameAgeNanos.toDouble() / 1_000_000.0}",
        )
        startedAtNanos = acknowledgedAtNanos
        sentFrames = 0L
        coalescedFrames = 0L
        totalEncodeNanos = 0L
        totalAckNanos = 0L
        totalFrameAgeNanos = 0L
        maxFrameAgeNanos = 0L
    }
}

private fun Intent.castInfo(): CastInfo? = runCatching {
    val values = getIntArrayExtra("targets") ?: error("缺少投屏方向")
    require(values.size >= 6 && values.size % 3 == 0)
    CastInfo(
        physicalWidth = getIntExtra("physical_width", 0),
        physicalHeight = getIntExtra("physical_height", 0),
        orientations = values.asList().chunked(3).map { CastTarget(it[0], it[1], it[2]) },
        codec = getStringExtra("codec") ?: error("缺少投屏编码"),
        jpegQuality = getIntExtra("jpeg_quality", 0),
        targetFps = getIntExtra("target_fps", 0),
        maxFrameBytes = getIntExtra("max_frame_bytes", 0),
    )
}.getOrNull()

private class CastSocket(host: String) {
    private val endpoint = URI("http://$host")
    private val socket = Socket().apply {
        connect(InetSocketAddress(requireNotNull(endpoint.host), if (endpoint.port >= 0) endpoint.port else 80), 4_000)
        soTimeout = 4_000
        tcpNoDelay = true
    }; private val output = BufferedOutputStream(socket.getOutputStream()); private val input = BufferedInputStream(socket.getInputStream()); private val random = SecureRandom()
    init {
        val key = Base64.encodeToString(ByteArray(16).also { random.nextBytes(it) }, Base64.NO_WRAP)
        output.write("GET /cast HTTP/1.1\r\nHost: $host\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: $key\r\n\r\n".toByteArray())
        output.flush()
        val response = generateSequence { readLine() }.takeWhile { it.isNotEmpty() }.toList()
        check(response.firstOrNull()?.contains(" 101 ") == true) { "WebSocket 握手失败" }
    }
    fun send(payload: ByteArray) { val mask = ByteArray(4).also { random.nextBytes(it) }; output.write(0x82); output.write(CastProtocol.maskedPayloadLength(payload.size)); output.write(mask); for (index in payload.indices) payload[index] = (payload[index].toInt() xor mask[index % 4].toInt()).toByte(); output.write(payload); output.flush() }
    fun readAck(): Int? {
        val first = input.read()
        val second = input.read()
        if (first != 0x82 || second !in 0..127 || second != 5) return null
        val data = ByteArray(5)
        var offset = 0
        while (offset < data.size) {
            val read = input.read(data, offset, data.size - offset)
            if (read < 0) return null
            offset += read
        }
        return CastProtocol.ackSequence(data)
    }
    private fun readLine(): String { val bytes = ArrayList<Byte>(); while (true) { val value = input.read(); if (value < 0 || value == '\n'.code) return bytes.toByteArray().toString(Charsets.ISO_8859_1).trimEnd('\r'); bytes += value.toByte() } }
    fun close() { socket.close() }
}
