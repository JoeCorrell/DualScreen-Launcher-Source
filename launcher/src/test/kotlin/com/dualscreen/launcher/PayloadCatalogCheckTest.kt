package com.dualscreen.launcher

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PayloadCatalogCheckTest {
    @Test fun firstAppStartupChecksEvenImmediatelyAfterDeviceBoot() {
        assertTrue(shouldCheckPayloadCatalog(false, 0, 10, 60_000))
    }
    @Test fun subsequentRefreshesRespectCooldown() {
        assertFalse(shouldCheckPayloadCatalog(false, 10, 20, 60_000))
        assertTrue(shouldCheckPayloadCatalog(false, 10, 60_010, 60_000))
    }
    @Test fun explicitRetryAlwaysChecks() {
        assertTrue(shouldCheckPayloadCatalog(true, 10, 20, 60_000))
    }
}
