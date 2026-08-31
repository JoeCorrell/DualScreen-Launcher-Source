package com.wemu.engine.xconnector;

import java.io.IOException;

public interface XStreamLock extends AutoCloseable {
    void close() throws IOException;
}
