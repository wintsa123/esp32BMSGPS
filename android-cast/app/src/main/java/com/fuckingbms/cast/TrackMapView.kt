package com.fuckingbms.cast

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.View

internal class TrackMapView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private val backgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xff101820.toInt() }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xff263642.toInt()
        strokeWidth = 1f
    }
    private val routePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xff17bcff.toInt()
        strokeWidth = 6f * resources.displayMetrics.density
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val markerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xff4cdc7c.toInt() }
    private val endPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xffffb44c.toInt() }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xffaab7c2.toInt()
        textSize = 13f * resources.displayMetrics.scaledDensity
        typeface = Typeface.DEFAULT_BOLD
    }
    private var points: List<GpsPoint> = emptyList()

    fun setPoints(value: List<GpsPoint>) {
        points = value
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backgroundPaint)
        val gridStep = 48f * resources.displayMetrics.density
        var x = 0f
        while (x <= width) {
            canvas.drawLine(x, 0f, x, height.toFloat(), gridPaint)
            x += gridStep
        }
        var y = 0f
        while (y <= height) {
            canvas.drawLine(0f, y, width.toFloat(), y, gridPaint)
            y += gridStep
        }
        if (points.isEmpty()) {
            canvas.drawText("暂无 GPS 轨迹", 20f, height / 2f, textPaint)
            return
        }

        val minLatitude = points.minOf { it.latitude }
        val maxLatitude = points.maxOf { it.latitude }
        val minLongitude = points.minOf { it.longitude }
        val maxLongitude = points.maxOf { it.longitude }
        val latitudeSpan = (maxLatitude - minLatitude).coerceAtLeast(0.00001)
        val longitudeSpan = (maxLongitude - minLongitude).coerceAtLeast(0.00001)
        val mapPadding = 28f * resources.displayMetrics.density
        val mapWidth = (width - mapPadding * 2f).coerceAtLeast(1f)
        val mapHeight = (height - mapPadding * 2f).coerceAtLeast(1f)
        val scale = minOf(mapWidth / longitudeSpan.toFloat(), mapHeight / latitudeSpan.toFloat())
        val centerLongitude = (minLongitude + maxLongitude) / 2.0
        val centerLatitude = (minLatitude + maxLatitude) / 2.0
        val path = Path()
        points.forEachIndexed { index, point ->
            val px = width / 2f + ((point.longitude - centerLongitude) * scale).toFloat()
            val py = height / 2f - ((point.latitude - centerLatitude) * scale).toFloat()
            if (index == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        canvas.drawPath(path, routePaint)
        drawMarker(canvas, points.first(), centerLatitude, centerLongitude, scale, markerPaint, "起点")
        if (points.size > 1) {
            drawMarker(canvas, points.last(), centerLatitude, centerLongitude, scale, endPaint, "终点")
        }
    }

    private fun drawMarker(
        canvas: Canvas,
        point: GpsPoint,
        centerLatitude: Double,
        centerLongitude: Double,
        scale: Float,
        paint: Paint,
        label: String,
    ) {
        val px = width / 2f + ((point.longitude - centerLongitude) * scale).toFloat()
        val py = height / 2f - ((point.latitude - centerLatitude) * scale).toFloat()
        canvas.drawCircle(px, py, 9f * resources.displayMetrics.density, paint)
        canvas.drawText(label, px + 12f, py + 5f, textPaint)
    }
}
