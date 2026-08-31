package com.wemu.engine.xserver.requests;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.fail;

import com.wemu.engine.xserver.WindowManager;
import com.wemu.engine.xserver.errors.BadValue;
import com.wemu.engine.xserver.errors.BadWindow;

import java.util.HashMap;
import java.util.Map;

import org.junit.Test;

public class InputFocusTargetResolverTest {
    private final Object root = new Object();
    private final Map<Integer, Object> windows = new HashMap<>();

    @Test
    public void noneResolvesToNoWindowWithoutLookup() throws Exception {
        assertNull(InputFocusTargetResolver.resolve(0, root, id -> {
            fail("None must not be looked up as a resource id");
            return null;
        }));
    }

    @Test
    public void pointerRootResolvesToCurrentRootWithoutLookup() throws Exception {
        assertSame(root, InputFocusTargetResolver.resolve(1, root, id -> {
            fail("PointerRoot must not be looked up as a resource id");
            return null;
        }));
    }

    @Test
    public void ordinaryResourceIdResolvesThroughWindowManagerLookup() throws Exception {
        Object expected = new Object();
        windows.put(0x10203040, expected);

        assertSame(
                expected,
                InputFocusTargetResolver.resolve(0x10203040, root, windows::get));
    }

    @Test
    public void missingOrdinaryResourceIdRaisesBadWindowWithRequestedId() throws Exception {
        int requestedId = 0x10203040;
        try {
            InputFocusTargetResolver.resolve(requestedId, root, windows::get);
            fail("A missing resource id must fail the SetInputFocus request");
        } catch (BadWindow expected) {
            assertEquals(3, expected.getCode());
            assertEquals(requestedId, expected.getData());
        }
    }

    @Test
    public void revertPolicyIsIndependentFromFocusTarget() throws Exception {
        assertSame(
                WindowManager.FocusRevertTo.NONE,
                WindowRequests.resolveFocusRevertTo((byte) 0));
        assertSame(
                WindowManager.FocusRevertTo.POINTER_ROOT,
                WindowRequests.resolveFocusRevertTo((byte) 1));
        assertSame(
                WindowManager.FocusRevertTo.PARENT,
                WindowRequests.resolveFocusRevertTo((byte) 2));
    }

    @Test
    public void invalidUnsignedRevertPolicyRaisesBadValue() throws Exception {
        try {
            WindowRequests.resolveFocusRevertTo((byte) 0xff);
            fail("An invalid revert-to value must fail the SetInputFocus request");
        } catch (BadValue expected) {
            assertEquals(2, expected.getCode());
            assertEquals(255, expected.getData());
        }
    }
}
