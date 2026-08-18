package com.fuckingbms.cast

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.View
import kotlin.math.max
import kotlin.math.min

internal class HistoryChartView(context: Context) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = 3f; style = Paint.Style.STROKE }
    private var samples: List<HistorySample> = emptyList()

    fun setSamples(value: List<HistorySample>) {
        samples = value
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (samples.size < 2) return
        drawSeries(canvas, Color.rgb(30, 130, 90), samples.map { it.packVoltageMv.toFloat() })
        drawSeries(canvas, Color.rgb(210, 75, 65), samples.map { it.currentDeciAmps.toFloat() })
        drawSeries(canvas, Color.rgb(45, 105, 190), samples.map { it.socPercent.toFloat() })
    }

    private fun drawSeries(canvas: Canvas, color: Int, values: List<Float>) {
        val low = values.minOrNull() ?: return
        val high = values.maxOrNull() ?: return
        val range = max(1f, high - low)
        val path = Path()
        values.forEachIndexed { index, value ->
            val x = index.toFloat() * width / max(1, values.lastIndex)
            val y = height - ((value - low) / range * height)
            if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        paint.color = color
        canvas.drawPath(path, paint)
    }
}
