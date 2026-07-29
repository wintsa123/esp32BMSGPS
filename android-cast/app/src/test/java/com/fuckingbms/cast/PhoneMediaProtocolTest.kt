package com.fuckingbms.cast

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class PhoneMediaProtocolTest {
    @Test fun mediaPermissionsCoverAndroidTenAndNearbyDevices() {
        assertEquals(listOf(android.Manifest.permission.ACCESS_FINE_LOCATION),
            MainActivity.mediaPermissionsForSdk(android.os.Build.VERSION_CODES.Q))
        assertEquals(listOf(android.Manifest.permission.BLUETOOTH_SCAN,
            android.Manifest.permission.BLUETOOTH_CONNECT,
            android.Manifest.permission.POST_NOTIFICATIONS),
            MainActivity.mediaPermissionsForSdk(android.os.Build.VERSION_CODES.TIRAMISU))
    }

    @Test fun statePayloadUsesVersionFlagsAndUtf8Boundary() {
        val payload = PhoneMediaProtocol.statePayload(true, true, true, "AéB", 4)
        assertEquals(PhoneMediaProtocol.VERSION, payload[0])
        assertEquals(PhoneMediaProtocol.STATE_READY or PhoneMediaProtocol.STATE_ACTIVE or PhoneMediaProtocol.STATE_PLAYING,
            payload[1].toInt() and 0xff)
        assertArrayEquals("A".encodeToByteArray(), payload.copyOfRange(2, payload.size))
    }

    @Test fun titleNeverExceedsProtocolLimit() {
        val payload = PhoneMediaProtocol.statePayload(false, false, false, "x".repeat(120), 128)
        assertEquals(98, payload.size)
    }

    @Test fun mediaConnectionStatusRequiresNotificationAccess() {
        assertEquals("BLE 已连接；请先授予通知访问",
            MediaControlService.mediaConnectionStatusForNotificationAccess(false))
        assertEquals("手机媒体已连接",
            MediaControlService.mediaConnectionStatusForNotificationAccess(true))
    }
}
