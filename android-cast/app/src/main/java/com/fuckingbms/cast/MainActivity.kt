package com.fuckingbms.cast

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

internal enum class UiStage {
    WAITING_SCAN, CONNECTING_WIFI, QUERYING_DEVICE, READY, REQUESTING_PERMISSION,
    CAST_CONNECTING, CASTING, STOPPED, FAILED
}

internal data class PrimaryAction(val label: String, val enabled: Boolean, val stopsCast: Boolean = false)

internal fun primaryAction(stage: UiStage, deviceReady: Boolean) = when (stage) {
    UiStage.CASTING -> PrimaryAction("停止投屏", true, true)
    UiStage.CAST_CONNECTING -> PrimaryAction("停止投屏", true, true)
    UiStage.REQUESTING_PERMISSION -> PrimaryAction("正在申请投屏权限", false)
    UiStage.STOPPED -> PrimaryAction("重新开始投屏", deviceReady)
    UiStage.FAILED -> PrimaryAction("重试投屏", deviceReady)
    else -> PrimaryAction("开始投屏", stage == UiStage.READY && deviceReady)
}

class MainActivity : Activity() {
    private lateinit var statusView: TextView
    private lateinit var deviceView: TextView
    private lateinit var castButton: Button
    private lateinit var connectionButton: Button
    private var stage = UiStage.WAITING_SCAN
    private var detail: String? = null
    private var host = "192.168.4.1"
    private var ssid: String? = null
    private var network: Network? = null
    private var info: CastInfo? = null
    private var infoRequestInFlight = false
    private var infoRequestId = 0

    private val castStatusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.getStringExtra(CastService.EXTRA_STATE)) {
                CastService.STATE_CONNECTING, CastService.STATE_RETRYING -> setStage(UiStage.CAST_CONNECTING)
                CastService.STATE_STREAMING -> setStage(UiStage.CASTING)
                CastService.STATE_FAILED -> {
                    info = null
                    setStage(UiStage.FAILED, intent.getStringExtra(CastService.EXTRA_DETAIL))
                }
                CastService.STATE_STOPPED -> if (info != null) setStage(UiStage.STOPPED)
            }
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        registerReceiverCompat(castStatusReceiver, CastService.ACTION_STATUS)
        content()
        if (readDeepLink(intent)) {
            connectExistingWifiOrLoadInfo()
        } else {
            render()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (readDeepLink(intent)) {
            stopService(Intent(this, CastService::class.java))
            releaseNetworkRequest()
            invalidateInfoRequest()
            info = null
            connectExistingWifiOrLoadInfo()
        } else {
            setStage(UiStage.FAILED, "投屏二维码无效，请在设备投屏页重新扫码")
        }
    }

    override fun onResume() {
        super.onResume()
        when (CastService.currentState) {
            CastService.STATE_CONNECTING, CastService.STATE_RETRYING -> setStage(UiStage.CAST_CONNECTING)
            CastService.STATE_STREAMING -> setStage(UiStage.CASTING)
            else -> connectExistingWifiOrLoadInfo()
        }
    }

    override fun onDestroy() {
        releaseNetworkRequest()
        invalidateInfoRequest()
        unregisterReceiver(castStatusReceiver)
        connectivityManager().bindProcessToNetwork(null)
        super.onDestroy()
    }

    private fun readDeepLink(intent: Intent): Boolean {
        val uri = intent.data ?: return false
        val linkedHost = uri.getQueryParameter("host")
        val linkedSsid = uri.getQueryParameter("ssid")
        if (linkedHost != host) return false
        ssid = linkedSsid
        return true
    }

    private fun content() {
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.START
            setPadding(dp(24), dp(24), dp(24), dp(24))
        }
        column.addView(TextView(this).apply {
            text = "BMS 无线投屏"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 26f)
        }, rowParams(bottom = 24))
        column.addView(TextView(this).apply {
            text = "当前状态"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        })
        statusView = TextView(this).apply { setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f) }
        column.addView(statusView, rowParams(bottom = 24))
        column.addView(TextView(this).apply {
            text = "设备信息"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        })
        deviceView = TextView(this).apply { setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f) }
        column.addView(deviceView, rowParams(bottom = 24))
        castButton = Button(this).apply {
            setOnClickListener {
                if (primaryAction(stage, info != null).stopsCast) stopCasting() else requestProjection()
            }
        }
        column.addView(castButton, rowParams(bottom = 12))
        connectionButton = Button(this).apply {
            setOnClickListener {
                info = null
                connectExistingWifiOrLoadInfo()
            }
        }
        column.addView(connectionButton, rowParams(bottom = 24))
        column.addView(TextView(this).apply {
            text = "投屏仅在手机与设备热点之间传输。网络较慢时会主动丢弃旧画面以减少延迟。"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        })
        setContentView(ScrollView(this).apply {
            isFillViewport = true
            setOnApplyWindowInsetsListener { view, insets ->
                view.setPadding(
                    insets.systemWindowInsetLeft,
                    insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight,
                    insets.systemWindowInsetBottom
                )
                insets
            }
            addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        })
    }

    private fun rowParams(bottom: Int = 0) = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    ).apply { bottomMargin = dp(bottom) }

    private fun dp(value: Int) = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP,
        value.toFloat(),
        resources.displayMetrics
    ).toInt()

    private fun registerReceiverCompat(receiver: BroadcastReceiver, action: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(receiver, IntentFilter(action))
        }
    }

    private fun connectivityManager() = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    private fun connectExistingWifiOrLoadInfo() {
        if (infoRequestInFlight || stage in setOf(UiStage.REQUESTING_PERMISSION, UiStage.CAST_CONNECTING, UiStage.CASTING)) return
        val cm = connectivityManager()
        val wifi = network?.takeIf { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
            ?: cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
        if (wifi != null) {
            network = wifi
            cm.bindProcessToNetwork(wifi)
            loadInfo()
        } else {
            setStage(UiStage.WAITING_SCAN, "请先在系统 Wi-Fi 设置中连接设备热点")
        }
    }

    private fun releaseNetworkRequest() {
        network = null
    }

    private fun invalidateInfoRequest() {
        infoRequestId++
        infoRequestInFlight = false
    }

    private fun loadInfo() {
        if (infoRequestInFlight) return
        infoRequestInFlight = true
        val requestId = ++infoRequestId
        setStage(UiStage.QUERYING_DEVICE)
        thread {
            try {
                val connection = (URL("http://$host/api/cast/info").openConnection() as HttpURLConnection).apply {
                    connectTimeout = 4_000
                    readTimeout = 4_000
                }
                if (connection.responseCode != 200) error("HTTP ${connection.responseCode}")
                val json = connection.inputStream.bufferedReader().use { it.readText() }
                val loaded = CastInfo.parse(json)
                runOnUiThread {
                    if (requestId != infoRequestId) return@runOnUiThread
                    infoRequestInFlight = false
                    info = loaded
                    setStage(UiStage.READY)
                }
            } catch (error: Exception) {
                runOnUiThread {
                    if (requestId != infoRequestId) return@runOnUiThread
                    infoRequestInFlight = false
                    info = null
                    setStage(UiStage.FAILED, "无法获取设备投屏能力：${error.message ?: error.javaClass.simpleName}")
                }
            }
        }
    }

    private fun requestProjection() {
        if (info == null) return
        setStage(UiStage.REQUESTING_PERMISSION)
        val manager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val request = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            manager.createScreenCaptureIntent()
        }
        startActivityForResult(request, PROJECTION_REQUEST)
    }

    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        if (request != PROJECTION_REQUEST) return
        val castInfo = info
        if (result != RESULT_OK || data == null) return setStage(UiStage.FAILED, "未授予屏幕录制权限，点击重试投屏可再次申请")
        if (castInfo == null) return setStage(UiStage.FAILED, "设备连接已失效，请重新连接")
        startForegroundService(CastService.intent(this, result, data, host, castInfo))
        setStage(UiStage.CAST_CONNECTING)
    }

    private fun stopCasting() {
        stopService(Intent(this, CastService::class.java))
        setStage(UiStage.STOPPED)
    }

    private fun setStage(value: UiStage, message: String? = null) {
        stage = value
        detail = message
        render()
    }

    private fun render() {
        if (!::statusView.isInitialized) return
        statusView.text = detail ?: when (stage) {
            UiStage.WAITING_SCAN -> "等待连接设备热点"
            UiStage.CONNECTING_WIFI -> "正在连接设备热点…"
            UiStage.QUERYING_DEVICE -> "热点已连接，正在查询设备能力…"
            UiStage.READY -> "设备已就绪"
            UiStage.REQUESTING_PERMISSION -> "请确认屏幕录制权限"
            UiStage.CAST_CONNECTING -> "正在连接投屏服务，失败时会自动重试…"
            UiStage.CASTING -> "投屏中"
            UiStage.STOPPED -> "投屏已停止"
            UiStage.FAILED -> "操作失败，请重试"
        }
        deviceView.text = info?.let { "${it.width} × ${it.height}  ·  旋转 ${it.rotation}°\n目标：$host" }
            ?: ssid?.let { "热点：$it\n目标：$host" }
            ?: "尚未连接设备"
        val action = primaryAction(stage, info != null)
        castButton.text = action.label
        castButton.isEnabled = action.enabled
        connectionButton.text = when {
            stage == UiStage.FAILED && info == null -> "重试连接"
            else -> "连接 / 刷新设备"
        }
        connectionButton.isEnabled = !infoRequestInFlight && stage !in setOf(
            UiStage.REQUESTING_PERMISSION, UiStage.CAST_CONNECTING, UiStage.CASTING
        )
    }

    companion object { private const val PROJECTION_REQUEST = 10 }
}
