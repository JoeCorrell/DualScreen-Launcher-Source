import android.graphics.PixelFormat;
import android.media.ImageReader;
import android.view.Surface;
import com.wemu.engine.renderer.ASurfaceRenderer;
import java.lang.reflect.*;
import java.util.concurrent.atomic.AtomicReference;

/** Private app_process test. Its ImageReader never attaches to either display. */
public final class SurfaceSceneStressCheck {
    private static Method method(String name, Class<?>... types) throws Exception {
        Method m = ASurfaceRenderer.class.getDeclaredMethod(name, types);
        m.setAccessible(true); return m;
    }
    public static void main(String[] args) throws Exception {
        Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
        Field field = unsafeClass.getDeclaredField("theUnsafe"); field.setAccessible(true);
        Object renderer = unsafeClass.getMethod("allocateInstance", Class.class)
                .invoke(field.get(null), ASurfaceRenderer.class);
        Method init = method("nativeInit", Surface.class, int.class, int.class);
        Method destroy = method("nativeDestroy");
        Method register = method("nativeRegisterWindowSC", long.class, String.class);
        Method unregister = method("nativeUnregisterWindowSC", long.class);
        Method update = method("nativeUpdateWindows", int[].class, int.class);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        try (ImageReader reader = ImageReader.newInstance(32, 32, PixelFormat.RGBA_8888, 2)) {
            if (!(Boolean)init.invoke(renderer, reader.getSurface(), 32, 32))
                throw new AssertionError("Native renderer initialization failed");
            try {
                update.invoke(renderer, new int[0], 1);
                update.invoke(renderer, new int[11], Integer.MAX_VALUE);
                Thread[] workers = new Thread[3];
                for (int t = 0; t < workers.length; ++t) {
                    final int id = t + 1;
                    workers[t] = new Thread(() -> {
                        try {
                            for (int n = 0; n < 600; ++n) {
                                register.invoke(renderer, (long)id, "private-scene-stress");
                                int[] rows = {id, 1, id, 0, 0, 32, 32, n % 3, 0, 32, 32};
                                update.invoke(renderer, rows, 1);
                                // Also submit a different worker's SC while it retires.
                                rows[0] = id % 3 + 1; rows[1] = 0;
                                update.invoke(renderer, rows, 1);
                                unregister.invoke(renderer, (long)id);
                            }
                        } catch (Throwable e) { failure.compareAndSet(null, e); }
                    }, "private-scene-" + id);
                    workers[t].start();
                }
                // Context lifetime must cover every transaction in flight.
                Thread.sleep(10);
                destroy.invoke(renderer);
                if (!(Boolean)init.invoke(renderer, reader.getSurface(), 32, 32))
                    throw new AssertionError("Native renderer recreation failed");
                for (Thread worker : workers) {
                    worker.join(15000);
                    if (worker.isAlive()) throw new AssertionError("Scene worker deadlocked");
                }
                if (failure.get() != null) throw new AssertionError("Scene worker failed", failure.get());
            } finally { destroy.invoke(renderer); }
        }
        System.out.println("PASS: 3600 concurrent scene batches, SC retirement, context destroy/recreate, malformed batch guards.");
    }
}
