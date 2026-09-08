package com.wemu.engine.alsaserver;

/** ALSA's hardware cursor counts consumed frames, not frames queued by write(). */
final class PcmPlaybackClock {
    private long accepted;
    private long consumed;

    void reset() { accepted = consumed = 0; }
    void accept(int frames) { if (frames > 0) accepted += frames; }
    long accepted() { return accepted; }
    long position(long pending) {
        if (pending >= 0) consumed = Math.max(consumed, accepted - Math.min(accepted, pending));
        return consumed;
    }
    // A disconnected/closed stream cannot play its queued tail. Account for
    // that discontinuity once; never resend samples already accepted by it.
    void discardPending() { consumed = accepted; }
}
