package com.fuckingbms.cast

import org.junit.Assert.assertEquals
import org.junit.Test

class DeviceFormatTest {
    @Test fun formatsDeviceValuesWithStableUnits() {
        assertEquals("52.40 V", formatMillivolts(52_400))
        assertEquals("-12.6 A", formatDeciAmps(-126))
        assertEquals("18.6 Ah", formatCapacity(18_600, "ready"))
        assertEquals("估算中", formatCapacity(null, "estimating"))
    }
}
