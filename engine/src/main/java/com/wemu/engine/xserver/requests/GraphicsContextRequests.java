package com.wemu.engine.xserver.requests;

import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.GraphicsContext;
import com.wemu.engine.xserver.Bitmask;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.BadDrawable;
import com.wemu.engine.xserver.errors.BadGraphicsContext;
import com.wemu.engine.xserver.errors.BadIdChoice;
import com.wemu.engine.xserver.errors.XRequestError;

public abstract class GraphicsContextRequests {
    public static void createGC(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        int gcId = inputStream.readInt();
        int drawableId = inputStream.readInt();
        Bitmask valueMask = new Bitmask(inputStream.readInt());

        if (!client.isValidResourceId(gcId)) throw new BadIdChoice(gcId);

        Drawable drawable = client.xServer.drawableManager.getDrawable(drawableId);
        if (drawable == null) throw new BadDrawable(drawableId);
        GraphicsContext graphicsContext = client.xServer.graphicsContextManager.createGraphicsContext(gcId, drawable);
        if (graphicsContext == null) throw new BadIdChoice(gcId);

        client.registerAsOwnerOfResource(graphicsContext);
        if (!valueMask.isEmpty()) client.xServer.graphicsContextManager.updateGraphicsContext(graphicsContext, valueMask, inputStream);
    }

    public static void copyGC(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        int srcGCId = inputStream.readInt();
        int dstGCId = inputStream.readInt();
        Bitmask valueMask = new Bitmask(inputStream.readInt());
        GraphicsContext srcGC = client.xServer.graphicsContextManager.getGraphicsContext(srcGCId);
        GraphicsContext dstGC = client.xServer.graphicsContextManager.getGraphicsContext(dstGCId);
        if (srcGC == null) throw new BadGraphicsContext(srcGCId);
        if (dstGC == null) throw new BadGraphicsContext(dstGCId);
        if (!valueMask.isEmpty()) client.xServer.graphicsContextManager.copyGraphicsContext(srcGC, dstGC, valueMask);
    }

    public static void changeGC(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        int gcId = inputStream.readInt();
        Bitmask valueMask = new Bitmask(inputStream.readInt());
        GraphicsContext graphicsContext = client.xServer.graphicsContextManager.getGraphicsContext(gcId);
        if (graphicsContext == null) throw new BadGraphicsContext(gcId);

        if (!valueMask.isEmpty()) client.xServer.graphicsContextManager.updateGraphicsContext(graphicsContext, valueMask, inputStream);
    }

    public static void freeGC(XClient client, XInputStream inputStream, XOutputStream outputStream) throws XRequestError {
        client.xServer.graphicsContextManager.freeGraphicsContext(inputStream.readInt());
    }
}
