package com.wemu.engine.xserver.extensions;

import com.wemu.engine.renderer.GPUImage;
import com.wemu.engine.renderer.NativeTexture;

import static com.wemu.engine.xserver.XClientRequestHandler.RESPONSE_CODE_SUCCESS;

import com.wemu.engine.core.Callback;
import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.sysvshm.SysVSharedMemory;
import com.wemu.engine.xconnector.XConnectorEpoll;
import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.Pixmap;
import com.wemu.engine.xserver.Window;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.XLock;
import com.wemu.engine.xserver.XServer;
import com.wemu.engine.xserver.errors.BadAlloc;
import com.wemu.engine.xserver.errors.BadDrawable;
import com.wemu.engine.xserver.errors.BadIdChoice;
import com.wemu.engine.xserver.errors.BadImplementation;
import com.wemu.engine.xserver.errors.BadWindow;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicLong;

public class DRI3Extension implements Extension {
    public static final byte MAJOR_OPCODE = -102;

    private final Callback<Drawable> onDestroyDrawableListener = (drawable) -> {
        ByteBuffer data = drawable.getData();
        SysVSharedMemory.unmapSHMSegment(data, data.capacity());
    };

    private static final long FAILURE_LOG_INTERVAL_MS = 5_000L;
    private static final long INITIAL_IMPORT_LOGS = 12L;
    private final AtomicLong importCount = new AtomicLong();
    private final AtomicLong ahbImportCount = new AtomicLong();
    private final AtomicLong mappedImportCount = new AtomicLong();
    private final AtomicLong failedAhbCount = new AtomicLong();
    private final AtomicLong lastFailureLogAtMs = new AtomicLong();

    private static abstract class ClientOpcodes {
        private static final byte QUERY_VERSION = 0;
        private static final byte OPEN = 1;
        private static final byte PIXMAP_FROM_BUFFER = 2;
        private static final byte PIXMAP_FROM_BUFFERS = 7;
    }

    @Override public String getName() { return "DRI3"; }
    @Override public byte getMajorOpcode() { return MAJOR_OPCODE; }
    @Override public byte getFirstErrorId() { return 0; }
    @Override public byte getFirstEventId() { return 0; }

    private void queryVersion(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        inputStream.skip(8);
        try (XStreamLock lock = outputStream.lock()) {
            outputStream.writeByte(RESPONSE_CODE_SUCCESS);
            outputStream.writeByte((byte)0);
            outputStream.writeShort(client.getSequenceNumber());
            outputStream.writeInt(0);
            outputStream.writeInt(1);
            outputStream.writeInt(0);
            outputStream.writePad(16);
        }
    }

    private void open(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int drawableId = inputStream.readInt();
        inputStream.skip(4);
        Drawable drawable = client.xServer.drawableManager.getDrawable(drawableId);
        if (drawable == null) throw new BadDrawable(drawableId);
        try (XStreamLock lock = outputStream.lock()) {
            outputStream.writeByte(RESPONSE_CODE_SUCCESS);
            outputStream.writeByte((byte)0);
            outputStream.writeShort(client.getSequenceNumber());
            outputStream.writeInt(0);
            outputStream.writePad(24);
        }
    }

    private void pixmapFromBuffer(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int pixmapId = inputStream.readInt();
        int windowId = inputStream.readInt();
        int size = inputStream.readInt();
        short width = inputStream.readShort();
        short height = inputStream.readShort();
        short stride = inputStream.readShort();
        byte depth = inputStream.readByte();
        inputStream.skip(1);
        Window window = client.xServer.windowManager.getWindow(windowId);
        if (window == null) throw new BadWindow(windowId);
        Pixmap pixmap = client.xServer.pixmapManager.getPixmap(pixmapId);
        if (pixmap != null) throw new BadIdChoice(pixmapId);
        int fd = inputStream.getAncillaryFd();
        pixmapFromFd(client, pixmapId, width, height, stride, 0, depth, fd, size);
        reportImport("single-shm", width, height, stride, 0L, false);
    }

    private void pixmapFromBuffers(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int pixmapId = inputStream.readInt();
        int windowId = inputStream.readInt();
        inputStream.skip(4);
        short width = inputStream.readShort();
        short height = inputStream.readShort();
        int stride = inputStream.readInt();
        int offset = inputStream.readInt();
        inputStream.skip(24);
        byte depth = inputStream.readByte();
        inputStream.skip(3);
        long modifiers = inputStream.readLong();

        Window window = client.xServer.windowManager.getWindow(windowId);
        if (window == null) throw new BadWindow(windowId);
        Pixmap pixmap = client.xServer.pixmapManager.getPixmap(pixmapId);
        if (pixmap != null) throw new BadIdChoice(pixmapId);

        int fd = inputStream.getAncillaryFd();
        long size = (long)stride * height;

        if (modifiers == 1255 || modifiers == 1256) {
            pixmapFromHardwareBuffer(
                    client, pixmapId, width, height, stride, depth, fd, modifiers);
        } else if (modifiers == 1274) {

            pixmapFromFd(client, pixmapId, width, height, stride, offset, depth, fd, size);
            reportImport("mapped", width, height, stride, modifiers, false);
        } else {
            pixmapFromFd(client, pixmapId, width, height, stride, offset, depth, fd, size);
            reportImport("mapped", width, height, stride, modifiers, false);
        }
    }

    private void pixmapFromHardwareBuffer(
            XClient client, int pixmapId, short width, short height, int requestedStride,
            byte depth, int fd, long modifier) throws IOException, XRequestError {
        NativeTexture gpuImage = NativeTexture.fromSocket(fd);
        if (gpuImage.getHardwareBufferPtr() != 0) {
            try {
                // Drawable geometry is the X11 pixmap geometry from the DRI3
                // request. AHardwareBuffer stride is only its row pitch. Using
                // the padded stride as the visible width turns the Thor's
                // 1240x1080 panel buffers into 1280x1080 windows, so the lower
                // output applies the wrong geometry and DXVK never completes
                // the real auxiliary presentation stream.
                // Imported AHBs are not CPU-mapped in this process, but the X
                // drawable contract still requires a correctly-sized backing
                // for GetImage/CopyArea safety. It is only a compatibility
                // placeholder: PresentExtension never substitutes it when a
                // direct-scanout AHB is rejected.
                int stridePixels = Math.max((int) width, (int) gpuImage.getStride());
                long placeholderBytes = (long) stridePixels * (long) height * 4L;
                if (placeholderBytes <= 0L || placeholderBytes > Integer.MAX_VALUE) {
                    gpuImage.destroy();
                    throw new BadAlloc();
                }
                ByteBuffer placeholder;
                try {
                    placeholder = ByteBuffer.allocateDirect((int) placeholderBytes)
                            .order(ByteOrder.LITTLE_ENDIAN);
                } catch (OutOfMemoryError allocationFailure) {
                    gpuImage.destroy();
                    throw new BadAlloc();
                }
                Drawable drawable = client.xServer.drawableManager
                        .createDrawableForExternalBacking(
                                pixmapId, width, height, depth, placeholder);
                if (drawable == null) {
                    gpuImage.destroy();
                    throw new BadIdChoice(pixmapId);
                }
                drawable.setTexture(gpuImage);
                drawable.setDirectScanout(true);
                client.xServer.pixmapManager.createPixmap(drawable);
                ahbImportCount.incrementAndGet();
                int actualStride = gpuImage.getStride() > 0
                        ? gpuImage.getStride() : requestedStride;
                reportImport("socket-ahb", width, height, actualStride, modifier, false);
            } finally {
                XConnectorEpoll.closeFd(fd);
            }
            return;
        }
        gpuImage.destroy();
        failedAhbCount.incrementAndGet();

        try {
            long size = (long)width * height * 4;
            java.nio.ByteBuffer buffer = com.wemu.engine.sysvshm.SysVSharedMemory.mapSHMSegment(fd, size, 0, true);
            if (buffer == null) {
                reportImport("socket-ahb-no-fallback", width, height,
                        requestedStride, modifier, true);
                throw new BadAlloc();
            }
            Drawable drawable = client.xServer.drawableManager
                    .createDrawableForExternalBacking(
                            pixmapId, width, height, depth, buffer);
            drawable.setTexture(null);
            drawable.setOnDestroyListener(onDestroyDrawableListener);
            client.xServer.pixmapManager.createPixmap(drawable);
            mappedImportCount.incrementAndGet();
            reportImport("socket-shm-fallback", width, height,
                    requestedStride, modifier, true);
        } finally {
            XConnectorEpoll.closeFd(fd);
        }
    }

    private void pixmapFromFd(XClient client, int pixmapId, short width, short height, int stride, int offset, byte depth, int fd, long size) throws IOException, XRequestError {
        try {
            ByteBuffer buffer = SysVSharedMemory.mapSHMSegment(fd, size, offset, true);
            if (buffer == null) throw new BadAlloc();
            short totalWidth = (short)(stride / 4);
            Drawable drawable = client.xServer.drawableManager
                    .createDrawableForExternalBacking(
                            pixmapId, totalWidth, height, depth, buffer);
            drawable.setTexture(null);
            drawable.setOnDestroyListener(onDestroyDrawableListener);
            client.xServer.pixmapManager.createPixmap(drawable);
            mappedImportCount.incrementAndGet();
        } finally {
            XConnectorEpoll.closeFd(fd);
        }
    }

    private void reportImport(String path, int width, int height, int stride,
                              long modifier, boolean ahbFailure) {
        long count = importCount.incrementAndGet();
        long now = android.os.SystemClock.uptimeMillis();
        boolean initial = count <= INITIAL_IMPORT_LOGS;
        boolean failureDue = false;
        if (ahbFailure) {
            long previous = lastFailureLogAtMs.get();
            failureDue = now - previous >= FAILURE_LOG_INTERVAL_MS
                    && lastFailureLogAtMs.compareAndSet(previous, now);
        }
        if (!initial && !failureDue) return;

        ProcessHelper.emitDiagnostic("DRI3 import #" + count
                + ": path=" + path
                + " size=" + width + "x" + height
                + " stride=" + stride
                + " modifier=" + modifier
                + " totals(ahb=" + ahbImportCount.get()
                + " mapped=" + mappedImportCount.get()
                + " ahbFail=" + failedAhbCount.get() + ")");
    }

    @Override
    public void handleRequest(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int opcode = client.getRequestData();
        switch (opcode) {
            case ClientOpcodes.QUERY_VERSION:
                queryVersion(client, inputStream, outputStream);
                break;
            case ClientOpcodes.OPEN:
                try (XLock lock = client.xServer.lock(XServer.Lockable.DRAWABLE_MANAGER)) {
                    open(client, inputStream, outputStream);
                }
                break;
            case ClientOpcodes.PIXMAP_FROM_BUFFER:
                try (XLock lock = client.xServer.lock(XServer.Lockable.WINDOW_MANAGER, XServer.Lockable.PIXMAP_MANAGER, XServer.Lockable.DRAWABLE_MANAGER)) {
                    pixmapFromBuffer(client, inputStream, outputStream);
                }
                break;
            case ClientOpcodes.PIXMAP_FROM_BUFFERS:
                try (XLock lock = client.xServer.lock(XServer.Lockable.WINDOW_MANAGER, XServer.Lockable.PIXMAP_MANAGER, XServer.Lockable.DRAWABLE_MANAGER)) {
                    pixmapFromBuffers(client, inputStream, outputStream);
                }
                break;
            default:
                throw new BadImplementation();
        }
    }
}
