package com.wemu.engine.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

import java.io.ByteArrayInputStream;

public class TarCompressorUtilsTest {
    @Test
    public void detectsSupportedCompressionSignatures() throws Exception {
        assertEquals(TarCompressorUtils.Type.GZIP,
                detect(0x1f, 0x8b, 0x08, 0x00));
        assertEquals(TarCompressorUtils.Type.ZSTD,
                detect(0x28, 0xb5, 0x2f, 0xfd));
        assertEquals(TarCompressorUtils.Type.XZ,
                detect(0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00));
        assertNull(detect(0x50, 0x4b, 0x03, 0x04));
    }

    private static TarCompressorUtils.Type detect(int... bytes) throws Exception {
        byte[] value = new byte[bytes.length];
        for (int index = 0; index < bytes.length; index++) {
            value[index] = (byte) bytes[index];
        }
        return TarCompressorUtils.detectType(new ByteArrayInputStream(value));
    }
}
