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
            """{"version":"1.2.3","speed":"32.0","speed_unit":"km/h","bms":"online","pack_voltage_mv":52400,"current_deci_amps":-126,"soc_percent":78,"local_battery_mv":52300,"bms_info":"JK BMS","bms_protections":[],"bms_warnings":["cell_ov"],"bms_temperatures_c":[28,null,31],"wifi":"setup_ap","setup_ap_enabled":true,"bms_capacity_estimate_mah":18600,"bms_capacity_estimate_state":"ready"}""",
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
}
