package com.fuckingbms.cast

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.*
import android.provider.Settings
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.view.inputmethod.EditorInfo
import android.widget.*

/** Phone controls for the two display pages; media text is rendered with Android's fonts. */
class CompanionActivity : Activity() {
    private val prefs by lazy { getSharedPreferences("companion",MODE_PRIVATE) }
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var root: LinearLayout
    private lateinit var content: LinearLayout
    private lateinit var status: TextView
    private val tabButtons=mutableMapOf<String,Button>()
    private var page = "music"
    private var english = false
    private var musicTitle: TextView? = null
    private var musicArtist: TextView? = null
    private var musicLyric: TextView? = null
    private var navigation: TextView? = null
    private var preview: ImageView? = null
    private var pendingDestination: String? = null
    private val background = Color.rgb(8,10,14)
    private val surface = Color.rgb(22,32,41)
    private val cyan = Color.rgb(0,229,255)
    private fun t(zh: String,en: String) = if (english) en else zh
    private fun dp(n: Int) = (n*resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        english=intent.getStringExtra("language") == "en"
        page=intent.getStringExtra("page")?.takeIf { it in listOf("music","navigation","settings") } ?: "music"
        window.statusBarColor=background; window.navigationBarColor=background
        root=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL; setBackgroundColor(this@CompanionActivity.background) }
        root.setOnApplyWindowInsetsListener { view,insets ->
            val top=if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).top else @Suppress("DEPRECATION") insets.systemWindowInsetTop
            val bottom=if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime()).bottom else @Suppress("DEPRECATION") insets.systemWindowInsetBottom
            view.setPadding(dp(16),top,dp(16),bottom); insets
        }
        val header=LinearLayout(this).apply { gravity=Gravity.CENTER_VERTICAL }
        header.addView(button("‹") { finish() }.apply { contentDescription=t("返回","Back") },LinearLayout.LayoutParams(dp(48),dp(48)))
        header.addView(label(t("音乐与导航","Music & navigation"),20,Color.WHITE).apply { gravity=Gravity.CENTER },LinearLayout.LayoutParams(0,dp(48),1f))
        header.addView(View(this),LinearLayout.LayoutParams(dp(48),dp(48)))
        root.addView(header)
        val tabs=LinearLayout(this)
        for ((id,title) in listOf("music" to t("音乐","Music"),"navigation" to t("导航","Navigate"),"settings" to t("设置","Settings"))) {
            val tab=button(title) { page=id; showPage() }; tabButtons[id]=tab
            tabs.addView(tab,LinearLayout.LayoutParams(0,dp(48),1f).apply { marginEnd=dp(5) })
        }
        root.addView(tabs)
        status=label("",12,Color.rgb(128,145,165)).apply { setPadding(dp(4),dp(14),0,dp(10)) }; root.addView(status)
        val scroll=ScrollView(this).apply { isFillViewport=true; clipToPadding=false; setPadding(0,0,0,dp(32)) }
        content=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL }
        scroll.addView(content); root.addView(scroll,LinearLayout.LayoutParams(-1,0,1f))
        setContentView(root); showPage()
    }

    private fun label(text: String,size: Int=16,color: Int=Color.WHITE) = TextView(this).apply {
        this.text=text; textSize=size.toFloat(); setTextColor(color); gravity=Gravity.CENTER_VERTICAL
        typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)
    }
    private fun button(text: String,action: () -> Unit) = Button(this).apply {
        this.text=text; isAllCaps=false; textSize=14f; setTextColor(cyan); minHeight=dp(48)
        background=GradientDrawable().apply { setColor(surface); cornerRadius=dp(14).toFloat() }
        setOnClickListener { action() }
    }
    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation=LinearLayout.VERTICAL; setPadding(dp(18),dp(18),dp(18),dp(18))
        background=GradientDrawable().apply { setColor(surface); cornerRadius=dp(22).toFloat() }
        content.addView(this,LinearLayout.LayoutParams(-1,-2).apply { bottomMargin=dp(14) })
    }
    private fun LinearLayout.paragraph(text: String) { addView(label(text,13,Color.rgb(169,180,200)).apply { setPadding(0,dp(10),0,dp(10)) }) }
    private fun LinearLayout.action(text: String, action: () -> Unit) { addView(button(text,action),LinearLayout.LayoutParams(-1,dp(50)).apply { topMargin=dp(8) }) }
    private fun LinearLayout.input(title: String,value: String="",secret: Boolean=false): EditText {
        addView(label(title,13,Color.rgb(169,180,200)).apply { setPadding(0,dp(14),0,dp(6)) })
        return EditText(this@CompanionActivity).apply {
            setText(value); textSize=16f; setTextColor(Color.WHITE); setHintTextColor(Color.GRAY); isSingleLine=true
            inputType=InputType.TYPE_CLASS_TEXT or if (secret) InputType.TYPE_TEXT_VARIATION_PASSWORD else InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            setPadding(dp(12),dp(10),dp(12),dp(10)); minHeight=dp(50)
            background=GradientDrawable().apply { setColor(backgroundColor()); cornerRadius=dp(10).toFloat() }
            this@input.addView(this,LinearLayout.LayoutParams(-1,-2))
        }
    }
    private fun backgroundColor()=Color.rgb(8,10,14)

    private fun showPage() {
        tabButtons.forEach { (id,button) ->
            (button.background as GradientDrawable).setColor(if (id == page) Color.rgb(0,65,75) else surface)
            button.isSelected=id == page
        }
        content.removeAllViews(); musicTitle=null; musicArtist=null; musicLyric=null; navigation=null; preview=null
        when (page) {
            "music" -> {
                card().apply {
                    addView(label("NOW PLAYING",12,cyan))
                    musicTitle=label(t("等待手机播放","Waiting for playback"),26).apply { setPadding(0,dp(22),0,dp(6)) }; addView(musicTitle)
                    musicArtist=label("",15,Color.LTGRAY); addView(musicArtist)
                    musicLyric=label(t("打开常用播放器，内容会自动同步到设备。","Open a music player to sync its media information."),19,cyan).apply { setPadding(0,dp(28),0,dp(28)); minHeight=dp(110) }; addView(musicLyric)
                    val row=LinearLayout(this@CompanionActivity)
                    for ((text,action) in listOf("‹‹" to "previous","▶ / Ⅱ" to "toggle","››" to "next")) row.addView(button(text) { CompanionMusic.control(action) },LinearLayout.LayoutParams(0,dp(54),1f).apply { marginEnd=dp(4) })
                    addView(row)
                    action(t("在设备显示音乐","Show music on display")) {
                        prefs.edit().putBoolean("enabled",true).apply(); enableSync()
                        if (DeviceBleSession.hasPermissions(this@CompanionActivity)) startForegroundService(Intent(this@CompanionActivity,CompanionService::class.java).setAction("SHOW_MUSIC"))
                    }
                }
                card().apply {
                    addView(label(t("歌词来源","Lyrics"),18))
                    paragraph(t("优先使用播放器共享的歌词；没有歌词时显示歌名与歌手。也可以为当前歌曲导入 LRC 时间轴。","Use lyrics shared by your player, or import an LRC file for the current song. Title and artist are shown when lyrics are unavailable."))
                    action(t("为当前歌曲导入 LRC","Import LRC for this song")) {
                        if (CompanionMusic.state.title.isBlank()) Toast.makeText(this@CompanionActivity,t("请先播放一首歌","Play a song first"),Toast.LENGTH_SHORT).show()
                        else startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),81)
                    }
                    action(t("授权读取播放器信息","Allow access to media information")) { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
                }
            }
            "navigation" -> {
                card().apply {
                    addView(label(t("少算路，持续导航","Plan once. Navigate locally."),22,cyan))
                    paragraph(t("手机 GPS 持续更新，天地图提供底图。高德只在出发和持续偏航后算路；自动重算至少间隔 2 分钟，每天最多 20 次请求。","GPS and progress run locally over Tianditu tiles. Recalculate only after sustained deviation, at least 2 minutes apart, up to 20 route requests per day."))
                    val destination=input(t("目的地完整地址 / 高德经纬度","Full destination address / Amap lon,lat"),prefs.getString("destination","").orEmpty())
                    destination.imeOptions=EditorInfo.IME_ACTION_GO
                    fun go() {
                        val query=destination.text.toString().trim()
                        if (query.isBlank()) { destination.error=t("请输入目的地","Enter a destination"); return }
                        if (prefs.getString("amap_js_key",BuildConfig.DEFAULT_AMAP_JS_KEY).isNullOrBlank() || prefs.getString("amap_security_js_code",BuildConfig.DEFAULT_AMAP_SECURITY_JS_CODE).isNullOrBlank()) {
                            Toast.makeText(this@CompanionActivity,t("请先在设置填写高德 JS API Key 和安全密钥","Add your Amap JS API key and security code in Settings"),Toast.LENGTH_LONG).show(); return
                        }
                        pendingDestination=query; prefs.edit().putString("destination",query).apply(); beginNavigation()
                    }
                    destination.setOnEditorActionListener { _,action,_ -> if (action == EditorInfo.IME_ACTION_GO) { go(); true } else false }
                    action(t("开始导航","Start navigation")) { go() }
                    action(t("结束导航","End navigation")) { if (CompanionService.isRunning()) startService(Intent(this@CompanionActivity,CompanionService::class.java).setAction("NAV_STOP")) }
                    navigation=label("",17,cyan).apply { setPadding(0,dp(16),0,0) }; addView(navigation)
                }
                preview=ImageView(this).apply { scaleType=ImageView.ScaleType.FIT_CENTER; contentDescription=t("设备导航画面","Display navigation preview") }
                content.addView(preview,LinearLayout.LayoutParams(-1,dp(360)))
            }
            else -> {
                card().apply {
                    addView(label(t("后台同步","Background sync"),20))
                    val enabled=Switch(this@CompanionActivity).apply { text=t("打开 App 后自动同步","Sync automatically when opening the app"); setTextColor(Color.WHITE); isChecked=prefs.getBoolean("enabled",true) }
                    enabled.setOnCheckedChangeListener { _,checked ->
                        prefs.edit().putBoolean("enabled",checked).apply()
                        if (checked) enableSync() else stopService(Intent(this@CompanionActivity,CompanionService::class.java))
                    }; addView(enabled)
                    paragraph(t("后台运行时显示常驻通知。关闭省电限制有助于保持蓝牙与导航；系统强制停止后需要重新打开 App。","A persistent notification keeps sync active. Allow background activity in your battery settings; reopen the app after a force stop."))
                    action(t("开启同步 / 授权蓝牙","Start sync / allow Bluetooth")) { prefs.edit().putBoolean("enabled",true).apply(); enableSync() }
                    action(t("播放器信息权限","Media information permission")) { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
                }
                card().apply {
                    addView(label(t("地图服务","Map services"),20))
                    val amap=input(t("高德 Web 端 JS API Key","Amap Web JS API key"),prefs.getString("amap_js_key",BuildConfig.DEFAULT_AMAP_JS_KEY).orEmpty(),true)
                    val security=input(t("高德安全密钥","Amap security code"),prefs.getString("amap_security_js_code",BuildConfig.DEFAULT_AMAP_SECURITY_JS_CODE).orEmpty(),true)
                    val tdt=input(t("天地图 Key","Tianditu key"),prefs.getString("tdt_key",BuildConfig.DEFAULT_TIANDITU_KEY).orEmpty(),true)
                    paragraph(t("使用高德普通骑行规划，天地图显示底图。骑行规划不包含摩托车专用禁限行规则。","Uses Amap bicycle routing over Tianditu tiles. Motorcycle-specific restrictions are not included."))
                    action(t("保存地图设置","Save map settings")) {
                        prefs.edit().putString("amap_js_key",amap.text.toString().trim()).putString("amap_security_js_code",security.text.toString().trim()).putString("tdt_key",tdt.text.toString().trim()).apply()
                        Toast.makeText(this@CompanionActivity,t("已保存在本机","Saved on this phone"),Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }
        update()
    }

    private fun enableSync() {
        if (!DeviceBleSession.hasPermissions(this)) { requestPermissions(DeviceBleSession.permissions(),82); return }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),84)
        CompanionService.startWhenAllowed(this)
    }
    private fun beginNavigation() {
        if (!DeviceBleSession.hasPermissions(this)) { requestPermissions(DeviceBleSession.permissions(),82); return }
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION,Manifest.permission.ACCESS_COARSE_LOCATION),83); return
        }
        val destination=pendingDestination ?: return
        pendingDestination=null; prefs.edit().putBoolean("enabled",true).apply()
        startForegroundService(Intent(this,CompanionService::class.java).setAction("NAV_START").putExtra("destination",destination))
    }
    private val refresh=object : Runnable { override fun run() { update(); handler.postDelayed(this,1000) } }
    private fun update() {
        status.text=CompanionService.linkStatus
        val state=CompanionMusic.state
        musicTitle?.text=state.title.ifBlank { t("等待手机播放","Waiting for playback") }
        musicArtist?.text=state.artist
        musicLyric?.text=state.line.ifBlank { if (state.title.isBlank()) t("打开播放器即可同步","Open a player to begin") else t("纯粹聆听","Enjoy the music") }
        navigation?.text=CompanionService.navigationStatus
        CompanionService.preview?.let { preview?.setImageBitmap(it) }
    }
    override fun onResume() { super.onResume(); CompanionService.previewVisible=true; CompanionService.startWhenAllowed(this); handler.post(refresh) }
    override fun onPause() { handler.removeCallbacks(refresh); CompanionService.previewVisible=false; super.onPause() }
    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code,permissions,results)
        if (code == 82 && results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) { CompanionService.startWhenAllowed(this); if (pendingDestination != null) beginNavigation() }
        if (code == 83 && checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) beginNavigation()
    }
    override fun onActivityResult(code: Int,result: Int,data: Intent?) {
        super.onActivityResult(code,result,data)
        if (code == 81 && result == RESULT_OK) runCatching {
            val uri=data?.data ?: return@runCatching
            val text=contentResolver.openInputStream(uri)?.bufferedReader()?.use { reader ->
                val chars=CharArray(200001); var count=0
                while (count < chars.size) { val n=reader.read(chars,count,chars.size-count); if (n < 0) break; count+=n }
                require(count <= 200000) { "LRC 文件过大" }; String(chars,0,count).removePrefix("\uFEFF")
            }.orEmpty()
            require(Lyrics.parse(text).isNotEmpty()) { "没有找到 LRC 时间轴" }
            CompanionMusic.importLyrics(CompanionMusic.state.title,text)
            Toast.makeText(this,t("歌词已导入","Lyrics imported"),Toast.LENGTH_SHORT).show()
        }.onFailure { Toast.makeText(this,it.message ?: "LRC 读取失败",Toast.LENGTH_LONG).show() }
    }
}
