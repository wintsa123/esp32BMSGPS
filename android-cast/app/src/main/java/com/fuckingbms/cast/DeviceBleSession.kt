package com.fuckingbms.cast

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

internal enum class DeviceBleState { DISCONNECTED, SCANNING, CONNECTING, CONNECTED, ERROR }

internal fun bleRequestFragments(payload: ByteArray, mtu: Int): List<ByteArray> {
    require(payload.size <= DeviceBleSession.MAX_REQUEST_BYTES) { "BLE 请求过大" }
    val chunkSize = (mtu.coerceAtLeast(23) - 4).coerceAtLeast(1)
    return payload.asList().chunked(chunkSize).mapIndexed { index, values ->
        byteArrayOf(((if (index == 0) 1 else 0) or (if ((index + 1) * chunkSize >= payload.size) 2 else 0)).toByte()) +
            values.toByteArray()
    }
}

internal class BleResponseAssembler {
    private val bytes = ByteArrayOutputStream()

    fun accept(chunk: ByteArray): ByteArray? {
        require(chunk.isNotEmpty()) { "BLE 响应分片为空" }
        val flags = chunk[0].toInt() and 0xff
        require(flags and 0xfc == 0) { "BLE 响应分片标记无效" }
        if (flags and 1 != 0) bytes.reset() else require(bytes.size() > 0) { "BLE 响应缺少起始分片" }
        bytes.write(chunk, 1, chunk.size - 1)
        require(bytes.size() <= DeviceBleSession.MAX_RESPONSE_BYTES) { "BLE 响应过大" }
        return if (flags and 2 != 0) bytes.toByteArray().also { bytes.reset() } else null
    }

    fun reset() = bytes.reset()
}

@SuppressLint("MissingPermission")
internal class DeviceBleSession(
    private val context: Context,
    private val onStateChanged: (DeviceBleState, String?) -> Unit,
) : DeviceTransport {
    @Volatile var state = DeviceBleState.DISCONNECTED
        private set
    val connected: Boolean get() = state == DeviceBleState.CONNECTED
    val connectedAddress: String? get() = address

    private val mainHandler = Handler(Looper.getMainLooper())
    private val requestIds = AtomicInteger(1)
    private val requestLock = Any()
    private val assembler = BleResponseAssembler()
    private var scannerCallbackActive = false
    private var gatt: BluetoothGatt? = null
    private var requestCharacteristic: BluetoothGattCharacteristic? = null
    private var responseCharacteristic: BluetoothGattCharacteristic? = null
    private var mtu = 23
    private var address: String? = null
    @Volatile private var writeLatch: CountDownLatch? = null
    @Volatile private var responseLatch: CountDownLatch? = null
    @Volatile private var pendingResponse: JSONObject? = null
    @Volatile private var pendingFailure: Throwable? = null

    private val scanTimeout = Runnable { fail("未发现支持设备 BLE 服务", closeGatt = false) }
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            address = result.device.address
            updateState(DeviceBleState.CONNECTING, "正在连接设备蓝牙")
            gatt = result.device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        }

        override fun onScanFailed(errorCode: Int) = fail("设备蓝牙扫描失败：$errorCode", closeGatt = false)
    }

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = if (Build.VERSION.SDK_INT >= 33) {
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            } else {
                @Suppress("DEPRECATION") intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            } ?: return
            if (device.address != address) return
            when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                BluetoothDevice.BOND_BONDED -> gatt?.discoverServices()
                BluetoothDevice.BOND_NONE -> fail("设备蓝牙配对失败")
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                this@DeviceBleSession.gatt = gatt
                gatt.requestMtu(MAX_MTU)
                if (gatt.device.bondState == BluetoothDevice.BOND_BONDED) {
                    gatt.discoverServices()
                } else if (!gatt.device.createBond()) {
                    fail("无法建立设备蓝牙配对")
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                clearGatt()
                updateState(DeviceBleState.DISCONNECTED, "设备蓝牙已断开，可点击重试")
                releasePending(IOException("设备蓝牙已断开"))
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                fail("设备蓝牙连接失败：$status")
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) this@DeviceBleSession.mtu = mtu
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service: BluetoothGattService? = gatt.getService(SERVICE_UUID)
            requestCharacteristic = service?.getCharacteristic(REQUEST_UUID)
            responseCharacteristic = service?.getCharacteristic(RESPONSE_UUID)
            val response = responseCharacteristic
            if (status != BluetoothGatt.GATT_SUCCESS || requestCharacteristic == null || response == null) {
                fail("未发现支持设备 BLE 服务")
                return
            }
            if (!gatt.setCharacteristicNotification(response, true)) {
                fail("无法订阅设备 BLE 响应")
                return
            }
            val descriptor = response.getDescriptor(CCCD_UUID) ?: run {
                fail("设备 BLE 响应描述符缺失")
                return
            }
            val started = if (Build.VERSION.SDK_INT >= 33) {
                gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS
            } else {
                @Suppress("DEPRECATION")
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION") gatt.writeDescriptor(descriptor)
            }
            if (!started) fail("无法启用设备 BLE 响应")
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid != CCCD_UUID) return
            if (status == BluetoothGatt.GATT_SUCCESS) updateState(DeviceBleState.CONNECTED, null)
            else fail("启用设备 BLE 响应失败：$status")
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) pendingFailure = IOException("设备 BLE 写入失败：$status")
            writeLatch?.countDown()
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") acceptResponse(characteristic.value ?: ByteArray(0))
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            acceptResponse(value)
        }
    }

    init {
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(bondReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else @Suppress("DEPRECATION") context.registerReceiver(bondReceiver, filter)
    }

    fun start() {
        if (state in setOf(DeviceBleState.SCANNING, DeviceBleState.CONNECTING, DeviceBleState.CONNECTED)) return
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        val scanner = adapter?.bluetoothLeScanner
        if (adapter == null || !adapter.isEnabled || scanner == null) {
            updateState(DeviceBleState.ERROR, "请先开启手机蓝牙")
            return
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        scannerCallbackActive = true
        updateState(DeviceBleState.SCANNING, "正在搜索设备蓝牙")
        mainHandler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS)
    }

    @SuppressLint("MissingPermission")
    fun connect(mac: String) {
        if (state == DeviceBleState.CONNECTING || state == DeviceBleState.CONNECTED) return
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        val device = runCatching { adapter?.getRemoteDevice(mac) }.getOrNull()
        if (device == null) return updateState(DeviceBleState.ERROR, "蓝牙设备地址无效")
        stopScan()
        clearGatt()
        address = mac
        updateState(DeviceBleState.CONNECTING, "正在连接设备蓝牙")
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    override fun request(method: String, path: String, body: JSONObject?): String = synchronized(requestLock) {
        check(connected) { "设备蓝牙未连接" }
        val id = requestIds.getAndIncrement()
        val envelope = JSONObject().put("v", PROTOCOL_VERSION).put("id", id).put("method", method).put("path", path)
            .put("body", body ?: JSONObject.NULL)
        val payload = envelope.toString().toByteArray(Charsets.UTF_8)
        val responseWait = CountDownLatch(1)
        assembler.reset()
        pendingResponse = null
        pendingFailure = null
        responseLatch = responseWait
        try {
            bleRequestFragments(payload, mtu).forEach(::writeFragment)
            if (!responseWait.await(REQUEST_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                fail("设备 BLE 请求超时")
                throw IOException("设备 BLE 请求超时")
            }
            pendingFailure?.let { throw it }
            val response = pendingResponse ?: throw IOException("设备 BLE 响应缺失")
            if (response.optInt("id", -1) != id) throw IOException("设备 BLE 响应编号不匹配")
            val status = response.optInt("status", 500)
            val responseBody = response.opt("body")
            val text = when (responseBody) {
                null, JSONObject.NULL -> ""
                is JSONObject, is JSONArray -> responseBody.toString()
                else -> responseBody.toString()
            }
            if (status !in 200..299) throw IOException("设备返回 BLE $status${text.takeIf { it.isNotBlank() }?.let { ": $it" }.orEmpty()}")
            text
        } catch (error: IllegalArgumentException) {
            fail("设备 BLE 协议错误：${error.message}")
            throw IOException(error.message, error)
        } finally {
            writeLatch = null
            responseLatch = null
        }
    }

    fun close() {
        stopScan()
        releasePending(IOException("设备蓝牙会话已关闭"))
        clearGatt()
        runCatching { context.unregisterReceiver(bondReceiver) }
        updateState(DeviceBleState.DISCONNECTED, null)
    }

    private fun writeFragment(fragment: ByteArray) {
        val currentGatt = gatt ?: throw IOException("设备蓝牙未连接")
        val characteristic = requestCharacteristic ?: throw IOException("设备 BLE 请求特征缺失")
        val latch = CountDownLatch(1)
        writeLatch = latch
        val started = if (Build.VERSION.SDK_INT >= 33) {
            currentGatt.writeCharacteristic(characteristic, fragment, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            characteristic.value = fragment
            @Suppress("DEPRECATION") currentGatt.writeCharacteristic(characteristic)
        }
        if (!started || !latch.await(WRITE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) throw IOException("设备 BLE 分片写入超时")
        pendingFailure?.let { throw it }
    }

    private fun acceptResponse(value: ByteArray) {
        try {
            val complete = assembler.accept(value) ?: return
            pendingResponse = JSONObject(String(complete, Charsets.UTF_8))
            responseLatch?.countDown()
        } catch (error: Exception) {
            fail("设备 BLE 协议错误：${error.message}")
        }
    }

    private fun fail(message: String, closeGatt: Boolean = true) {
        stopScan()
        if (closeGatt) clearGatt()
        releasePending(IOException(message))
        updateState(DeviceBleState.ERROR, message)
    }

    private fun releasePending(error: Throwable) {
        pendingFailure = error
        writeLatch?.countDown()
        responseLatch?.countDown()
    }

    private fun stopScan() {
        mainHandler.removeCallbacks(scanTimeout)
        if (!scannerCallbackActive) return
        val scanner = (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
        runCatching { scanner?.stopScan(scanCallback) }
        scannerCallbackActive = false
    }

    private fun clearGatt() {
        requestCharacteristic = null
        responseCharacteristic = null
        assembler.reset()
        val current = gatt
        gatt = null
        runCatching { current?.disconnect() }
        runCatching { current?.close() }
    }

    private fun updateState(value: DeviceBleState, detail: String?) {
        state = value
        mainHandler.post { onStateChanged(value, detail) }
    }

    companion object {
        internal const val PROTOCOL_VERSION = 1
        internal const val MAX_REQUEST_BYTES = 512
        internal const val MAX_RESPONSE_BYTES = 4096
        private const val MAX_MTU = 517
        private const val SCAN_TIMEOUT_MS = 10_000L
        private const val WRITE_TIMEOUT_SECONDS = 4L
        private const val REQUEST_TIMEOUT_SECONDS = 10L
        val SERVICE_UUID: UUID = UUID.fromString("4f91e100-23b2-4ee7-9f9a-86d1093f0a01")
        val REQUEST_UUID: UUID = UUID.fromString("4f91e100-23b2-4ee7-9f9a-86d1093f0a02")
        val RESPONSE_UUID: UUID = UUID.fromString("4f91e100-23b2-4ee7-9f9a-86d1093f0a03")
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        fun permissions(): Array<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        fun hasPermissions(context: Context) = permissions().all {
            context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
        }
    }
}
