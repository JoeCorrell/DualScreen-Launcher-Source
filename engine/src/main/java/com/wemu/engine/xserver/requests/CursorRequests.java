package com.wemu.engine.xserver.requests;

import static com.wemu.engine.xserver.XClientRequestHandler.RESPONSE_CODE_SUCCESS;

import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;
import com.wemu.engine.xserver.Cursor;
import com.wemu.engine.xserver.Pixmap;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.BadIdChoice;
import com.wemu.engine.xserver.errors.BadMatch;
import com.wemu.engine.xserver.errors.BadPixmap;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;

public abstract class CursorRequests {
    public static void createCursor(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        int cursorId = inputStream.readInt();
        int sourcePixmapId = inputStream.readInt();
        int maskPixmapId = inputStream.readInt();

        if (!client.isValidResourceId(cursorId)) throw new BadIdChoice(cursorId);

        Pixmap sourcePixmap = client.xServer.pixmapManager.getPixmap(sourcePixmapId);
        if (sourcePixmap == null) throw new BadPixmap(sourcePixmapId);

        Pixmap maskPixmap = client.xServer.pixmapManager.getPixmap(maskPixmapId);
        if (maskPixmap != null && (
            maskPixmap.drawable.visual.depth != 1 ||
            maskPixmap.drawable.width != sourcePixmap.drawable.width ||
            maskPixmap.drawable.height != sourcePixmap.drawable.height)) {
            throw new BadMatch();
        }

        byte foreRed = (byte)inputStream.readShort();
        byte foreGreen = (byte)inputStream.readShort();
        byte foreBlue = (byte)inputStream.readShort();
        byte backRed = (byte)inputStream.readShort();
        byte backGreen = (byte)inputStream.readShort();
        byte backBlue = (byte)inputStream.readShort();
        short x = inputStream.readShort();
        short y = inputStream.readShort();

        Cursor cursor = client.xServer.cursorManager.createCursor(cursorId, x, y, sourcePixmap, maskPixmap);
        if (cursor == null) throw new BadIdChoice(cursorId);
        client.xServer.cursorManager.recolorCursor(cursor, foreRed, foreGreen, foreBlue, backRed, backGreen, backBlue);
        client.registerAsOwnerOfResource(cursor);
    }

    public static void freeCursor(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        client.xServer.cursorManager.freeCursor(inputStream.readInt());
    }

    public static void getPointerMaping(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        try (XStreamLock lock = outputStream.lock()) {
            byte[] buttonsMap = {1, 2, 3};
            byte n = (byte) buttonsMap.length;
            int padLen = -n & 3;

            outputStream.writeByte(RESPONSE_CODE_SUCCESS);
            outputStream.writeByte(n);
            outputStream.writeShort(client.getSequenceNumber());
            outputStream.writeInt((n + padLen) / 4);
            outputStream.writePad(24);

            for (byte b: buttonsMap)
                outputStream.writeByte(b);
            outputStream.writePad(padLen);
        }
    }
}
