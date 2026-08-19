package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MainActivityStateTest {
    @Test fun systemBluetoothProfilesIncludeBleHidHost() {
        assertTrue(SYSTEM_BLUETOOTH_PROFILES.contains(BLUETOOTH_HID_HOST_PROFILE))
    }

    @Test fun primaryActionFollowsDeviceAndCastState() {
        assertFalse(primaryAction(UiStage.WAITING_SCAN, false).enabled)
        assertFalse(primaryAction(UiStage.READY, false).enabled)
        assertTrue(primaryAction(UiStage.READY, true).enabled)
        assertTrue(primaryAction(UiStage.CAST_CONNECTING, true).stopsCast)
        assertEquals("停止投屏", primaryAction(UiStage.CASTING, true).label)
        assertTrue(primaryAction(UiStage.CASTING, true).stopsCast)
        assertTrue(primaryAction(UiStage.FAILED, true).enabled)
        assertFalse(primaryAction(UiStage.FAILED, false).enabled)
        assertFalse(primaryAction(UiStage.READY, true, DeviceConnectionMode.BLE).enabled)
        assertTrue(primaryAction(UiStage.READY, true, DeviceConnectionMode.WIFI).enabled)
    }
}
