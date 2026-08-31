package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class StardewReferenceBundlesTest {
    @Test
    fun `identical bundle requirement stacks receive unique lazy keys`() {
        val wood = StardewBundleItem("(O)388", "Wood", count = 99)

        assertNotEquals(bundleItemKey(0, wood), bundleItemKey(1, wood))
        assertEquals("0:(O)388:Wood:99:0", bundleItemKey(0, wood))
    }

    @Test
    fun `duplicate bundle names receive unique lazy keys`() {
        val bundle = StardewBundle("Crafts Room", "Construction", have = 0, required = 4)

        assertNotEquals(
            bundleRowKey("Crafts Room", 0, bundle),
            bundleRowKey("Crafts Room", 1, bundle),
        )
    }
}
