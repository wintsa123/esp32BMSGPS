package com.fuckingbms.cast

data class CastTarget(val rotation: Int, val width: Int, val height: Int) {
    init {
        require(rotation in 0..3)
        require(width in 1..CastInfo.MAX_DIMENSION && height in 1..CastInfo.MAX_DIMENSION)
        require(width != height)
    }

    val landscape: Boolean get() = width > height
}

data class CastInfo(
    val physicalWidth: Int,
    val physicalHeight: Int,
    val orientations: List<CastTarget>,
    val codec: String,
    val jpegQuality: Int,
    val targetFps: Int,
    val maxFrameBytes: Int,
) {
    init {
        require(physicalWidth in 1..MAX_DIMENSION && physicalHeight in 1..MAX_DIMENSION)
        require(codec == "jpeg") { "设备投屏编码不兼容" }
        require(jpegQuality in CastProtocol.FALLBACK_JPEG_QUALITY..100)
        require(targetFps in 1..CastProtocol.TARGET_FPS)
        require(maxFrameBytes in 1..CastProtocol.MAX_FRAME_BYTES)
        require(orientations.size == 2)
        require(orientations.any { it.landscape })
        require(orientations.any { !it.landscape })
        require(orientations.all {
            (it.width == physicalWidth && it.height == physicalHeight) ||
                (it.width == physicalHeight && it.height == physicalWidth)
        })
        require(orientations.distinctBy { Triple(it.rotation, it.width, it.height) }.size == orientations.size)
    }

    val width: Int get() = orientations.first().width
    val height: Int get() = orientations.first().height
    val rotation: Int get() = orientations.first().rotation

    fun targetFor(sourceWidth: Int, sourceHeight: Int): CastTarget {
        require(sourceWidth > 0 && sourceHeight > 0)
        val wantsLandscape = sourceWidth > sourceHeight
        return orientations.firstOrNull { it.landscape == wantsLandscape }
            ?: error("设备没有匹配当前手机方向的投屏尺寸")
    }

    companion object {
        const val MAX_DIMENSION = 4096

        fun parse(json: String): CastInfo {
            require(integer(json, "protocol_version") == CastProtocol.VERSION) { "设备协议版本不兼容" }
            val orientationsBody = Regex("\\\"orientations\\\"\\s*:\\s*\\[(.*?)\\]", RegexOption.DOT_MATCHES_ALL)
                .find(json)?.groupValues?.get(1) ?: error("设备未返回投屏方向")
            val orientations = Regex("\\{([^{}]*)\\}").findAll(orientationsBody).map { match ->
                val fields = match.groupValues[1]
                CastTarget(
                    rotation = integer(fields, "rotation"),
                    width = integer(fields, "width"),
                    height = integer(fields, "height"),
                )
            }.toList()
            return CastInfo(
                physicalWidth = integer(json, "physical_width"),
                physicalHeight = integer(json, "physical_height"),
                orientations = orientations,
                codec = string(json, "codec"),
                jpegQuality = integer(json, "jpeg_quality"),
                targetFps = integer(json, "target_fps"),
                maxFrameBytes = integer(json, "max_frame_bytes"),
            )
        }

        private fun integer(json: String, name: String): Int =
            Regex("\\\"${Regex.escape(name)}\\\"\\s*:\\s*(\\d+)").find(json)?.groupValues?.get(1)?.toIntOrNull()
                ?: error("设备返回数据无效")

        private fun string(json: String, name: String): String =
            Regex("\\\"${Regex.escape(name)}\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"")
                .find(json)?.groupValues?.get(1) ?: error("设备返回数据无效")
    }
}
