package com.wemu.engine.xserver.requests;

import static com.wemu.engine.xserver.XClientRequestHandler.RESPONSE_CODE_SUCCESS;

import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;

public abstract class FontRequests {
    public static void openFont(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        inputStream.skip(4);
        int length = inputStream.readShort();
        inputStream.skip(2);
        String name = inputStream.readString8(length);
        if (!name.equals("cursor")) throw new UnsupportedOperationException("OpenFont supports only name: cursor.");
    }

    public static void listFonts(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        inputStream.skip(2);
        short patternLength = inputStream.readShort();
        inputStream.readString8(patternLength);

        try (XStreamLock lock = outputStream.lock()) {
            outputStream.writeByte(RESPONSE_CODE_SUCCESS);
            outputStream.writeByte((byte)0);
            outputStream.writeShort(client.getSequenceNumber());
            outputStream.writeInt(0);
            outputStream.writeShort((short)0);
            outputStream.writePad(22);
        }
    }
}
