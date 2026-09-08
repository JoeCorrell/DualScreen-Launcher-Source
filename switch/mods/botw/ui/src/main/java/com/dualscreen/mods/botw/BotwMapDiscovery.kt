package com.dualscreen.mods.botw

import android.graphics.Path
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.json.JSONObject
import kotlin.math.roundToInt

/** Native BECO region runs. No region polygons or discovery values are hand-authored. */
internal class BotwMapRegions(bytes: ByteArray) {
    private val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private val rowCount: Int
    private val divisor: Int
    private val dataOffset: Int

    init {
        require(bytes.size in 24..2*1024*1024 && buffer.getInt(0) == 0x00112233)
        rowCount = buffer.getInt(4)
        divisor = buffer.getInt(8)
        require(rowCount in 2..10000 && divisor in 1..1000)
        dataOffset = 16 + rowCount*4
        require(dataOffset <= bytes.size)
        var previous = 0
        for (row in 0 until rowCount) {
            val offset = buffer.getInt(16+row*4)
            require(offset >= previous && offset % 2 == 0 &&
                dataOffset.toLong()+offset.toLong()*2 <= bytes.size)
            previous = offset
        }
        for (row in 0 until rowCount-1) {
            var width = 0
            runs(row) { _, length -> require(length > 0); width += length }
            require(width == if (divisor == 10) 1000 else 10000)
        }
    }

    private inline fun runs(row: Int, visit: (Int, Int) -> Unit) {
        val start = dataOffset + buffer.getInt(16+row*4)*2
        val end = dataOffset + buffer.getInt(20+row*4)*2
        for (offset in start until end step 4) {
            visit(buffer.getShort(offset).toInt(), buffer.getShort(offset+2).toInt())
        }
    }

    // Mirrors Ecosystem::getMapArea (11858A8), including the sentinel row and
    // the native clamp/round/10-unit special case. Used by tests and marker policy.
    fun regionAt(x: Float, z: Float): Int {
        val row = ((z.coerceIn(-4000f,4000f)+4000f).roundToInt()/divisor)
            .coerceIn(0,rowCount-2)
        val column = (x.coerceIn(-5000f,4999f)+5000f).roundToInt() /
            if (divisor == 10) 10 else 1
        var end = 0
        var found = -1
        runs(row) { region,length ->
            if (column >= end && column < end+length) found = region
            end += length
        }
        return found
    }

    fun hiddenPath(visibleFlags: Set<String>): Path {
        val result = Path()
        val openRegions = visibleFlags.asSequence().filter { it.startsWith("MapTower_") }
            .mapNotNull { it.removePrefix("MapTower_").toIntOrNull()?.minus(1) }.toSet()
        if (openRegions.isEmpty()) {
            result.addRect(-6000f,-6000f,6000f,6000f,Path.Direction.CW)
            return result
        }
        val horizontalUnit = if (divisor == 10) 10 else 1
        // Merge identical spans across adjacent rows. Native region geometry can
        // contain thousands of rows; rebuilding/drawing one rectangle per run is
        // unnecessary work, especially while panning the map.
        var pending = mutableMapOf<Pair<Float,Float>,Pair<Float,Float>>()
        for (row in 0 until rowCount-1) {
            val top = if (row == 0) -6000f else row*divisor-4000.5f
            val bottom = if (row == rowCount-2) 6000f else (row+1)*divisor-4000.5f
            var end = 0
            var hiddenStart: Float? = null
            val spans = mutableListOf<Pair<Float,Float>>()
            runs(row) { region,length ->
                val left = if (end == 0) -6000f else end*horizontalUnit-5000.5f
                end += length
                val right = if (end*horizontalUnit >= 10000) 6000f else end*horizontalUnit-5000.5f
                if (region < 0 || region !in openRegions) {
                    if (hiddenStart == null) hiddenStart = left
                    if (right == 6000f) spans.add(requireNotNull(hiddenStart) to right)
                } else if (hiddenStart != null) {
                    spans.add(requireNotNull(hiddenStart) to left)
                    hiddenStart = null
                }
            }
            val next = mutableMapOf<Pair<Float,Float>,Pair<Float,Float>>()
            for (span in spans) next[span] = (pending.remove(span)?.first ?: top) to bottom
            for ((span,vertical) in pending) {
                result.addRect(span.first,vertical.first,span.second,vertical.second,Path.Direction.CW)
            }
            pending = next
        }
        for ((span,vertical) in pending) {
            result.addRect(span.first,vertical.first,span.second,vertical.second,Path.Direction.CW)
        }
        return result
    }
}

internal data class BotwUserMapMarker(val slot: Int, val icon: Int, val x: Float, val z: Float)

internal data class BotwMapDiscovery(
    val revision: String,
    val visible: Set<String>,
    val regions: BotwMapRegions,
    val fog: Path,
    val userMarkers: List<BotwUserMapMarker>
) {
    companion object {
        fun parse(json: String, previous: BotwMapDiscovery?): BotwMapDiscovery? {
            val root = JSONObject(json)
            if (root.optString("status") != "ok") return null
            val revision = root.getString("revision")
            val flags = root.getJSONArray("visible")
            val visible = (0 until flags.length()).map { flags.getString(it) }.toSet()
            val saved = root.optJSONArray("userMarkers")
            require(saved == null || saved.length() <= 512)
            val markers = (0 until (saved?.length() ?: 0)).map {
                val value = requireNotNull(saved).getJSONObject(it)
                BotwUserMapMarker(value.getInt("slot"),value.getInt("icon"),
                    value.getDouble("x").toFloat(),value.getDouble("z").toFloat()).also { marker ->
                    require(marker.slot in 0..511 && marker.icon in 27..41 && marker.icon != 36 &&
                        marker.x.isFinite() && marker.z.isFinite() &&
                        marker.x in -6000f..6000f && marker.z in -6000f..6000f)
                }
            }
            require(markers.map { it.slot }.toSet().size == markers.size)
            val sameFlags = previous != null && previous.revision == revision && previous.visible == visible
            if (sameFlags && previous!!.userMarkers == markers) return previous
            val regions = if (root.has("regions")) {
                val hex = root.getString("regions")
                require(hex.length % 2 == 0 && hex.length <= 4*1024*1024)
                BotwMapRegions(ByteArray(hex.length/2) { hex.substring(it*2,it*2+2).toInt(16).toByte() })
            } else {
                require(previous?.revision == revision)
                requireNotNull(previous).regions
            }
            return BotwMapDiscovery(revision,visible,regions,
                if (sameFlags) previous!!.fog else regions.hiddenPath(visible),markers)
        }
    }
}
