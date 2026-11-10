package com.fuckingbms.cast

import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.net.URI
import java.net.HttpURLConnection
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.interfaces.RSAPublicKey
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import java.util.UUID

/** Signed, complete immutable releases. Only the small active/pending pointers are replaced. */
internal class WebResourceUpdate(
    private val home: File,
    private val config: JSONObject,
    private val appVersionCode: Long,
    private val fetch: (String, Int) -> ByteArray = ::httpsBytes,
) {
    data class Release(val version: String, val sequence: Long, val directory: File, val paths: Set<String>)
    private val lock = Any()
    private var busy = false
    private var active: Release? = null
    private var pending: Release? = null
    private var previous: Release? = null
    private val leases = mutableMapOf<File, Int>()
    private var status = "unconfigured"
    private var error: String? = null
    private val bundledVersion = config.optString("bundledVersion", "内置资源")
    private val bundledSequence = config.optLong("bundledSequence", 0).coerceAtLeast(0)
    private val manifestUrl = config.optString("manifestUrl")
    private val publicKey = config.optString("publicKey")
    private val configured = manifestUrl.isNotBlank() && publicKey.isNotBlank()

    init {
        home.mkdirs()
        synchronized(lock) {
            active = readPointer("active")?.takeIf { it.sequence > bundledSequence }
            pending = readPointer("pending")?.takeIf { it.sequence > (active?.sequence ?: bundledSequence) }
            status = if (pending != null) "ready" else if (configured) "current" else "unconfigured"
            collect(removeStaging = true)
        }
    }

    fun state(): String = synchronized(lock) {
        JSONObject().put("currentVersion", active?.version ?: bundledVersion)
            .put("pendingVersion", pending?.version ?: JSONObject.NULL).put("status", status)
            .apply { error?.let { put("error", it) } }.toString()
    }

    /** Called only at Activity creation or explicit restart; downloads never call this. */
    fun activate(): Release? = synchronized(lock) {
        val next = pending ?: return@synchronized active
        try {
            val verified = readRelease(next.directory.name)
            atomicPointer("active", next.directory.name)
            previous = active
            active = verified
            File(home, "pending.json").delete()
            pending = null
            error = null
            status = if (configured) "current" else "unconfigured"
        } catch (reason: Exception) {
            status = "error"; error = reason.message ?: "资源激活失败"
        }
        active
    }

    /** A lease protects a running Activity's immutable resources across later activations. */
    fun acquire(): Release? = synchronized(lock) {
        activate()?.also { leases[it.directory] = (leases[it.directory] ?: 0) + 1 }
            .also { collect() }
    }

    fun release(snapshot: Release?) = synchronized(lock) {
        snapshot?.directory?.let { directory ->
            val count = (leases[directory] ?: 0) - 1
            if (count <= 0) leases.remove(directory) else leases[directory] = count
        }
        collect()
    }

    private fun collect(removeStaging: Boolean = false) {
        // A worker may have renamed staging but not published pending yet.
        if (busy) return
        val kept = setOfNotNull(active?.directory, pending?.directory, previous?.directory) + leases.keys
        home.listFiles()?.filter { (removeStaging && it.name.startsWith("stage-")) || (it.name.startsWith("release-") && it !in kept) }
            ?.forEach { it.deleteRecursively() }
    }

    /** Blocking worker API; caller schedules off the WebView/UI thread. */
    fun check() {
        synchronized(lock) {
            if (busy) return
            if (!configured) { status = "unconfigured"; return }
            busy = true; status = "checking"; error = null
        }
        var staging: File? = null
        try {
            val envelope = fetch(manifestUrl, MAX_MANIFEST)
            val manifest = verify(envelope)
            val sequence = integer(manifest, "sequence", 1)
            synchronized(lock) {
                if (sequence <= maxOf(bundledSequence, active?.sequence ?: 0, pending?.sequence ?: 0)) {
                    status = if (pending != null) "ready" else "current"
                    return
                }
                status = "downloading"
            }
            val stage = File(home, "stage-${UUID.randomUUID()}"); staging = stage
            check(stage.mkdirs())
            val files = manifest.getJSONArray("files")
            val assets = File(stage, "assets"); check(assets.mkdir())
            for (i in 0 until files.length()) {
                val item = files.getJSONObject(i)
                val bytes = fetch(item.getString("url"), integer(item, "size", 1).toInt())
                require(bytes.size.toLong() == integer(item, "size", 1)) { "资源长度不符" }
                require(sha256(bytes) == item.getString("sha256")) { "资源哈希不符" }
                val destination = File(assets, item.getString("path"))
                destination.parentFile!!.mkdirs()
                destination.outputStream().use { it.write(bytes); it.fd.sync() }
            }
            File(stage, "manifest.json").outputStream().use { it.write(envelope); it.fd.sync() }
            val releaseName = "release-$sequence-${UUID.randomUUID()}"
            val destination = File(home, releaseName)
            Files.move(stage.toPath(), destination.toPath(), StandardCopyOption.ATOMIC_MOVE)
            staging = null
            val release = readRelease(releaseName)
            synchronized(lock) {
                atomicPointer("pending", releaseName)
                pending = release; status = "ready"; error = null
                collect()
            }
        } catch (reason: Exception) {
            synchronized(lock) { status = "error"; error = reason.message ?: "资源更新失败" }
        } finally {
            staging?.deleteRecursively()
            synchronized(lock) { busy = false; collect() }
        }
    }

    private fun readPointer(name: String): Release? = runCatching {
        val pointer = File(home, "$name.json")
        require(pointer.length() in 1..1024)
        readRelease(JSONObject(pointer.readText()).getString("release"))
    }.getOrNull()

    private fun readRelease(name: String): Release {
        require(Regex("release-[0-9]+-[a-f0-9-]+").matches(name))
        val directory = File(home, name)
        val envelope = File(directory, "manifest.json")
        require(envelope.length() in 1..MAX_MANIFEST.toLong())
        val manifest = verify(envelope.readBytes())
        val files = manifest.getJSONArray("files")
        val paths = mutableSetOf<String>()
        for (i in 0 until files.length()) {
            val item = files.getJSONObject(i); val path = item.getString("path")
            val file = File(directory, "assets/$path")
            require(file.isFile && file.length() == integer(item, "size", 1)) { "本地资源不完整" }
            require(sha256(file.readBytes()) == item.getString("sha256")) { "本地资源校验失败" }
            paths.add(path)
        }
        return Release(manifest.getString("version"), integer(manifest, "sequence", 1), directory, paths)
    }

    private fun atomicPointer(name: String, release: String) {
        val temp = File(home, "$name.tmp")
        temp.outputStream().use { it.write(JSONObject().put("release", release).toString().toByteArray()); it.fd.sync() }
        Files.move(temp.toPath(), File(home, "$name.json").toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    }

    private fun verify(envelope: ByteArray): JSONObject {
        require(envelope.size in 1..MAX_MANIFEST) { "清单大小无效" }
        val signed = JSONObject(envelope.toString(Charsets.UTF_8))
        val payload = signed.getString("payload").toByteArray(Charsets.UTF_8)
        val key = KeyFactory.getInstance("RSA").generatePublic(X509EncodedKeySpec(Base64.getDecoder().decode(publicKey))) as RSAPublicKey
        require(key.modulus.bitLength() >= 2048) { "更新密钥强度不足" }
        require(Signature.getInstance("SHA256withRSA").run { initVerify(key); update(payload); verify(Base64.getDecoder().decode(signed.getString("signature"))) }) { "更新清单签名无效" }
        val manifest = JSONObject(payload.toString(Charsets.UTF_8))
        require(integer(manifest, "schema", 1) == 1L)
        require(manifest.getString("version").let { it.isNotBlank() && it.length <= 100 })
        integer(manifest, "sequence", 1)
        require(integer(manifest, "minAppVersionCode", 1) <= appVersionCode) { "请先更新 App" }
        trustedUrl(manifestUrl, manifestUrl)
        val files = manifest.getJSONArray("files")
        require(files.length() in 1..512)
        val paths = mutableSetOf<String>(); var total = 0L
        for (i in 0 until files.length()) {
            val item = files.getJSONObject(i); val path = item.getString("path")
            require(safePath(path) && paths.add(path)) { "资源路径无效或重复" }
            require(paths.none { it != path && (it.startsWith("$path/") || path.startsWith("$it/")) }) { "资源路径冲突" }
            val size = integer(item, "size", 1); require(size <= MAX_FILE)
            total += size; require(total <= MAX_TOTAL)
            require(Regex("[a-f0-9]{64}").matches(item.getString("sha256")))
            trustedUrl(item.getString("url"), manifestUrl)
        }
        require("index.html" in paths) { "缺少首页资源" }
        return manifest
    }

    companion object {
        const val MAX_MANIFEST = 256 * 1024
        const val MAX_FILE = 8 * 1024 * 1024
        const val MAX_TOTAL = 32 * 1024 * 1024
        fun safePath(path: String): Boolean = path.length in 1..240 && Regex("[A-Za-z0-9_./-]+").matches(path) &&
            path.split('/').all { it.isNotEmpty() && !it.startsWith('.') && it != "dashboard-update.json" }
        private fun integer(value: JSONObject, key: String, min: Long): Long {
            val raw = value.get(key)
            require(raw is Number && Regex("[0-9]+").matches(raw.toString())) { "整数无效: $key" }
            return raw.toString().toLong().also { require(it in min..9007199254740991L) }
        }
        private fun sha256(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
        private fun trustedUrl(value: String, origin: String) {
            val uri = URI(value); val base = URI(origin)
            require(uri.scheme == "https" && uri.host != null && uri.rawUserInfo == null && uri.rawFragment == null &&
                uri.host.equals(base.host, true) && uri.port == base.port) { "资源必须使用同源 HTTPS" }
        }
        private fun httpsBytes(url: String, limit: Int): ByteArray {
            require(URI(url).scheme == "https")
            val connection = URI(url).toURL().openConnection() as HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 15000; connection.readTimeout = 20000
            try {
                require(connection.responseCode == 200) { "资源服务 HTTP ${connection.responseCode}" }
                require(connection.contentLengthLong <= limit) { "资源超限" }
                return connection.inputStream.use { bounded(it, limit) }
            } finally { connection.disconnect() }
        }
        private fun bounded(input: InputStream, limit: Int): ByteArray {
            val output = java.io.ByteArrayOutputStream(); val buffer = ByteArray(8192)
            while (true) {
                val read = input.read(buffer); if (read < 0) break
                require(output.size() + read <= limit) { "资源超限" }
                output.write(buffer, 0, read)
            }
            return output.toByteArray()
        }
    }
}
