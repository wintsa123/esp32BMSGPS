package com.fuckingbms.cast

import org.junit.Assert.*
import org.junit.Test

class DashboardPolicyTest {
    @Test fun bridgeIsLimitedToTrustedDocumentAndDeviceApis() {
        assertTrue(DashboardPolicy.isDocument("https://bms.local/assets/index.js"))
        listOf("https://bms.local.evil/", "http://bms.local/", "https://evil@bms.local/", "https://bms.local:443/").forEach {
            assertFalse(DashboardPolicy.isDocument(it))
        }
        assertTrue(DashboardPolicy.apiPath("/api/history/samples?session=1"))
        listOf("https://evil/api/status", "/api/../admin", "/api/%2e%2e/admin", "//evil/api/status", "/api/status#x").forEach {
            assertFalse(DashboardPolicy.apiPath(it))
        }
    }

    @Test fun bridgeAcceptsOnlyLocalIpv4AndValidPorts() {
        assertEquals("192.168.4.1", DashboardPolicy.localHost("http://192.168.4.1/"))
        assertEquals("10.0.0.2:8080", DashboardPolicy.localHost("10.0.0.2:8080"))
        listOf("evil.test", "8.8.8.8", "127.0.0.1", "192.168.4.1:0", "192.168.4.1:65536", "192.168.4.1/evil", "user@192.168.4.1").forEach {
            assertTrue(runCatching { DashboardPolicy.localHost(it) }.isFailure)
        }
    }
}
