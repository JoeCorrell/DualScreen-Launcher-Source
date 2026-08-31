package com.wemu.engine.xenvironment;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.core.FileUtils;
import com.wemu.engine.xenvironment.components.GuestProgramLauncherComponent;

import java.io.File;
import java.util.ArrayList;
import java.util.Iterator;

public class XEnvironment implements Iterable<EnvironmentComponent> {
    private final Context context;
    private final ImageFs imageFs;
    private final ArrayList<EnvironmentComponent> components = new ArrayList<>();

    public XEnvironment(Context context, ImageFs imageFs) {
        this.context = context;
        this.imageFs = imageFs;
    }

    public Context getContext() {
        return context;
    }

    public ImageFs getImageFs() {
        return imageFs;
    }

    public void addComponent(EnvironmentComponent environmentComponent) {
        environmentComponent.environment = this;
        components.add(environmentComponent);
    }

    public <T extends EnvironmentComponent> T getComponent(Class<T> componentClass) {
        for (EnvironmentComponent component : components) {
            if (component.getClass() == componentClass) return (T)component;
        }
        return null;
    }

    @Override
    public Iterator<EnvironmentComponent> iterator() {
        return components.iterator();
    }

    public File getTmpDir() {
        File tmpDir = new File(context.getFilesDir(), "tmp");
        if (!tmpDir.isDirectory()) {
            tmpDir.mkdirs();
            FileUtils.chmod(tmpDir, 0771);
        }
        return tmpDir;
    }

    public void startEnvironmentComponents() {
        FileUtils.clear(getTmpDir());
        for (EnvironmentComponent environmentComponent : this) environmentComponent.start();
    }

    public void stopEnvironmentComponents() {
        /*
         * Components are acquired in order, so release them in reverse order.
         * In particular the guest launcher must stop its Wine prefix before the
         * X/audio sockets disappear underneath Wine's service threads. The old
         * forward order tore the sockets down first and left detached
         * winedevice/plugplay processes spinning against dead endpoints.
         */
        for (int i = components.size() - 1; i >= 0; i--) {
            EnvironmentComponent component = components.get(i);
            try {
                component.stop();
            } catch (RuntimeException failure) {
                Log.w("XEnvironment", "Could not stop "
                        + component.getClass().getSimpleName(), failure);
            }
        }
    }

    public void onPause() {
        GuestProgramLauncherComponent guestProgramLauncherComponent = getComponent(GuestProgramLauncherComponent.class);
        if (guestProgramLauncherComponent != null) guestProgramLauncherComponent.suspendProcess();
    }

    public void onResume() {
        GuestProgramLauncherComponent guestProgramLauncherComponent = getComponent(GuestProgramLauncherComponent.class);
        if (guestProgramLauncherComponent != null) guestProgramLauncherComponent.resumeProcess();
    }
}
