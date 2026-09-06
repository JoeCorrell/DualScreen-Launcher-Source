package com.dualscreen.launcher

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test
import org.yuzu.yuzu_emu.activities.SmoMapState
import org.yuzu.yuzu_emu.activities.SmoMapViewport
import org.yuzu.yuzu_emu.activities.SmoMessages
import org.yuzu.yuzu_emu.activities.SmoMoons
import org.yuzu.yuzu_emu.activities.SmoSnapshot
import org.yuzu.yuzu_emu.activities.SmoMarkers
import org.yuzu.yuzu_emu.activities.SmoUiLayout
import org.yuzu.yuzu_emu.activities.SmoCollection
import org.yuzu.yuzu_emu.activities.SmoCollectionGrid

class SmoSnapshotTest {
    @Test fun largerPortraitUsesFormerHeaderSpaceWithoutStretchingOrCoveringStatus() {
        assertEquals(48f, SmoUiLayout.PLAYER_TOP, 0f)
        assertTrue(SmoUiLayout.PLAYER_BOTTOM < 550f)
        val width = SmoUiLayout.PLAYER_RIGHT - SmoUiLayout.PLAYER_LEFT
        val height = SmoUiLayout.PLAYER_BOTTOM - SmoUiLayout.PLAYER_TOP
        assertTrue(height > 413f * 1.2f)
        assertEquals(width / height,
            SmoUiLayout.PLAYER_RENDER_WIDTH.toFloat() / SmoUiLayout.PLAYER_RENDER_HEIGHT, .0001f)
    }
    @Test fun collectionGridUsesTheRenderedCellsAndRejectsGaps() {
        assertEquals(0, SmoCollectionGrid.itemAt(70f, 365f, 0f, 12))
        assertEquals(3, SmoCollectionGrid.itemAt(70f, 365f, 298f, 12))
        assertNull(SmoCollectionGrid.itemAt(295f, 365f, 0f, 12))
        assertNull(SmoCollectionGrid.itemAt(70f, 648f, 0f, 12))
        assertNull(SmoCollectionGrid.itemAt(Float.NaN, 365f, 0f, 12))
        assertEquals(0f, SmoCollectionGrid.maxScroll(0))
    }
    @Test fun collectionPacketsNeverExposeUnownedNames() {
        val b = ByteBuffer.allocateDirect(SmoCollection.BYTES).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, 0x434f4d53); b.putInt(4, 1); b.putInt(8, SmoCollection.BYTES)
        b.putInt(12, 1); b.putLong(16, 9); b.putInt(24, 2); b.putInt(28, 0)
        assertFalse(SmoCollection.decode(b, SmoCollection.BYTES)!!.entries.single().owned)
        "Mario".toByteArray().forEachIndexed { i, v -> b.put(40+i, v) }
        assertNull(SmoCollection.decode(b, SmoCollection.BYTES))
        b.putInt(36, 1)
        assertEquals("Mario", SmoCollection.decode(b, SmoCollection.BYTES)!!.entries.single().name)
        b.putInt(32, 99)
        assertNull(SmoCollection.decode(b, SmoCollection.BYTES))
    }
    @Test fun referenceNavigationHitsOnlyVisibleButtons() {
        for (i in 0..3) assertEquals(i, SmoUiLayout.tabAt(166f + i * 304, 1020f))
        for ((x,y) in listOf(10f to 1020f, 318f to 1020f, 100f to 975f, 1229f to 1020f))
            assertNull(SmoUiLayout.tabAt(x,y))
        assertNull(SmoUiLayout.tabAt(Float.NaN, 1020f))
    }

    @Test fun markerPacketsValidateIdentityBoundsAndCoordinates() {
        val b = ByteBuffer.allocateDirect(SmoMarkers.BYTES).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, 0x4b4f4d53); b.putInt(4, 1); b.putInt(8, SmoMarkers.BYTES)
        b.putInt(12, 1); b.putLong(16, 8); b.putInt(24, 2); b.putInt(28, 1)
        b.putInt(32, 3); b.putInt(36, 0)
        "SandWorldHomeStage".toByteArray().forEachIndexed { i, v -> b.put(56+i,v) }
        "obj655".toByteArray().forEachIndexed { i, v -> b.put(120+i,v) }
        val packet = SmoMarkers.decode(b, SmoMarkers.BYTES)!!
        assertEquals("Checkpoint_obj655", packet.entries.single().messageKey)
        b.putFloat(40, Float.NaN)
        assertNull(SmoMarkers.decode(b, SmoMarkers.BYTES))
        b.putFloat(40, 0f); b.putInt(12, 33)
        assertNull(SmoMarkers.decode(b, SmoMarkers.BYTES))
    }
    private fun frame(): ByteBuffer = ByteBuffer.allocateDirect(SmoSnapshot.BYTES)
        .order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0, 0x534f4d53); putInt(4, 1); putInt(8, SmoSnapshot.BYTES)
            putInt(12, 4); putLong(16, 7); putInt(24, 2); putInt(28, 1)
            putInt(32, 175); putInt(36, 6); putInt(40, 3); putInt(44, 3)
            "Mario".toByteArray().forEachIndexed { i, v -> put(64 + i, v) }
            "MarioCaptain".toByteArray().forEachIndexed { i, v -> put(128 + i, v) }
            "SandWorldHomeStage".toByteArray().forEachIndexed { i, v -> put(192 + i, v) }
        }

    @Test fun snapshotsCopyNativeBufferAndRespectLifecycle() {
        val b = frame()
        val state = SmoSnapshot.decode(b, SmoSnapshot.BYTES)!!
        assertEquals(175, state.coins); assertEquals("MarioCaptain", state.hat)
        b.putInt(32, 2)
        assertEquals(175, state.coins)
        b.putInt(12, 5)
        val transition = SmoSnapshot.decode(b, SmoSnapshot.BYTES)!!
        assertFalse(transition.active); assertEquals(0, transition.coins)
        assertEquals("", transition.hat); assertNull(transition.map)
    }

    @Test fun decoderRejectsMalformedVersionSizeStateAndStrings() {
        for ((offset, value) in listOf(0 to 0, 4 to 2, 8 to 256, 12 to 8,
            24 to 17, 28 to -1, 32 to -1, 36 to -1, 40 to 10, 44 to 4)) {
            val b = frame().apply { putInt(offset, value) }
            assertNull("offset $offset", SmoSnapshot.decode(b, SmoSnapshot.BYTES))
        }
        assertNull(SmoSnapshot.decode(frame(), 256))
        val b = frame().apply { repeat(64) { put(64 + it, 65.toByte()) } }
        assertNull(SmoSnapshot.decode(b, SmoSnapshot.BYTES))
    }

    @Test fun combinedNativeMapMatrixProjectsXYWithCanvasYFlip() {
        val matrix = listOf(.001f,0f,0f,.1f, 0f,0f,.001f,.2f,
            0f,.001f,0f,0f, 0f,0f,0f,1f)
        val map = SmoMapState("CapWorldHomeStage2", 2, 2048, 2048,
            matrix, listOf(100f, 20f, 300f))
        val (u, v) = map.playerUv()!!
        assertEquals(.6f, u, .00001f); assertEquals(.25f, v, .00001f)
        val b = frame().apply {
            putInt(256, 1); putInt(260, 2); putInt(264, 2048); putInt(268, 2048)
            map.texture.toByteArray().forEachIndexed { i, value -> put(272 + i, value) }
            matrix.forEachIndexed { i, value -> putFloat(336 + i * 4, value) }
            map.position.forEachIndexed { i, value -> putFloat(400 + i * 4, value) }
        }
        assertEquals(map, SmoSnapshot.decode(b, SmoSnapshot.BYTES)!!.map)
        b.putFloat(336, Float.NaN)
        assertNull(SmoSnapshot.decode(b, SmoSnapshot.BYTES))
    }

    @Test fun pinchKeepsTheSameMapPointUnderTheFinger() {
        val viewport = SmoMapViewport().apply { resize(720f, 720f); scale(2f, 360f, 360f) }
        val x = 410f; val y = 450f
        val u = (x - viewport.left) / viewport.extent
        val v = (y - viewport.top) / viewport.extent
        viewport.scale(1.4f, x, y)
        val point = viewport.point(u, v)
        assertEquals(x, point.first, .001f); assertEquals(y, point.second, .001f)
    }

    @Test fun repeatedPanAndZoomCannotLoseTheMapOrProduceNonFiniteValues() {
        val v = SmoMapViewport().apply { resize(1184f, 720f) }
        repeat(100) {
            v.scale(1.2f, 600f, 380f); v.drag(100000f, -100000f)
            if (v.extent >= v.width) {
                assertTrue(v.left <= 0f); assertTrue(v.left + v.extent >= v.width)
            } else assertEquals((v.width - v.extent) / 2, v.left, .001f)
            assertTrue(v.top <= 0f); assertTrue(v.top + v.extent >= v.height)
        }
        assertEquals(5f, v.zoom, .001f)
        v.scale(Float.NaN, 0f, 0f); v.drag(Float.POSITIVE_INFINITY, 0f)
        assertTrue(v.left.isFinite())
        v.reset()
        assertEquals(1f, v.zoom, .001f)
        assertEquals(0f, v.panX, .001f)
    }

    @Test fun messageDecoderRejectsUntrustedLengthsWithoutThrowing() {
        assertTrue(SmoMessages.decode(ByteArray(0)).isEmpty())
        val bytes = ByteArray(64)
        "MsgStdBn".toByteArray().copyInto(bytes)
        bytes[8] = 0xff.toByte(); bytes[9] = 0xfe.toByte()
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(18, bytes.size); putShort(14, 1); putInt(36, Int.MAX_VALUE)
        }
        assertTrue(SmoMessages.decode(bytes).isEmpty())
    }

    @Test fun moonPacketsRejectHiddenNamesAndDuplicateIdentities() {
        val b = ByteBuffer.allocateDirect(SmoMoons.BYTES).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0, 0x4d4f4d53); putInt(4, 1); putInt(8, SmoMoons.BYTES)
            putInt(12, 2); putLong(16, 7); putInt(24, 2); putInt(28, 1)
            putInt(32, 7); putInt(36, 1); putInt(40, 3)
            putInt(176, 8); putInt(180, 2); putInt(184, 0)
            "SandWorldHomeStage".toByteArray().forEachIndexed { i, v -> put(48 + i, v) }
            "Obj123".toByteArray().forEachIndexed { i, v -> put(112 + i, v) }
        }
        val moons = SmoMoons.decode(b, SmoMoons.BYTES)!!
        assertEquals(2, moons.entries.size)
        assertEquals("ScenarioName_Obj123", moons.entries[0].messageKey)
        assertTrue(moons.entries[0].known); assertFalse(moons.entries[1].known)
        b.put(192, 65.toByte())
        assertNull(SmoMoons.decode(b, SmoMoons.BYTES))
        b.put(192, 0.toByte()); b.putInt(176, 7)
        assertNull(SmoMoons.decode(b, SmoMoons.BYTES))
    }
}
