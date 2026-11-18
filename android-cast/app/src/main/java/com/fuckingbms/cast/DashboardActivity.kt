package com.fuckingbms.cast

import android.annotation.SuppressLint
import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.IntentFilter
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.Build
import android.provider.Settings
import android.view.View
import android.view.WindowInsets
import android.webkit.JavascriptInterface
import android.webkit.MimeTypeMap
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.Toast
import org.json.JSONObject
import org.json.JSONTokener
import java.io.ByteArrayInputStream
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import java.util.concurrent.Executors

// Adapted from dashboard's MainActivity/Client: trusted WebView document, native bridge and system insets.
class DashboardActivity : Activity() {
    private lateinit var web: WebView
    private lateinit var ble: DeviceBleSession
    private val requests = Executors.newSingleThreadExecutor()
    private var chooser: ValueCallback<Array<Uri>>? = null
    @Volatile private var bleError: String? = null
    @Volatile private var castStatus = JSONObject().put("state", CastService.currentState).toString()
    @Volatile private var appUpdateState = "{\"status\":\"idle\"}"
    private val appUpdateBusy = AtomicBoolean(false)
    private var castAttempt = 0
    private var projectionRequestOpen = false
    private var connectionSettingsOpen = false
    private var connectionSettingsPaused = false
    private var pendingCast: Pair<String, CastInfo>? = null
    private var projectionInsets = CaptureInsets.ZERO
    private val castReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            reportCast(intent.getStringExtra(CastService.EXTRA_STATE) ?: return, intent.getStringExtra(CastService.EXTRA_DETAIL))
        }
    }
    private lateinit var updates: WebResourceUpdate
    @Volatile private var release: WebResourceUpdate.Release? = null

    companion object {
        private var resourceUpdates: WebResourceUpdate? = null
        private val updateWorker = Executors.newSingleThreadExecutor()
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        updates = synchronized(DashboardActivity::class.java) {
            resourceUpdates ?: run {
                val config = runCatching { JSONObject(assets.open("dashboard-update.json").bufferedReader().use { it.readText() }) }.getOrDefault(JSONObject())
                val bundled = runCatching { JSONObject(assets.open("dashboard/web-version.json").bufferedReader().use { it.readText() }).getString("version") }.getOrNull()
                if (bundled != null) config.put("bundledVersion", bundled)
                WebResourceUpdate(File(filesDir, "dashboard-releases"), config, packageManager.getPackageInfo(packageName, 0).longVersionCode)
            }.also { resourceUpdates = it }
        }
        release = updates.acquire()
        if (Build.VERSION.SDK_INT >= 30) window.setDecorFitsSystemWindows(false)
        else @Suppress("DEPRECATION") run {
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
        ble = createBleSession()
        val castFilter = IntentFilter(CastService.ACTION_STATUS)
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(castReceiver, castFilter, RECEIVER_NOT_EXPORTED)
        else registerReceiver(castReceiver, castFilter)
        web = WebView(this)
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
            cacheMode = WebSettings.LOAD_NO_CACHE
        }
        web.addJavascriptInterface(Bridge(), "BmsNative")
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
                if (!DashboardPolicy.isDocument(request.url.toString())) return null
                val path = request.url.path.orEmpty().removePrefix("/").ifEmpty { "index.html" }
                if (!WebResourceUpdate.safePath(path)) return missing()
                return runCatching {
                    val mime = when (path.substringAfterLast('.')) {
                        "js" -> "text/javascript"
                        "css" -> "text/css"
                        else -> MimeTypeMap.getSingleton().getMimeTypeFromExtension(path.substringAfterLast('.')) ?: "text/html"
                    }
                    WebResourceResponse(mime, "UTF-8", 200, "OK", mapOf(
                        "Cache-Control" to "no-store",
                        "Content-Security-Policy" to "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src https: http:; img-src 'self' data:; object-src 'none'; frame-src 'none'; worker-src 'none'; base-uri 'none'; form-action 'self'"
                    ), release.let { snapshot ->
                        if (snapshot == null) assets.open("dashboard/$path")
                        else { require(path in snapshot.paths); File(snapshot.directory, "assets/$path").inputStream() }
                    })
                }.getOrElse { missing() }
            }

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                if (DashboardPolicy.isDocument(request.url.toString())) return false
                if (request.isForMainFrame && request.url.scheme in listOf("https", "http", "fuckingbms")) {
                    runCatching { startActivity(Intent(Intent.ACTION_VIEW, request.url)) }
                }
                return true
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            override fun onShowFileChooser(view: WebView, callback: ValueCallback<Array<Uri>>, params: FileChooserParams): Boolean {
                chooser?.onReceiveValue(null)
                chooser = callback
                return try {
                    startActivityForResult(params.createIntent(), 71)
                    true
                } catch (_: Exception) {
                    chooser = null
                    false
                }
            }
        }
        val content = FrameLayout(this)
        content.setBackgroundColor(android.graphics.Color.rgb(10, 15, 20))
        content.addView(web, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        setContentView(content)
        // WebView padding does not resize its HTML viewport; inset the parent instead.
        content.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val safe = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout() or WindowInsets.Type.ime())
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                projectionInsets = CaptureInsets(bars.left, bars.top, bars.right, bars.bottom)
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            } else @Suppress("DEPRECATION") run {
                projectionInsets = CaptureInsets(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
                view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            if (Build.VERSION.SDK_INT >= 30) WindowInsets.CONSUMED else @Suppress("DEPRECATION") insets.consumeSystemWindowInsets()
        }
        content.requestApplyInsets()
        web.loadUrl(DashboardPolicy.castLaunchUrl(intent.dataString) ?: "${DashboardPolicy.ORIGIN}/")
        updateWorker.execute { updates.check() }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        DashboardPolicy.castLaunchUrl(intent.dataString)?.let(web::loadUrl)
    }

    private fun wifiNetwork() = (getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager).let { manager ->
        manager.allNetworks.firstOrNull { manager.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
    }

    private fun reportCast(state: String, detail: String? = null) {
        castStatus = JSONObject().put("state", state).put("detail", detail).toString()
        if (state in listOf(CastService.STATE_STOPPED, CastService.STATE_FAILED)) {
            (getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager).bindProcessToNetwork(null)
        }
    }

    private fun requestCast(value: String) {
        if (projectionRequestOpen || pendingCast != null || CastService.currentState in listOf(CastService.STATE_CONNECTING, CastService.STATE_RETRYING, CastService.STATE_STREAMING)) return
        val attempt = ++castAttempt
        reportCast("starting")
        thread {
            val result = runCatching {
                val host = DashboardPolicy.localHost(value)
                val network = wifiNetwork() ?: error("投屏需要先连接面板 Wi-Fi")
                val connection = (network.openConnection(URL("http://$host/api/cast/info")) as HttpURLConnection).apply { connectTimeout = 4000; readTimeout = 4000 }
                val info = try {
                    check(connection.responseCode == 200) { "面板未启用投屏，或当前 Wi-Fi 不是面板热点" }
                    connection.inputStream.bufferedReader().use { CastInfo.parse(it.readText()) }
                } finally { connection.disconnect() }
                host to info
            }
            runOnUiThread {
                if (isDestroyed || attempt != castAttempt) return@runOnUiThread
                result.onSuccess { target ->
                    pendingCast = target
                    runCatching {
                        val manager = getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
                        val request = if (Build.VERSION.SDK_INT >= 34) manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay()) else manager.createScreenCaptureIntent()
                        projectionRequestOpen = true
                        startActivityForResult(request, 74)
                    }.onFailure { projectionRequestOpen = false; pendingCast = null; reportCast(CastService.STATE_FAILED, it.message) }
                }.onFailure { reportCast(CastService.STATE_FAILED, it.message) }
            }
        }
    }

    private fun missing() = WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(byteArrayOf()))

    private fun createBleSession() = CompanionLink.observe(this,this) { status, detail ->
        bleError = if (status == DeviceBleState.ERROR) detail ?: "面板蓝牙连接失败" else null
        if (status == DeviceBleState.CONNECTED) {
            ble.connectedAddress?.let { getSharedPreferences("ble", MODE_PRIVATE).edit().putString("last_mac", it).apply() }
        }
        if (detail != null) runOnUiThread { Toast.makeText(this, detail, Toast.LENGTH_SHORT).show() }
    }

    private fun connectDeviceBle() {
        if (!DeviceBleSession.hasPermissions(this)) {
            requestPermissions(DeviceBleSession.permissions(), 72)
            return
        }
        val saved = getSharedPreferences("ble", MODE_PRIVATE).getString("last_mac", null)
        if (saved != null && ble.state != DeviceBleState.ERROR) ble.connect(saved) else ble.start()
    }

    inner class Bridge {
        @JavascriptInterface fun getWebUpdateState(): String = updates.state()
        @JavascriptInterface fun checkWebUpdate() { updateWorker.execute { updates.check() } }
        @JavascriptInterface fun checkWebUpdateFrom(source: String) {
            if (UpdateApi.Source.from(source) != null) updateWorker.execute { updates.check(source) }
        }
        @JavascriptInterface fun restartWeb() {
            updateWorker.execute {
                val next = updates.acquire()
                runOnUiThread {
                    if (!isDestroyed) {
                        val previous = release
                        release = next
                        web.clearCache(true)
                        web.reload()
                        updateWorker.execute { updates.release(previous) }
                    } else updates.release(next)
                }
            }
        }
        @JavascriptInterface fun getAppUpdateState() = appUpdateState
        @JavascriptInterface fun installAppUpdate() = installAppUpdateFrom("cf")
        @JavascriptInterface fun installAppUpdateFrom(source: String) {
            val selected = UpdateApi.Source.from(source) ?: return
            if (!appUpdateBusy.compareAndSet(false, true)) return
            appUpdateState = "{\"status\":\"downloading\"}"
            thread {
                val result = runCatching {
                    val release = UpdateApi.checkApkUpdate(selected) ?: error("无可用安装包")
                    if (release.versionCode <= packageManager.getPackageInfo(packageName, 0).longVersionCode) null
                    else File(File(cacheDir, "update").apply { mkdirs() }, "update.apk").also { apk ->
                        UpdateApi.downloadFile(release.url, apk) {}
                    }
                }
                runOnUiThread {
                    try {
                        val apk = result.getOrThrow()
                        if (apk == null) appUpdateState = "{\"status\":\"current\"}"
                        else {
                            val uri = UpdateFileProvider.getUriForFile(this@DashboardActivity, "$packageName.fileprovider", apk)
                            startActivity(Intent(Intent.ACTION_VIEW).apply {
                                setDataAndType(uri, "application/vnd.android.package-archive")
                                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            })
                            appUpdateState = "{\"status\":\"installing\"}"
                        }
                    } catch (error: Exception) {
                        appUpdateState = JSONObject().put("status", "error").put("error", error.message ?: "安装失败，请允许安装未知应用后重试").toString()
                    } finally { appUpdateBusy.set(false) }
                }
            }
        }

        @JavascriptInterface fun getAppInfo(): String = runCatching {
            val info = packageManager.getPackageInfo(packageName, 0)
            JSONObject().put("versionName", info.versionName.orEmpty())
                .put("versionCode", info.longVersionCode).put("updateBaseUrl", UpdateApi.BASE_URL).toString()
        }.getOrElse { JSONObject().put("versionName", "").put("versionCode", 0).put("updateBaseUrl", UpdateApi.BASE_URL).toString() }

        @JavascriptInterface fun bleConnected() = ble.connected
        @JavascriptInterface fun connectBle() = runOnUiThread { bleError = null; connectDeviceBle() }
        @JavascriptInterface fun getBleState(): String = JSONObject().put("state", ble.state.name).put("error", bleError).toString()
        @JavascriptInterface fun getCastState(): String = castStatus
        @JavascriptInterface fun startCast(host: String) = runOnUiThread { requestCast(host) }
        @JavascriptInterface fun stopCast() = runOnUiThread {
            castAttempt++
            pendingCast = null
            stopService(Intent(this@DashboardActivity, CastService::class.java))
            reportCast(CastService.STATE_STOPPED)
        }

        @JavascriptInterface fun openConnectionSettings(kind: String) = runOnUiThread {
            val action = when (kind) {
                "http" -> Settings.ACTION_WIFI_SETTINGS
                "ble" -> Settings.ACTION_BLUETOOTH_SETTINGS
                else -> return@runOnUiThread
            }
            if (connectionSettingsOpen) return@runOnUiThread
            connectionSettingsOpen = true
            connectionSettingsPaused = false
            try {
                startActivity(Intent(action))
            } catch (_: android.content.ActivityNotFoundException) {
                connectionSettingsOpen = false
                web.evaluateJavascript("window.bmsConnectionSettingsClosed?.()", null)
            }
        }

        @JavascriptInterface fun openCompanion(page: String, language: String) = runOnUiThread {
            if (page !in listOf("music","navigation","settings")) return@runOnUiThread
            startActivity(Intent(this@DashboardActivity,CompanionActivity::class.java).putExtra("page",page).putExtra("language",language))
        }

        @JavascriptInterface fun request(id: String, method: String, path: String, body: String, host: String) {
            requests.execute {
                val reply = runCatching {
                    require(id.length <= 100 && method in listOf("GET", "POST") && DashboardPolicy.apiPath(path))
                    require(body.length <= 65536)
                    val transport = if (host == "ble") ble else DeviceApi.httpTransport(DashboardPolicy.localHost(host))
                    val result = transport.request(method, path, if (body.isBlank()) null else JSONObject(body))
                    JSONObject().put("ok", true).put("body", runCatching { JSONTokener(result).nextValue() }.getOrDefault(result))
                }.getOrElse { JSONObject().put("ok", false).put("error", it.message ?: "设备请求失败") }
                runOnUiThread {
                    if (!isDestroyed) web.evaluateJavascript("window.__bmsNativeReply?.(${JSONObject.quote(id)},${JSONObject.quote(reply.toString())})", null)
                }
            }
        }
    }

    override fun onPause() {
        if (connectionSettingsOpen) connectionSettingsPaused = true
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        CompanionService.startWhenAllowed(this)
        if (connectionSettingsOpen && connectionSettingsPaused) {
            connectionSettingsOpen = false
            connectionSettingsPaused = false
            web.evaluateJavascript("window.bmsConnectionSettingsClosed?.()", null)
        }
    }

    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, permissions, results)
        if (code == 72 && results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) {
            connectDeviceBle()
            CompanionService.startWhenAllowed(this)
        } else if (code == 72) bleError = "未授予蓝牙权限，连接已取消"
    }

    override fun onActivityResult(code: Int, result: Int, data: Intent?) {
        super.onActivityResult(code, result, data)
        if (code == 74) {
            projectionRequestOpen = false
            val target = pendingCast
            pendingCast = null
            if (target == null) return
            if (result != RESULT_OK || data == null) { reportCast(CastService.STATE_STOPPED, "屏幕共享已取消"); return }
            runCatching {
                val network = wifiNetwork() ?: error("投屏需要先连接面板 Wi-Fi")
                check((getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager).bindProcessToNetwork(network)) { "无法使用面板 Wi-Fi" }
                reportCast(CastService.STATE_CONNECTING)
                startForegroundService(CastService.intent(this, result, data, target.first, target.second, projectionInsets))
            }.onFailure { reportCast(CastService.STATE_FAILED, it.message) }
        }
        if (code == 71) {
            chooser?.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(result, data))
            chooser = null
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        web.evaluateJavascript("""(() => { if (!document.querySelector('.dashboard-sheet.show')) return false; document.dispatchEvent(new KeyboardEvent('keydown', {key:'Escape',bubbles:true})); return true; })()""") { handled ->
            if (handled != "true") { if (web.canGoBack()) web.goBack() else finish() }
        }
    }

    override fun onDestroy() {
        castAttempt++
        pendingCast = null
        unregisterReceiver(castReceiver)
        chooser?.onReceiveValue(null)
        CompanionLink.remove(this)
        requests.shutdownNow()
        web.removeJavascriptInterface("BmsNative")
        web.destroy()
        val previous = release
        updateWorker.execute { updates.release(previous) }
        super.onDestroy()
    }
}
