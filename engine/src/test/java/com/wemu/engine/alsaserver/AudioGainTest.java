package com.wemu.engine.alsaserver;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class AudioGainTest {
    @Test
    public void invalidAndNonPositiveGainRemainUnity() {
        assertEquals(1.0f, AudioGain.linearFromDb(Float.NaN), 0.0f);
        assertEquals(1.0f, AudioGain.linearFromDb(-6.0f), 0.0f);
        assertEquals(1.0f, AudioGain.linearFromDb(0.0f), 0.0f);
    }

    @Test
    public void gainIsBoundedAtNineDb() {
        assertEquals(AudioGain.linearFromDb(9.0f), AudioGain.linearFromDb(100.0f), 0.0f);
        assertEquals(1.995f, AudioGain.linearFromDb(6.0f), 0.002f);
    }

    @Test
    public void integerGainSoftLimitsInsteadOfWrapping() {
        float gain = AudioGain.linearFromDb(6.0f);
        short positivePeak = AudioGain.amplify((short)30000, gain);
        short negativePeak = AudioGain.amplify((short)-30000, gain);
        assertTrue(positivePeak > 30000 && positivePeak < Short.MAX_VALUE);
        assertTrue(negativePeak < -30000 && negativePeak > Short.MIN_VALUE);
        short quietSample = AudioGain.amplify((short)1000, gain);
        assertTrue(quietSample > 1950 && quietSample < 2000);
    }

    @Test
    public void floatGainIsFiniteAndBounded() {
        float gain = AudioGain.linearFromDb(6.0f);
        float positive = AudioGain.amplify(0.9f, gain);
        float negative = AudioGain.amplify(-0.9f, gain);
        assertTrue(positive > 0.9f && positive < 1.0f);
        assertTrue(negative < -0.9f && negative > -1.0f);
        assertEquals(0.0f, AudioGain.amplify(Float.NaN, gain), 0.0f);
    }

    @Test
    public void unityIsBitTransparent() {
        assertEquals((short)12345, AudioGain.amplify((short)12345, 1.0f));
        assertTrue(Float.isNaN(AudioGain.amplify(Float.NaN, 1.0f)));
        assertEquals(Float.POSITIVE_INFINITY,
                AudioGain.amplify(Float.POSITIVE_INFINITY, 1.0f), 0.0f);
    }

    @Test
    public void loudnessEqualizationBoostsQuietMaterialMoreThanLoudMaterial() {
        AudioGain.LoudnessState quiet = new AudioGain.LoudnessState();
        AudioGain.LoudnessState loud = new AudioGain.LoudnessState();
        float makeup = AudioGain.linearFromDb(6.0f);
        float quietGain = makeup;
        float loudGain = makeup;
        for (int frame = 0; frame < 48000; ++frame) {
            quietGain = quiet.gainForFrame(0.02f, 48000, makeup);
            loudGain = loud.gainForFrame(0.80f, 48000, makeup);
        }
        assertTrue(quietGain > loudGain);
        assertTrue(quietGain > makeup * 1.8f);
        assertTrue(loudGain >= makeup && loudGain < makeup * 1.1f);
    }

    @Test
    public void loudnessGainIsFiniteForInvalidInputAndRate() {
        AudioGain.LoudnessState state = new AudioGain.LoudnessState();
        float gain = state.gainForFrame(Float.NaN, 0, AudioGain.linearFromDb(6.0f));
        assertTrue(Float.isFinite(gain));
        assertTrue(gain >= AudioGain.linearFromDb(6.0f));
    }
}
