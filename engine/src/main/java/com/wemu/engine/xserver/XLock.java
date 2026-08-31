package com.wemu.engine.xserver;

public interface XLock extends AutoCloseable {
    @Override
    void close();
}
