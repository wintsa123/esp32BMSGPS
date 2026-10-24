package com.fuckingbms.cast

import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Where one cast frame lands on the panel.
 *
 * [src] is the visible region of the virtual-display image (image pixel space, system
 * bars already excluded); [dst] is where that region is drawn on the panel (panel pixel
 * space). The picture is scaled to fit rather than cropped to fill, so [dst] can be
 * smaller than the panel and the leftover area is letterboxed.
 */
internal data class CastLayout(val src: CaptureRect, val dst: CaptureRect)

/**
 * Scales the phone picture down until it fits the panel, then centers it.
 *
 * Returns the source region to read and the destination rectangle to draw into. A
 * degenerate input falls back to the full image drawn over the whole panel.
 */
internal fun castLayoutFor(
    imageWidth: Int,
    imageHeight: Int,
    sourceWidth: Int,
    sourceHeight: Int,
    insets: CaptureInsets,
    targetWidth: Int,
    targetHeight: Int,
): CastLayout {
    val fullImage = CaptureRect(0, 0, imageWidth.coerceAtLeast(0), imageHeight.coerceAtLeast(0))
    if (imageWidth <= 0 || imageHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
        targetWidth <= 0 || targetHeight <= 0
    ) {
        return CastLayout(
            src = fullImage,
            dst = CaptureRect(0, 0, targetWidth.coerceAtLeast(0), targetHeight.coerceAtLeast(0)),
        )
    }

    var left = floor(insets.left.toDouble() * imageWidth / sourceWidth).toInt()
    var top = floor(insets.top.toDouble() * imageHeight / sourceHeight).toInt()
    var right = imageWidth - ceil(insets.right.toDouble() * imageWidth / sourceWidth).toInt()
    var bottom = imageHeight - ceil(insets.bottom.toDouble() * imageHeight / sourceHeight).toInt()
    if (insets.left < 0 || insets.top < 0 || insets.right < 0 || insets.bottom < 0 ||
        right <= left || bottom <= top
    ) {
        left = 0
        top = 0
        right = imageWidth
        bottom = imageHeight
    }

    val srcWidth = right - left
    val srcHeight = bottom - top
    val scale = min(targetWidth.toDouble() / srcWidth, targetHeight.toDouble() / srcHeight)
    val dstWidth = (srcWidth * scale).roundToInt().coerceIn(1, targetWidth)
    val dstHeight = (srcHeight * scale).roundToInt().coerceIn(1, targetHeight)
    val dstLeft = (targetWidth - dstWidth) / 2
    val dstTop = (targetHeight - dstHeight) / 2
    return CastLayout(
        src = CaptureRect(left, top, right, bottom),
        dst = CaptureRect(dstLeft, dstTop, dstLeft + dstWidth, dstTop + dstHeight),
    )
}

/** Geometry of the frame currently on the panel, used to map touch back to the phone. */
internal data class CastTouchGeometry(
    val imageWidth: Int,
    val imageHeight: Int,
    val sourceWidth: Int,
    val sourceHeight: Int,
    val layout: CastLayout,
) {
    /** Panel pixel -> phone screen pixel, or null when the point sits in the letterbox. */
    fun toScreen(panelX: Int, panelY: Int): Pair<Int, Int>? {
        val src = layout.src
        val dst = layout.dst
        val dstWidth = dst.right - dst.left
        val dstHeight = dst.bottom - dst.top
        if (imageWidth <= 0 || imageHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
            dstWidth <= 0 || dstHeight <= 0 ||
            panelX < dst.left || panelX >= dst.right ||
            panelY < dst.top || panelY >= dst.bottom
        ) {
            return null
        }
        val imageX = src.left + (panelX - dst.left).toLong() * (src.right - src.left) / dstWidth
        val imageY = src.top + (panelY - dst.top).toLong() * (src.bottom - src.top) / dstHeight
        val screenX = (imageX * sourceWidth / imageWidth).toInt().coerceIn(0, sourceWidth - 1)
        val screenY = (imageY * sourceHeight / imageHeight).toInt().coerceIn(0, sourceHeight - 1)
        return screenX to screenY
    }
}
