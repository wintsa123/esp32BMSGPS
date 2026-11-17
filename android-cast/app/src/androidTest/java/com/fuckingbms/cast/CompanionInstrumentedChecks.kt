package com.fuckingbms.cast

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Bundle
import android.os.SystemClock
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** No provider key or network calls: exercise Android fonts, media sessions, and the actual wire encoder. */
class CompanionInstrumentedChecks : Instrumentation() {
    private var ridingOnly=false
    override fun onCreate(arguments: Bundle?) { super.onCreate(arguments); ridingOnly=arguments?.getString("check") == "riding"; start() }
    override fun onStart() {
        if (ridingOnly) {
            try {
                CompanionRidingChecks.run(this)
                finish(Activity.RESULT_OK,Bundle().apply { putString("stream","Real JS API riding, geocoding, route conversion, Tianditu tile, and cancellation checks passed\n") })
            } catch (error: Throwable) {
                finish(Activity.RESULT_CANCELED,Bundle().apply { putString("stream",error.stackTraceToString()) })
            }
            return
        }
        var session: MediaSession? = null
        try {
            val output=File(targetContext.filesDir,"companion-checks").apply { mkdirs() }
            runOnMainSync {
                session=MediaSession(targetContext,"companion-check").apply {
                    setMetadata(MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE,"测试歌曲 · 向前")
                        .putString(MediaMetadata.METADATA_KEY_ARTIST,"演示歌手")
                        .putString("android.media.metadata.LYRICS","[00:00]一路向前\n[00:01]下一段路，也有新的风景").build())
                    setPlaybackState(PlaybackState.Builder().setState(PlaybackState.STATE_PLAYING,1500,0f).build())
                    isActive=true
                }
            }
            var music=CompanionMusic.State()
            repeat(30) {
                if (music.title.isBlank()) { SystemClock.sleep(100); music=CompanionMusic.refresh(targetContext) }
            }
            check(music.artist == "演示歌手" && music.line == "下一段路，也有新的风景") { "MediaSession / lyrics unavailable; enable notification access for the test app" }
            fun save(name: String,slot: Int,bitmap: Bitmap) {
                File(output,"$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG,100,it) }
                val pixels=IntArray(bitmap.width*bitmap.height)
                bitmap.getPixels(pixels,0,bitmap.width,0,0,bitmap.width,bitmap.height)
                val encoded=CompanionFrame.encode(pixels)
                val header=ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putShort(bitmap.width.toShort())
                    .putShort(bitmap.height.toShort()).put(slot.toByte()).array()
                File(output,"$name.frame").writeBytes(header+encoded)
                check(encoded.size < pixels.size) { "Frame compression ineffective" }
                bitmap.recycle()
            }
            save("music-320x480",0,CompanionFrame.music(320,360,music))
            save("music-480x320",0,CompanionFrame.music(480,200,music))
            val points=listOf(NavPoint(31.2304,121.4737),NavPoint(31.232,121.4737),NavPoint(31.232,121.477))
            val nav=LocalNavigator(NavRoute(points,listOf(NavStep(1,"右转进入测试路","测试路"),NavStep(2,"到达演示终点","测试路")),150,"演示终点"))
            val progress=nav.update(points[0],5f,1000)
            save("navigation-320x480",1,CompanionNavigationFrame.render(targetContext,320,480,nav,progress,""))
            save("navigation-480x320",1,CompanionNavigationFrame.render(targetContext,480,320,nav,progress,""))
            startActivitySync(Intent(targetContext,CompanionActivity::class.java).putExtra("page","music").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            SystemClock.sleep(1000)
            check(CompanionService.isRunning()) { "Background sync did not start" }
            uiAutomation.takeScreenshot()?.let { shot -> File(output,"android-music-demo.png").outputStream().use { shot.compress(Bitmap.CompressFormat.PNG,100,it) }; shot.recycle() }
            targetContext.startActivity(Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            SystemClock.sleep(1200)
            check(CompanionService.isRunning()) { "Background sync stopped on Home" }
            finish(Activity.RESULT_OK,Bundle().apply { putString("stream","MediaSession, timed lyrics, 4 frames, and background-service smoke passed\n") })
        } catch (e: Throwable) {
            finish(Activity.RESULT_CANCELED,Bundle().apply { putString("stream",e.stackTraceToString()) })
        } finally { runOnMainSync { session?.release() } }
    }
}
