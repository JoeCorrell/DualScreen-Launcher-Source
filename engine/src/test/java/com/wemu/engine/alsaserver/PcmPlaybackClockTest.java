package com.wemu.engine.alsaserver;

import org.junit.Test;
import static org.junit.Assert.*;

public class PcmPlaybackClockTest {
    @Test public void queuedAudioIsNotReportedAsPlayed() {
        PcmPlaybackClock c = new PcmPlaybackClock();
        c.accept(1920);
        assertEquals(0, c.position(1920));
        assertEquals(480, c.position(1440));
        c.accept(480);
        assertEquals(480, c.position(1920));
        assertEquals(2400, c.position(0));
    }
    @Test public void partialWriteRecoveryDoesNotReplayOrRegress() {
        PcmPlaybackClock c = new PcmPlaybackClock();
        c.accept(100); c.accept(20); c.accept(-1);
        assertEquals(30, c.position(90));
        c.discardPending();
        c.accept(80);
        assertEquals(120, c.position(80));
        assertEquals(200, c.position(0));
        assertEquals(200, c.accepted());
    }
    @Test public void invalidOrStaleNativeReadingCannotMoveCursorBackward() {
        PcmPlaybackClock c = new PcmPlaybackClock(); c.accept(400);
        assertEquals(100, c.position(300));
        assertEquals(100, c.position(-1));
        assertEquals(100, c.position(600));
        assertEquals(400, c.position(0));
        c.reset(); assertEquals(0, c.position(0)); assertEquals(0, c.accepted());
    }
    @Test public void longSessionsKeepAnUnsignedWireCursor() {
        PcmPlaybackClock c = new PcmPlaybackClock();
        c.accept(Integer.MAX_VALUE); c.accept(1920);
        assertEquals(2147485567L, c.position(0));
        assertEquals(2147485567L, Integer.toUnsignedLong((int)c.position(0)));
    }
}
