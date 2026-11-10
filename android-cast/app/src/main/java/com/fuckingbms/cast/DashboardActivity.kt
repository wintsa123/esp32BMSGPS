package com.fuckingbms.cast

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.Build
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
import android.widget.Toast
import org.json.JSONObject
import org.json.JSONTokener
import java.io.ByteArrayInputStream
import java.util.concurrent.Executors

// Adapted from dashboard's MainActivity/Client: trusted WebView document, native bridge and system insets.
class DashboardActivity : Activity() {
    private lateinit var web: WebView
    private lateinit var ble: DeviceBleSession
    private val requests = Executors.newSingleThreadExecutor()
    private var chooser: ValueCallback<Array<Uri>>? = null
    private var resumeBle = false

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        if (Build.VERSION.SDK_INT >= 30) window.setDecorFitsSystemWindows(false)
        else @Suppress("DEPRECATION") run {
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
        ble = createBleSession()
        web = WebView(this)
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
        }
        web.addJavascriptInterface(Bridge(), "BmsNative")
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
                if (!DashboardPolicy.isDocument(request.url.toString())) return null
                val path = request.url.path.orEmpty().removePrefix("/").ifEmpty { "index.html" }
                if (path.split('/').any { it == ".." }) return missing()
                return runCatching {
                    val mime = when (path.substringAfterLast('.')) {
                        "js" -> "text/javascript"
                        "css" -> "text/css"
                        else -> MimeTypeMap.getSingleton().getMimeTypeFromExtension(path.substringAfterLast('.')) ?: "text/html"
                    }
                    WebResourceResponse(mime, "UTF-8", 200, "OK", mapOf(
                        "Content-Security-Policy" to "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src https: http:; img-src 'self' data:; object-src 'none'; frame-src 'none'; base-uri 'none'; form-action 'self'"
                    ), assets.open("dashboard/$path"))
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
        setContentView(web)
        web.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val safe = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout() or WindowInsets.Type.ime())
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            } else @Suppress("DEPRECATION") run {
                view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            if (Build.VERSION.SDK_INT >= 30) WindowInsets.CONSUMED else @Suppress("DEPRECATION") insets.consumeSystemWindowInsets()
        }
        web.requestApplyInsets()
        web.loadUrl("${DashboardPolicy.ORIGIN}/")
    }

    private fun missing() = WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(byteArrayOf()))

    private fun createBleSession() = DeviceBleSession(this) { status, detail ->
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
        @JavascriptInterface fun bleConnected() = ble.connected
        @JavascriptInterface fun connectBle() = runOnUiThread { connectDeviceBle() }

        @JavascriptInterface fun openNative(route: String) = runOnUiThread {
            val target = runCatching { AppRoute.valueOf(route) }.getOrNull()
            if (target !in listOf(AppRoute.CAST, AppRoute.BMS, AppRoute.RECORDS, AppRoute.SETTINGS)) return@runOnUiThread
            resumeBle = true
            ble.close()
            startActivity(Intent(this@DashboardActivity, MainActivity::class.java).putExtra("dashboard_route", route))
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

    override fun onResume() {
        super.onResume()
        if (resumeBle) {
            resumeBle = false
            ble = createBleSession()
            connectDeviceBle()
        }
    }

    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, permissions, results)
        if (code == 72 && results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) connectDeviceBle()
    }

    override fun onActivityResult(code: Int, result: Int, data: Intent?) {
        super.onActivityResult(code, result, data)
        if (code == 71) {
            chooser?.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(result, data))
            chooser = null
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (web.canGoBack()) web.goBack() else super.onBackPressed()
    }

    override fun onDestroy() {
        chooser?.onReceiveValue(null)
        ble.close()
        requests.shutdownNow()
        web.removeJavascriptInterface("BmsNative")
        web.destroy()
        super.onDestroy()
    }
}
