package com.fuckingbms.cast

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.After
import java.nio.file.Files
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.Signature
import java.util.Base64

class WebResourceUpdateTest {
    private val keys = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
    private val home = Files.createTempDirectory("dashboard-update-test").toFile().apply { deleteOnExit() }
    private val bodies = mutableMapOf("index.html" to "<script src='assets/app.js'></script>".toByteArray(), "assets/app.js" to "window.release=2".toByteArray())
    private var signed: ByteArray = byteArrayOf()
    private val config get() = JSONObject().put("manifestUrl", "https://updates.example/manifest.json")
        .put("publicKey", Base64.getEncoder().encodeToString(keys.public.encoded))
        .put("bundledVersion", "built-in-1").put("bundledSequence", 1)
    private fun manifest(sequence: Long = 2) = JSONObject().put("schema", 1).put("version", "web-$sequence")
        .put("sequence", sequence).put("minAppVersionCode", 3).put("files", JSONArray().apply {
            bodies.forEach { (path, bytes) -> put(JSONObject().put("path", path).put("url", "https://updates.example/$path")
                .put("size", bytes.size).put("sha256", MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) })) }
        })
    private fun sign(payload: JSONObject): ByteArray {
        val raw = payload.toString()
        val signature = Signature.getInstance("SHA256withRSA").run { initSign(keys.private); update(raw.toByteArray()); sign() }
        return JSONObject().put("payload", raw).put("signature", Base64.getEncoder().encodeToString(signature)).toString().toByteArray()
    }
    private fun updater(settings: JSONObject = config, fail: String? = null) = WebResourceUpdate(home, settings, 3) { url, limit ->
        if (url.contains(fail ?: "unmatchable")) throw IllegalStateException("offline")
        val data = if (url.endsWith("manifest.json")) signed else bodies.getValue(url.removePrefix("https://updates.example/"))
        require(data.size <= limit)
        data
    }
    private fun state(updater: WebResourceUpdate) = JSONObject(updater.state())
    @After fun cleanup() { home.deleteRecursively() }

    @Test fun downloadIsPendingUntilExplicitOrNextOpenActivationAndWorksOffline() {
        signed = sign(manifest()); val update = updater(); assertNull(update.activate())
        update.check(); assertEquals("ready", state(update).getString("status"))
        assertEquals("built-in-1", state(update).getString("currentVersion")); assertEquals("web-2", state(update).getString("pendingVersion"))
        assertFalse(home.resolve("active.json").exists())
        val nextOpen = updater(fail = "https")
        val release = nextOpen.activate()!!; assertEquals("web-2", release.version)
        assertEquals(bodies.keys, release.paths); assertEquals("window.release=2", release.directory.resolve("assets/assets/app.js").readText())
        nextOpen.check(); assertEquals("error", state(nextOpen).getString("status")); assertEquals("web-2", state(nextOpen).getString("currentVersion"))
        assertEquals("web-2", updater(fail = "https").activate()!!.version)
    }

    @Test fun failuresNeverReplaceActiveOrGoodPendingRelease() {
        signed = sign(manifest()); val update = updater(); update.check(); val release = update.activate()!!
        val oldPointer = home.resolve("active.json").readText()
        val payload = manifest(3); signed = sign(payload)
        bodies["assets/app.js"] = "tampered".toByteArray(); update.check()
        assertEquals("error", state(update).getString("status")); assertEquals(oldPointer, home.resolve("active.json").readText())
        assertEquals(release.directory, update.activate()!!.directory)
        assertTrue(home.listFiles()!!.none { it.name.startsWith("stage-") })
    }

    @Test fun invalidSignatureCompatibilityOriginsPathsAndSizesAreRejected() {
        val variants = listOf<(JSONObject) -> Unit>(
            { it.put("minAppVersionCode", 4) },
            { it.put("sequence", 2.5) },
            { it.getJSONArray("files").getJSONObject(0).put("url", "http://updates.example/index.html") },
            { it.getJSONArray("files").getJSONObject(0).put("url", "https://evil.example/index.html") },
            { it.getJSONArray("files").getJSONObject(0).put("url", "https://user@updates.example/index.html") },
            { it.getJSONArray("files").getJSONObject(0).put("url", "https://updates.example/index.html#fragment") },
            { it.getJSONArray("files").getJSONObject(0).put("path", "../index.html") },
            { it.getJSONArray("files").getJSONObject(0).put("path", "foo/dashboard-update.json") },
            { it.getJSONArray("files").getJSONObject(0).put("size", WebResourceUpdate.MAX_FILE + 1) },
            { it.getJSONArray("files").getJSONObject(0).put("size", 0) },
            { it.getJSONArray("files").getJSONObject(0).put("path", "other.html") },
            { it.getJSONArray("files").getJSONObject(1).put("path", "index.html/app.js") },
            { it.getJSONArray("files").put(it.getJSONArray("files").getJSONObject(0)) },
            { it.getJSONArray("files").getJSONObject(0).put("sha256", "bad") },
        )
        for (mutate in variants) {
            val value = manifest(); mutate(value); signed = sign(value)
            val update = updater(); update.check(); assertEquals(value.toString(), "error", state(update).getString("status"))
            assertNull(update.activate()); assertFalse(home.resolve("active.json").exists())
        }
        val envelope = JSONObject(sign(manifest()).toString(Charsets.UTF_8)).put("payload", manifest(9).toString())
        signed = envelope.toString().toByteArray(); val update = updater(); update.check()
        assertEquals("error", state(update).getString("status")); assertNull(update.activate())
    }

    @Test fun sequenceRollbackIgnoredAndFailedNewDownloadPreservesPending() {
        signed = sign(manifest(3)); val update = updater(); update.check()
        val pendingPointer = home.resolve("pending.json").readText()
        signed = sign(manifest(2)); update.check(); assertEquals("ready", state(update).getString("status"))
        assertEquals("web-3", state(update).getString("pendingVersion"))
        signed = sign(manifest(4)); bodies["assets/app.js"] = "wrong-hash".toByteArray(); update.check()
        assertEquals("error", state(update).getString("status")); assertEquals(pendingPointer, home.resolve("pending.json").readText())
        assertEquals("web-3", update.activate()!!.version)
    }

    @Test fun corruptedPendingCannotActivateAndBundledUpgradeSupersedesOlderRelease() {
        signed = sign(manifest()); val update = updater(); update.check()
        val directory = home.listFiles()!!.first { it.name.startsWith("release-") }
        directory.resolve("assets/assets/app.js").writeText("corruption")
        assertNull(update.activate()); assertEquals("error", state(update).getString("status"))
        assertNull(updater().activate())
        signed = sign(manifest(5)); val second = updater(); second.check(); second.activate()
        val upgraded = updater(config.put("bundledSequence", 10).put("bundledVersion", "bundled-new"))
        assertNull(upgraded.activate()); assertEquals("bundled-new", state(upgraded).getString("currentVersion"))
    }

    @Test fun absentConfigurationIsExplicitAndNeverContactsNetwork() {
        val update = WebResourceUpdate(home, JSONObject().put("bundledVersion", "bundled"), 3) { _, _ -> fail("must not fetch"); byteArrayOf() }
        update.check(); assertEquals("unconfigured", state(update).getString("status")); assertEquals("bundled", state(update).getString("currentVersion"))
    }

    @Test fun untrustedAndAmbiguousAssetPathsNeverResolve() {
        for (path in listOf("../a", "/index.html", "a//b", "a/./b", "a/%2e%2e/b", "a\\b", "a?b", "a/.secret", "dashboard-update.json")) assertFalse(path, WebResourceUpdate.safePath(path))
        for (path in listOf("index.html", "assets/app-a1.js", "art/presets/city.svg")) assertTrue(WebResourceUpdate.safePath(path))
    }

    @Test fun garbageCollectionRetainsAllLiveActivitiesAndOnlyRecentUnleasedReleases() {
        val update = updater()
        signed = sign(manifest(2)); update.check(); val first = update.acquire()!!
        val otherActivity = update.acquire()!!
        signed = sign(manifest(3)); update.check(); val third = update.acquire()!!
        signed = sign(manifest(4)); update.check(); update.acquire()
        assertTrue(first.directory.exists())
        update.release(first); assertTrue(otherActivity.directory.exists())
        update.release(otherActivity); assertFalse(first.directory.exists())
        assertTrue(third.directory.exists())
        signed = sign(manifest(5)); update.check(); update.acquire()
        update.release(third); assertFalse(third.directory.exists())
        assertEquals(2, home.listFiles()!!.count { it.name.startsWith("release-") })
    }
}
