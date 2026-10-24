package com.fuckingbms.cast

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.content.Intent
import android.graphics.Path
import android.view.accessibility.AccessibilityEvent

/**
 * Injects taps and swipes into the phone while casting.
 *
 * Android only lets an AccessibilityService drive input, and the user has to enable it
 * manually in system settings, so a cast session stays view-only until that happens.
 */
class CastAccessibilityService : AccessibilityService() {
    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
    }

    override fun onUnbind(intent: Intent?): Boolean {
        if (instance === this) instance = null
        return super.onUnbind(intent)
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit

    override fun onInterrupt() = Unit

    private fun dispatch(fromX: Float, fromY: Float, toX: Float, toY: Float, durationMs: Long): Boolean {
        val path = Path().apply {
            moveTo(fromX, fromY)
            if (fromX != toX || fromY != toY) lineTo(toX, toY)
        }
        val stroke = GestureDescription.StrokeDescription(path, 0L, durationMs.coerceAtLeast(1L))
        return dispatchGesture(GestureDescription.Builder().addStroke(stroke).build(), null, null)
    }

    companion object {
        private const val TAP_DURATION_MS = 40L
        private const val SWIPE_DURATION_MS = 160L

        @Volatile
        private var instance: CastAccessibilityService? = null

        /** True once the user has enabled this service in system settings. */
        fun isConnected(): Boolean = instance != null

        fun tap(x: Int, y: Int): Boolean = instance?.dispatch(
            fromX = x.toFloat(),
            fromY = y.toFloat(),
            toX = x.toFloat(),
            toY = y.toFloat(),
            durationMs = TAP_DURATION_MS,
        ) ?: false

        fun swipe(fromX: Int, fromY: Int, toX: Int, toY: Int): Boolean = instance?.dispatch(
            fromX = fromX.toFloat(),
            fromY = fromY.toFloat(),
            toX = toX.toFloat(),
            toY = toY.toFloat(),
            durationMs = SWIPE_DURATION_MS,
        ) ?: false
    }
}
