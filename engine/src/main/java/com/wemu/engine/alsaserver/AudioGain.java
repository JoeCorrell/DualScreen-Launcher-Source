package com.wemu.engine.alsaserver;

/** Small, bounded PCM-gain helpers shared by the live ALSA clients and tests. */
final class AudioGain {
    // More than this is no longer a useful volume correction; it is mostly
    // compression. Skyrim currently requests +6 dB.
    static final float MAX_DB = 9.0f;

    private AudioGain() {}

    /**
     * Lightweight upward compressor used for Skyrim's loudness-equalization
     * preset. It tracks one peak envelope per interleaved frame so every channel
     * receives the same gain and the stereo image cannot wander.
     */
    static final class LoudnessState {
        private static final float TARGET_PEAK = 0.18f;
        private static final float MAX_EXTRA_GAIN = 2.0f; // +6 dB above makeup gain
        private float envelope;
        private float extraGain = MAX_EXTRA_GAIN;
        private int configuredRate;
        private float envelopeAttack;
        private float envelopeRelease;
        private float gainAttack;
        private float gainRelease;

        void reset() {
            envelope = 0.0f;
            extraGain = MAX_EXTRA_GAIN;
        }

        float gainForFrame(float framePeak, int sampleRate, float makeupGain) {
            if (!Float.isFinite(framePeak)) framePeak = 0.0f;
            framePeak = Math.max(0.0f, Math.min(1.0f, framePeak));
            configure(sampleRate);

            float envelopeCoefficient = framePeak > envelope
                    ? envelopeAttack : envelopeRelease;
            envelope = envelopeCoefficient * envelope
                    + (1.0f - envelopeCoefficient) * framePeak;

            float desiredExtra = TARGET_PEAK / Math.max(envelope, 0.001f);
            desiredExtra = Math.max(1.0f, Math.min(MAX_EXTRA_GAIN, desiredExtra));
            // Reduce gain quickly when loud material arrives; restore it slowly
            // enough that dialogue pauses do not produce audible pumping.
            float gainCoefficient = desiredExtra < extraGain ? gainAttack : gainRelease;
            extraGain = gainCoefficient * extraGain
                    + (1.0f - gainCoefficient) * desiredExtra;
            return makeupGain * extraGain;
        }

        private void configure(int sampleRate) {
            int boundedRate = sampleRate >= 8000 && sampleRate <= 384000
                    ? sampleRate : 48000;
            if (configuredRate == boundedRate) return;
            configuredRate = boundedRate;
            envelopeAttack = coefficient(0.005f, boundedRate);
            envelopeRelease = coefficient(0.450f, boundedRate);
            gainAttack = coefficient(0.008f, boundedRate);
            gainRelease = coefficient(0.300f, boundedRate);
        }

        private static float coefficient(float seconds, int sampleRate) {
            return (float)Math.exp(-1.0 / (seconds * sampleRate));
        }
    }

    static float linearFromDb(float db) {
        if (!Float.isFinite(db) || db <= 0.0f) return 1.0f;
        float bounded = Math.min(db, MAX_DB);
        return (float)Math.pow(10.0, bounded / 20.0);
    }

    static short amplify(short sample, float linear) {
        if (linear == 1.0f) return sample;
        float normalized = sample >= 0
                ? sample / (float)Short.MAX_VALUE
                : sample / 32768.0f;
        float limited = softLimit(normalized * linear);
        int value = Math.round(limited * (limited >= 0.0f ? Short.MAX_VALUE : 32768.0f));
        return (short)Math.max(Short.MIN_VALUE, Math.min(Short.MAX_VALUE, value));
    }

    static float amplify(float sample, float linear) {
        // Unity must remain bit-transparent, including unusual IEEE values.
        if (linear == 1.0f) return sample;
        if (!Float.isFinite(sample)) return 0.0f;
        return softLimit(sample * linear);
    }

    static float absolute(short sample) {
        return Math.abs(sample >= 0
                ? sample / (float)Short.MAX_VALUE
                : sample / 32768.0f);
    }

    static float absolute(float sample) {
        return Float.isFinite(sample) ? Math.min(1.0f, Math.abs(sample)) : 0.0f;
    }

    /**
     * A cheap symmetric soft limiter. Unlike hard clipping, this progressively
     * compresses peaks as they approach full scale, so Skyrim can gain loudness
     * without flattening every sample above half scale.
     */
    private static float softLimit(float value) {
        float magnitude = Math.abs(value);
        if (magnitude >= 3.0f) return Math.copySign(1.0f, value);
        float squared = value * value;
        float limited = value * (27.0f + squared) / (27.0f + 9.0f * squared);
        return Math.max(-1.0f, Math.min(1.0f, limited));
    }
}
