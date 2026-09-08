package com.wemu.engine.alsaserver;

import org.junit.Test;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

public class PcmWritePumpTest {
    private static class Sink extends PcmWritePump {
        final int[] results;
        final List<Byte> received = new ArrayList<>();
        int calls, recoveries;
        boolean playing = true;
        boolean recoverable = true;
        int stopAfter = Integer.MAX_VALUE;
        Sink(int... results) { this.results = results; }
        @Override int write(ByteBuffer data, int offset, int frames) {
            int count = calls < results.length ? results[calls] : frames;
            ++calls;
            if (count > 0 && count <= frames)
                for (int i = 0; i < count * 4; ++i) received.add(data.get(offset + i));
            if (calls >= stopAfter) playing = false;
            return count;
        }
        @Override boolean recover(int error) { ++recoveries; return recoverable; }
        @Override boolean isPlaying() { return playing; }
    }
    private static ByteBuffer packet() {
        ByteBuffer data = ByteBuffer.allocateDirect(48);
        for (int i = 0; i < 48; ++i) data.put((byte)i);
        data.position(8);
        data.limit(40);
        return data;
    }
    private static void assertSequence(Sink sink, int byteCount) {
        assertEquals(byteCount, sink.received.size());
        for (int i = 0; i < byteCount; ++i) assertEquals((byte)(i + 8), (byte)sink.received.get(i));
    }
    @Test public void partialWritesPreserveEverySampleAndBufferPosition() {
        Sink sink = new Sink(2, 1, 3, 2);
        ByteBuffer data = packet();
        assertEquals(8, sink.transfer(data, 4));
        assertSequence(sink, 32);
        assertEquals(3, sink.partialWrites);
        assertEquals(8, data.position());
        assertEquals(40, data.limit());
    }
    @Test public void routeRecoveryRetriesOnlyUnwrittenTail() {
        Sink sink = new Sink(2, -899, 6);
        assertEquals(8, sink.transfer(packet(), 4));
        assertSequence(sink, 32);
        assertEquals(1, sink.recoveries);
    }
    @Test public void transientTimeoutDoesNotDropPacket() {
        Sink sink = new Sink(2, 0, 0, 6);
        assertEquals(8, sink.transfer(packet(), 4));
        assertSequence(sink, 32);
        assertEquals(2, sink.timeouts);
    }
    @Test public void permanentlyStalledOutputHasBoundedRetries() {
        Sink sink = new Sink(2, 0, 0, 0, 6);
        assertEquals(2, sink.transfer(packet(), 4));
        assertEquals(4, sink.calls);
        assertSequence(sink, 8);
    }
    @Test public void closingStreamCancelsTailBeforeNextNativeWrite() {
        Sink sink = new Sink(2, 6);
        sink.stopAfter = 1;
        assertEquals(2, sink.transfer(packet(), 4));
        assertEquals(1, sink.calls);
    }
    @Test public void secondDisconnectDoesNotReopenForever() {
        Sink sink = new Sink(2, -899, 1, -899);
        assertEquals(3, sink.transfer(packet(), 4));
        assertEquals(1, sink.recoveries);
        assertSequence(sink, 12);
    }
    @Test public void recoveryFailureStopsWithoutReplaying() {
        Sink sink = new Sink(2, -899);
        sink.recoverable = false;
        assertEquals(2, sink.transfer(packet(), 4));
        assertSequence(sink, 8);
    }
    @Test public void invalidBuffersNeverReachNative() {
        Sink sink = new Sink();
        assertEquals(0, sink.transfer(ByteBuffer.allocate(32), 4));
        ByteBuffer data = packet();
        data.limit(39);
        assertEquals(0, sink.transfer(data, 4));
        assertEquals(0, sink.transfer(null, 4));
        assertEquals(0, sink.calls);
    }
}
