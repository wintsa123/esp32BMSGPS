package com.fuckingbms.cast

import org.junit.Assert.*
import org.junit.Test

class CompanionTest {
    @Test fun ridingBridgeConvertsGcjRouteForTiandituAndPreservesInstructions() {
        val response=org.json.JSONObject("""{"status":"1","route":{"paths":[{"duration":120,"steps":[{"instruction":"右转","road":"测试路","polyline":"121.47,31.23;121.48,31.24"}]}]}}""")
        val parsed=CompanionMaps.parseRoute(response,"终点")
        assertEquals(120,parsed.seconds)
        assertEquals("测试路",parsed.steps.single().road)
        assertTrue(NavGeo.distance(parsed.points.first(),NavPoint(31.23,121.47)) > 100)
        assertTrue(NavGeo.distance(NavGeo.gcj(parsed.points.first()),NavPoint(31.23,121.47)) < .1)
    }
    @Test fun ridingBridgeRejectsBadCoordinatesAndHidesCredentialErrors() {
        val error=assertThrows(IllegalStateException::class.java) { CompanionMaps.parseRoute(org.json.JSONObject("""{"status":"0","info":"denied:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"""),"终点") }
        assertFalse(error.message.orEmpty().contains("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
        for (polyline in listOf("121.47,31.23", "181,31.23;121.48,31.24", "NaN,31.23;121.48,31.24")) {
            val json=org.json.JSONObject("""{"status":"1","route":{"paths":[{"duration":120,"steps":[{"polyline":"$polyline"}]}]}}""")
            assertThrows(IllegalArgumentException::class.java) { CompanionMaps.parseRoute(json,"终点") }
        }
    }
    @Test fun ridingWebViewOnlyLoadsHttpsAmapResources() {
        for (url in listOf("https://webapi.amap.com/loader.js", "https://restapi.amap.com/v4/direction/bicycling")) assertTrue(CompanionRiding.trustedResource(url))
        for (url in listOf("http://webapi.amap.com/maps", "https://webapi.amap.com.evil.test/x", "https://evilamap.com/x", "https://evil.test/?host=amap.com", "file:///etc/passwd", "https://user@webapi.amap.com/maps", "https://webapi.amap.com:8080/maps")) assertFalse(CompanionRiding.trustedResource(url))
    }
    @Test fun lyricsUsesFractionOffsetAndLastLine() {
        val lines=Lyrics.parse("[offset:-100]\n[00:00.1]第一句\n[00:01.23][00:02.000]重复\n[00:80]非法")
        assertEquals(listOf(0L,1130L,1900L),lines.map { it.timeMs })
        assertEquals("第一句",Lyrics.current(lines,1000))
        assertEquals("重复",Lyrics.current(lines,2000))
        assertTrue(Lyrics.parse("普通通知不应当冒充时间轴").isEmpty())
        assertEquals("",Lyrics.current(listOf(Lyrics.Line(2000,"稍后")),0))
    }
    @Test fun coordinateRoundTripAndOutsideChina() {
        val p=NavPoint(31.2304,121.4737)
        val g=NavGeo.gcj(p)
        assertTrue(NavGeo.distance(p,g) > 100)
        assertTrue(NavGeo.distance(p,NavGeo.wgs(g)) < .1)
        assertEquals(NavPoint(51.5,-.1),NavGeo.gcj(NavPoint(51.5,-.1)))
    }
    @Test fun localProgressDoesNotSpendQuotaOrRewindAndRequiresSustainedDeviation() {
        val route=NavRoute(listOf(NavPoint(30.0,120.0),NavPoint(30.01,120.0),NavPoint(30.01,120.01)),
            listOf(NavStep(1,"右转","道路 A"),NavStep(2,"到达","道路 B")),300,"测试终点")
        val nav=LocalNavigator(route)
        val first=nav.update(NavPoint(30.005,120.0),5f,1000)
        assertEquals("右转",first.instruction)
        assertTrue(first.turnDistance in 500.0..600.0)
        val behind=nav.update(NavPoint(30.0049,120.0),5f,2000)
        assertEquals(first.remaining,behind.remaining,.1)
        assertFalse(nav.update(NavPoint(30.005,120.002),5f,3000).offRoute)
        assertTrue(nav.update(NavPoint(30.005,120.002),5f,16000).offRoute)
        assertFalse(nav.update(NavPoint(30.005,120.002),100f,17000).offRoute)
        assertFalse(nav.update(NavPoint(30.005,120.002),100f,17000).reliable)
        assertTrue(nav.update(route.points.last(),5f,18000).arrived)
    }
    @Test fun mercatorProjectionMatchesTileGrid() {
        val equator=CompanionMaps.pixel(NavPoint(0.0,0.0),0)
        assertEquals(128.0,equator.first,.0001); assertEquals(128.0,equator.second,.0001)
        val north=CompanionMaps.pixel(NavPoint(66.51326044311186,0.0),0)
        assertEquals(64.0,north.second,.001)
    }
    @Test fun frameEncodingPreservesLongRunsAndNibbles() {
        val encoded=CompanionFrame.encode(IntArray(70000) { CompanionFrame.palette[8] })
        assertArrayEquals(byteArrayOf(0xf8.toByte(),0xff.toByte(),0xff.toByte(),0xf8.toByte(),0x71,0x11),encoded)
        assertArrayEquals(byteArrayOf(1,0x22),CompanionFrame.encode(intArrayOf(CompanionFrame.palette[1],CompanionFrame.palette[2],CompanionFrame.palette[2],CompanionFrame.palette[2])))
    }
}
