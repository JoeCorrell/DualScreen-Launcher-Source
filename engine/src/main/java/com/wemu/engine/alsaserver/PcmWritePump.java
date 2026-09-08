package com.wemu.engine.alsaserver;

import java.nio.ByteBuffer;

/** Completes a PCM packet before the connector consumes the next packet. */
abstract class PcmWritePump {
    private static final int MAX_TIMEOUTS = 3;
    int partialWrites;
    int timeouts;
    int error;

    abstract int write(ByteBuffer data, int byteOffset, int frames);
    abstract boolean recover(int error);
    abstract boolean isPlaying();

    final int transfer(ByteBuffer data, int frameBytes) {
        partialWrites = timeouts = error = 0;
        if (data == null || !data.isDirect() || frameBytes <= 0
                || data.remaining() % frameBytes != 0) return 0;
        final int frames = data.remaining() / frameBytes;
        final int start = data.position();
        int accepted = 0;
        boolean recovered = false;
        while (accepted < frames && isPlaying()) {
            int remaining = frames - accepted;
            int result = write(data, start + accepted * frameBytes, remaining);
            if (result > remaining) {
                error = -1; // A faulty sink must never advance outside this packet.
                break;
            }
            if (result > 0) {
                if (result < remaining) ++partialWrites;
                accepted += result;
            } else if (result == 0) {
                // Each native write already waits up to 100 ms. Bound a stalled
                // device without spinning or holding teardown indefinitely.
                if (++timeouts >= MAX_TIMEOUTS) break;
            } else {
                error = result;
                if (recovered || !isPlaying() || !recover(result)) break;
                recovered = true;
                // Retry only the tail: a route change must not replay samples
                // already accepted by the previous stream.
            }
        }
        return accepted;
    }
}
