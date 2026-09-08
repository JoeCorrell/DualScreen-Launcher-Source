package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class XcxMapSegmentsTest {
    private val field = XcxMapSegments.Field(16, 1401, -1903f, 6697f, 56.45f)
    private val catalog = XcxMapSegments(listOf(field), emptyMap())

    @Test fun nativeStaggeredColumnsIncludeNegativeCoordinates() {
        val even = field.position(2, 3)
        val odd = field.position(3, 3)
        val negative = field.position(-1, -1)
        assertEquals(-1818.325f, even.first, .001f)
        assertEquals(6866.35f, even.second, .001f)
        assertEquals(28.225f, odd.second - even.second, .001f)
        assertEquals(6668.775f, negative.second, .001f)
    }

    private fun record(kind: Int = 2): ByteArray = ByteBuffer.allocate(48)
        .order(ByteOrder.LITTLE_ENDIAN).apply {
            put(0, (-1).toByte()); put(1, 3)
            putInt(4, 16); putShort(8, 3813); put(10, kind.toByte())
            putShort(12, 359); putShort(18, 589); put(33, 63)
            putInt(44, 1)
        }.array()

    @Test fun recordUsesSeparateDiscoveryAndTwoBitCompletionFlags() {
        val cell = catalog.cells(field, record())!!.single()
        assertEquals(-1, cell.column)
        assertEquals(3813, cell.openFlag)
        assertEquals(589, cell.stateFlag)
        assertEquals(359, cell.id)
        assertFalse(cell.open)
        assertFalse(cell.complete)
        assertTrue(catalog.cells(field, record(4))!!.isEmpty())
    }

    @Test fun truncatedCrossFieldAndOversizedSnapshotsAreRejected() {
        val good = record()
        for (size in 1 until good.size) assertNull(catalog.cells(field, good.copyOf(size)))
        assertNull(catalog.cells(field, good + byteArrayOf(0)))
        assertNull(catalog.cells(field, ByteArray(129 * 48)))
        assertNull(catalog.cells(field, record(0)))
        ByteBuffer.wrap(good).order(ByteOrder.LITTLE_ENDIAN).putInt(4, 3)
        assertNull(catalog.cells(field, good))
    }

    @Test fun installedProbePredicateIsIndependentOfObjectiveCompletion() {
        val bytes = record(1)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(36, 1)
        val cell = catalog.cells(field, bytes)!!.single()
        assertTrue(cell.probeInstalled)
        assertFalse(cell.complete)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(36, 39).putShort(16, 143)
        assertEquals(39, catalog.cells(field, bytes)!!.single().probeItem)
        assertEquals(143, catalog.cells(field, bytes)!!.single().probeId)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(36, 40)
        assertNull(catalog.cells(field, bytes))
    }

    @Test fun probeVisibilityFollowsNativeStoryPredicateIndependentlyOfInstallation() {
        val bytes = record(1)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(36, 1).putInt(44, 3)
        val hidden = catalog.cells(field, bytes)!!.single()
        assertTrue(hidden.probeInstalled)
        assertFalse(hidden.probeVisible)
        b.putInt(44, 1)
        assertTrue(catalog.cells(field, bytes)!!.single().probeVisible)
        b.putInt(44, 0)
        assertFalse(catalog.cells(field, bytes)!!.single().probeVisible)
        b.putInt(44, 8)
        assertNull(catalog.cells(field, bytes))
    }

    @Test fun relatedTipBadgeIsIndependentOfCompletionAndProbeVisibility() {
        val bytes = record(2)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(44, 5).putShort(28, 317)
        val cell = catalog.cells(field, bytes)!!.single()
        assertTrue(cell.relatedVisible)
        assertEquals(317, cell.relatedId)
        assertFalse(cell.complete)
    }

    @Test fun missionLinkUsesLiveContentReferenceAndNativeParentMapping() {
        val bytes = record(2)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(40, 21)
        val definitions = XcxMapSegments(listOf(field), emptyMap(), mapOf(21 to 7))
        assertEquals(7, definitions.cells(field, bytes)!!.single().questId)
        buffer.putInt(40, 2559)
        assertNull(definitions.cells(field, bytes))
        buffer.putInt(40, 21).put(10, 1)
        assertNull(definitions.cells(field, bytes)) // Probe IDs are never quest references.
    }

    @Test fun catalogCarriesQuestParentsAndRejectsOldOrTruncatedPackets() {
        val bytes = ByteBuffer.allocate(60).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0x4C475358).putInt(3).putInt(1)
            putInt(16).putInt(1401).putFloat(-1903f).putFloat(6697f).putFloat(56.45f)
            putInt(1).putInt(359).putInt(1)
            putInt(1).putInt(21).putInt(7)
            putInt(0)
        }.array()
        assertEquals(7, XcxMapSegments.parse(bytes)!!.questParents[21])
        for (size in 0 until bytes.size) assertNull(XcxMapSegments.parse(bytes.copyOf(size)))
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(4, 1)
        assertNull(XcxMapSegments.parse(bytes))
    }

    @Test fun segmentDescriptionsUseContentIdsAndPreserveNativeParagraphs() {
        val b = ByteBuffer.allocate(512).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x4C475358).putInt(3).putInt(1)
            .putInt(16).putInt(1401).putFloat(-1903f).putFloat(6697f).putFloat(56.45f)
            .putInt(1).putInt(359).putInt(4).putInt(0)
            .putInt(1).putInt(359)
        fun text(value: String) { val encoded = value.toByteArray(); b.putInt(encoded.size).put(encoded) }
        text("Heart-to-Heart"); text("Name: Celica\nAffinity: 5\nAppears: Night")
        val bytes = b.array().copyOf(b.position())
        val parsed = XcxMapSegments.parse(bytes)!!
        assertEquals("Heart-to-Heart", parsed.details[359]!!.title)
        assertEquals("Name: Celica\nAffinity: 5\nAppears: Night", parsed.details[359]!!.description)
        assertNull(parsed.details[21]) // A quest reference is not a segment-content identity.
        for (size in 0 until bytes.size) assertNull(XcxMapSegments.parse(bytes.copyOf(size)))
        for ((offset, value) in listOf(48 to 8193, 52 to 360, 56 to -1, 56 to 16385)) {
            val bad = bytes.copyOf()
            ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(offset, value)
            assertNull("offset $offset", XcxMapSegments.parse(bad))
        }
        assertNull(XcxMapSegments.parse(bytes + byteArrayOf(0)))
    }
}
