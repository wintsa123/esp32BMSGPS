package com.fuckingbms.cast

import android.content.Context
import android.graphics.*
import android.text.TextPaint
import android.text.TextUtils
import kotlin.math.floor

internal object CompanionNavigationFrame {
    fun render(context: Context, width: Int, height: Int, navigator: LocalNavigator, progress: NavProgress, tileKey: String): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val colors = CompanionFrame.palette
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        canvas.drawColor(colors[0])
        val top = if (height > width) 100 else 78
        val bottom = 56
        val mapHeight = height - top - bottom
        val zoom = 16
        val center = CompanionMaps.pixel(progress.position, zoom)
        val originX = center.first - width / 2.0
        val originY = center.second - mapHeight * .65
        canvas.save(); canvas.clipRect(0,top,width,height-bottom)
        for (x in floor(originX/256).toInt()..floor((originX+width)/256).toInt()) {
            for (y in floor(originY/256).toInt()..floor((originY+mapHeight)/256).toInt()) {
                CompanionMaps.tile(context,tileKey,x,y,zoom)?.let { tile ->
                    canvas.drawBitmap(tile,(x*256-originX).toFloat(),(y*256-originY+top).toFloat(),p); tile.recycle()
                }
            }
        }
        val path = Path()
        navigator.route.points.forEachIndexed { i, point ->
            val px = CompanionMaps.pixel(point,zoom)
            val x = (px.first-originX).toFloat(); val y = (px.second-originY+top).toFloat()
            if (i == 0) path.moveTo(x,y) else path.lineTo(x,y)
        }
        p.style = Paint.Style.STROKE; p.strokeWidth = 10f; p.strokeJoin = Paint.Join.ROUND; p.strokeCap = Paint.Cap.ROUND
        p.color = colors[7]; canvas.drawPath(path,p); p.strokeWidth = 6f; p.color = colors[9]; canvas.drawPath(path,p)
        p.style = Paint.Style.FILL
        val cx = width/2f; val cy = (mapHeight*.65+top).toFloat()
        p.color = colors[8]; canvas.drawCircle(cx,cy,12f,p); p.color = colors[7]; canvas.drawCircle(cx,cy,5f,p)
        canvas.restore()
        p.color = colors[0]; canvas.drawRect(0f,0f,width.toFloat(),top.toFloat(),p)
        canvas.drawRect(0f,(height-bottom).toFloat(),width.toFloat(),height.toFloat(),p)
        fun text(value: String, x: Float, y: Float, size: Float, color: Int, maxWidth: Float = width-32f) {
            p.textSize=size; p.color=color; p.typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)
            canvas.drawText(TextUtils.ellipsize(value,TextPaint(p),maxWidth,TextUtils.TruncateAt.END).toString(),x,y,p)
        }
        val instruction = when { progress.arrived -> "已到达目的地"; !progress.reliable -> "定位精度不足，等待 GPS"; progress.offRoute -> "已偏航 · 正在确认路线"; else -> progress.instruction }
        text(if (progress.arrived) "到达" else distance(progress.turnDistance),16f,35f,28f,colors[8])
        text(instruction,16f,top-15f,17f,colors[7])
        text("剩余 ${distance(progress.remaining)}",16f,height-29f,18f,colors[7])
        text(navigator.route.destination,16f,height-9f,12f,colors[5])
        text("天地图",width-56f,height-bottom-8f,10f,colors[0],54f)
        return bitmap
    }

    fun distance(meters: Double) = if (meters >= 1000) "%.1f km".format(java.util.Locale.ROOT,meters/1000) else "${meters.toInt().coerceAtLeast(0)} m"
}
