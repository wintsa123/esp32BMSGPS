package com.fuckingbms.cast

import android.annotation.SuppressLint
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.webkit.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.FilterInputStream
import java.util.concurrent.CancellationException

/** JS API owns authentication and routing; the service consumes a bounded, validated route result. */
internal class CompanionRiding(private val context: Context) {
    private val handler = Handler(Looper.getMainLooper())
    private var web: WebView? = null
    private var ready = false
    private var credentials: Pair<String,String>? = null
    private var serial = 0L
    private var closed = false
    private var pending: Request? = null
    private data class Request(val id: String, val name: String, val data: JSONObject,
        val callback: (Result<Pair<NavPoint,NavRoute>>) -> Unit, var sent: Boolean = false)
    private val timeout = Runnable { finish(Result.failure(IllegalStateException("骑行规划超时，请检查联网和高德 JS API 配置"))) }

    fun request(key: String, securityCode: String, from: NavPoint, destination: NavPoint?, name: String,
                callback: (Result<Pair<NavPoint,NavRoute>>) -> Unit) {
        check(Looper.myLooper() == Looper.getMainLooper())
        check(!closed)
        cancel()
        if (!listOf(key,securityCode).all { it.matches(Regex("[a-fA-F0-9]{32}")) }) {
            callback(Result.failure(IllegalArgumentException("请在设置填写正确的高德 JS API Key 和安全密钥"))); return
        }
        if (credentials != (key to securityCode)) {
            web?.destroy(); web=null; ready=false; credentials=key to securityCode
        }
        val origin=NavGeo.gcj(from)
        val data=JSONObject().put("id",(++serial).toString()).put("key",key).put("securityCode",securityCode)
            .put("origin",JSONArray(listOf(origin.lon,origin.lat))).put("name",name.take(160))
        destination?.let { data.put("destination",JSONArray(listOf(it.lon,it.lat))) }
        pending=Request(serial.toString(),name,data,callback)
        handler.postDelayed(timeout,60000)
        runCatching { if (web == null) createWebView(); dispatch() }
            .onFailure { finish(Result.failure(IllegalStateException("无法启动骑行规划，请检查系统 WebView"))) }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun createWebView() {
        web=WebView(context).apply {
            settings.javaScriptEnabled=true
            settings.allowFileAccess=false; settings.allowContentAccess=false
            settings.mixedContentMode=WebSettings.MIXED_CONTENT_NEVER_ALLOW
            addJavascriptInterface(Bridge(),"BmsRiding")
            webChromeClient=object : WebChromeClient() {
                override fun onConsoleMessage(message: ConsoleMessage?): Boolean {
                    if (BuildConfig.DEBUG) android.util.Log.d("CompanionRiding",message?.message().orEmpty().replace(Regex("[a-fA-F0-9]{32}"),"[隐藏]").take(240))
                    return true
                }
            }
            webViewClient=object : WebViewClient() {
                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest) = true
                override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse {
                    if (request.url.toString() == PAGE && request.isForMainFrame) {
                        return WebResourceResponse("text/html","UTF-8",200,"OK",mapOf("Content-Security-Policy" to
                            "default-src 'none'; script-src 'unsafe-inline' 'unsafe-eval' https://*.amap.com; connect-src https://*.amap.com; img-src data: https://*.amap.com; style-src 'unsafe-inline'; worker-src blob:; frame-src 'none'; object-src 'none'; base-uri 'none'"),context.assets.open("companion-riding.html"))
                    }
                    if (request.isForMainFrame || request.method != "GET" || !trustedResource(request.url.toString())) return unavailable()
                    return resource(request)
                }
                override fun onPageFinished(view: WebView, url: String) { if (url == PAGE) { ready=true; dispatch() } }
                override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
                    if (request.isForMainFrame) finish(Result.failure(IllegalStateException("骑行规划页面加载失败")))
                }
                override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
                    ready=false; web=null; view.destroy()
                    finish(Result.failure(IllegalStateException("系统 WebView 已退出，请重试骑行规划")))
                    return true
                }
            }
            loadUrl(PAGE)
        }
    }

    private fun resource(request: WebResourceRequest): WebResourceResponse {
        val connection=CompanionMaps.openConnection(context,request.url.toString())
        return try {
            // Keep the SDK's browser identity while choosing an internet network independently of cast Wi-Fi.
            request.requestHeaders.filterKeys { it.equals("User-Agent",true) || it.equals("Referer",true) || it.equals("Origin",true) }
                .forEach { (name,value) -> connection.setRequestProperty(name,value) }
            check(connection.responseCode == 200)
            val headers=connection.headerFields.entries.mapNotNull { (name,values) ->
                name?.takeUnless { it.lowercase() in setOf("content-length","content-encoding","transfer-encoding") }?.let { it to values.joinToString(",") }
            }.toMap()
            val input=object : FilterInputStream(connection.inputStream) {
                private var bytes=0
                override fun read(): Int = super.read().also { if (it >= 0) { bytes++; check(bytes <= 8_000_000) } }
                override fun read(buffer: ByteArray, offset: Int, length: Int): Int = `in`.read(buffer,offset,length).also {
                    if (it > 0) { bytes+=it; check(bytes <= 8_000_000) }
                }
                override fun close() { try { super.close() } finally { connection.disconnect() } }
            }
            WebResourceResponse(connection.contentType?.substringBefore(';') ?: "application/javascript","UTF-8",200,"OK",headers,input)
        } catch (error: Exception) {
            android.util.Log.w("CompanionRiding","${request.url.host}${request.url.path}: ${error.javaClass.simpleName}")
            connection.disconnect(); unavailable()
        }
    }

    private fun dispatch() {
        val request=pending ?: return
        if (!ready || request.sent) return
        request.sent=true
        web?.evaluateJavascript("window.planRiding(${request.data});",null)
    }

    private inner class Bridge {
        @JavascriptInterface fun complete(id: String, payload: String) {
            handler.post {
                val request=pending ?: return@post
                if (id != request.id) return@post
                finish(runCatching {
                    require(payload.length <= 2_000_000) { "骑行路线响应过大" }
                    val json=JSONObject(payload)
                    val route=CompanionMaps.parseRoute(json,request.name)
                    val pair=json.getJSONArray("destination")
                    require(pair.length() == 2)
                    val destination=NavPoint(pair.getDouble(1),pair.getDouble(0))
                    require(destination.lat.isFinite() && destination.lon.isFinite() && destination.lat in -85.0..85.0 && destination.lon in -180.0..180.0)
                    destination to route
                })
            }
        }
    }

    private fun finish(result: Result<Pair<NavPoint,NavRoute>>) {
        val request=pending ?: return
        pending=null; handler.removeCallbacks(timeout)
        if (result.isFailure) { web?.destroy(); web=null; ready=false }
        else web?.evaluateJavascript("window.cancelRiding && window.cancelRiding();",null)
        request.callback(result)
    }

    fun cancel() { finish(Result.failure(CancellationException("骑行规划已取消"))) }
    fun close() { closed=true; cancel(); web?.destroy(); web=null; ready=false }

    companion object {
        private const val PAGE = "https://bms.local/companion-riding.html"
        fun trustedResource(url: String): Boolean = runCatching {
            val uri=java.net.URI(url)
            uri.scheme == "https" && uri.userInfo == null && (uri.port == -1 || uri.port == 443) &&
                uri.host?.let { it == "amap.com" || it.endsWith(".amap.com") } == true
        }.getOrDefault(false)
        private fun unavailable() = WebResourceResponse("text/plain","UTF-8",503,"Unavailable",emptyMap(),ByteArrayInputStream(ByteArray(0)))
    }
}
