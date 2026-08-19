package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.json.JSONObject

class DeviceApiTest {
    @Test fun serializesSpeedUnitForDeviceConfig() {
        assertEquals(
            """{"speed_unit":"km/h"}""",
            JSONObject().put("speed_unit", "km/h").toString(),
        )
    }

    @Test fun parsesStatusRecordsAndManifestFromDeviceContracts() {
        val status = DeviceStatus.parse(
            """{"version":"1.2.3","speed":"32.0","speed_unit":"km/h","bms":"online","pack_voltage_mv":52400,"current_deci_amps":-126,"soc_percent":78,"local_battery_mv":52300,"bms_info":"JK BMS","bms_protections":[],"bms_warnings":["cell_ov"],"bms_temperatures_c":[28,null,31],"wifi":"setup_ap","setup_ap_enabled":true,"bms_capacity_estimate_mah":18600,"bms_capacity_estimate_state":"ready","controller_online":true,"controller_speed_deci_units":456,"controller_rpm":1800,"controller_gear":1,"controller_power_w":3200,"controller_temp_c":42,"motor_temp_c":51}""",
        )
        val records = JSONObject(
            """{"records":[{"current":true,"max_current":{"pack_voltage_mv":52000,"current_deci_amps":-220,"delta_cell_voltage_mv":18,"soc_percent":76,"temperatures_c":[29]},"max_delta":{"pack_voltage_mv":51900,"current_deci_amps":-40,"delta_cell_voltage_mv":32,"soc_percent":76,"temperatures_c":[30]}}]}""",
        ).getJSONArray("records")
        val snapshot = RideSnapshot.parse(records.getJSONObject(0).getJSONObject("max_current"))
        val capabilities = DeviceCapabilities.parse(
            """{"protocol_version":1,"sections":[{"id":"device","items":[{"id":"brightness"},{"id":"bms_mac"}]},{"id":"records","items":[{"id":"gps_track"}]},{"id":"bms","items":[{"id":"ota"}]}]}""",
        )

        assertEquals("1.2.3", status.version)
        assertEquals(-126, status.currentDeciAmps)
        assertEquals(listOf(28, 31), status.temperaturesC)
        assertTrue(status.controllerOnline)
        assertEquals(456, status.controllerSpeedDeci)
        assertEquals(1800, status.controllerRpm)
        assertEquals(1, status.controllerGear)
        assertEquals(3200, status.controllerPowerW)
        assertEquals(42, status.controllerTempC)
        assertEquals(51, status.motorTempC)
        assertEquals(1, records.length())
        assertEquals(-220, snapshot.currentDeciAmps)
        assertTrue(capabilities.supports("bms_mac"))
        assertTrue(capabilities.supports("gps_track"))
        assertTrue(capabilities.supports("ota"))
        assertFalse(capabilities.supports("missing"))
        assertTrue(capabilities.hasSection("device"))
        assertTrue(capabilities.hasSection("records"))
        assertFalse(capabilities.hasSection("missing"))
    }

    @Test fun parsesPagedFlashDbHistoryContracts() {
        val overview = DeviceApi.parseHistoryOverview(
            """{"ready":true,"backend":"flash","capacity_bytes":4128768,"sessions":[{"id":7,"start_s":1000,"end_s":1002,"samples":3,"capacity_samples":18000,"elapsed_s":2,"calibrated":true,"truncated":false,"capacity_reached":false}]}""",
        )
        val samples = DeviceApi.parseHistorySamplesPage(
            """{"samples":[{"t":1000,"elapsed_s":0,"flags":3,"lat_e7":311234567,"lon_e7":1211234567,"pack_voltage_mv":52000,"current_deci_amps":-125,"soc_percent":80,"temperatures_c":[28,29]}],"next_cursor":1000}""",
        )
        val faults = DeviceApi.parseHistoryFaultsPage(
            """{"faults":[{"t":2,"session":7,"active_mask":1,"supported_mask":3,"bms_type":2}],"next_cursor":null}""",
        )

        assertTrue(overview.ready)
        assertEquals(7L, overview.sessions.single().id)
        assertEquals(-125, samples.values.single().currentDeciAmps)
        assertEquals(1000L, samples.nextCursor)
        assertEquals(7L, faults.values.single().sessionId)
        assertEquals(null, faults.nextCursor)
    }

    @Test fun flashDbHistoryUsesTheSelectedDeviceTransport() {
        val paths = mutableListOf<String>()
        val transport = DeviceTransport { _, path, _ ->
            paths += path
            when {
                path.endsWith("/sessions") -> """{"ready":true,"sessions":[]}"""
                "/samples?" in path -> """{"samples":[],"next_cursor":null}"""
                else -> """{"faults":[],"next_cursor":null}"""
            }
        }

        DeviceApi.historyOverview(transport)
        DeviceApi.historySamplesPage(transport, session = 7, limit = 8)
        DeviceApi.historyFaultsPage(transport, session = 7, limit = 20)

        assertEquals("/api/history/sessions", paths[0])
        assertTrue(paths[1].startsWith("/api/history/samples?session=7"))
        assertTrue(paths[2].startsWith("/api/history/faults?"))
    }
}
