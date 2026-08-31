package com.wemu.engine.xserver.extensions;

import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;

public interface Extension {
    String getName();

    byte getMajorOpcode();

    byte getFirstErrorId();

    byte getFirstEventId();

    /** Releases any callbacks or worker threads owned by this extension. */
    default void shutdown() {}

    void handleRequest(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError;
}
