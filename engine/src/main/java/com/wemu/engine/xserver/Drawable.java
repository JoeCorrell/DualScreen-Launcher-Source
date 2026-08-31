package com.wemu.engine.xserver;

import android.graphics.Bitmap;

import com.wemu.engine.core.Callback;
import com.wemu.engine.math.Mathf;
import com.wemu.engine.renderer.AHBImage;
import com.wemu.engine.renderer.NativeTexture;
import com.wemu.engine.renderer.Texture;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class Drawable extends XResource {
    public final short width;
    public final short height;
    public final Visual visual;
    private Texture texture = new Texture();
    private ByteBuffer data;
    private Runnable onDrawListener;
    private Callback<Drawable> onDestroyListener;
    public final Object renderLock = new Object();
    private boolean directScanout = false;

    static {
        System.loadLibrary("wemu");
    }

    /**
     * Whether new drawables should be backed by an {@link AHBImage}.
     *
     * Set once, before any drawable exists, from the renderer the container
     * chose. The scanout renderer needs every drawable to already live in an
     * {@code AHardwareBuffer} — it has no step in which it could upload one —
     * whereas the Vulkan compositor attaches a {@code GPUImage} later, through
     * {@link #setTexture}, and is happy with a plain buffer until then.
     *
     * Static because {@code Drawable} is constructed from the X server's own
     * request handling, which has no route to the renderer and no business
     * knowing about one.
     */
    private static volatile boolean drawableForAsr = false;

    public static void DRAWABLE_ASR_MODE(boolean value) {
        drawableForAsr = value;
    }

    public static boolean IS_ASR() {
        return drawableForAsr;
    }

    public Drawable(int id, int width, int height, Visual visual) {
        this(id, width, height, visual, null, true);
    }

    /**
     * Builds a drawable around storage supplied by an X extension.
     *
     * DRI3 pixmaps already own their storage. Going through the ordinary ASR
     * constructor would allocate an AHardwareBuffer and its three scanout
     * buffers, only for DRI3 to replace that texture immediately. Besides
     * wasting the allocation, replacing it used to leak all four buffers for
     * every DXVK swapchain image.
     */
    static Drawable fromExternalBacking(
            int id, int width, int height, Visual visual, ByteBuffer initialData) {
        if (initialData == null) {
            throw new IllegalArgumentException("External drawable data cannot be null");
        }
        return new Drawable(id, width, height, visual, initialData, false);
    }

    private Drawable(int id, int width, int height, Visual visual,
                     ByteBuffer initialData, boolean allocateNativeBacking) {
        super(id);
        this.width = (short)width;
        this.height = (short)height;
        this.visual = visual;

        /*
         * The hardware buffer only replaces the heap one if it actually mapped.
         *
         * A failed allocation leaves `getVirtualData` null, and this class
         * treats null data as fatal a few lines down. Falling back keeps a
         * device that cannot hand out a mappable AHardwareBuffer running on the
         * ordinary path rather than refusing to open a window.
        */
        ByteBuffer mapped = null;
        if (allocateNativeBacking && drawableForAsr) {
            AHBImage image = new AHBImage((short) width, (short) height);
            mapped = image.getVirtualData();
            if (mapped != null) {
                this.texture = image;
            } else {
                image.destroy();
            }
        }

        this.data = initialData != null
                ? initialData
                : mapped != null
                    ? mapped
                    : ByteBuffer.allocateDirect(width * height * 4)
                        .order(ByteOrder.LITTLE_ENDIAN);
        if (this.data == null) {
            throw new IllegalStateException("Drawable.data initialized as null!");
        }
    }

    public static Drawable fromBitmap(Bitmap bitmap) {
        Drawable drawable = new Drawable(0, bitmap.getWidth(), bitmap.getHeight(), null);
        fromBitmap(bitmap, drawable.data);
        return drawable;
    }

    public Texture getTexture() {
        return texture;
    }

    public void setTexture(Texture texture) {
        if (texture instanceof NativeTexture) {
            ByteBuffer vd = ((NativeTexture)texture).getVirtualData();
            if (vd != null) data = vd;

        }
        this.texture = texture;
    }

    public ByteBuffer getData() {
        return data;
    }

    public void setData(ByteBuffer data) {
        if (data == null) {
            throw new IllegalArgumentException("Attempting to set Drawable.data to null!");
        }
        this.data = data;
    }

    
    public void setDirectScanout(boolean value) {
        this.directScanout = value;
    }

    public boolean isDirectScanout() {
        return directScanout;
    }

    private short getStride() {
        return texture instanceof NativeTexture ? ((NativeTexture)texture).getStride() : width;
    }

    public Runnable getOnDrawListener() {
        return onDrawListener;
    }

    public void setOnDrawListener(Runnable onDrawListener) {
        this.onDrawListener = onDrawListener;
    }

    public Callback<Drawable> getOnDestroyListener() {
        return onDestroyListener;
    }

    public void setOnDestroyListener(Callback<Drawable> onDestroyListener) {
        this.onDestroyListener = onDestroyListener;
    }

    public void drawImage(short srcX, short srcY, short dstX, short dstY, short width, short height, byte depth, ByteBuffer data, short totalWidth, short totalHeight) {
        if (depth == 1) {
            drawBitmap(width, height, data, this.data);
        }
        /*
         * A rectangle these requests supply cannot be trusted to be positive.
         *
         * Width and height arrive from the guest as signed shorts, and the
         * clamps below only ever shrink a rectangle that is too large -- a
         * negative one passes straight through, because `(x + width) > this.width`
         * is false when width is negative. What follows then indexes backwards
         * out of the buffer. This is an X server reading requests from an
         * untrusted client, so an impossible rectangle is refused rather than
         * clamped into something plausible.
         */
        else if (depth == 24 || depth == 32) {
            if (width <= 0 || height <= 0 || this.width <= 0 || this.height <= 0) return;
            dstX = (short)Mathf.clamp(dstX, 0, this.width-1);
            dstY = (short)Mathf.clamp(dstY, 0, this.height-1);
            if ((dstX + width) > this.width) width = (short)((this.width - dstX));
            if ((dstY + height) > this.height) height = (short)((this.height - dstY));

            copyArea(srcX, srcY, dstX, dstY, width, height, totalWidth, this.getStride(), data, this.data);
        }

        this.data.rewind();
        data.rewind();

        texture.setNeedsUpdate(true);
        if (onDrawListener != null) onDrawListener.run();
    }

    public ByteBuffer getImage(short x, short y, short width, short height) {
        if (width <= 0 || height <= 0 || this.width <= 0 || this.height <= 0) {
            // An empty request still owes the caller a buffer to rewind and read.
            return ByteBuffer.allocateDirect(0).order(ByteOrder.LITTLE_ENDIAN);
        }
        ByteBuffer dstData = ByteBuffer.allocateDirect(width * height * 4).order(ByteOrder.LITTLE_ENDIAN);

        x = (short)Mathf.clamp(x, 0, this.width-1);
        y = (short)Mathf.clamp(y, 0, this.height-1);
        if ((x + width) > this.width) width = (short)(this.width - x);
        if ((y + height) > this.height) height = (short)(this.height - y);

        copyArea(x, y, (short)0, (short)0, width, height, this.getStride(), width, this.data, dstData);

        this.data.rewind();
        dstData.rewind();
        return dstData;
    }

    public void copyArea(short srcX, short srcY, short dstX, short dstY, short width, short height, Drawable drawable) {
        copyArea(srcX, srcY, dstX, dstY, width, height, drawable, GraphicsContext.Function.COPY);
    }

    public void copyArea(short srcX, short srcY, short dstX, short dstY, short width, short height, Drawable drawable, GraphicsContext.Function gcFunction) {
        if (width <= 0 || height <= 0 || this.width <= 0 || this.height <= 0) return;
        dstX = (short)Mathf.clamp(dstX, 0, this.width-1);
        dstY = (short)Mathf.clamp(dstY, 0, this.height-1);
        if ((dstX + width) > this.width) width = (short)(this.width - dstX);
        if ((dstY + height) > this.height) height = (short)(this.height - dstY);

        if (gcFunction == GraphicsContext.Function.COPY) {
            copyArea(srcX, srcY, dstX, dstY, width, height, drawable.getStride(), this.getStride(), drawable.data, this.data);
        }
        else copyAreaOp(srcX, srcY, dstX, dstY, width, height, drawable.getStride(), this.getStride(), drawable.data, this.data, gcFunction.ordinal());

        this.data.rewind();
        drawable.data.rewind();

        texture.setNeedsUpdate(true);
        if (onDrawListener != null) onDrawListener.run();
    }

    public void fillColor(int color) {
        fillRect(0, 0, width, height, color);
    }

    public void fillRect(int x, int y, int width, int height, int color) {
        if (width <= 0 || height <= 0 || this.width <= 0 || this.height <= 0) return;
        x = (short)Mathf.clamp(x, 0, this.width-1);
        y = (short)Mathf.clamp(y, 0, this.height-1);
        if ((x + width) > this.width) width = (short)((this.width - x));
        if ((y + height) > this.height) height = (short)((this.height - y));

        fillRect((short)x, (short)y, (short)width, (short)height, color, this.getStride(), this.data);
        this.data.rewind();

        texture.setNeedsUpdate(true);
        if (onDrawListener != null) onDrawListener.run();
    }

    public void drawLines(int color, int lineWidth, short... points) {
        for (int i = 2; i < points.length; i += 2) {
            drawLine(points[i-2], points[i-1], points[i+0], points[i+1], color, (short)lineWidth);
        }
    }

    public void drawLine(int x0, int y0, int x1, int y1, int color, int lineWidth) {
        x0 = Mathf.clamp(x0, 0, width-lineWidth);
        y0 = Mathf.clamp(y0, 0, height-lineWidth);
        x1 = Mathf.clamp(x1, 0, width-lineWidth);
        y1 = Mathf.clamp(y1, 0, height-lineWidth);

        drawLine((short)x0, (short)y0, (short)x1, (short)y1, color, (short)lineWidth, this.getStride(), this.data);

        this.data.rewind();

        texture.setNeedsUpdate(true);
        if (onDrawListener != null) onDrawListener.run();
    }

    public void drawAlphaMaskedBitmap(byte foreRed, byte foreGreen, byte foreBlue, byte backRed, byte backGreen, byte backBlue, Drawable srcDrawable, Drawable maskDrawable) {
        drawAlphaMaskedBitmap(foreRed, foreGreen, foreBlue, backRed, backGreen, backBlue, srcDrawable.data, maskDrawable.data, this.data);
        this.data.rewind();

        texture.setNeedsUpdate(true);
        if (onDrawListener != null) onDrawListener.run();
    }

    private static native void drawBitmap(short width, short height, ByteBuffer srcData, ByteBuffer dstData);

    private static native void drawAlphaMaskedBitmap(byte foreRed, byte foreGreen, byte foreBlue, byte backRed, byte backGreen, byte backBlue, ByteBuffer srcData, ByteBuffer maskData, ByteBuffer dstData);

    private static native void copyArea(short srcX, short srcY, short dstX, short dstY, short width, short height, short srcStride, short dstStride, ByteBuffer srcData, ByteBuffer dstData);

    private static native void copyAreaOp(short srcX, short srcY, short dstX, short dstY, short width, short height, short srcStride, short dstStride, ByteBuffer srcData, ByteBuffer dstData, int gcFunction);

    private static native void fillRect(short x, short y, short width, short height, int color, short stride, ByteBuffer data);

    private static native void drawLine(short x0, short y0, short x1, short y1, int color, short lineWidth, short stride, ByteBuffer data);

    private static native void fromBitmap(Bitmap bitmap, ByteBuffer data);
public java.nio.ByteBuffer getBuffer() {
        return this.data;
}
}
