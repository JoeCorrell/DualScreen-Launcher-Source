package com.wemu.engine.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Test;

public class FileUtilsAtomicWriteTest {
    @Test
    public void replacementPublishesCompleteUtf8AndLeavesNoTemporarySibling() throws Exception {
        File root = Files.createTempDirectory("wemu-atomic-write").toFile();
        File config = new File(root, ".container");

        assertTrue(FileUtils.writeStringAtomic(config, "{\"name\":\"first\"}"));
        String unicodeJson = "{\"name\":\"Th\u00f3r \u7b2c\u4e8c\"}";
        assertTrue(FileUtils.writeStringAtomic(config, unicodeJson));
        assertEquals(unicodeJson,
                new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8));

        File[] leftovers = root.listFiles(file -> file.getName().endsWith(".tmp"));
        assertTrue(leftovers != null && leftovers.length == 0);
    }
}
