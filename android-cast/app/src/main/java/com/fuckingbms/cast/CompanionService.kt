package com.fuckingbms.cast

import android.Manifest
import android.app.*
import android.graphics.Bitmap
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.*
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/** Connected-device foreground service keeps the single BLE link alive while the app is backgrounded. */
class CompanionService : Service(), LocationListener {
    private val handler = Handler(Looper.getMainLooper())
    private val frames = Executors.newSingleThreadExecutor()
    private val riding by lazy { CompanionRiding(this) }
    private val sending = AtomicBoolean(false)
    private val clearNavigation = AtomicBoolean(false)
    private val generation = AtomicLong(0)
    private lateinit var ble: DeviceBleSession
    @Volatile private var width = 0; private var height = 0; private var musicHeight = 0
    private var signature = ""; private var lastPing = 0L; private var lastReconnect = 0L
    private var lastInfo = 0L; private var displaySlot = -1
    private var lastFrame = 0L; private var lastRouteRequest = 0L
    @Volatile private var running = true
    @Volatile private var navigator: LocalNavigator? = null
    @Volatile private var progress: NavProgress? = null
    @Volatile private var target: NavPoint? = null
    private var destinationName = ""
    @Volatile private var requestedPage = -1
    private var routing = false
    private var waitingForRouteFix = false
    private var location: Location? = null
    private val prefs by lazy { getSharedPreferences("companion",MODE_PRIVATE) }
    private val locations by lazy { getSystemService(Context.LOCATION_SERVICE) as LocationManager }

    override fun onCreate() {
        super.onCreate(); active = this
        ble = CompanionLink.observe(this,this) { state,_ ->
            if (state == DeviceBleState.CONNECTED) { width=0; signature="" }
        }
        (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).createNotificationChannel(
            NotificationChannel("companion","音乐与导航",NotificationManager.IMPORTANCE_LOW))
        foreground(false)
        handler.post(tick)
    }

    private fun foreground(navigating: Boolean) {
        val open = PendingIntent.getActivity(this,0,Intent(this,CompanionActivity::class.java),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val stop = PendingIntent.getService(this,1,Intent(this,CompanionService::class.java).setAction("STOP"),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val notification = Notification.Builder(this,"companion").setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(if (navigating) "本地导航进行中" else "音乐同步已开启")
            .setContentText(if (navigating) destinationName else "自动同步歌名、歌手与可用歌词")
            .setContentIntent(open).setOngoing(true).addAction(Notification.Action.Builder(null,"停止",stop).build()).build()
        var types = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        if (navigating) types = types or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
        startForeground(52,notification,types)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            "STOP" -> { prefs.edit().putBoolean("enabled",false).apply(); stopSelf(); return START_NOT_STICKY }
            "NAV_STOP" -> stopNavigation()
            "SHOW_MUSIC" -> { requestedPage=0; lastInfo=0 }
            "NAV_START" -> startNavigation(intent.getStringExtra("destination").orEmpty())
        }
        return START_STICKY
    }

    private val tick = object : Runnable {
        override fun run() {
            if (!running) return
            val now = SystemClock.elapsedRealtime()
            if (!ble.connected && ble.state != DeviceBleState.CONNECTING && ble.state != DeviceBleState.SCANNING && now-lastReconnect > 15000) {
                lastReconnect=now
                if (DeviceBleSession.hasPermissions(this@CompanionService)) getSharedPreferences("ble",MODE_PRIVATE).getString("last_mac",null)?.let(ble::connect)
            }
            val gps=location
            if (gps != null && progress?.arrived != true && now-gps.elapsedRealtimeNanos/1000000 > 15000) {
                progress=progress?.copy(reliable=false,offRoute=false)
                navigationStatus="GPS 信号中断，等待可靠定位"
            }
            val music = CompanionMusic.refresh(this@CompanionService)
            if ((ble.connected || (previewVisible && navigator != null)) && sending.compareAndSet(false,true)) {
                val epoch = generation.get()
                frames.execute {
                    try {
                        if (ble.connected && clearNavigation.compareAndSet(true,false)) {
                            try { ble.request("POST","/api/companion",JSONObject().put("op","clear").put("slot",1)) }
                            catch (error: Exception) { clearNavigation.set(true); throw error }
                        }
                        if (ble.connected && requestedPage >= 0) {
                            ble.request("POST","/api/companion",JSONObject().put("op","select").put("slot",requestedPage))
                            requestedPage=-1; lastInfo=0
                        }
                        if (ble.connected && (width == 0 || now-lastInfo > 1200)) {
                            pollDevice()
                        }
                        fun cancelled() = !running || generation.get() != epoch || !ble.connected
                        fun pollDuringFrame() {
                            if (SystemClock.elapsedRealtime()-lastInfo >= 1200) pollDevice()
                        }
                        val currentSignature = "${music.title}\u0000${music.artist}\u0000${music.line}\u0000${music.playing}"
                        if (ble.connected && displaySlot == 0 && currentSignature != signature) {
                            if (music.title.isBlank()) ble.request("POST","/api/companion",JSONObject().put("op","clear").put("slot",0))
                            else CompanionFrame.music(width,musicHeight,music).let { image ->
                                try { CompanionFrame.send(ble,0,image,{ cancelled() || displaySlot != 0 },::pollDuringFrame) } finally { image.recycle() }
                            }
                            signature=currentSignature
                        }
                        val nav = navigator; val position = progress
                        if (nav != null && position != null && ((ble.connected && displaySlot == 1) || previewVisible) && now-lastFrame >= 2500 && running && generation.get() == epoch) {
                            CompanionNavigationFrame.render(this@CompanionService,if (width > 0) width else 320,if (height > 0) height else 480,nav,position,prefs.getString("tdt_key",BuildConfig.DEFAULT_TIANDITU_KEY).orEmpty()).let { image ->
                                try {
                                    if (previewVisible) preview=image.copy(Bitmap.Config.ARGB_8888,false)
                                    if (ble.connected && displaySlot == 1) CompanionFrame.send(ble,1,image,{ cancelled() || displaySlot != 1 },::pollDuringFrame)
                                } finally { image.recycle() }
                            }
                            lastFrame=now
                        }
                        if (now-lastPing >= 10000 && !cancelled()) {
                            for (slot in 0..1) ble.request("POST","/api/companion",JSONObject().put("op","ping").put("slot",slot))
                            lastPing=now
                        }
                        linkStatus=if (ble.connected) "设备已连接 · 自动同步" else "手机导航中 · 等待设备连接"
                    } catch (_: Exception) {
                        // Missing metadata and a disconnected display are normal; the device expires stale frames itself.
                        linkStatus=if (ble.connected) "等待设备支持同步协议" else "等待设备连接"
                    } finally { sending.set(false) }
                }
            }
            handler.postDelayed(this,600)
        }
    }

    private fun pollDevice() {
        val info = JSONObject(ble.request("GET","/api/companion",null))
        require(info.optInt("version") == 1 && info.optString("encoding") == "rle4")
        val w = info.getInt("width"); val h = info.getInt("height")
        require(w in 2..480 && w%2 == 0 && h in 1..480 && w*h <= 153600)
        val actionId=info.optLong("action_id",0)
        if (actionId != 0L) {
            val token="${info.optLong("boot_id")}:$actionId"
            if (prefs.getString("last_action","") != token) {
                check(prefs.edit().putString("last_action",token).commit())
                when (info.optInt("action")) {
                    1 -> CompanionMusic.control("previous")
                    2 -> CompanionMusic.control("toggle")
                    3 -> CompanionMusic.control("next")
                    4,5 -> (getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager).adjustStreamVolume(
                        android.media.AudioManager.STREAM_MUSIC,if (info.optInt("action") == 4) -1 else 1,0)
                }
            }
            ble.request("POST","/api/companion",JSONObject().put("op","ack").put("slot",0).put("frame",actionId))
        }
        val slot=info.optInt("page",-1)
        if (slot != displaySlot || w != width || h != height) { signature=""; lastFrame=0 }
        displaySlot=slot; lastInfo=SystemClock.elapsedRealtime()
        width=w; height=h; musicHeight=info.getInt("music_height").coerceIn(1,h)
    }

    private fun startNavigation(query: String) {
        if (query.isBlank() || routing) return
        generation.incrementAndGet(); clearNavigation.set(true); navigator=null; progress=null; preview=null
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            navigationStatus="请允许精确定位后再开始导航"; return
        }
        try {
            foreground(true)
            locations.requestLocationUpdates(LocationManager.GPS_PROVIDER,1000L,0f,this,Looper.getMainLooper())
            location = listOf(LocationManager.GPS_PROVIDER,LocationManager.NETWORK_PROVIDER).mapNotNull {
                runCatching { locations.getLastKnownLocation(it) }.getOrNull()
            }.filter { SystemClock.elapsedRealtimeNanos()-it.elapsedRealtimeNanos < 60_000_000_000L }.maxByOrNull { it.elapsedRealtimeNanos }
        } catch (_: Exception) { navigationStatus="定位不可用，请开启系统定位"; return }
        destinationName=query.take(160); target=null; waitingForRouteFix=true
        navigationStatus="等待 GPS 定位，随后请求一次路线"
        val gps = location
        if (gps != null && gps.accuracy <= 40) { waitingForRouteFix=false; requestRoute(gps,false) }
    }

    private fun requestRoute(gps: Location, automatic: Boolean) {
        if (routing || !running || destinationName.isBlank()) return
        val now = SystemClock.elapsedRealtime()
        if (lastRouteRequest != 0L && now-lastRouteRequest < if (automatic) 120000 else 10000) {
            if (!automatic) navigationStatus="请求过于频繁，请十秒后再试"
            return
        }
        val day = java.text.SimpleDateFormat("yyyy-MM-dd",java.util.Locale.ROOT).format(java.util.Date())
        val calls = if (prefs.getString("budget_day","") == day) prefs.getInt("route_calls",0) else 0
        if (calls >= 20) { navigationStatus="今日已请求 20 次路线，已暂停自动重算"; return }
        // Count attempts, including failures; retries also consume provider quotas.
        prefs.edit().putString("budget_day",day).putInt("route_calls",calls+1).apply()
        lastRouteRequest=now; routing=true; navigationStatus=if (automatic) "偏航确认，正在重算" else "正在获取路线（仅本次请求）"
        val epoch = generation.get(); val name = destinationName
        riding.request(prefs.getString("amap_js_key",BuildConfig.DEFAULT_AMAP_JS_KEY).orEmpty(),
            prefs.getString("amap_security_js_code",BuildConfig.DEFAULT_AMAP_SECURITY_JS_CODE).orEmpty(),
            NavPoint(gps.latitude,gps.longitude),target,name) { result ->
                routing=false
                if (!running || epoch != generation.get()) return@request
                result.onSuccess { (destination,route) ->
                    val nav=LocalNavigator(route)
                    target=destination; navigator=nav
                    progress=nav.update(NavPoint(gps.latitude,gps.longitude),gps.accuracy,SystemClock.elapsedRealtime())
                    navigationStatus="导航中 · 本地定位 · 今日算路 ${calls+1}/20"; foreground(true)
                    requestedPage=1; lastInfo=0
                }.onFailure { navigationStatus=it.message?.take(160) ?: "算路失败，请稍后重试" }
        }
    }

    override fun onLocationChanged(value: Location) {
        if (SystemClock.elapsedRealtimeNanos()-value.elapsedRealtimeNanos > 15_000_000_000L) return
        location=value
        val nav = navigator
        if (nav == null) {
            if (waitingForRouteFix && value.accuracy <= 40 && destinationName.isNotBlank()) {
                waitingForRouteFix=false; requestRoute(value,false)
            }
            return
        }
        val state=nav.update(NavPoint(value.latitude,value.longitude),value.accuracy,SystemClock.elapsedRealtime())
        progress=state
        navigationStatus=when {
            state.arrived -> "已到达 ${nav.route.destination}"
            !state.reliable -> "GPS 精度不足，等待可靠定位"
            state.offRoute -> "确认偏航，等待重算冷却"
            else -> "${CompanionNavigationFrame.distance(state.turnDistance)} · ${state.instruction}"
        }
        if (state.offRoute) requestRoute(value,true)
        if (state.arrived) locations.removeUpdates(this)
    }

    private fun stopNavigation() {
        generation.incrementAndGet(); clearNavigation.set(true); navigator=null; progress=null; preview=null; target=null; destinationName=""; waitingForRouteFix=false
        riding.cancel(); routing=false
        locations.removeUpdates(this); navigationStatus="导航未开始"; foreground(false)
    }

    override fun onDestroy() {
        running=false; generation.incrementAndGet(); handler.removeCallbacksAndMessages(null)
        locations.removeUpdates(this); CompanionLink.remove(this); active=null
        frames.execute {
            for (slot in 0..1) runCatching { ble.request("POST","/api/companion",JSONObject().put("op","clear").put("slot",slot)) }
        }
        frames.shutdown(); riding.close(); super.onDestroy()
    }
    override fun onBind(intent: Intent?) = null

    companion object {
        @Volatile var previewVisible=false
        @Volatile var preview: Bitmap? = null; private set
        @Volatile private var active: CompanionService? = null
        @Volatile var linkStatus="等待设备连接"; private set
        @Volatile var navigationStatus="导航未开始"; private set
        fun startWhenAllowed(context: Context) {
            if (DeviceBleSession.hasPermissions(context) && context.getSharedPreferences("companion",MODE_PRIVATE).getBoolean("enabled",true)) {
                runCatching { context.startForegroundService(Intent(context,CompanionService::class.java)) }
            }
        }
        fun isRunning() = active != null
    }
}
