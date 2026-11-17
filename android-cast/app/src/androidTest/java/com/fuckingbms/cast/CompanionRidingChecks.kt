package com.fuckingbms.cast

import android.app.Instrumentation
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CancellationException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

/** Opt in with `am instrument -e check riding`; performs one real riding request and one geocode. */
internal object CompanionRidingChecks {
    fun run(test: Instrumentation) {
        val context=test.targetContext
        val done=CountDownLatch(1)
        val response=AtomicReference<Result<Pair<NavPoint,NavRoute>>>()
        var planner: CompanionRiding? = null
        val origin=NavGeo.wgs(NavPoint(39.844818,116.397933))
        try {
            test.runOnMainSync {
                planner=CompanionRiding(context).also { riding ->
                    riding.request(BuildConfig.DEFAULT_AMAP_JS_KEY,BuildConfig.DEFAULT_AMAP_SECURITY_JS_CODE,
                        origin,null,"北京南站") { response.set(it); done.countDown() }
                }
            }
            check(done.await(75,TimeUnit.SECONDS)) { "Real JS API callback missing" }
            val (destination,route)=response.get().getOrThrow()
            check(route.seconds > 0 && route.points.size >= 2 && route.steps.isNotEmpty())
            check(route.steps.any { it.instruction != "沿路线骑行" }) { "Provider riding instructions were lost" }
            check(NavGeo.distance(origin,route.points.first()) < 500) { "GCJ/WGS conversion misplaced the route" }
            check(NavGeo.distance(NavGeo.wgs(destination),route.points.last()) < 1000) { "Geocoded destination mismatch" }
            val nav=LocalNavigator(route)
            val progress=nav.update(route.points.first(),5f,1000)
            check(progress.reliable && !progress.arrived && progress.remaining > 100)
            val output=File(context.filesDir,"companion-checks").apply { mkdirs() }
            val pixel=CompanionMaps.pixel(route.points.first(),16)
            val x=(pixel.first/256).toInt(); val y=(pixel.second/256).toInt()
            val tile=CompanionMaps.fetch(context,"https://t0.tianditu.gov.cn/DataServer?T=vec_w&x=$x&y=$y&l=16&tk=${BuildConfig.DEFAULT_TIANDITU_KEY}",300000)
            val decoded=BitmapFactory.decodeByteArray(tile,0,tile.size) ?: error("Real Tianditu tile was not an image")
            check(decoded.width == 256 && decoded.height == 256); decoded.recycle()
            File(context.cacheDir,"companion-tiles").apply { mkdirs() }.resolve("16-$x-$y.png").writeBytes(tile)
            val frame=CompanionNavigationFrame.render(context,320,480,nav,progress,BuildConfig.DEFAULT_TIANDITU_KEY)
            try { File(output,"navigation-jsapi-riding.png").outputStream().use { frame.compress(Bitmap.CompressFormat.PNG,100,it) } }
            finally { frame.recycle() }
            File(output,"riding-validation.json").writeText(JSONObject().put("provider","AMap.Riding JS API 2.0")
                .put("geocoded",true).put("route_points",route.points.size).put("steps",route.steps.size)
                .put("seconds",route.seconds).put("tile_width",256).put("tile_height",256)
                .put("local_remaining_m",progress.remaining).toString(2))
        } finally { test.runOnMainSync { planner?.close() } }

        val callbacks=AtomicInteger(0)
        var cancellation: Throwable? = null
        test.runOnMainSync {
            val cancelled=CompanionRiding(context)
            cancelled.request(BuildConfig.DEFAULT_AMAP_JS_KEY,BuildConfig.DEFAULT_AMAP_SECURITY_JS_CODE,
                origin,null,"北京南站") { callbacks.incrementAndGet(); cancellation=it.exceptionOrNull() }
            cancelled.cancel(); cancelled.close()
        }
        check(callbacks.get() == 1 && cancellation is CancellationException) { "Cancellation did not finish exactly once" }
    }
}
