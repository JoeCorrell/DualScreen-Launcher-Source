package com.wemu.engine.winhandler;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import java.io.IOException;
import java.net.DatagramSocket;

import org.junit.Test;

public class WinHandlerSocketTest {
    @Test
    public void guestBridgePortCannotBeSharedByTwoSessions() throws Exception {
        try (DatagramSocket first = WinHandler.bindExclusiveUdpSocket(0)) {
            assertFalse(first.getReuseAddress());
            assertThrows(IOException.class,
                    () -> WinHandler.bindExclusiveUdpSocket(first.getLocalPort()));
        }
    }
}
