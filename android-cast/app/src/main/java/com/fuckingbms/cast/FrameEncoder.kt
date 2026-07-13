package com.fuckingbms.cast

/** Keeps the acknowledged image only; it never queues more than one frame. */
class FrameEncoder(private val width: Int, private val height: Int, private val blockSide: Int = CastProtocol.MAX_BLOCK_SIDE) {
    private var baseline: ByteArray? = null

    init {
        require(width > 0 && height > 0 && blockSide in 1..CastProtocol.MAX_BLOCK_SIDE)
    }

    fun encode(sequence: Int, rotation: Int, pixels: ByteArray): List<ByteArray> {
        require(pixels.size == width * height * 2)
        val previous = baseline
        val packets = ArrayList<ByteArray>()
        packets += CastProtocol.frameBegin(sequence, rotation)
        for (y in 0 until height step blockSide) for (x in 0 until width step blockSide) {
            val w = minOf(blockSide, width - x)
            val h = minOf(blockSide, height - y)
            if (previous == null || blockChanged(previous, pixels, x, y, w, h)) {
                val block = ByteArray(w * h * 2)
                for (row in 0 until h) {
                    val source = ((y + row) * width + x) * 2
                    System.arraycopy(pixels, source, block, row * w * 2, w * 2)
                }
                packets += CastProtocol.block(x, y, w, h, block)
            }
        }
        packets += CastProtocol.frameEnd(sequence)
        return packets
    }

    fun acknowledge(pixels: ByteArray) { require(pixels.size == width * height * 2); baseline = pixels.copyOf() }
    fun reset() { baseline = null }

    private fun blockChanged(before: ByteArray, after: ByteArray, x: Int, y: Int, w: Int, h: Int): Boolean {
        for (row in 0 until h) {
            val offset = ((y + row) * width + x) * 2
            for (index in 0 until w * 2) if (before[offset + index] != after[offset + index]) return true
        }
        return false
    }
}
