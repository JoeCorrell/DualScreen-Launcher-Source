package com.wemu.engine.xserver.extensions;

import static org.junit.Assert.assertEquals;

import com.wemu.engine.xserver.ScreenInfo;
import com.wemu.engine.xserver.ScreenLayout;

import org.junit.Test;

/**
 * Covers the reply arithmetic, which is the part of RandR that fails silently.
 *
 * A wrong {@code length} does not produce an error the guest reports. The
 * client library reads the number of bytes it was told to and then reads the
 * next reply from the wrong offset, so the symptom is an X connection that
 * works for a while and then returns nonsense — a monitor list with a garbage
 * resolution, or a hang. Worth pinning by hand rather than by observation.
 */
public class RandRExtensionTest {

    @Test
    public void screenResourcesCountsEveryObjectAndPadsTheNames() {
        // Two monitors, names "1920x1080" and "1240x1080": 18 bytes, padded to 20.
        assertEquals(2 + 2 + 16 + 5,
                RandRExtension.screenResourcesReplyLength(2, 18));

        // A single monitor with an already-aligned name block adds no padding.
        assertEquals(1 + 1 + 8 + 2,
                RandRExtension.screenResourcesReplyLength(1, 8));
    }

    @Test
    public void outputInfoCountsTheFourHeaderBytesPastTheFirstThirtyTwo() {
        // "THOR-BOTTOM" is 11 bytes, padded to 12.
        assertEquals(1 + 1 + 1 + 3, RandRExtension.outputInfoReplyLength(11));

        // A name that already lands on a boundary is not padded to the next one.
        assertEquals(1 + 1 + 1 + 2, RandRExtension.outputInfoReplyLength(8));
    }

    @Test
    public void paddingBringsAnyLengthOntoAFourByteBoundary() {
        assertEquals(0, RandRExtension.paddingFor(0));
        assertEquals(3, RandRExtension.paddingFor(1));
        assertEquals(2, RandRExtension.paddingFor(2));
        assertEquals(1, RandRExtension.paddingFor(3));
        assertEquals(0, RandRExtension.paddingFor(4));
    }

    /**
     * The layout is what decides whether a guest can reach the second panel at
     * all, so the two facts it has to get right are pinned here: the monitors
     * do not overlap, and their extent is the root window the session built.
     */
    @Test
    public void stackedLayoutPlacesTheSecondPanelBelowTheFirst() {
        ScreenLayout layout = ScreenLayout.stacked(1920, 1080, 1240, 1080);

        assertEquals(2, layout.size());
        ScreenLayout.Monitor top = layout.getMonitors().get(0);
        ScreenLayout.Monitor bottom = layout.getMonitors().get(1);

        assertEquals(0, top.y);
        assertEquals(top.y + top.height, bottom.y);
        assertEquals(1240, bottom.width);

        assertEquals(1920, layout.getWidth());
        assertEquals(2160, layout.getHeight());
    }

    @Test
    public void singleLayoutIsTheWholeScreenAndNothingElse() {
        ScreenLayout layout = ScreenLayout.single(new ScreenInfo(1280, 720));

        assertEquals(1, layout.size());
        assertEquals(1280, layout.getWidth());
        assertEquals(720, layout.getHeight());
    }
}
