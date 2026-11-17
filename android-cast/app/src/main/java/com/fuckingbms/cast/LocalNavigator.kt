package com.fuckingbms.cast

import kotlin.math.*

internal data class NavPoint(val lat: Double, val lon: Double)
internal data class NavStep(val end: Int, val instruction: String, val road: String)
internal data class NavRoute(val points: List<NavPoint>, val steps: List<NavStep>, val seconds: Int, val destination: String)
internal data class NavProgress(val position: NavPoint, val segment: Int, val remaining: Double, val turnDistance: Double,
    val instruction: String, val road: String, val offRoute: Boolean, val arrived: Boolean, val reliable: Boolean)

internal object NavGeo {
    private const val R = 6371000.0
    fun distance(a: NavPoint, b: NavPoint): Double {
        val lat = Math.toRadians((a.lat + b.lat) / 2)
        return hypot(Math.toRadians(a.lat - b.lat), Math.toRadians(a.lon - b.lon) * cos(lat)) * R
    }
    fun gcj(w: NavPoint): NavPoint {
        if (w.lon !in 72.004..137.8347 || w.lat !in .8293..55.8271) return w
        val x = w.lon - 105; val y = w.lat - 35
        var a = -100 + 2*x + 3*y + .2*y*y + .1*x*y + .2*sqrt(abs(x))
        a += (20*sin(6*x*PI) + 20*sin(2*x*PI))*2/3
        a += (20*sin(y*PI) + 40*sin(y*PI/3))*2/3
        a += (160*sin(y*PI/12) + 320*sin(y*PI/30))*2/3
        var b = 300 + x + 2*y + .1*x*x + .1*x*y + .1*sqrt(abs(x))
        b += (20*sin(6*x*PI) + 20*sin(2*x*PI))*2/3
        b += (20*sin(x*PI) + 40*sin(x*PI/3))*2/3
        b += (150*sin(x*PI/12) + 300*sin(x*PI/30))*2/3
        val rad = Math.toRadians(w.lat)
        val magic = 1 - .00669342162296594323 * sin(rad).pow(2)
        a = a * 180 / ((6378245.0 * (1 - .00669342162296594323)) / (magic * sqrt(magic)) * PI)
        b = b * 180 / (6378245.0 / sqrt(magic) * cos(rad) * PI)
        return NavPoint(w.lat + a, w.lon + b)
    }
    fun wgs(g: NavPoint): NavPoint {
        var w = g
        repeat(5) { val shifted = gcj(w); w = NavPoint(w.lat + g.lat - shifted.lat, w.lon + g.lon - shifted.lon) }
        return w
    }
}

/** GPS route progress is computed locally; this class never makes network requests. */
internal class LocalNavigator(val route: NavRoute) {
    private val cumulative = DoubleArray(route.points.size)
    private var progress = 0.0
    private var acquired = false
    private var offSince: Long? = null
    init {
        require(route.points.size >= 2)
        for (i in 1 until cumulative.size) cumulative[i] = cumulative[i-1] + NavGeo.distance(route.points[i-1], route.points[i])
    }

    fun update(position: NavPoint, accuracy: Float, now: Long): NavProgress {
        val reliable = accuracy.isFinite() && accuracy in 0f..40f
        var best = Double.MAX_VALUE
        var along = progress
        var segment = 0
        for (i in 0 until route.points.lastIndex) {
            if (acquired && (cumulative[i+1] < progress - 120 || cumulative[i] > progress + 1200)) continue
            val a = route.points[i]; val b = route.points[i+1]
            val cosLat = cos(Math.toRadians(position.lat))
            val ax = (a.lon - position.lon) * cosLat; val ay = a.lat - position.lat
            val dx = (b.lon - a.lon) * cosLat; val dy = b.lat - a.lat
            val denom = dx*dx + dy*dy
            val t = if (denom < 1e-20) 0.0 else (-(ax*dx + ay*dy)/denom).coerceIn(0.0, 1.0)
            val projected = NavPoint(a.lat + (b.lat-a.lat)*t, a.lon + (b.lon-a.lon)*t)
            val error = NavGeo.distance(position, projected)
            if (error < best) { best = error; along = cumulative[i] + (cumulative[i+1]-cumulative[i])*t; segment = i }
        }
        val outside = reliable && best > max(45.0, accuracy * 2.0)
        if (outside) { if (offSince == null) offSince = now } else offSince = null
        if (reliable && !outside) { progress = if (acquired) max(progress, along) else along; acquired = true }
        val step = route.steps.firstOrNull { cumulative[it.end.coerceIn(0, cumulative.lastIndex)] > progress + 8 } ?: route.steps.lastOrNull()
        val turn = step?.let { (cumulative[it.end.coerceIn(0, cumulative.lastIndex)] - progress).coerceAtLeast(0.0) } ?: 0.0
        val remaining = (cumulative.last() - progress).coerceAtLeast(0.0)
        val arrived = reliable && remaining < 35 && NavGeo.distance(position, route.points.last()) < 45
        return NavProgress(position, segment, remaining, turn, step?.instruction ?: "沿路线行驶", step?.road.orEmpty(),
            offSince?.let { now - it >= 12000 } == true, arrived, reliable)
    }
}
