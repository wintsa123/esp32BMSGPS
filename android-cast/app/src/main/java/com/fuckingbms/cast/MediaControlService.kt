@file:Suppress("DEPRECATION")

package com.fuckingbms.cast

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import java.util.UUID

class MediaControlService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private var scanner: BluetoothLeScanner? = null
    private var scanning = false
    private var desired = false
    private var retryCount = 0
    private var negotiatedMtu = 23
    private var pendingDevice: BluetoothDevice? = null
    @Volatile private var activeGatt: BluetoothGatt? = null
    private var commandCharacteristic: BluetoothGattCharacteristic? = null
    private var stateCharacteristic: BluetoothGattCharacteristic? = null
    private var mediaController: MediaController? = null
    private var lastStatePayload: ByteArray? = null

    private val scanTimeout = Runnable {
        if (scanning) {
            stopScan()
            report("未发现媒体设备，请确认设备已开启 PHONE MEDIA")
        }
    }
    private val retryScan = Runnable { startScan() }

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE) ?: return
            if (device.address != pendingDevice?.address) return
            when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                BluetoothDevice.BOND_BONDED -> connect(device)
                BluetoothDevice.BOND_NONE -> report("蓝牙配对未完成")
            }
        }
    }

    private val sessionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == ACTION_MEDIA_SESSIONS_CHANGED) refreshMediaController()
        }
    }

    private val controllerCallback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) = publishState()
        override fun onPlaybackStateChanged(state: android.media.session.PlaybackState?) = publishState()
        override fun onSessionDestroyed() = refreshMediaController()
    }

    override fun onCreate() {
        super.onCreate()
        registerReceiverCompat(bondReceiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
        registerReceiverCompat(sessionReceiver, IntentFilter(ACTION_MEDIA_SESSIONS_CHANGED))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                desired = false
                stopSelf()
            }
            ACTION_START -> {
                startForeground(NOTIFICATION_ID, foregroundNotification())
                desired = true
                retryCount = 0
                refreshMediaController()
                startScan()
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        desired = false
        handler.removeCallbacksAndMessages(null)
        stopScan()
        closeGatt()
        mediaController?.unregisterCallback(controllerCallback)
        mediaController = null
        unregisterReceiver(bondReceiver)
        unregisterReceiver(sessionReceiver)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun registerReceiverCompat(receiver: BroadcastReceiver, filter: IntentFilter) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(receiver, filter)
        }
    }

    private fun bluetoothReady(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
        return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    }

    private fun bluetoothAdapter(): BluetoothAdapter? =
        (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter

    private fun startScan() {
        if (!desired || activeGatt != null || scanning) return
        if (!bluetoothReady()) {
            report("未授予附近设备权限")
            return
        }
        val adapter = bluetoothAdapter()
        if (adapter == null || !adapter.isEnabled) {
            report("请先开启手机蓝牙")
            return
        }
        scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            report("手机不支持 BLE 扫描")
            return
        }
        try {
            scanning = true
            report("正在查找媒体设备")
            scanner?.startScan(
                listOf(ScanFilter.Builder().setServiceUuid(android.os.ParcelUuid(PhoneMediaProtocol.SERVICE_UUID)).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
                scanCallback
            )
            handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS)
        } catch (error: SecurityException) {
            scanning = false
            report("BLE 扫描权限不可用")
        }
    }

    private fun stopScan() {
        handler.removeCallbacks(scanTimeout)
        if (!scanning) return
        scanning = false
        if (!bluetoothReady()) return
        try {
            scanner?.stopScan(scanCallback)
        } catch (_: SecurityException) {
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (!desired || activeGatt != null) return
            stopScan()
            pendingDevice = result.device
            if (result.device.bondState != BluetoothDevice.BOND_BONDED) {
                report("正在请求设备蓝牙配对")
                if (!result.device.createBond()) report("无法发起蓝牙配对")
                return
            }
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            report("BLE 扫描失败（$errorCode）")
        }
    }

    private fun connect(device: BluetoothDevice) {
        if (!desired || !bluetoothReady()) return
        closeGatt()
        negotiatedMtu = 23
        commandCharacteristic = null
        stateCharacteristic = null
        lastStatePayload = null
        report("正在连接 ${device.name ?: device.address}")
        activeGatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        if (activeGatt == null) scheduleRetry("无法建立 BLE 连接")
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (gatt != activeGatt) {
                gatt.close()
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                report("BLE 已连接，正在订阅媒体控制")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP && gatt.requestMtu(128)) return
                discoverServices(gatt)
                return
            }
            closeGatt(gatt)
            scheduleRetry("BLE 连接已断开（$status）")
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (gatt != activeGatt) return
            negotiatedMtu = if (status == BluetoothGatt.GATT_SUCCESS) mtu else 23
            discoverServices(gatt)
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (gatt != activeGatt || status != BluetoothGatt.GATT_SUCCESS) {
                failGatt(gatt, "未找到媒体服务")
                return
            }
            val service = gatt.getService(PhoneMediaProtocol.SERVICE_UUID)
            val command = service?.getCharacteristic(PhoneMediaProtocol.COMMAND_UUID)
            val state = service?.getCharacteristic(PhoneMediaProtocol.STATE_UUID)
            if (command == null || state == null || !gatt.setCharacteristicNotification(command, true)) {
                failGatt(gatt, "媒体服务特征不可用")
                return
            }
            val descriptor = command.getDescriptor(CCCD_UUID)
            if (descriptor == null || !writeDescriptor(gatt, descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)) {
                failGatt(gatt, "无法订阅设备控制")
                return
            }
            commandCharacteristic = command
            stateCharacteristic = state
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (gatt != activeGatt || descriptor.uuid != CCCD_UUID || status != BluetoothGatt.GATT_SUCCESS) {
                failGatt(gatt, "媒体控制订阅失败")
                return
            }
            retryCount = 0
            report(mediaConnectionStatusForNotificationAccess(notificationAccessGranted()))
            refreshMediaController()
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            handleCommand(characteristic.value ?: ByteArray(0))
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt,
                                             characteristic: BluetoothGattCharacteristic,
                                             value: ByteArray) {
            handleCommand(value)
        }
    }

    private fun discoverServices(gatt: BluetoothGatt) {
        if (!gatt.discoverServices()) failGatt(gatt, "无法发现媒体服务")
    }

    private fun failGatt(gatt: BluetoothGatt, message: String) {
        if (gatt != activeGatt) return
        closeGatt(gatt)
        scheduleRetry(message)
    }

    private fun closeGatt(gatt: BluetoothGatt? = activeGatt) {
        if (gatt == null) return
        if (gatt == activeGatt) activeGatt = null
        commandCharacteristic = null
        stateCharacteristic = null
        lastStatePayload = null
        try {
            gatt.disconnect()
            gatt.close()
        } catch (_: SecurityException) {
        }
    }

    private fun scheduleRetry(message: String) {
        if (!desired) return
        if (retryCount >= MAX_RETRIES) {
            report("$message；请点重试")
            return
        }
        retryCount++
        report("$message；${RETRY_DELAY_MS / 1000} 秒后重试（$retryCount/$MAX_RETRIES）")
        handler.removeCallbacks(retryScan)
        handler.postDelayed(retryScan, RETRY_DELAY_MS)
    }

    private fun refreshMediaController() {
        val next = activeSessions().firstOrNull()
        if (next?.sessionToken != mediaController?.sessionToken) {
            mediaController?.unregisterCallback(controllerCallback)
            mediaController = next
            mediaController?.registerCallback(controllerCallback)
        }
        publishState()
    }

    private fun activeSessions(): List<MediaController> {
        if (!notificationAccessGranted()) return emptyList()
        return try {
            val manager = getSystemService(MEDIA_SESSION_SERVICE) as MediaSessionManager
            val sessions = manager.getActiveSessions(ComponentName(this, MediaNotificationListenerService::class.java))
            sessions.sortedByDescending {
                when (it.playbackState?.state) {
                    android.media.session.PlaybackState.STATE_PLAYING,
                    android.media.session.PlaybackState.STATE_BUFFERING -> 1
                    else -> 0
                }
            }
        } catch (_: SecurityException) {
            emptyList()
        }
    }

    private fun notificationAccessGranted(): Boolean {
        val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        return manager.isNotificationListenerAccessGranted(
            ComponentName(this, MediaNotificationListenerService::class.java)
        )
    }

    private fun publishState() {
        val controller = mediaController
        val playbackState = controller?.playbackState?.state
        val title = controller?.metadata?.getString(MediaMetadata.METADATA_KEY_TITLE)
            ?.takeIf { it.isNotBlank() }
            ?: controller?.packageName.orEmpty()
        val payload = PhoneMediaProtocol.statePayload(
            notificationAccessGranted(),
            controller != null,
            playbackState == android.media.session.PlaybackState.STATE_PLAYING,
            title,
            negotiatedMtu - 3
        )
        val gatt = activeGatt ?: return
        val characteristic = stateCharacteristic ?: return
        if (lastStatePayload?.contentEquals(payload) == true) return
        if (writeCharacteristic(gatt, characteristic, payload)) lastStatePayload = payload
    }

    private fun handleCommand(value: ByteArray) {
        if (value.size != 1) return
        refreshMediaController()
        when (value[0]) {
            PhoneMediaProtocol.COMMAND_PREVIOUS -> {
                mediaController?.transportControls?.skipToPrevious()
                report("已请求上一曲")
            }
            PhoneMediaProtocol.COMMAND_NEXT -> {
                mediaController?.transportControls?.skipToNext()
                report("已请求下一曲")
            }
            PhoneMediaProtocol.COMMAND_VOLUME_DOWN -> {
                (getSystemService(AUDIO_SERVICE) as AudioManager).adjustStreamVolume(
                    AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, 0
                )
                report("已降低手机媒体音量")
            }
            PhoneMediaProtocol.COMMAND_VOLUME_UP -> {
                (getSystemService(AUDIO_SERVICE) as AudioManager).adjustStreamVolume(
                    AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, 0
                )
                report("已提高手机媒体音量")
            }
            else -> return
        }
        publishState()
    }

    private fun writeDescriptor(gatt: BluetoothGatt,
                                descriptor: BluetoothGattDescriptor,
                                value: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, value) == BluetoothStatusCodes.SUCCESS
        } else {
            descriptor.value = value
            gatt.writeDescriptor(descriptor)
        }

    private fun writeCharacteristic(gatt: BluetoothGatt,
                                    characteristic: BluetoothGattCharacteristic,
                                    value: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(characteristic, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) ==
                    BluetoothStatusCodes.SUCCESS
        } else {
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            characteristic.value = value
            gatt.writeCharacteristic(characteristic)
        }

    private fun report(message: String) {
        sendBroadcast(Intent(ACTION_STATUS).setPackage(packageName).putExtra(EXTRA_STATUS, message))
    }

    private fun foregroundNotification(): Notification {
        val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "BMS 手机媒体", NotificationManager.IMPORTANCE_LOW)
        )
        val intent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("BMS 手机媒体控制")
            .setContentText("正在等待设备连接")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(intent)
            .setOngoing(true)
            .build()
    }

    companion object {
        const val ACTION_START = "com.fuckingbms.cast.PHONE_MEDIA_START"
        const val ACTION_STOP = "com.fuckingbms.cast.PHONE_MEDIA_STOP"
        const val ACTION_STATUS = "com.fuckingbms.cast.PHONE_MEDIA_STATUS"
        const val ACTION_MEDIA_SESSIONS_CHANGED = "com.fuckingbms.cast.MEDIA_SESSIONS_CHANGED"
        const val EXTRA_STATUS = "status"
        private const val CHANNEL_ID = "phone-media"
        private const val NOTIFICATION_ID = 2
        private const val SCAN_TIMEOUT_MS = 10_000L
        private const val RETRY_DELAY_MS = 3_000L
        private const val MAX_RETRIES = 3
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        internal fun mediaConnectionStatusForNotificationAccess(granted: Boolean): String =
            if (granted) "手机媒体已连接" else "BLE 已连接；请先授予通知访问"

        fun startIntent(context: Context) = Intent(context, MediaControlService::class.java).setAction(ACTION_START)
        fun stopIntent(context: Context) = Intent(context, MediaControlService::class.java).setAction(ACTION_STOP)
    }
}
