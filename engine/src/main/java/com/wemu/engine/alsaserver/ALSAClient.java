package com.wemu.engine.alsaserver;

import com.wemu.engine.sysvshm.SysVSharedMemory;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import android.util.Log;

public class ALSAClient {
    private static final String TAG = "WemuAudio";
    public enum DataType {
        U8(1), S16LE(2), S16BE(2), FLOATLE(4), FLOATBE(4);
        public final byte byteCount;

        DataType(int byteCount) {
            this.byteCount = (byte)byteCount;
        }
    }
    private DataType dataType = DataType.U8;
    private byte channelCount = 2;
    private int sampleRate = 0;
    private int position;
    private int bufferSize;
    private int frameBytes;
    private ByteBuffer sharedBuffer;
    private ByteBuffer conversionBuffer;
    private volatile boolean playing = false;
    private volatile boolean releasing;
    private long streamPtr = 0;
    private final boolean burstAligned;
    private volatile float linearGain = 1.0f;
    private final AudioGain.LoudnessState loudnessState = new AudioGain.LoudnessState();
    private boolean loudnessActive;
    private long lastRecoveryLogMillis;
    private long lastWriteLogMillis;
    private long writePackets, partialWrites, writeTimeouts, droppedFrames;
    private final PcmWritePump writePump = new PcmWritePump() {
        @Override int write(ByteBuffer data, int byteOffset, int frames) {
            int accepted = ALSAClient.this.write(streamPtr, data, byteOffset, frames);
            if (accepted > 0) position += accepted;
            return accepted;
        }
        @Override boolean recover(int error) { return recoverStreamLocked(error); }
        @Override boolean isPlaying() { return playing && !releasing; }
    };

    public ALSAClient() {
        this(false);
    }

    public ALSAClient(boolean burstAligned) {
        this.burstAligned = burstAligned;
    }

    public ALSAClient(boolean burstAligned, float gainDb) {
        this.burstAligned = burstAligned;
        setGainDb(gainDb);
    }

    public void setGainDb(float gainDb) {
        linearGain = AudioGain.linearFromDb(gainDb);
    }

    static {
        System.loadLibrary("wemu");
    }

    /**
     * Guards the stream pointer against being closed while it is in use.
     *
     * The guest's audio client and the session's teardown are different threads:
     * writes arrive on the connector's epoll thread, and `release` is called
     * when the session ends. Nothing held them apart, so leaving a container
     * crashed the app — the epoll thread passed the `playing` check, teardown
     * closed the AAudio stream underneath it, and the write went into freed
     * memory:
     *
     * <pre>
     *   ALSAClient.writeDataToStream -> AAudioStream_write
     *     -> AudioTrack::write -> SIGSEGV
     * </pre>
     *
     * Holding this across the write means `release` waits for one buffer to
     * finish rather than pulling the stream out from under it. That wait is
     * bounded by the buffer duration — milliseconds — and `playing` is cleared
     * first so writes queued behind it give up instead of taking their turn.
     */
    private final Object streamLock = new Object();

    public void release() {
        // Before the lock, so a client mid-flood stops trying rather than
        // queueing up behind the close.
        releasing = true;
        playing = false;

        synchronized (streamLock) {
            if (sharedBuffer != null) {
                SysVSharedMemory.unmapSHMSegment(sharedBuffer, sharedBuffer.capacity());
                sharedBuffer = null;
            }
            conversionBuffer = null;

            // Only when there is one. `prepare` calls this before creating a
            // stream, so on the first pass there is nothing to stop or close.
            if (streamPtr != 0) {
                stop(streamPtr);
                close(streamPtr);
            }
            streamPtr = 0;
        }
    }

    public void prepare() {
        writePackets = partialWrites = writeTimeouts = droppedFrames = 0;
        lastWriteLogMillis = android.os.SystemClock.elapsedRealtime();
        frameBytes = channelCount * dataType.byteCount;
        release();

        if (!isValidBufferSize()) return;

        synchronized (streamLock) {
            position = 0;
            releasing = false;
            streamPtr = create(
                    dataType.ordinal(), channelCount, sampleRate, bufferSize, burstAligned);
        }
        // Native pointers are opaque bits; Android may tag the high byte, which
        // makes a valid pointer negative as a Java long.
        if (streamPtr != 0) start();
    }

    public void start() {
        synchronized (streamLock) {
            if (streamPtr != 0 && !playing && !releasing) {
                start(streamPtr);
                playing = true;
            }
        }
    }

    public void stop() {
        synchronized (streamLock) {
            if (streamPtr != 0 && playing) {
                stop(streamPtr);
                playing = false;
            }
        }
    }

    public void pause() {
        synchronized (streamLock) {
            if (streamPtr != 0) {
                pause(streamPtr);
                playing = false;
            }
        }
    }

    public void drain() {
        synchronized (streamLock) {
            if (streamPtr != 0) flush(streamPtr);
        }
    }

    public void writeDataToStream(ByteBuffer data) {
        if (data == null || !data.isDirect() || frameBytes <= 0) return;
        if (dataType == DataType.S16LE || dataType == DataType.FLOATLE) {
            data.order(ByteOrder.LITTLE_ENDIAN);
        }
        else if (dataType == DataType.S16BE || dataType == DataType.FLOATBE) {
            data.order(ByteOrder.BIG_ENDIAN);
        }

        // Checked twice deliberately: once cheaply so a client still streaming
        // into a closing session gives up without contending for the lock, and
        // once inside it, because only there is the pointer known to still be
        // open when it is handed to native code.
        if (!playing) return;

        synchronized (streamLock) {
            if (!playing || streamPtr == 0) return;

            int byteCount = data.remaining();
            if (byteCount <= 0 || byteCount % frameBytes != 0) {
                data.rewind();
                return;
            }
            int numFrames = byteCount / frameBytes;
            ByteBuffer output = convertForAAudio(data, byteCount);
            int outputFrameBytes = dataType == DataType.U8 ? frameBytes * 2 : frameBytes;
            int framesWritten = writePump.transfer(output, outputFrameBytes);
            ++writePackets;
            partialWrites += writePump.partialWrites;
            writeTimeouts += writePump.timeouts;
            droppedFrames += numFrames - framesWritten;
            long now = android.os.SystemClock.elapsedRealtime();
            if (now - lastWriteLogMillis >= 10000L) {
                Log.i(TAG, "PCM packets=" + writePackets + " partialWrites=" + partialWrites
                        + " timeouts=" + writeTimeouts + " droppedFrames=" + droppedFrames
                        + " xruns=" + (streamPtr != 0 ? getXRunCount(streamPtr) : -1)
                        + " accepted=" + Integer.toUnsignedLong(position));
                lastWriteLogMillis = now;
            }
            data.rewind();
        }
    }

    /**
     * AAudio streams are permanently unusable after a route disconnect. Wine
     * continues feeding the same ALSA client, so dropping the error (the old
     * behavior) made silence permanent. Recreate once and retry the buffer.
     */
    private boolean recoverStreamLocked(int error) {
        if (releasing) return false;
        long now = android.os.SystemClock.elapsedRealtime();
        if (now - lastRecoveryLogMillis >= 1000L) {
            Log.w(TAG, "AAudio write failed (" + error + "); reopening guest stream "
                    + sampleRate + " Hz, " + Byte.toUnsignedInt(channelCount) + " channels");
            lastRecoveryLogMillis = now;
        }
        playing = false;
        if (streamPtr != 0) close(streamPtr);
        streamPtr = create(dataType.ordinal(), channelCount, sampleRate, bufferSize, burstAligned);
        if (streamPtr == 0) return false;
        if (releasing) return false; // release owns the close after this write unlocks.
        start(streamPtr);
        playing = true;
        return true;
    }

    public int pointer() {
        synchronized (streamLock) {
            // Compatibility rollback: the bundled guest fills a 1920-frame
            // ring while AudioTrack can require 1922 frames to start. Waiting
            // for the consumed cursor here can stop both sides permanently.
            // Restore the pre-regression accepted-write cursor. A consumed
            // cursor must not be re-enabled without guest-ring startup tests.
            return position;
        }
    }

    public void setDataType(DataType dataType) {
        this.dataType = dataType;
    }

    public void setChannelCount(int channelCount) {
        this.channelCount = (byte)channelCount;
    }

    public void setSampleRate(int sampleRate) {
        this.sampleRate = sampleRate;
    }

    public void setBufferSize(int bufferSize) {
        this.bufferSize = bufferSize;
    }

    public ByteBuffer getSharedBuffer() {
        return sharedBuffer;
    }

    public void setSharedBuffer(ByteBuffer sharedBuffer) {
        this.sharedBuffer = sharedBuffer;
    }

    public DataType getDataType() {
        return dataType;
    }

    public byte getChannelCount() {
        return channelCount;
    }

    public int getSampleRate() {
        return sampleRate;
    }

    public int getBufferSize() {
        return bufferSize;
    }

    public int getBufferSizeInBytes() {
        return bufferSize * frameBytes;
    }

    private boolean isValidBufferSize() {
        long bytes = (long)bufferSize * frameBytes;
        return frameBytes > 0 && bufferSize > 0 && sampleRate > 0
                && bytes > 0 && bytes <= Integer.MAX_VALUE;
    }

    public int computeLatencyMillis() {
        return sampleRate > 0 ? (int)(((float)bufferSize / sampleRate) * 1000) : 0;
    }

    /** Converts formats AAudio cannot consume directly into native-endian PCM. */
    private ByteBuffer convertForAAudio(ByteBuffer source, int byteCount) {
        final float gain = linearGain;
        final boolean applyLoudness = gain > 1.0f;
        if (applyLoudness && !loudnessActive) {
            loudnessState.reset();
            loudnessActive = true;
        } else if (!applyLoudness && loudnessActive) {
            loudnessState.reset();
            loudnessActive = false;
        }
        if (gain == 1.0f &&
                (dataType == DataType.S16LE || dataType == DataType.FLOATLE)) return source;

        final int outputBytes = dataType == DataType.U8 ? byteCount * 2 : byteCount;
        if (outputBytes < 0) return null;
        if (conversionBuffer == null || conversionBuffer.capacity() < outputBytes) {
            conversionBuffer = ByteBuffer.allocateDirect(outputBytes).order(ByteOrder.LITTLE_ENDIAN);
        }
        conversionBuffer.clear();

        ByteBuffer input = source.duplicate();
        final int channels = Byte.toUnsignedInt(channelCount);
        if (channels <= 0) return null;
        if (dataType == DataType.U8) {
            while (input.remaining() >= channels) {
                int frameStart = input.position();
                float peak = 0.0f;
                for (int channel = 0; channel < channels; ++channel) {
                    short sample = (short)(((input.get(frameStart + channel) & 0xff) - 128) << 8);
                    peak = Math.max(peak, AudioGain.absolute(sample));
                }
                float frameGain = applyLoudness
                        ? loudnessState.gainForFrame(peak, sampleRate, gain) : gain;
                for (int channel = 0; channel < channels; ++channel) {
                    short sample = (short)(((input.get(frameStart + channel) & 0xff) - 128) << 8);
                    conversionBuffer.putShort(AudioGain.amplify(sample, frameGain));
                }
                input.position(frameStart + channels);
            }
        } else if (dataType == DataType.S16LE || dataType == DataType.S16BE) {
            input.order(dataType == DataType.S16LE
                    ? ByteOrder.LITTLE_ENDIAN : ByteOrder.BIG_ENDIAN);
            int frameSize = channels * Short.BYTES;
            while (input.remaining() >= frameSize) {
                int frameStart = input.position();
                float peak = 0.0f;
                for (int channel = 0; channel < channels; ++channel) {
                    peak = Math.max(peak,
                            AudioGain.absolute(input.getShort(frameStart + channel * Short.BYTES)));
                }
                float frameGain = applyLoudness
                        ? loudnessState.gainForFrame(peak, sampleRate, gain) : gain;
                for (int channel = 0; channel < channels; ++channel) {
                    short sample = input.getShort(frameStart + channel * Short.BYTES);
                    conversionBuffer.putShort(AudioGain.amplify(sample, frameGain));
                }
                input.position(frameStart + frameSize);
            }
        } else if (dataType == DataType.FLOATLE || dataType == DataType.FLOATBE) {
            input.order(dataType == DataType.FLOATLE
                    ? ByteOrder.LITTLE_ENDIAN : ByteOrder.BIG_ENDIAN);
            int frameSize = channels * Float.BYTES;
            while (input.remaining() >= frameSize) {
                int frameStart = input.position();
                float peak = 0.0f;
                for (int channel = 0; channel < channels; ++channel) {
                    peak = Math.max(peak,
                            AudioGain.absolute(input.getFloat(frameStart + channel * Float.BYTES)));
                }
                float frameGain = applyLoudness
                        ? loudnessState.gainForFrame(peak, sampleRate, gain) : gain;
                for (int channel = 0; channel < channels; ++channel) {
                    float sample = input.getFloat(frameStart + channel * Float.BYTES);
                    conversionBuffer.putFloat(AudioGain.amplify(sample, frameGain));
                }
                input.position(frameStart + frameSize);
            }
        }
        conversionBuffer.flip();
        return conversionBuffer;
    }

    private native long create(int format, byte channelCount, int sampleRate, int bufferSize,
                               boolean burstAligned);

    private native int write(long streamPtr, ByteBuffer buffer, int byteOffset, int numFrames);

    private native int getXRunCount(long streamPtr);
    // Diagnostic queue depth only; not the guest's compatibility cursor.
    private native long getPendingFrames(long streamPtr);

    private native void start(long streamPtr);

    private native void stop(long streamPtr);

    private native void pause(long streamPtr);

    private native void flush(long streamPtr);

    private native void close(long streamPtr);
}
