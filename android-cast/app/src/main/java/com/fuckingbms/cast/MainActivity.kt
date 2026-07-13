package com.fuckingbms.cast

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.projection.MediaProjectionManager
import android.media.projection.MediaProjectionConfig
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Bundle
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var castButton: Button
    private var host = "192.168.4.1"
    private var ssid: String? = null
    private var password: String? = null
    private var network: Network? = null
    private var info: CastInfo? = null
    private val castStatusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            show(intent.getStringExtra(CastService.EXTRA_STATUS) ?: return)
        }
    }

    override fun onCreate(state: Bundle?) { super.onCreate(state); if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) registerReceiver(castStatusReceiver, IntentFilter(CastService.ACTION_STATUS), Context.RECEIVER_NOT_EXPORTED) else registerReceiver(castStatusReceiver, IntentFilter(CastService.ACTION_STATUS)); readDeepLink(intent); content(); connectExistingWifiOrLoadInfo() }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); readDeepLink(intent); content() }
    override fun onResume() { super.onResume(); connectExistingWifiOrLoadInfo() }
    override fun onDestroy() { unregisterReceiver(castStatusReceiver); (getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).bindProcessToNetwork(null); super.onDestroy() }

    private fun readDeepLink(intent: Intent) { intent.data?.let { host = it.getQueryParameter("host") ?: host; ssid = it.getQueryParameter("ssid") ?: ssid; password = it.getQueryParameter("password") ?: password } }
    private fun content() {
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; setPadding(32,32,32,32) }
        status = TextView(this).apply { text = "连接设备热点后，点击开始投屏。\n目标：$host\n未启用无障碍服务：仅镜像。" }; root.addView(status)
        root.addView(Button(this).apply { text = "连接/刷新设备"; setOnClickListener { connectExistingWifiOrLoadInfo() } })
        castButton = Button(this).apply { text = "开始投屏"; isEnabled = false; setOnClickListener { requestProjection() } }; root.addView(castButton)
        root.addView(Button(this).apply { text = "停止投屏"; setOnClickListener { stopService(Intent(this@MainActivity, CastService::class.java)) } })
        root.addView(Button(this).apply { text = "返回（需无障碍服务）"; isEnabled = false })
        root.addView(Button(this).apply { text = "主页（需无障碍服务）"; isEnabled = false })
        setContentView(root)
    }
    private fun connectExistingWifiOrLoadInfo() {
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val active = cm.activeNetwork
        if (active != null && cm.getNetworkCapabilities(active)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) {
            network = active
            cm.bindProcessToNetwork(active)
            loadInfo()
            return
        }
        if (ssid == null || password == null) {
            // A manually joined device hotspot needs no QR credentials.
            loadInfo()
            return
        }
        requestSetupNetwork(cm)
    }
    private fun requestSetupNetwork(cm: ConnectivityManager) {
        val name = ssid ?: return show("二维码缺少热点 SSID，请在设备 REMOTE CAST 页扫码")
        val secret = password ?: return show("二维码缺少热点密码")
        val request = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .setNetworkSpecifier(WifiNetworkSpecifier.Builder().setSsid(name).setWpa2Passphrase(secret).build()).build()
        cm.requestNetwork(request, object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(value: Network) { network = value; cm.bindProcessToNetwork(value); loadInfo() }
            override fun onUnavailable() { loadInfo(); show("未能请求热点；若已手动连接，请稍候自动检测") }
            override fun onLost(value: Network) { info = null; castButton.isEnabled = false; stopService(Intent(this@MainActivity, CastService::class.java)); show("热点已断开，投屏已停止") }
        })
        show("等待系统确认连接 $name")
    }
    private fun loadInfo() = thread {
        try {
            val connection = (URL("http://$host/api/cast/info").openConnection() as HttpURLConnection).apply {
                connectTimeout = 4_000; readTimeout = 4_000
            }
            if (connection.responseCode != 200) error("HTTP ${connection.responseCode}")
            val json = connection.inputStream.bufferedReader().readText()
            fun value(name: String) = Regex("\\\"$name\\\":(\\d+)").find(json)?.groupValues?.get(1)?.toInt() ?: error("invalid cast info")
            val loaded = CastInfo(value("width"), value("height"), value("rotation"), value("max_block_side"))
            runOnUiThread { info = loaded; castButton.isEnabled = true; show("热点已连接；设备 ${loaded.width}×${loaded.height}，可开始投屏") }
        } catch (e: Exception) { show("无法获取设备投屏能力：${e.message}") }
    }
    private fun requestProjection() {
        val manager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val request = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            manager.createScreenCaptureIntent()
        }
        startActivityForResult(request, 10)
    }
    override fun onActivityResult(request: Int, result: Int, data: Intent?) { super.onActivityResult(request,result,data); if (request != 10 || result != RESULT_OK || data == null || info == null) return show("投屏权限未授予或设备未连接"); startForegroundService(CastService.intent(this, result, data, host, info!!)); show("投屏已启动；网络慢时将丢弃旧画面") }
    private fun show(message: String) { Handler(Looper.getMainLooper()).post { status.text = message } }
}

data class CastInfo(val width: Int, val height: Int, val rotation: Int, val maxBlockSide: Int)
