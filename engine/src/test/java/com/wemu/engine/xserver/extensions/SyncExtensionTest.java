package com.wemu.engine.xserver.extensions;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.wemu.engine.xserver.errors.BadFence;

import org.junit.Test;

public class SyncExtensionTest {
    @Test
    public void awaitRequiresEveryFenceToBeTriggered() throws BadFence {
        int[] ids = {11, 22, 33};

        assertFalse(SyncExtension.areAllFencesTriggered(
                ids,
                ignored -> true,
                id -> id != 22));
        assertTrue(SyncExtension.areAllFencesTriggered(
                ids,
                ignored -> true,
                ignored -> true));
    }

    @Test
    public void invalidFenceIsNotHiddenByEarlierUntriggeredFence() {
        try {
            SyncExtension.areAllFencesTriggered(
                    new int[]{11, 22},
                    id -> id == 11,
                    ignored -> false);
            fail("Expected BadFence");
        } catch (BadFence error) {
            assertEquals(22, error.getData());
        }
    }
}
