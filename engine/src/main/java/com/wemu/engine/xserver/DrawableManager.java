package com.wemu.engine.xserver;

import android.util.SparseArray;

import com.wemu.engine.core.Callback;
import com.wemu.engine.renderer.Texture;

import java.nio.ByteBuffer;

public class DrawableManager extends XResourceManager implements XResourceManager.OnResourceLifecycleListener {
    private final XServer xServer;
    private final SparseArray<Drawable> drawables = new SparseArray<>();

    public DrawableManager(XServer xServer) {
        this.xServer = xServer;
        xServer.pixmapManager.addOnResourceLifecycleListener(this);
    }

    public Drawable getDrawable(int id) {
        Drawable drawable = drawables.get(id);
        if (drawable != null && drawable.getData() == null) {
            throw new IllegalStateException("Drawable with id " + id + " has null data when fetched.");
        }
        return drawable;
    }


    public Drawable createDrawable(int id, short width, short height, byte depth) {
        return createDrawable(id, width, height, xServer.pixmapManager.getVisualForDepth(depth));
    }

    public Drawable createDrawable(int id, short width, short height, Visual visual) {
        if (id == 0) {
            Drawable drawable = new Drawable(id, width, height, visual);
            if (drawable.getData() == null) {
                throw new IllegalStateException("Drawable with id 0 has null data at creation.");
            }
            return drawable;
        }
        if (drawables.indexOfKey(id) >= 0) return null;
        Drawable drawable = new Drawable(id, width, height, visual);
        if (drawable.getData() == null) {
            throw new IllegalStateException("Drawable with id " + id + " has null data at creation.");
        }
        drawables.put(id, drawable);
        return drawable;
    }

    /**
     * Registers a drawable whose pixels are supplied by DRI3 or another X
     * extension. This deliberately skips Drawable's automatic ASR backing;
     * the caller is about to attach an imported texture or mapped buffer.
     */
    public Drawable createDrawableForExternalBacking(
            int id, short width, short height, byte depth, ByteBuffer initialData) {
        return createDrawableForExternalBacking(
                id, width, height, xServer.pixmapManager.getVisualForDepth(depth), initialData);
    }

    public Drawable createDrawableForExternalBacking(
            int id, short width, short height, Visual visual, ByteBuffer initialData) {
        if (id != 0 && drawables.indexOfKey(id) >= 0) return null;
        Drawable drawable = Drawable.fromExternalBacking(
                id, width, height, visual, initialData);
        if (drawable.getData() == null) {
            throw new IllegalStateException(
                    "External drawable with id " + id + " has null data at creation.");
        }
        if (id != 0) drawables.put(id, drawable);
        return drawable;
    }

    public void removeDrawable(int id) {
        Drawable drawable = drawables.get(id);
        if (drawable == null) {
            throw new IllegalStateException("Attempting to remove non-existent Drawable with id " + id);
        }
        if (drawable.getData() == null) {
            throw new IllegalStateException("Drawable with id " + id + " has null data during removal.");
        }

        final Texture texture = drawable.getTexture();
        if (texture != null) xServer.getRenderer().getRendererView().queueEvent(texture::destroy);

        Callback<Drawable> onDestroyListener = drawable.getOnDestroyListener();
        if (onDestroyListener != null) onDestroyListener.call(drawable);

        drawable.setOnDrawListener(null);
        drawables.remove(id);
    }


    @Override
    public void onFreeResource(XResource resource) {
        if (resource instanceof Pixmap) {
            Pixmap pixmap = (Pixmap) resource;
            Drawable drawable = pixmap.drawable;
            if (drawable.getData() == null) {
                throw new IllegalStateException("Drawable for Pixmap with id " + pixmap.drawable.id + " has null data during free.");
            }
            removeDrawable(drawable.id);
        }
    }


    public Visual getVisual() {
        return xServer.pixmapManager.visual;
    }
}
