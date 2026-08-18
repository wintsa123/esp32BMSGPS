package com.fuckingbms.cast

import android.app.Activity
import android.app.AlertDialog
import android.Manifest
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.text.InputType
import android.text.TextUtils
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

internal enum class UiStage {
    WAITING_SCAN, CONNECTING_WIFI, QUERYING_DEVICE, READY, REQUESTING_PERMISSION,
    CAST_CONNECTING, CASTING, STOPPED, FAILED
}

internal enum class AppRoute(val label: String) {
    CAST("投屏"),
    BMS("BMS"),
    RECORDS("记录"),
    MAP("地图"),
    SETTINGS("设置"),
    DASHBOARD("仪表"),
}

internal data class PrimaryAction(val label: String, val enabled: Boolean, val stopsCast: Boolean = false)

private enum class BleScanTarget { BMS, CONTROLLER }

internal fun primaryAction(stage: UiStage, deviceReady: Boolean) = when (stage) {
    UiStage.CASTING -> PrimaryAction("停止投屏", true, true)
    UiStage.CAST_CONNECTING -> PrimaryAction("停止投屏", true, true)
    UiStage.REQUESTING_PERMISSION -> PrimaryAction("正在申请投屏权限", false)
    UiStage.STOPPED -> PrimaryAction("重新开始投屏", deviceReady)
    UiStage.FAILED -> PrimaryAction("重试投屏", deviceReady)
    else -> PrimaryAction("开始投屏", stage == UiStage.READY && deviceReady)
}

class MainActivity : Activity() {
    private lateinit var statusChipView: TextView
    private lateinit var statusView: TextView
    private lateinit var deviceView: TextView
    private lateinit var castButton: Button
    private lateinit var connectionButton: Button

    private lateinit var dashboardPage: ScrollView
    private lateinit var bmsPage: ScrollView
    private lateinit var recordsPage: ScrollView
    private lateinit var settingsPage: ScrollView
    private lateinit var castPage: ScrollView
    private lateinit var mapPage: ScrollView
    private lateinit var routeBar: View
    private val routePages = mutableMapOf<AppRoute, View>()
    private val routeTabs = mutableMapOf<AppRoute, TextView>()
    private var selectedRoute = AppRoute.CAST

    private lateinit var dashboardConnectionView: TextView
    private lateinit var dashboardSpeedView: TextView
    private lateinit var dashboardVoltageView: TextView
    private lateinit var dashboardSocView: TextView
    private lateinit var dashboardBmsView: TextView
    private lateinit var dashboardRecordView: TextView

    private lateinit var bmsStateView: TextView
    private lateinit var bmsInfoView: TextView
    private lateinit var bmsVoltageView: TextView
    private lateinit var bmsCurrentView: TextView
    private lateinit var bmsTemperatureView: TextView
    private lateinit var bmsCapacityView: TextView
    private lateinit var bmsProtectionView: TextView
    private lateinit var bmsWarningView: TextView
    private lateinit var bmsDetailsView: TextView
    private lateinit var bmsMacInput: EditText
    private lateinit var bmsTypeValue: TextView
    private lateinit var bmsScanButton: Button
    private lateinit var bmsCandidatesHost: LinearLayout
    private lateinit var bmsBindingCard: View
    private lateinit var controllerScanButton: Button
    private lateinit var controllerCandidatesHost: LinearLayout
    private var pendingBleScanTarget: BleScanTarget? = null
    private var activeBleScanTarget: BleScanTarget? = null
    private val bleCandidates = linkedMapOf<String, BmsCandidate>()
    private val bleScanTimeout = Runnable { stopBleScan() }
    private val bleScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) = acceptBleResult(result)

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach(::acceptBleResult)
        }

        override fun onScanFailed(errorCode: Int) {
            runOnUiThread {
                stopBleScan()
                toast("蓝牙扫描失败：$errorCode")
            }
        }
    }

    private lateinit var recordsHost: LinearLayout
    private lateinit var recordMapButton: Button
    private lateinit var trackMapView: TrackMapView
    private lateinit var trackSummaryView: TextView
    private lateinit var settingsConnectionView: TextView
    private lateinit var brightnessValue: TextView
    private lateinit var volumeValue: TextView
    private lateinit var brightnessSeek: SeekBar
    private lateinit var volumeSeek: SeekBar
    private lateinit var rotationValue: TextView
    private lateinit var speedUnitValue: TextView
    private lateinit var speedSourceValue: TextView
    private lateinit var languageValue: TextView
    private lateinit var settingsDeviceCard: View
    private lateinit var settingsSaveButton: Button
    private val settingRows = mutableMapOf<String, View>()
    private val settingControls = mutableMapOf<String, View>()
    private lateinit var otaFileValue: TextView
    private lateinit var otaCodeInput: EditText
    private lateinit var otaProgressBar: ProgressBar
    private lateinit var otaProgressText: TextView
    private lateinit var otaUploadButton: Button
    private lateinit var otaCard: View

    private var stage = UiStage.WAITING_SCAN
    private var detail: String? = null
    private var host = "192.168.4.1"
    private var ssid: String? = null
    private var network: Network? = null
    private var info: CastInfo? = null
    private var infoRequestInFlight = false
    private var infoRequestId = 0

    private var latestStatus: DeviceStatus? = null
    private var latestConfig: DeviceConfig? = null
    private var latestRecords: List<RideRecord> = emptyList()
    private var latestTrack: List<GpsPoint> = emptyList()
    private var latestHistorySessions: List<HistorySession> = emptyList()
    private var capabilities: DeviceCapabilities? = null
    private var profileRequestId = 0
    private var profileLoading = false
    private var refreshRouteWhenProfileLoaded = false
    private var deviceRequestId = 0
    private var deviceLoading = false
    private var deviceError = "请连接两轮智控热点"
    private var renderedSettingsConfig: DeviceConfig? = null
    private var renderedBmsConfig: DeviceConfig? = null
    private var bmsType = "ant"
    private var brightness = 80
    private var volume = 50
    private var displayRotation = "landscape"
    private var speedUnit = "km/h"
    private var speedSource = "gps"
    private var language = "zh"
    private var otaFile: Uri? = null
    private var otaPollAttempt = 0
    private val uiHandler = Handler(Looper.getMainLooper())
    private val bmsTypeOptions = listOf(
        "ant" to "蚂蚁 ANT",
        "jk" to "极空 JK",
        "jbd" to "嘉佰达 JBD",
        "daly" to "达锂 Daly",
        "yanyang" to "彦阳 BMS",
    )

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
            selectRoute(AppRoute.CAST, refresh = false)
        } else {
            render()
        }
        refreshDeviceProfile()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (readDeepLink(intent)) {
            stopService(Intent(this, CastService::class.java))
            releaseNetworkRequest()
            invalidateInfoRequest()
            info = null
            capabilities = null
            selectRoute(AppRoute.CAST, refresh = false)
            refreshDeviceProfile(force = true)
        } else {
            setStage(UiStage.FAILED, "投屏二维码无效，请在设备投屏页重新扫码")
        }
    }

    override fun onResume() {
        super.onResume()
        when (CastService.currentState) {
            CastService.STATE_CONNECTING, CastService.STATE_RETRYING -> setStage(UiStage.CAST_CONNECTING)
            CastService.STATE_STREAMING -> setStage(UiStage.CASTING)
            else -> refreshDeviceProfile()
        }
    }

    override fun onDestroy() {
        stopBleScan()
        uiHandler.removeCallbacksAndMessages(null)
        releaseNetworkRequest()
        invalidateInfoRequest()
        unregisterReceiver(castStatusReceiver)
        connectivityManager().bindProcessToNetwork(null)
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != BLE_PERMISSION_REQUEST) return
        val target = pendingBleScanTarget
        pendingBleScanTarget = null
        if (target != null && grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            startPhoneBleScan(target)
        } else {
            toast("需要蓝牙扫描权限")
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (selectedRoute == AppRoute.MAP) {
            selectRoute(AppRoute.RECORDS, refresh = false)
            return
        }
        if (selectedRoute != AppRoute.CAST) {
            selectRoute(AppRoute.CAST, refresh = false)
            return
        }
        super.onBackPressed()
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
        val root = FrameLayout(this).apply {
            setBackgroundColor(COLOR_BACKGROUND)
            setOnApplyWindowInsetsListener { view, insets ->
                view.setPadding(
                    insets.systemWindowInsetLeft,
                    insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight,
                    insets.systemWindowInsetBottom,
                )
                insets
            }
        }
        val shell = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(12))
        }
        shell.addView(buildHeader(), rowParams(bottom = 8))

        val pages = FrameLayout(this)
        dashboardPage = buildDashboardPage()
        bmsPage = buildBmsPage()
        recordsPage = buildRecordsPage()
        mapPage = buildMapPage()
        settingsPage = buildSettingsPage()
        castPage = buildCastPage()
        routePages.clear()
        routePages[AppRoute.DASHBOARD] = dashboardPage
        routePages[AppRoute.BMS] = bmsPage
        routePages[AppRoute.RECORDS] = recordsPage
        routePages[AppRoute.MAP] = mapPage
        routePages[AppRoute.SETTINGS] = settingsPage
        routePages[AppRoute.CAST] = castPage
        routePages.values.forEach { page ->
            pages.addView(page, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ))
        }
        shell.addView(pages, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            0,
            1f,
        ))
        routeBar = buildRouteBar()
        shell.addView(routeBar, rowParams(top = 10))
        root.addView(shell, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT,
        ))
        setContentView(root)
        selectRoute(AppRoute.CAST, refresh = false)
        renderNativePages()
    }

    private fun buildHeader(): View {
        val row = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        val brand = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        brand.addView(label("两轮智控", 25f, COLOR_TEXT, true))
        brand.addView(label("原生仪表、BMS 管理与无线投屏", 13f, COLOR_MUTED), rowParams(top = 2))
        row.addView(brand, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(actionButton("刷新", false) { refreshAll() }, LinearLayout.LayoutParams(dp(76), dp(44)))
        return row
    }

    private fun buildDashboardPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        val statusCard = card()
        dashboardConnectionView = label("正在等待设备", 14f, COLOR_MUTED, true)
        statusCard.addView(dashboardConnectionView, rowParams(bottom = 12))
        val metrics = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        val speedMetric = metric("当前速度", 38f)
        val voltageMetric = metric("电池电压", 32f)
        val socMetric = metric("SOC", 32f)
        dashboardSpeedView = speedMetric.second
        dashboardVoltageView = voltageMetric.second
        dashboardSocView = socMetric.second
        metrics.addView(speedMetric.first, weightedParams(1f))
        metrics.addView(verticalRule())
        metrics.addView(voltageMetric.first, weightedParams(1f))
        metrics.addView(verticalRule())
        metrics.addView(socMetric.first, weightedParams(1f))
        statusCard.addView(metrics)
        val statusFooter = LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(14), 0, 0)
        }
        dashboardBmsView = label("BMS --", 14f, COLOR_GREEN, true)
        dashboardRecordView = label("暂无骑行记录", 14f, COLOR_MUTED)
        statusFooter.addView(dashboardBmsView, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        statusFooter.addView(dashboardRecordView, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        statusCard.addView(statusFooter)
        column.addView(statusCard, rowParams(bottom = 12))

        column.addView(actionButton("进入投屏", true) { selectRoute(AppRoute.CAST) }, rowParams(bottom = 10, height = dp(54)))
        val shortcuts = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        shortcuts.addView(actionButton("BMS 详情", false) { selectRoute(AppRoute.BMS) }, weightedParams(1f, dp(48)))
        shortcuts.addView(space(dp(10), 1))
        shortcuts.addView(actionButton("骑行记录", false) { selectRoute(AppRoute.RECORDS) }, weightedParams(1f, dp(48)))
        column.addView(shortcuts, rowParams(bottom = 12))

        val note = card()
        note.addView(label("设备控制", 18f, COLOR_TEXT, true))
        note.addView(label("BMS 连接、保护信息、骑行峰值、显示设置、热点与固件更新均使用原生页面管理。", 14f, COLOR_MUTED), rowParams(top = 6))
        column.addView(note)
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildBmsPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        val summary = card()
        bmsStateView = label("BMS 未连接", 20f, COLOR_TEXT, true)
        bmsInfoView = label("连接设备后显示保护板状态", 14f, COLOR_MUTED)
        summary.addView(bmsStateView)
        summary.addView(bmsInfoView, rowParams(top = 6))
        val metrics = LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(14), 0, 0)
        }
        val voltage = metric("包电压", 20f)
        val current = metric("电流", 20f)
        val temperature = metric("温度", 20f)
        bmsVoltageView = voltage.second
        bmsCurrentView = current.second
        bmsTemperatureView = temperature.second
        metrics.addView(voltage.first, weightedParams(1f))
        metrics.addView(verticalRule())
        metrics.addView(current.first, weightedParams(1f))
        metrics.addView(verticalRule())
        metrics.addView(temperature.first, weightedParams(1f))
        summary.addView(metrics)
        bmsCapacityView = label("真实容量 --", 14f, COLOR_MUTED)
        summary.addView(bmsCapacityView, rowParams(top = 12))
        bmsDetailsView = label("单体电压 --\n剩余容量 --\n运行时间 --", 14f, COLOR_MUTED)
        summary.addView(bmsDetailsView, rowParams(top = 8))
        column.addView(summary, rowParams(bottom = 12))

        val safety = card()
        safety.addView(label("保护与告警", 18f, COLOR_TEXT, true))
        safety.addView(label("保护", 13f, COLOR_MUTED, true), rowParams(top = 10))
        bmsProtectionView = label("无", 15f, COLOR_GREEN)
        safety.addView(bmsProtectionView, rowParams(top = 3))
        safety.addView(label("告警", 13f, COLOR_MUTED, true), rowParams(top = 10))
        bmsWarningView = label("无", 15f, COLOR_GREEN)
        safety.addView(bmsWarningView, rowParams(top = 3))
        column.addView(safety, rowParams(bottom = 12))

        bmsBindingCard = card().also { binding ->
            binding.addView(label("连接保护板", 18f, COLOR_TEXT, true))
            binding.addView(label("选择类型后扫描附近设备，或输入 MAC 地址绑定。", 14f, COLOR_MUTED), rowParams(top = 6, bottom = 6))
            val type = selectorRow("保护板类型") {
                chooseOption("保护板类型", bmsTypeOptions, bmsType) { value ->
                    bmsType = value
                    updateBmsTypeLabel()
                }
            }
            bmsTypeValue = type.second
            binding.addView(type.first, rowParams(bottom = 4))
            bmsMacInput = input("AA:BB:CC:DD:EE:FF", InputType.TYPE_CLASS_TEXT).apply {
                setSingleLine(true)
            }
            binding.addView(label("蓝牙 MAC", 13f, COLOR_MUTED, true), rowParams(top = 8))
            binding.addView(bmsMacInput, rowParams(top = 4, bottom = 10, height = dp(48)))
            val controls = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
            bmsScanButton = actionButton("扫描 BMS", false) { startBmsScan() }
            controls.addView(bmsScanButton, weightedParams(1f, dp(48)))
            controls.addView(space(dp(10), 1))
            controls.addView(actionButton("保存并绑定", true) { saveBmsBinding() }, weightedParams(1f, dp(48)))
            binding.addView(controls)
            bmsCandidatesHost = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, dp(8), 0, 0)
            }
            binding.addView(bmsCandidatesHost)
        }
        column.addView(bmsBindingCard)

        val controller = card()
        controller.addView(label("控制器蓝牙", 18f, COLOR_TEXT, true))
        controllerScanButton = actionButton("扫描控制器", false) { requestBleScan(BleScanTarget.CONTROLLER) }
        controller.addView(controllerScanButton, rowParams(top = 10, height = dp(48)))
        controllerCandidatesHost = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(8), 0, 0)
        }
        controller.addView(controllerCandidatesHost)
        column.addView(controller, rowParams(top = 12))
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildRecordsPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        val header = card()
        header.addView(label("骑行峰值记录", 20f, COLOR_TEXT, true))
        header.addView(label("记录每次骑行的最大电流和最大单体压差快照。", 14f, COLOR_MUTED), rowParams(top = 6))
        recordMapButton = actionButton("查看 GPS 轨迹", false) { selectRoute(AppRoute.MAP) }
        header.addView(recordMapButton, rowParams(top = 12, height = dp(46)))
        column.addView(header, rowParams(bottom = 12))
        recordsHost = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        column.addView(recordsHost)
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildMapPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        val header = card()
        header.addView(label("GPS 轨迹地图", 20f, COLOR_TEXT, true))
        trackSummaryView = label("连接设备后同步轨迹", 14f, COLOR_MUTED)
        header.addView(trackSummaryView, rowParams(top = 6))
        column.addView(header, rowParams(bottom = 12))
        trackMapView = TrackMapView(this).apply {
            background = roundedBackground(COLOR_SURFACE_LIGHT, 8, COLOR_BORDER)
        }
        column.addView(trackMapView, rowParams(bottom = 10, height = dp(300)))
        val actions = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        actions.addView(actionButton("返回记录", false) { selectRoute(AppRoute.RECORDS) }, weightedParams(1f, dp(48)))
        actions.addView(space(dp(10), 1))
        actions.addView(actionButton("刷新轨迹", true) { refreshGpsTrack() }, weightedParams(1f, dp(48)))
        column.addView(actions)
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildSettingsPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        val connection = card()
        connection.addView(label("设备设置", 20f, COLOR_TEXT, true))
        settingsConnectionView = label("连接设备后可同步设置", 14f, COLOR_MUTED)
        connection.addView(settingsConnectionView, rowParams(top = 6))
        column.addView(connection, rowParams(bottom = 12))

        val device = card()
        settingsDeviceCard = device
        settingRows.clear()
        settingControls.clear()
        device.addView(label("显示与语言", 18f, COLOR_TEXT, true))
        brightnessValue = label("亮度 80%", 14f, COLOR_MUTED)
        brightnessSeek = SeekBar(this).apply {
            max = 90
            progress = 70
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    brightness = progress + 10
                    brightnessValue.text = "亮度 $brightness%"
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar) = Unit
            })
        }
        val brightnessRow = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(brightnessValue)
            addView(brightnessSeek, rowParams(bottom = 4))
        }
        settingRows["brightness"] = brightnessRow
        settingControls["brightness"] = brightnessSeek
        device.addView(brightnessRow, rowParams(top = 10))
        volumeValue = label("音量 50%", 14f, COLOR_MUTED)
        volumeSeek = SeekBar(this).apply {
            max = 100
            progress = 50
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    volume = progress
                    volumeValue.text = "音量 $volume%"
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar) = Unit
            })
        }
        val volumeRow = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(volumeValue)
            addView(volumeSeek, rowParams(bottom = 6))
        }
        settingRows["volume"] = volumeRow
        settingControls["volume"] = volumeSeek
        device.addView(volumeRow, rowParams(top = 4))
        val rotation = selectorRow("屏幕方向") {
            chooseOption("屏幕方向", rotationOptions, displayRotation) { value ->
                displayRotation = value
                updateSettingsValues()
            }
        }
        rotationValue = rotation.second
        settingRows["display_rotation"] = rotation.first
        settingControls["display_rotation"] = rotation.first
        device.addView(rotation.first)
        val unit = selectorRow("速度单位") {
            chooseOption("速度单位", speedUnitOptions, speedUnit) { value ->
                speedUnit = value
                updateSettingsValues()
            }
        }
        speedUnitValue = unit.second
        settingRows["speed_unit"] = unit.first
        settingControls["speed_unit"] = unit.first
        device.addView(unit.first)
        val source = selectorRow("速度来源") {
            chooseOption("速度来源", speedSourceOptions, speedSource) { value ->
                speedSource = value
                updateSettingsValues()
            }
        }
        speedSourceValue = source.second
        settingRows["speed_source"] = source.first
        settingControls["speed_source"] = source.first
        device.addView(source.first)
        val language = selectorRow("设备语言") {
            chooseOption("设备语言", languageOptions, this.language) { value ->
                this.language = value
                updateSettingsValues()
            }
        }
        languageValue = language.second
        settingRows["language"] = language.first
        settingControls["language"] = language.first
        device.addView(language.first, rowParams(bottom = 10))
        settingsSaveButton = actionButton("保存设备设置", true) { saveDeviceSettings() }
        device.addView(settingsSaveButton, rowParams(height = dp(48)))
        column.addView(device, rowParams(bottom = 12))

        otaCard = card().also { ota ->
            ota.addView(label("固件更新", 18f, COLOR_TEXT, true))
            ota.addView(label("选择 .bin 固件并输入四位验证码。升级过程中不要断电或离开设备热点。", 14f, COLOR_MUTED), rowParams(top = 6, bottom = 8))
            otaFileValue = label("尚未选择固件", 14f, COLOR_MUTED)
            ota.addView(otaFileValue, rowParams(bottom = 8))
            ota.addView(actionButton("选择 .bin 文件", false) { chooseFirmware() }, rowParams(bottom = 8, height = dp(46)))
            otaCodeInput = input("四位固件验证码", InputType.TYPE_CLASS_NUMBER).apply {
                setSingleLine(true)
                filters = arrayOf(android.text.InputFilter.LengthFilter(4))
            }
            ota.addView(otaCodeInput, rowParams(bottom = 8, height = dp(48)))
            otaProgressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
                max = 100
                progress = 0
            }
            ota.addView(otaProgressBar, rowParams(bottom = 4))
            otaProgressText = label("", 13f, COLOR_MUTED)
            ota.addView(otaProgressText, rowParams(bottom = 8))
            otaUploadButton = actionButton("上传并更新", true) { uploadFirmware() }
            ota.addView(otaUploadButton, rowParams(height = dp(48)))
        }
        column.addView(otaCard)
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildCastPage(): ScrollView {
        val page = page()
        val column = pageColumn()
        statusChipView = label("等待设备连接", 13f, COLOR_MUTED, true).apply {
            background = roundedBackground(COLOR_SURFACE, 18, COLOR_BORDER)
            gravity = Gravity.CENTER
            setPadding(dp(12), dp(6), dp(12), dp(6))
        }
        column.addView(statusChipView, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        ).apply { bottomMargin = dp(10) })

        val statusCard = card()
        statusCard.addView(label("投屏状态", 14f, COLOR_MUTED, true))
        statusView = label("等待连接设备热点", 24f, COLOR_TEXT, true).apply {
            maxLines = 3
            ellipsize = TextUtils.TruncateAt.END
        }
        statusCard.addView(statusView, rowParams(top = 8))
        deviceView = label("尚未连接设备", 14f, COLOR_MUTED)
        statusCard.addView(deviceView, rowParams(top = 8))
        column.addView(statusCard, rowParams(bottom = 12))

        castButton = actionButton("开始投屏", true) {
            if (primaryAction(stage, info != null).stopsCast) stopCasting() else requestProjection()
        }
        column.addView(castButton, rowParams(bottom = 10, height = dp(56)))
        connectionButton = actionButton("连接 / 刷新设备", false) {
            info = null
            refreshAll()
        }
        column.addView(connectionButton, rowParams(bottom = 14, height = dp(52)))

        column.addView(label("投屏只在手机与设备热点之间传输。网络较慢时会主动丢弃旧画面以减少延迟。", 13f, COLOR_MUTED))
        page.addView(column, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return page
    }

    private fun buildRouteBar(): View {
        val bar = LinearLayout(this).apply {
            gravity = Gravity.CENTER
            background = roundedBackground(COLOR_SURFACE, 10, COLOR_BORDER)
            setPadding(dp(4), dp(5), dp(4), dp(5))
        }
        routeTabs.clear()
        listOf(AppRoute.CAST, AppRoute.BMS, AppRoute.RECORDS, AppRoute.SETTINGS).forEach { route ->
            val tab = label(route.label, 14f, COLOR_MUTED, true).apply {
                gravity = Gravity.CENTER
                isClickable = true
                isFocusable = true
                setOnClickListener { selectRoute(route) }
            }
            routeTabs[route] = tab
            bar.addView(tab, LinearLayout.LayoutParams(0, dp(48), 1f))
        }
        return bar
    }

    private fun routeVisible(route: AppRoute): Boolean {
        val profile = capabilities ?: return route != AppRoute.MAP
        return when (route) {
            AppRoute.CAST, AppRoute.DASHBOARD -> true
            AppRoute.BMS -> profile.hasSection("bms")
            AppRoute.RECORDS -> profile.hasSection("records") || profile.hasSection("bms")
            AppRoute.MAP -> profile.supports("gps_track")
            AppRoute.SETTINGS -> profile.hasSection("device")
        }
    }

    private fun renderRouteBar() {
        if (!::routeBar.isInitialized) return
        if (!routeVisible(selectedRoute)) selectRoute(AppRoute.CAST, refresh = false)
        routeTabs.forEach { (route, tab) ->
            tab.visibility = if (routeVisible(route)) View.VISIBLE else View.GONE
        }
    }

    private fun selectRoute(route: AppRoute, refresh: Boolean = true) {
        if (!routeVisible(route)) return
        selectedRoute = route
        routePages.forEach { (key, page) -> page.visibility = if (key == route) View.VISIBLE else View.GONE }
        routeTabs.forEach { (key, tab) -> styleTab(tab, key == route) }
        routeBar.visibility = View.VISIBLE
        if (refresh) refreshRouteData(route)
    }

    private fun refreshAll() {
        info = null
        refreshDeviceProfile(force = true, refreshRoute = true)
    }

    private fun bindDeviceNetwork(): Boolean {
        val cm = connectivityManager()
        val wifi = network?.takeIf { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
            ?: cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
            ?: return false
        network = wifi
        cm.bindProcessToNetwork(wifi)
        return true
    }

    private fun refreshDeviceProfile(force: Boolean = false, refreshRoute: Boolean = false) {
        if (refreshRoute) refreshRouteWhenProfileLoaded = true
        if (!bindDeviceNetwork()) {
            profileLoading = false
            deviceLoading = false
            latestStatus = null
            latestConfig = null
            latestRecords = emptyList()
            latestTrack = emptyList()
            capabilities = null
            deviceError = "请先在系统 Wi-Fi 设置中连接设备热点"
            renderNativePages()
            return
        }
        if (profileLoading) return
        if (!force && capabilities != null) {
            if (refreshRouteWhenProfileLoaded) {
                refreshRouteWhenProfileLoaded = false
                refreshRouteData(selectedRoute)
            }
            return
        }
        val requestId = ++profileRequestId
        profileLoading = true
        renderNativePages()
        thread {
            val profile = runCatching { DeviceApi.capabilities(host) }.getOrNull()
            runOnUiThread {
                if (requestId != profileRequestId || isFinishing) return@runOnUiThread
                profileLoading = false
                if (profile == null) {
                    latestStatus = null
                    latestConfig = null
                    latestRecords = emptyList()
                    latestTrack = emptyList()
                    capabilities = null
                    deviceError = "无法读取设备功能配置，请确认已连接两轮智控热点"
                    if (stage !in setOf(UiStage.REQUESTING_PERMISSION, UiStage.CAST_CONNECTING, UiStage.CASTING)) {
                        setStage(UiStage.FAILED, deviceError)
                    }
                } else {
                    capabilities = profile
                    deviceError = ""
                    if (stage !in setOf(UiStage.REQUESTING_PERMISSION, UiStage.CAST_CONNECTING, UiStage.CASTING)) {
                        setStage(UiStage.READY)
                    }
                }
                renderNativePages()
                if (profile != null && refreshRouteWhenProfileLoaded) {
                    refreshRouteWhenProfileLoaded = false
                    refreshRouteData(selectedRoute)
                }
            }
        }
    }

    private fun refreshRouteData(route: AppRoute) {
        if (capabilities == null) {
            refreshDeviceProfile(refreshRoute = true)
            return
        }
        if (!routeVisible(route)) return
        when (route) {
            AppRoute.CAST -> connectExistingWifiOrLoadInfo()
            AppRoute.BMS -> refreshDeviceData(loadStatus = true, loadConfig = true)
            AppRoute.RECORDS -> refreshDeviceData(loadRecords = true, loadTrack = true)
            AppRoute.MAP -> Unit
            AppRoute.SETTINGS -> refreshDeviceData(loadConfig = true)
            AppRoute.DASHBOARD -> refreshDeviceData(loadStatus = true)
        }
    }

    private fun refreshGpsTrack() {
        if (capabilities?.supports("gps_track") == true) {
            refreshDeviceData(loadTrack = true)
        }
    }

    private fun refreshDeviceData(
        loadStatus: Boolean = false,
        loadConfig: Boolean = false,
        loadRecords: Boolean = false,
        loadTrack: Boolean = false,
    ) {
        if (!bindDeviceNetwork()) {
            deviceLoading = false
            latestStatus = null
            latestConfig = null
            if (loadRecords) latestRecords = emptyList()
            if (loadTrack) latestTrack = emptyList()
            deviceError = "请先在系统 Wi-Fi 设置中连接设备热点"
            renderNativePages()
            return
        }
        val requestId = ++deviceRequestId
        deviceLoading = true
        renderNativePages()
        thread {
            val status = if (loadStatus) runCatching { DeviceApi.status(host) } else null
            val config = if (loadConfig) runCatching { DeviceApi.config(host) } else null
            val records = if (loadRecords) runCatching { DeviceApi.rideRecords(host) } else null
            val track = if (loadTrack) runCatching { DeviceApi.gpsTrack(host) } else null
            val history = if (loadTrack) runCatching { DeviceApi.historySessions(host) } else null
            val historyTrack = if (loadTrack) runCatching {
                history?.getOrNull()?.maxByOrNull { it.id }?.let { session ->
                    DeviceApi.historySamples(host, session.id).filter { it.flags and 1 != 0 }.map {
                        GpsPoint(it.latitudeE7 / 10_000_000.0, it.longitudeE7 / 10_000_000.0, it.timestamp)
                    }
                }.orEmpty()
            } else null
            val allRequestedCallsFailed =
                (!loadStatus || status?.isFailure == true) &&
                (!loadConfig || config?.isFailure == true) &&
                (!loadRecords || records?.isFailure == true) &&
                (!loadTrack || (track?.isFailure == true && history?.isFailure == true))
            runOnUiThread {
                if (requestId != deviceRequestId || isFinishing) return@runOnUiThread
                if (loadStatus) latestStatus = status?.getOrNull()
                if (loadConfig) latestConfig = config?.getOrNull()
                if (loadRecords) latestRecords = records?.getOrDefault(emptyList()).orEmpty()
                if (loadTrack) latestTrack = track?.getOrDefault(emptyList()).orEmpty()
                if (loadTrack) {
                    latestHistorySessions = history?.getOrDefault(emptyList()).orEmpty()
                    historyTrack?.getOrNull()?.takeIf { it.isNotEmpty() }?.let { latestTrack = it }
                }
                deviceLoading = false
                deviceError = if (allRequestedCallsFailed) "设备数据读取失败，请点击刷新重试" else ""
                renderNativePages()
            }
        }
    }

    private fun renderNativePages() {
        if (!::dashboardConnectionView.isInitialized) return
        renderRouteBar()
        renderDashboard()
        renderBms()
        renderRecords()
        renderSettings()
        renderMap()
    }

    private fun renderDashboard() {
        val status = latestStatus
        dashboardConnectionView.text = when {
            deviceLoading -> "正在同步设备数据"
            status != null -> "设备在线  |  固件 ${status.version}"
            else -> deviceError
        }
        dashboardConnectionView.setTextColor(if (status != null) COLOR_GREEN else COLOR_MUTED)
        dashboardSpeedView.text = status?.let { "${it.speed}\n${it.speedUnit}" } ?: "--"
        dashboardVoltageView.text = formatMillivolts(status?.localBatteryMv ?: status?.packVoltageMv)
        dashboardSocView.text = status?.socPercent?.let { "$it%" } ?: "--"
        dashboardBmsView.text = status?.let { formatBmsState(it) } ?: "BMS --"
        dashboardBmsView.setTextColor(if (status?.bms == "online") COLOR_GREEN else COLOR_MUTED)
        dashboardRecordView.text = when {
            latestRecords.isEmpty() -> "暂无峰值记录"
            latestRecords.any { it.current } -> "当前骑行正在记录"
            else -> "${latestRecords.size} 条峰值记录"
        }
    }

    private fun renderBms() {
        val status = latestStatus
        bmsStateView.text = status?.let(::formatBmsState) ?: "BMS 未连接"
        bmsStateView.setTextColor(if (status?.bms == "online") COLOR_GREEN else COLOR_TEXT)
        bmsInfoView.text = status?.bmsInfo ?: "连接设备后显示保护板信息"
        bmsVoltageView.text = formatMillivolts(status?.packVoltageMv)
        bmsCurrentView.text = formatDeciAmps(status?.currentDeciAmps)
        bmsTemperatureView.text = status?.temperaturesC?.joinToString("/") { "$it C" } ?: "--"
        bmsCapacityView.text = "真实容量 ${formatCapacity(status?.capacityMah, status?.capacityState.orEmpty())}"
        bmsDetailsView.text = status?.let {
            "单体电压 ${formatCellRange(it)}\n" +
                "剩余容量 ${formatCapacityValue(it.remainingCapacityMah)}  | 总容量 ${formatCapacityValue(it.totalCapacityMah)}\n" +
                "运行时间 ${formatDuration(it.runningTimeSeconds)}  | 循环容量 ${formatCapacityValue(it.cycleCapacityMah)}"
        } ?: "单体电压 --\n剩余容量 --\n运行时间 --"
        bmsProtectionView.text = status?.protections?.takeIf { it.isNotEmpty() }?.joinToString("  ") ?: "无"
        bmsWarningView.text = status?.warnings?.takeIf { it.isNotEmpty() }?.joinToString("  ") ?: "无"
        bmsProtectionView.setTextColor(if (status?.protections.isNullOrEmpty()) COLOR_GREEN else COLOR_WARN)
        bmsWarningView.setTextColor(if (status?.warnings.isNullOrEmpty()) COLOR_GREEN else COLOR_WARN)
        latestConfig?.let { config ->
            if (renderedBmsConfig != config) {
                renderedBmsConfig = config
                bmsType = config.bmsType
                updateBmsTypeLabel()
                if (bmsMacInput.text.toString() != config.bmsMac) bmsMacInput.setText(config.bmsMac)
            }
        }
        val bmsSupported = capabilities?.let {
            it.supports("bms_mac") || it.supports("bms_type") || it.supports("bms_scan")
        } ?: false
        bmsBindingCard.visibility = if (bmsSupported) View.VISIBLE else View.GONE
        setViewEnabled(bmsBindingCard, latestConfig != null && bmsSupported)
    }

    private fun renderRecords() {
        recordMapButton.visibility = if (capabilities?.supports("gps_track") == false) View.GONE else View.VISIBLE
        recordMapButton.isEnabled = capabilities?.supports("gps_track") == true
        recordMapButton.alpha = if (recordMapButton.isEnabled) 1f else 0.45f
        trackSummaryView.text = when {
            capabilities == null && !deviceLoading -> "连接设备后同步轨迹"
            latestTrack.isEmpty() -> "暂无已记录的 GPS 坐标"
            else -> "已记录 ${latestTrack.size} 个坐标点"
        }
        trackMapView.setPoints(latestTrack)
        recordsHost.removeAllViews()
        if (capabilities == null && !deviceLoading) {
            recordsHost.addView(emptyState("连接设备后可查看骑行峰值记录"))
            return
        }
        if (latestRecords.isEmpty()) {
            recordsHost.addView(emptyState(if (deviceLoading) "正在同步骑行记录" else "暂时没有骑行峰值记录"))
            return
        }
        latestRecords.forEachIndexed { index, record ->
            val card = card()
            card.addView(label(if (record.current) "当前骑行" else "骑行记录 ${index + 1}", 17f, COLOR_TEXT, true))
            card.addView(label("最大电流", 13f, COLOR_MUTED, true), rowParams(top = 10))
            card.addView(label(formatSnapshot(record.maxCurrent), 14f, COLOR_TEXT), rowParams(top = 3))
            card.addView(label("最大单体压差", 13f, COLOR_MUTED, true), rowParams(top = 10))
            card.addView(label(formatSnapshot(record.maxDelta), 14f, COLOR_TEXT), rowParams(top = 3))
            recordsHost.addView(card, rowParams(bottom = 10))
        }
    }

    private fun renderSettings() {
        val config = latestConfig
        settingsConnectionView.text = when {
            deviceLoading -> "正在同步设备设置"
            config != null -> "已连接 ${config.setupApSsid}"
            else -> "连接设备后可同步设置"
        }
        if (config != null && renderedSettingsConfig != config) {
            renderedSettingsConfig = config
            brightness = config.brightness.coerceIn(10, 100)
            volume = config.volume.coerceIn(0, 100)
            displayRotation = config.displayRotation
            speedUnit = config.speedUnit
            speedSource = config.speedSource
            language = config.language
            brightnessSeek.progress = brightness - 10
            volumeSeek.progress = volume
            updateSettingsValues()
        }
        val online = config != null && capabilities != null
        settingRows.forEach { (id, row) ->
            val supported = capabilities?.supports(id) ?: true
            row.visibility = if (supported) View.VISIBLE else View.GONE
        }
        settingControls.values.forEach { control -> setViewEnabled(control, online && control.visibility == View.VISIBLE) }
        settingsSaveButton.isEnabled = online && settingRows.any { (id, row) ->
            row.visibility == View.VISIBLE && capabilities?.supports(id) != false
        }
        settingsSaveButton.alpha = if (settingsSaveButton.isEnabled) 1f else 0.45f
        otaCard.visibility = if (capabilities?.supports("ota") == false) View.GONE else View.VISIBLE
        setViewEnabled(otaCard, online && capabilities?.supports("ota") != false)
    }

    private fun renderMap() {
        if (!::trackMapView.isInitialized) return
        trackMapView.setPoints(latestTrack)
    }

    private fun setViewEnabled(view: View, enabled: Boolean) {
        view.isEnabled = enabled
        view.alpha = if (enabled) 1f else 0.45f
        if (view is ViewGroup) {
            for (index in 0 until view.childCount) {
                setViewEnabled(view.getChildAt(index), enabled)
            }
        }
    }

    private fun updateBmsTypeLabel() {
        if (!::bmsTypeValue.isInitialized) return
        bmsTypeValue.text = bmsTypeOptions.firstOrNull { it.first == bmsType }?.second ?: bmsType
    }

    private fun updateSettingsValues() {
        if (!::rotationValue.isInitialized) return
        brightnessValue.text = "亮度 $brightness%"
        volumeValue.text = "音量 $volume%"
        rotationValue.text = rotationOptions.firstOrNull { it.first == displayRotation }?.second ?: displayRotation
        speedUnitValue.text = speedUnitOptions.firstOrNull { it.first == speedUnit }?.second ?: speedUnit
        speedSourceValue.text = speedSourceOptions.firstOrNull { it.first == speedSource }?.second ?: speedSource
        languageValue.text = languageOptions.firstOrNull { it.first == language }?.second ?: language
    }

    private fun startBmsScan() {
        requestBleScan(BleScanTarget.BMS)
    }

    private fun requestBleScan(target: BleScanTarget) {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = permissions.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) {
            pendingBleScanTarget = target
            requestPermissions(missing.toTypedArray(), BLE_PERMISSION_REQUEST)
            return
        }
        startPhoneBleScan(target)
    }

    private fun startPhoneBleScan(target: BleScanTarget) {
        val adapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        val scanner = adapter?.bluetoothLeScanner
        if (adapter == null || !adapter.isEnabled || scanner == null) {
            toast("请先开启手机蓝牙")
            return
        }
        stopBleScan()
        activeBleScanTarget = target
        bleCandidates.clear()
        renderBleCandidates(target)
        setBleScanButtonsEnabled(false)
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(null, settings, bleScanCallback)
        uiHandler.postDelayed(bleScanTimeout, BLE_SCAN_DURATION_MS)
    }

    private fun acceptBleResult(result: ScanResult) {
        val target = activeBleScanTarget ?: return
        val mac = result.device.address
        val name = result.scanRecord?.deviceName?.takeIf { it.isNotBlank() } ?: "未命名设备"
        bleCandidates[mac] = BmsCandidate(name, mac, result.rssi)
        runOnUiThread { if (activeBleScanTarget == target) renderBleCandidates(target) }
    }

    private fun stopBleScan() {
        uiHandler.removeCallbacks(bleScanTimeout)
        if (activeBleScanTarget != null) {
            val scanner = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
            runCatching { scanner?.stopScan(bleScanCallback) }
        }
        activeBleScanTarget = null
        if (::bmsScanButton.isInitialized && ::controllerScanButton.isInitialized) setBleScanButtonsEnabled(true)
    }

    private fun setBleScanButtonsEnabled(enabled: Boolean) {
        bmsScanButton.isEnabled = enabled
        controllerScanButton.isEnabled = enabled
    }

    private fun renderBleCandidates(target: BleScanTarget) {
        val host = if (target == BleScanTarget.BMS) bmsCandidatesHost else controllerCandidatesHost
        host.removeAllViews()
        if (bleCandidates.isEmpty()) return
        host.addView(label("扫描结果", 13f, COLOR_MUTED, true), rowParams(top = 4, bottom = 4))
        bleCandidates.values.sortedByDescending { it.rssi ?: Int.MIN_VALUE }.forEach { candidate ->
            val strength = candidate.rssi?.let { "  $it dBm" }.orEmpty()
            val item = actionButton("${candidate.name}\n${candidate.mac}$strength", false) {
                if (target == BleScanTarget.BMS) {
                    bmsMacInput.setText(candidate.mac)
                    bmsMacInput.setSelection(bmsMacInput.length())
                }
            }.apply {
                gravity = Gravity.START or Gravity.CENTER_VERTICAL
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            }
            host.addView(item, rowParams(bottom = 6, height = dp(56)))
        }
    }

    private fun saveBmsBinding() {
        if (!bindDeviceNetwork()) {
            toast("请先连接设备热点")
            return
        }
        val mac = bmsMacInput.text.toString().trim()
        val profile = capabilities
        thread {
            try {
                if (profile?.supports("bms_type") == true) {
                    DeviceApi.saveConfig(host, JSONObject().put("bms_type", bmsType))
                }
                if (mac.isNotBlank() && profile?.supports("bms_mac") == true) DeviceApi.bindBms(host, mac)
                runOnUiThread {
                    toast(if (mac.isBlank()) "已保存保护板类型" else "已提交 BMS 绑定")
                    refreshRouteData(AppRoute.BMS)
                }
            } catch (error: Exception) {
                runOnUiThread { toast("保存失败：${error.message ?: "请检查保护板地址"}") }
            }
        }
    }

    private fun saveDeviceSettings() {
        val profile = capabilities
        if (latestConfig == null || profile == null || !bindDeviceNetwork()) {
            toast("请先连接设备并等待配置加载")
            return
        }
        val values = JSONObject()
        if (profile.supports("brightness")) values.put("brightness", brightness)
        if (profile.supports("volume")) values.put("volume", volume)
        if (profile.supports("display_rotation")) values.put("display_rotation", displayRotation)
        if (profile.supports("speed_unit")) values.put("speed_unit", speedUnit)
        if (profile.supports("speed_source")) values.put("speed_source", speedSource)
        if (profile.supports("language")) values.put("language", language)
        if (values.length() == 0) {
            toast("当前固件没有可编辑的设备设置")
            return
        }
        settingsSaveButton.isEnabled = false
        settingsSaveButton.alpha = 0.45f
        thread {
            try {
                DeviceApi.saveConfig(host, values)
                runOnUiThread {
                    toast("设备设置已保存")
                    refreshRouteData(AppRoute.SETTINGS)
                }
            } catch (error: Exception) {
                runOnUiThread {
                    settingsSaveButton.isEnabled = true
                    settingsSaveButton.alpha = 1f
                    toast("设备设置保存失败：${error.message ?: "设备拒绝请求"}")
                }
            }
        }
    }

    private fun chooseFirmware() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
        }
        startActivityForResult(intent, OTA_FILE_REQUEST)
    }

    private fun uploadFirmware() {
        val uri = otaFile ?: run {
            toast("请先选择 .bin 固件")
            return
        }
        val code = otaCodeInput.text.toString()
        if (!code.matches(Regex("\\d{4}"))) {
            toast("请输入四位固件验证码")
            return
        }
        if (!bindDeviceNetwork()) {
            toast("请先连接设备热点")
            return
        }
        otaUploadButton.isEnabled = false
        otaProgressBar.progress = 0
        otaProgressText.text = "正在读取固件"
        thread {
            try {
                val firmware = readFirmware(uri)
                DeviceApi.uploadFirmware(host, firmware, code) { percent ->
                    runOnUiThread {
                        otaProgressBar.progress = percent
                        otaProgressText.text = "正在上传 $percent%"
                    }
                }
                runOnUiThread {
                    otaProgressText.text = "正在校验固件"
                    otaPollAttempt = 0
                    pollOtaProgress()
                }
            } catch (error: Exception) {
                runOnUiThread {
                    otaUploadButton.isEnabled = true
                    otaProgressText.text = "更新失败：${error.message ?: "请重试"}"
                }
            }
        }
    }

    private fun readFirmware(uri: Uri): ByteArray {
        val input = contentResolver.openInputStream(uri) ?: error("无法读取固件文件")
        return input.use { stream ->
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(16 * 1024)
            while (true) {
                val count = stream.read(buffer)
                if (count < 0) break
                if (output.size() + count > DeviceApi.MAX_FIRMWARE_BYTES) error("固件文件超过 1.5 MB")
                output.write(buffer, 0, count)
            }
            output.toByteArray()
        }
    }

    private fun pollOtaProgress() {
        thread {
            val progress = runCatching { DeviceApi.otaProgress(host) }.getOrNull()
            runOnUiThread {
                if (isFinishing) return@runOnUiThread
                if (progress == null) {
                    if (otaPollAttempt++ < 20) {
                        otaProgressText.text = "设备正在重启"
                        uiHandler.postDelayed({ pollOtaProgress() }, 1_000)
                    } else {
                        otaUploadButton.isEnabled = true
                        otaProgressText.text = "设备重启中，请稍后刷新确认"
                    }
                    return@runOnUiThread
                }
                otaProgressBar.progress = progress.percent
                when (progress.state) {
                    "UPDATE FAILED" -> {
                        otaUploadButton.isEnabled = true
                        otaProgressText.text = "更新失败：${progress.message.ifBlank { "请重试" }}"
                    }
                    "REBOOTING" -> {
                        otaProgressText.text = "设备正在重启"
                        uiHandler.postDelayed({
                            otaUploadButton.isEnabled = true
                            refreshAll()
                        }, 4_000)
                    }
                    else -> {
                        otaProgressText.text = when (progress.state) {
                            "UPLOADING" -> "设备正在接收固件"
                            "VERIFYING" -> "正在校验固件 ${progress.percent}%"
                            else -> if (progress.active) "正在更新 ${progress.percent}%" else "更新请求已提交"
                        }
                        if (progress.active) {
                            uiHandler.postDelayed({ pollOtaProgress() }, 500)
                        } else {
                            otaUploadButton.isEnabled = true
                        }
                    }
                }
            }
        }
    }

    private fun selectedFileName(uri: Uri): String {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0) return cursor.getString(index)
            }
        }
        return uri.lastPathSegment ?: "firmware.bin"
    }

    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        if (request == OTA_FILE_REQUEST) {
            val uri = data?.data ?: return
            val name = selectedFileName(uri)
            if (!name.endsWith(".bin", ignoreCase = true)) {
                toast("请选择 .bin 固件文件")
                return
            }
            otaFile = uri
            otaFileValue.text = name
            otaProgressBar.progress = 0
            otaProgressText.text = ""
            return
        }
        if (request != PROJECTION_REQUEST) return
        val castInfo = info
        if (result != RESULT_OK || data == null) return setStage(UiStage.FAILED, "未授予屏幕录制权限，点击重试投屏可再次申请")
        if (castInfo == null) return setStage(UiStage.FAILED, "设备连接已失效，请重新连接")
        startForegroundService(CastService.intent(this, result, data, host, castInfo))
        setStage(UiStage.CAST_CONNECTING)
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
            UiStage.CONNECTING_WIFI -> "正在连接设备热点..."
            UiStage.QUERYING_DEVICE -> "热点已连接，正在查询设备能力..."
            UiStage.READY -> "设备已就绪"
            UiStage.REQUESTING_PERMISSION -> "请确认屏幕录制权限"
            UiStage.CAST_CONNECTING -> "正在连接投屏服务，失败时会自动重试..."
            UiStage.CASTING -> "投屏中"
            UiStage.STOPPED -> "投屏已停止"
            UiStage.FAILED -> "操作失败，请重试"
        }
        deviceView.text = info?.let {
            "显示分辨率 ${it.width} x ${it.height}  |  旋转 ${it.rotation}°\n目标：$host"
        } ?: ssid?.let {
            "热点：$it\n目标：$host"
        } ?: "尚未连接设备"
        val online = info != null || stage in setOf(UiStage.READY, UiStage.CAST_CONNECTING, UiStage.CASTING)
        statusChipView.text = if (online) "设备在线" else "等待设备连接"
        statusChipView.setTextColor(if (online) COLOR_GREEN else COLOR_MUTED)
        statusChipView.background = roundedBackground(if (online) COLOR_GREEN_SOFT else COLOR_SURFACE, 18, if (online) COLOR_GREEN else COLOR_BORDER)

        val action = primaryAction(stage, info != null)
        castButton.text = action.label
        castButton.isEnabled = action.enabled
        castButton.alpha = if (action.enabled) 1f else 0.45f
        connectionButton.text = when {
            stage == UiStage.FAILED && info == null -> "重试连接"
            else -> "连接 / 刷新设备"
        }
        connectionButton.isEnabled = !infoRequestInFlight && stage !in setOf(
            UiStage.REQUESTING_PERMISSION,
            UiStage.CAST_CONNECTING,
            UiStage.CASTING,
        )
        connectionButton.alpha = if (connectionButton.isEnabled) 1f else 0.45f
    }

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

    private fun formatBmsState(status: DeviceStatus): String =
        if (status.bms == "online") "BMS 正常" else "BMS 离线"

    private fun chooseOption(
        title: String,
        options: List<Pair<String, String>>,
        selected: String,
        onSelected: (String) -> Unit,
    ) {
        val selectedIndex = options.indexOfFirst { it.first == selected }.coerceAtLeast(0)
        AlertDialog.Builder(this)
            .setTitle(title)
            .setSingleChoiceItems(options.map { it.second }.toTypedArray(), selectedIndex) { dialog, index ->
                onSelected(options[index].first)
                dialog.dismiss()
            }
            .show()
    }

    private fun selectorRow(title: String, onClick: () -> Unit): Pair<View, TextView> {
        val row = LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            isClickable = true
            isFocusable = true
            setPadding(0, dp(12), 0, dp(12))
            setOnClickListener { onClick() }
        }
        row.addView(label(title, 15f, COLOR_TEXT), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        val value = label("--", 15f, COLOR_CYAN, true).apply { gravity = Gravity.END }
        row.addView(value, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        return row to value
    }

    private fun metric(title: String, textSize: Float): Pair<LinearLayout, TextView> {
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(4), 0, dp(4), 0)
        }
        column.addView(label(title, 12f, COLOR_MUTED), LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        val value = label("--", textSize, COLOR_CYAN, true).apply {
            gravity = Gravity.CENTER
            setPadding(0, dp(6), 0, 0)
        }
        column.addView(value, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return column to value
    }

    private fun emptyState(text: String) = card().apply {
        addView(label(text, 15f, COLOR_MUTED).apply {
            gravity = Gravity.CENTER
            setPadding(dp(12), dp(12), dp(12), dp(12))
        })
    }

    private fun page() = ScrollView(this).apply {
        isFillViewport = true
        clipToPadding = false
    }

    private fun pageColumn() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(0, dp(4), 0, dp(8))
    }

    private fun card() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = roundedBackground(COLOR_SURFACE, 8, COLOR_BORDER)
        setPadding(dp(16), dp(16), dp(16), dp(16))
    }

    private fun actionButton(value: String, primary: Boolean, action: () -> Unit) = Button(this).apply {
        text = value
        isAllCaps = false
        gravity = Gravity.CENTER
        minimumHeight = 0
        minHeight = 0
        setPadding(dp(12), 0, dp(12), 0)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        setTextColor(if (primary) COLOR_BACKGROUND else COLOR_TEXT)
        background = roundedBackground(if (primary) COLOR_CYAN else COLOR_SURFACE_LIGHT, 8, if (primary) COLOR_CYAN else COLOR_BORDER)
        setOnClickListener { action() }
    }

    private fun input(hint: String, type: Int) = EditText(this).apply {
        this.hint = hint
        inputType = type
        setHintTextColor(COLOR_MUTED)
        setTextColor(COLOR_TEXT)
        textSize = 16f
        background = roundedBackground(COLOR_SURFACE_LIGHT, 8, COLOR_BORDER)
        setPadding(dp(12), 0, dp(12), 0)
    }

    private fun label(value: String, size: Float, color: Int, bold: Boolean = false) = TextView(this).apply {
        text = value
        setTextSize(TypedValue.COMPLEX_UNIT_SP, size)
        setTextColor(color)
        if (bold) typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
    }

    private fun styleTab(tab: TextView, active: Boolean) {
        tab.setTextColor(if (active) COLOR_BACKGROUND else COLOR_MUTED)
        tab.background = roundedBackground(if (active) COLOR_CYAN else COLOR_SURFACE, 7)
        tab.typeface = Typeface.create(Typeface.DEFAULT, if (active) Typeface.BOLD else Typeface.NORMAL)
    }

    private fun verticalRule() = View(this).apply {
        setBackgroundColor(COLOR_BORDER)
    }.also { view ->
        view.layoutParams = LinearLayout.LayoutParams(dp(1), dp(62)).apply {
            marginStart = dp(4)
            marginEnd = dp(4)
        }
    }

    private fun space(width: Int, height: Int) = View(this).apply {
        layoutParams = LinearLayout.LayoutParams(width, height)
    }

    private fun rowParams(
        bottom: Int = 0,
        top: Int = 0,
        height: Int = ViewGroup.LayoutParams.WRAP_CONTENT,
    ) = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        height,
    ).apply {
        topMargin = dp(top)
        bottomMargin = dp(bottom)
    }

    private fun weightedParams(weight: Float, height: Int = ViewGroup.LayoutParams.WRAP_CONTENT) =
        LinearLayout.LayoutParams(0, height, weight)

    private fun roundedBackground(fill: Int, radius: Int, stroke: Int = Color.TRANSPARENT) = GradientDrawable().apply {
        setColor(fill)
        cornerRadius = dp(radius).toFloat()
        if (stroke != Color.TRANSPARENT) setStroke(dp(1), stroke)
    }

    private fun dp(value: Int) = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP,
        value.toFloat(),
        resources.displayMetrics,
    ).toInt()

    private fun toast(value: String) = Toast.makeText(this, value, Toast.LENGTH_SHORT).show()

    private fun registerReceiverCompat(receiver: BroadcastReceiver, action: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(receiver, IntentFilter(action))
        }
    }

    private fun connectivityManager() = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    private companion object {
        const val PROJECTION_REQUEST = 10
        const val OTA_FILE_REQUEST = 11
        const val BLE_PERMISSION_REQUEST = 12
        const val BLE_SCAN_DURATION_MS = 12_000L

        val COLOR_BACKGROUND = Color.rgb(10, 15, 20)
        val COLOR_SURFACE = Color.rgb(23, 31, 40)
        val COLOR_SURFACE_LIGHT = Color.rgb(34, 45, 56)
        val COLOR_BORDER = Color.rgb(57, 70, 82)
        val COLOR_TEXT = Color.rgb(241, 246, 249)
        val COLOR_MUTED = Color.rgb(170, 183, 194)
        val COLOR_CYAN = Color.rgb(23, 188, 255)
        val COLOR_GREEN = Color.rgb(76, 220, 124)
        val COLOR_GREEN_SOFT = Color.rgb(22, 67, 47)
        val COLOR_WARN = Color.rgb(244, 181, 76)
        val rotationOptions = listOf(
            "portrait" to "竖屏",
            "landscape" to "横屏",
            "inverted_portrait" to "反向竖屏",
            "inverted_landscape" to "反向横屏",
        )
        val speedUnitOptions = listOf("km/h" to "km/h", "mph" to "mph")
        val speedSourceOptions = listOf("gps" to "GPS", "controller" to "控制器")
        val languageOptions = listOf("zh" to "中文", "en" to "English")
    }
}
