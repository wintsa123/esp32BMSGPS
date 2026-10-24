package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CastGeometryTest {
    /* 2400x1080 phone (20:9) captured into a 480x320 panel (3:2). */
    private fun widePhoneLayout() = castLayoutFor(
        imageWidth = 712,
        imageHeight = 320,
        sourceWidth = 2400,
        sourceHeight = 1080,
        insets = CaptureInsets.ZERO,
        targetWidth = 480,
        targetHeight = 320,
    )

    @Test fun fitsTheWholePictureInsteadOfCroppingIt() {
        val layout = widePhoneLayout()
        assertEquals(CaptureRect(0, 0, 712, 320), layout.src)
        // Full width, scaled height, centered vertically -> 52px letterbox top and bottom.
        assertEquals(CaptureRect(0, 52, 480, 268), layout.dst)
    }

    @Test fun mapsThePanelCentreBackToThePhoneCentre() {
        val geometry = CastTouchGeometry(
            imageWidth = 712,
            imageHeight = 320,
            sourceWidth = 2400,
            sourceHeight = 1080,
            layout = widePhoneLayout(),
        )
        assertEquals(1200 to 540, geometry.toScreen(240, 160))
    }

    @Test fun ignoresTouchInsideTheLetterbox() {
        val geometry = CastTouchGeometry(
            imageWidth = 712,
            imageHeight = 320,
            sourceWidth = 2400,
            sourceHeight = 1080,
            layout = widePhoneLayout(),
        )
        assertNull(geometry.toScreen(240, 10))
        assertNull(geometry.toScreen(240, 319))
        assertEquals(0 to 0, geometry.toScreen(0, 52))
        // The last panel pixel inside the picture lands just short of the screen corner.
        assertEquals(2393 to 1073, geometry.toScreen(479, 267))
    }

    @Test fun excludesSystemBarsFromTheSourceRegion() {
        val layout = castLayoutFor(
            imageWidth = 712,
            imageHeight = 320,
            sourceWidth = 2400,
            sourceHeight = 1080,
            insets = CaptureInsets(0, 100, 0, 100),
            targetWidth = 480,
            targetHeight = 320,
        )
        assertEquals(CaptureRect(0, 29, 712, 290), layout.src)
        assertEquals(CaptureRect(0, 72, 480, 248), layout.dst)
    }

    @Test fun rejectsDegenerateGeometry() {
        val layout = castLayoutFor(
            imageWidth = 0,
            imageHeight = 500,
            sourceWidth = 500,
            sourceHeight = 500,
            insets = CaptureInsets.ZERO,
            targetWidth = 500,
            targetHeight = 500,
        )
        assertNull(
            CastTouchGeometry(
                imageWidth = 0,
                imageHeight = 500,
                sourceWidth = 500,
                sourceHeight = 500,
                layout = layout,
            ).toScreen(1, 1),
        )
    }
}
