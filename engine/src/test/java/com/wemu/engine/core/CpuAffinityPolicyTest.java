package com.wemu.engine.core;

import org.junit.Test;
import static org.junit.Assert.*;

public class CpuAffinityPolicyTest {
    @Test public void thorPerformanceCoresHaveOneConsistentTopology() {
        assertEquals(0xf8, CpuAffinityPolicy.mask("3,4,5,6,7"));
        assertEquals("5:3,4,5,6,7", CpuAffinityPolicy.topology("7, 6,5,4,3,7"));
        assertEquals("0,31", CpuAffinityPolicy.list(0x80000001));
    }
    @Test public void malformedAndSingleCorePinsAreRejected() {
        for (String value : new String[]{null,"","7","7,7","-1,3","3,32","3,x","3,4,","3,,4","3,+4"}) {
            assertEquals(value, 0, CpuAffinityPolicy.mask(value));
            assertEquals(value, "", CpuAffinityPolicy.topology(value));
        }
    }
}
