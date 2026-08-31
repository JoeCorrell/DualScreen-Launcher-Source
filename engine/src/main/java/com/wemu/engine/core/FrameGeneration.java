package com.wemu.engine.core;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.container.Container;
import com.wemu.engine.xenvironment.ImageFs;

import java.io.File;

/**
 * Frame generation, as a Vulkan layer inside the guest.
 *
 * <h2>Where it runs, and why that is the whole story</h2>
 *
 * The layer hooks the game's own swapchain, in the game's own Vulkan device,
 * and inserts interpolated frames between the ones it draws. Nothing is copied
 * and nothing crosses a device boundary, which is what makes it cheap enough to
 * be worth having.
 *
 * Two other arrangements were built first and measured on an AYN Thor. Both are
 * recorded here because both look obvious from a standing start:
 *
 * <ol>
 *   <li><b>Stock lsfg-vk as a guest layer.</b> It runs its interpolation on a
 *       device of its own and needs an exportable semaphore to synchronise, so
 *       it asks for the opaque-FD external handle types. The ICD here is the
 *       wrapper under winevulkan, which offers the Win32 pair instead, so
 *       {@code vkCreateSemaphore} fails with -1000072003, DXGI never gets a
 *       swapchain, and the game exits during startup with no crash and no
 *       tombstone.</li>
 *   <li><b>This app's compositor.</b> It works, and it costs 45 ms a frame at
 *       1080p. The compositor is a second Vulkan device that does not own the
 *       game's frames, so each generated frame meant copying into an
 *       AHardwareBuffer, handing it across, waiting on {@code vkDeviceWaitIdle}
 *       because no semaphore can cross either, and copying the result back. The
 *       copies and the stall are the design, not a tuning problem. Removed; see
 *       commit 99ad959 for the code and the measurements.</li>
 * </ol>
 *
 * <h2>What ships</h2>
 *
 * {@code libloki_fg.so} — the bionic-fg layer, taken from Bannerlator, a
 * The original fork that had already solved this, and shipped under this app's own
 * name. It is single-device, so it needs no external handles, and it
 * carries its own shaders, so it needs no {@code Lossless.dll}. The DLL import
 * machinery further down is kept anyway: the other engine worth offering is a
 * patched lsfg-vk that does read one, and the button for it is already in the
 * container editor.
 */
public final class FrameGeneration {

    private static final String TAG = "FrameGeneration";

    /**
     * The layer that actually works here, and it is not the one this file was
     * originally written for.
     *
     * Frame generation belongs in the *guest's* Vulkan device, hooking the
     * game's own swapchain, where the frames already are. Two other routes were
     * built and measured first and both are worse by construction:
     *
     *  - Stock `lsfg-vk` as a guest layer asks for opaque-FD external
     *    semaphores. The wrapper ICD under winevulkan offers the Win32 pair
     *    instead, so `vkCreateSemaphore` fails and the game exits at startup.
     *  - Doing it in this app's compositor works and costs 45 ms a frame at
     *    1080p, because the frame has to be copied into an AHardwareBuffer,
     *    handed to a *second* Vulkan device, waited on with `vkDeviceWaitIdle`,
     *    and copied back. The copies and the stall are the design, not a
     *    tuning problem. That code has since been removed; see commit
     *    99ad959 for it and for the numbers.
     *
     * The layer is bionic-fg, a single-device layer with the shaders embedded,
     * so it needs no external handles and no `Lossless.dll`. Taken from
     * Bannerlator, which had already solved this, and
     * shipped here under this app's own name.
     *
     * The rename stops at the file. Its exported entry points, its
     * {@link #ENABLE_VAR} gate and its log tags are compiled into the binary and
     * still say bionic — see {@link #ENABLE_VAR} for why they are left alone
     * rather than patched.
     */
    private static final String LAYER_LIBRARY = "libloki_fg.so";

    /**
     * What earlier builds called the same layer, kept so they can be removed.
     *
     * A container installed before the rename still has the old manifest, and a
     * manifest is all the loader needs — it would load this layer twice, once
     * under each name, each with its own swapchain hooks. Renaming a file that
     * something else has already been told about is not finished until the old
     * name is gone.
     */
    private static final String LEGACY_LAYER_LIBRARY = "libbionic_fg.so";
    private static final String LEGACY_LAYER_MANIFEST = "VkLayer_BIONIC_framegen.json";

    /**
     * Where the library goes, and it is not beside its manifest.
     *
     * The Vulkan loader reads `library_path` three ways: a full path is used as
     * given, a *relative* path is resolved against the manifest's directory,
     * and a bare *filename* is handed to `dlopen` to find on the system search
     * path. This wrote a bare filename with the library sitting next to the
     * manifest — so the loader looked on `LD_LIBRARY_PATH`, which is `usr/lib`
     * and `/system/lib64`, did not find it, and skipped the layer. Silently,
     * because that is what a loader does with a layer it cannot load.
     *
     * Which is why none of the activation work mattered. The layer was built,
     * installed, ABI-correct, and asked for by three different mechanisms in
     * turn; it could not have been loaded by any of them.
     *
     * It goes in `usr/lib` now, which is on that search path, and the manifest
     * names it by a relative path as well — the same belt and braces MangoHud
     * uses, whose manifest points at `../../../lib/mangohud/libMangoHud.so`
     * rather than trusting either mechanism on its own.
     */
    private static final String LIBRARY_DIR = "usr/lib";
    private static final String LIBRARY_RELATIVE = "../../../lib/" + LAYER_LIBRARY;
    private static final String LAYER_MANIFEST = "VkLayer_LOKI_framegen.json";

    /**
     * An implicit layer, gated by an environment variable.
     *
     * This was explicit, on the reasoning that an implicit layer loads into
     * every Vulkan application whether asked or not — which is true, and is
     * why {@code enable_environment} exists: an implicit layer that declares
     * one is inert until that variable is set. The reasoning was right and the
     * conclusion was wrong.
     *
     * Explicit does not work here, and the reason is Wine. An explicit layer
     * has to be named by the *application* in
     * {@code VkInstanceCreateInfo::ppEnabledLayerNames}; the loader's
     * environment variables exist to add them, but the application creating
     * the instance is winevulkan, which builds that list itself from what the
     * Windows side asked for. A host layer nobody on the Windows side has
     * heard of does not survive the trip.
     *
     * The proof was in the image the whole time. MangoHud works under Wine
     * here, and MangoHud is an implicit layer in this directory with
     * {@code enable_environment: MANGOHUD=1}. This is the same arrangement
     * with a different variable.
     */
    private static final String LAYER_DIR = "usr/share/vulkan/implicit_layer.d";

    /** Where the explicit manifest used to go, so a stale one can be removed. */
    private static final String LEGACY_LAYER_DIR = "usr/share/vulkan/explicit_layer.d";

    private static final String LAYER_NAME = "VK_LAYER_LOKI_framegen";

    /**
     * The gate the manifest declares; without it the layer loads inert.
     *
     * Still {@code BIONIC_FG_*}, and it has to be. The loader checks
     * {@code enable_environment} itself, so that half could be renamed freely —
     * but the layer *also* reads this variable with {@code getenv} and prints
     * "Layer disabled (BIONIC_FG_ENABLE not set)" when it is missing, and that
     * string is compiled into the binary.
     *
     * Its entry points are stuck for a harder reason. The loader finds them with
     * {@code dlsym} using the names in the manifest's {@code functions} block,
     * and {@code dlsym} goes through the ELF's {@code .gnu.hash} table, whose
     * buckets are computed *from the symbol name*. Editing the name in
     * {@code .dynstr} — the one edit a same-length patch could make safely —
     * leaves it hashed under the old one, so the lookup fails and the layer
     * silently does not load. Renaming them properly means rebuilding the layer
     * from source, not patching the binary we ship.
     */
    private static final String ENABLE_VAR = "BIONIC_FG_ENABLE";


    /**
     * On.
     *
     * This was `true` for as long as the layer here was stock lsfg-vk, which
     * loaded and then took the game down with it. The layer changed; the reason
     * for the switch did not survive it.
     */
    private static final boolean DISABLED = false;

    /**
     * A file that puts the layer back, for one device, deliberately.
     *
     * {@link #DISABLED} is the right default and this does not change it. What
     * it buys is a way to reproduce the crash on a device that has one without
     * shipping a build that crashes for everyone, and without the fifteen
     * minutes a rebuild-and-reinstall costs over a network.
     *
     * <pre>
     *   adb shell run-as com.wemu touch files/frameGeneration/DIAGNOSTIC
     *   adb shell run-as com.wemu rm    files/frameGeneration/DIAGNOSTIC
     * </pre>
     *
     * Deliberately not a setting. It is not in the UI, it is not in the
     * container, and it cannot be reached by anyone who has not gone looking
     * with a debugger attached — which is the point, because what it enables is
     * a feature known to take the game down with it.
     */
    private static final String DIAGNOSTIC_MARKER = "DIAGNOSTIC";

    /** Whether this device has asked to reproduce the frame-generation crash. */
    public static boolean isDiagnostic(Context context) {
        return new File(context.getFilesDir(), "frameGeneration/" + DIAGNOSTIC_MARKER).isFile();
    }

    private FrameGeneration() {}

    /**
     * Where the SPIR-V pulled out of the DLL is kept.
     *
     * Beside the imported DLL rather than inside a container: the shaders are a
     * property of the file, not of any one container, and extracting them once
     * per container would be the same work three times over.
     */
    public static File shaderCacheDir(Context context) {
        return new File(context.getFilesDir(), "frameGeneration/shaders");
    }

    /**
     * The DLL the compositor should read shaders from, or null if there is none.
     *
     * The same search {@link #prepare} used for the layer that this replaces —
     * pinned path, imported copy, shared folder, then drive C — exposed because
     * frame generation now happens in this app's own renderer and the renderer
     * has no {@code Context} to run the search itself.
     */
    public static File shaderDll(Context context, Container container) {
        return findLosslessDll(context, container, ImageFs.find(context).getRootDir());
    }

    /** Frames shown per frame drawn, clamped to what the layer accepts. */
    public static int multiplierOf(Container container) {
        return Math.max(2, Math.min(4, intExtra(container, "frameGenerationMultiplier", 2)));
    }

    /** Whether this container asked for frame generation. */
    public static boolean isEnabled(Container container) {
        return "1".equals(container.getExtra("frameGeneration"));
    }

    /**
     * Installs the layer and writes its configuration.
     *
     * @return true when the layer is present *and* has a DLL to work from.
     *   False means the caller should not enable it: a layer that loads and
     *   then cannot find its shaders takes the application down with it rather
     *   than declining politely.
     */
    public static boolean prepare(Context context, Container container) {
        ImageFs imageFs = ImageFs.find(context);
        File rootDir = imageFs.getRootDir();

        /*
         * Installed and enabled, which it was not for most of this file's life.
         *
         * The layer here used to be stock lsfg-vk, which loaded under the
         * wrapper ICD, asked for an opaque-FD external semaphore, was refused,
         * and took the game down during startup with no crash and no tombstone.
         * That is why everything below was gated off. The layer is now a
         * single-device one that needs no external handles, so the gate is
         * gone — see LAYER_LIBRARY for the two routes this replaces and what
         * each of them cost.
         */
        boolean diagnostic = isDiagnostic(context);
        if (DISABLED) {
            uninstall(rootDir);
            return false;
        }

        /*
         * The container's own switch, which this never asked about.
         *
         * Everything else consults {@link #isEnabled} — the renderer for its
         * multiplier, the bootstrap for the base frame cap — and this, the one
         * place that decides whether the layer is *switched on at all*, did not.
         * So `applyEnvVars` ran for every container and set the enable variable
         * unconditionally, and the layer, being implicit, hooked the swapchain
         * of every game whether or not anybody had asked for frame generation.
         *
         * Two things follow from that, and both were observed. Every container
         * paid for interpolation it had not enabled. And turning the switch off
         * to test whether the layer was causing a problem changed nothing, which
         * reads as evidence that the layer is innocent when it is only evidence
         * that the switch was not wired to anything.
         *
         * Nothing is uninstalled here. The manifest carries `enable_environment`,
         * so the loader skips the layer when the variable is absent, and leaving
         * the files alone avoids deleting and re-extracting them on every launch
         * of every container that does not use them.
         */
        /*
         * Loaded whether or not it is switched on, so that it can be switched on
         * later.
         *
         * This used to return here when the container's switch was off, which is
         * why frame generation was a decision for the next launch: a Vulkan
         * layer is inserted by the loader at instance creation, so a game that
         * started without it cannot be given it afterwards by any means this app
         * has. The switch was not being read too late; it was being read at the
         * only moment that could ever matter, and that moment is before the game
         * exists.
         *
         * The layer turns out to have an off state of its own — it watches its
         * config's mtime and logs `config hot-reloaded framegen=off` — so being
         * resident and being active are separable. It is now always resident and
         * starts in whichever state the container asked for, and the console's
         * switch rewrites the config rather than a preference for next time.
         *
         * This is not the old bug returning. That one had the layer loaded *and*
         * interpolating for every container, because nothing wrote an off state
         * and the layer's default is on; what it cost was interpolation nobody
         * had asked for. What residency costs by itself is a hook on
         * `vkQueuePresentKHR` and the swapchain images the layer provisions
         * against a later multiplier — real, and far short of a frame's work.
         */
        try {
            if (!installLayer(context, rootDir)) return false;

            /*
             * No `Lossless.dll` lookup, because this layer carries its shaders.
             *
             * The import machinery further down is deliberately kept. It is
             * still reachable from the container editor, and the other engine
             * worth offering — a patched lsfg-vk that does read the DLL — would
             * need it. Nothing on this path does.
             */
            writeConfig(rootDir, container);
            Log.d(TAG, "Frame generation ready (embedded shaders, no DLL needed)");
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Could not set up frame generation", e);
            return false;
        }
    }

    /** The environment the layer needs, once {@link #prepare} has said yes. */
    public static void applyEnvVars(EnvVars envVars) {
        /*
         * One variable, the one the manifest declares.
         *
         * Not `VK_INSTANCE_LAYERS` and not `VK_LOADER_LAYERS_ENABLE`. Both were
         * tried; both are for adding *explicit* layers to an application's own
         * list, and the application here is winevulkan, which composes that
         * list from the Windows side and does not carry a host layer through
         * it. An implicit layer needs no such permission — the loader inserts
         * it — and `enable_environment` is how it is kept from being inserted
         * into everything.
         */
        envVars.put(ENABLE_VAR, "1");

        /*
         * No profile variable any more.
         *
         * lsfg-vk 2.x matched a profile against the process name, which under
         * Wine is not reliably the game's, so one was selected explicitly by
         * name. This layer has no profiles — it reads one config file and
         * applies it to whatever instance it is loaded into — so there is
         * nothing left to select and nothing left to get wrong.
         */
        Log.d(TAG, "Layer requested: " + LAYER_NAME);
    }

    /**
     * Turns on the loader's own reporting.
     *
     * Called when the container has Wine logging on. A layer that fails to load
     * does so silently by design — the loader skips what it cannot use and
     * carries on — so without this the only symptom is that nothing happens,
     * which is indistinguishable from the feature not being wired up. Two
     * evenings of this project have gone on telling those apart.
     */
    public static void applyDebugEnvVars(EnvVars envVars, boolean verbose) {
        /*
         * `all` while chasing the crash, `warn,error` otherwise.
         *
         * The difference is whether the loader narrates the layers it found and
         * chose as well as the ones it rejected. That is noise during play and
         * the entire point during a diagnostic run: a layer that aborts during
         * initialisation leaves nothing behind except what was printed before
         * it went, so the last line before the abort is the finding.
         */
        envVars.put("VK_LOADER_DEBUG", verbose ? "all" : "warn,error");
    }

    /**
     * Takes the layer back out of a container.
     *
     * Every place any version of this has ever put a file: the manifest in
     * both the implicit and explicit directories, and the library beside
     * either of them as well as in `usr/lib`. A layer half-removed is a layer
     * that still loads.
     */
    private static void uninstall(File rootDir) {
        String[] manifestDirs = {LAYER_DIR, LEGACY_LAYER_DIR};
        for (String dir : manifestDirs) {
            deleteIfPresent(new File(rootDir, dir + "/" + LAYER_MANIFEST));
            deleteIfPresent(new File(rootDir, dir + "/" + LAYER_LIBRARY));
            deleteIfPresent(new File(rootDir, dir + "/" + LEGACY_LAYER_MANIFEST));
            deleteIfPresent(new File(rootDir, dir + "/" + LEGACY_LAYER_LIBRARY));
        }
        deleteIfPresent(new File(rootDir, LIBRARY_DIR + "/" + LAYER_LIBRARY));
        deleteIfPresent(new File(rootDir, LIBRARY_DIR + "/" + LEGACY_LAYER_LIBRARY));
    }

    private static void deleteIfPresent(File file) {
        if (file.isFile() && file.delete()) Log.d(TAG, "Removed " + file);
    }

    // ------------------------------------------------------------------ layer

    private static boolean installLayer(Context context, File rootDir) {
        File layerDir = new File(rootDir, LAYER_DIR);
        if (!layerDir.isDirectory() && !layerDir.mkdirs()) {
            Log.e(TAG, "Could not create " + layerDir);
            return false;
        }

        File libraryDir = new File(rootDir, LIBRARY_DIR);
        if (!libraryDir.isDirectory() && !libraryDir.mkdirs()) {
            Log.e(TAG, "Could not create " + libraryDir);
            return false;
        }

        File target = new File(libraryDir, LAYER_LIBRARY);
        File source = new File(AppUtils.getNativeLibDir(context), LAYER_LIBRARY);

        if (!source.isFile()) {
            Log.e(TAG, "The layer is missing from the APK: " + source);
            return false;
        }

        // Copied afresh whenever the shipped one is newer, so an app update
        // replaces a layer that a container installed months ago.
        if (!target.isFile() || target.lastModified() < source.lastModified()) {
            if (!FileUtils.copy(source, target)) {
                Log.e(TAG, "Could not copy the layer to " + target);
                return false;
            }
            FileUtils.chmod(target, 0755);
        }

        /*
         * The manifest, written rather than shipped as an asset.
         *
         * It is four lines of JSON that have to agree with the library name and
         * the layer name used above, and winhub's copy agreed with neither. A
         * file generated from the same constants cannot drift from them.
         */
        /*
         * Shaped after MangoHud's, which is the one in this image known to load
         * under Wine. Same file format version, same type, same api_version,
         * and the same pair of environment gates.
         */
        /*
         * `functions` is the part that cannot be left out.
         *
         * This layer exports `BionicFG_GetInstanceProcAddr`, not the standard
         * `vkGetInstanceProcAddr`, so the loader has to be told the names or it
         * finds nothing to call and skips the layer — silently, which is what a
         * loader does with every layer it cannot use.
         *
         * These two keep the layer's own names even though the file around them
         * is ours now; {@link #ENABLE_VAR} has why they cannot follow the
         * rename.
         *
         * `disable_environment` is required as well: the loader rejects an
         * implicit layer without one as malformed. It is not a second toggle.
         */
        String manifest = "{\n"
                + "  \"file_format_version\": \"1.0.0\",\n"
                + "  \"layer\": {\n"
                + "    \"name\": \"" + LAYER_NAME + "\",\n"
                + "    \"description\": \"Frame generation\",\n"
                + "    \"implementation_version\": \"2\",\n"
                + "    \"library_path\": \"" + LIBRARY_RELATIVE + "\",\n"
                + "    \"type\": \"GLOBAL\",\n"
                + "    \"api_version\": \"1.1.0\",\n"
                + "    \"functions\": {\n"
                + "      \"vkGetInstanceProcAddr\": \"BionicFG_GetInstanceProcAddr\",\n"
                + "      \"vkGetDeviceProcAddr\": \"BionicFG_GetDeviceProcAddr\"\n"
                + "    },\n"
                + "    \"enable_environment\": { \"" + ENABLE_VAR + "\": \"1\" },\n"
                // Ours to name: the loader reads this one, and the layer has no
                // idea it exists. Nothing sets it — see above, it is required
                // rather than useful.
                + "    \"disable_environment\": { \"LOKI_FG_DISABLE\": \"1\" }\n"
                + "  }\n"
                + "}\n";
        FileUtils.writeString(new File(layerDir, LAYER_MANIFEST), manifest);

        // An explicit manifest from an earlier build would be a second copy of
        // the same layer under the same name, which the loader reports as a
        // conflict rather than resolving.
        File legacy = new File(rootDir, LEGACY_LAYER_DIR + "/" + LAYER_MANIFEST);
        if (legacy.isFile()) {
            legacy.delete();
            Log.d(TAG, "Removed the old explicit manifest at " + legacy);
        }

        // And every trace of the name this layer shipped under before, in both
        // manifest directories and in `usr/lib`. Left behind, the old manifest
        // is a complete second registration of the same layer — a different
        // name, so the loader has no conflict to report, and it hooks the
        // swapchain twice.
        for (String dir : new String[]{LAYER_DIR, LEGACY_LAYER_DIR}) {
            deleteIfPresent(new File(rootDir, dir + "/" + LEGACY_LAYER_MANIFEST));
            deleteIfPresent(new File(rootDir, dir + "/" + LEGACY_LAYER_LIBRARY));
            // And the copies an earlier build left beside the manifests, where
            // the loader was never going to look for them.
            File stray = new File(rootDir, dir + "/" + LAYER_LIBRARY);
            if (stray.isFile()) stray.delete();
        }
        deleteIfPresent(new File(rootDir, LIBRARY_DIR + "/" + LEGACY_LAYER_LIBRARY));

        Log.d(TAG, "Layer at " + target + ", manifest in " + layerDir);

        return true;
    }

    // -------------------------------------------------------------------- dll

    /**
     * Where the user is asked to put the DLL.
     *
     * The shared folder, beside presets and control profiles — a place reachable
     * with any file manager, that survives the app being reinstalled, and that
     * is not inside a container, because the file has nothing to do with any
     * one container and copying it into each of them would be absurd.
     */
    public static File dllFolder() {
        return new File(WemuPaths.DEFAULT_SHARED_PATH, "FrameGeneration");
    }

    /**
     * Where an imported DLL is kept.
     *
     * The app's own storage, which needs no permission, cannot be deleted by a
     * file manager tidy-up, and exists whether or not anybody has granted
     * anything. Asking people to find a folder on shared storage was a mistake
     * twice: the folder did not exist until something created it, and creating
     * it needs a permission that has nothing to do with picking a file.
     */
    public static File importedDll(Context context) {
        return new File(context.getFilesDir(), "frameGeneration/" + DLL_NAME);
    }

    /**
     * Whether frame generation can be offered at all on this build.
     *
     * <h2>Not the same question as {@link #hasDll}</h2>
     *
     * It was, and screens are still asking the old one. Frame generation used
     * to be stock lsfg-vk, which read its shaders out of {@code Lossless.dll} —
     * paid software this app cannot ship — so "is it available" genuinely meant
     * "has somebody supplied that file". The layer that ships now carries its
     * own shaders and never looks for the DLL; {@code prepare} says so in as
     * many words. The requirement went and the gate stayed, which is why the
     * console's switch has been greyed out for a feature that was ready.
     *
     * The DLL machinery is still here and still reachable from the container
     * editor, because the other engine worth offering is a patched lsfg-vk that
     * does read one. That is what {@link #hasDll} is for. This is what a screen
     * deciding whether to offer the feature should ask.
     */
    public static boolean isAvailable() {
        return !DISABLED;
    }

    /** Whether the user has supplied the DLL yet, for a screen to say so. */
    public static boolean hasDll(Context context) {
        return importedDll(context).isFile() || new File(dllFolder(), DLL_NAME).isFile();
    }

    /**
     * Takes a copy of whatever the user picked.
     *
     * Copied rather than referenced: a content URI is a grant that can be
     * revoked, and the file is read at the start of every session for the life
     * of the container. Reads the header before keeping it, so picking the
     * wrong file is answered now rather than as a layer that fails to
     * initialise later.
     *
     * @return null on success, or a sentence explaining what was wrong.
     */
    public static String importDll(Context context, java.io.InputStream in) {
        File target = importedDll(context);
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            return "Could not create a place to keep it.";
        }

        File temp = new File(target.getPath() + ".part");
        try (java.io.OutputStream out = new java.io.FileOutputStream(temp)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            long total = 0;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
                total += read;
            }
            if (total < MIN_DLL_BYTES) {
                temp.delete();
                return "That file is too small to be Lossless.dll.";
            }
        } catch (java.io.IOException e) {
            temp.delete();
            Log.w(TAG, "Could not read the picked file", e);
            return "Could not read that file.";
        }

        if (!isWindowsDll(temp)) {
            temp.delete();
            return "That is not a Windows DLL.";
        }

        target.delete();
        if (!temp.renameTo(target)) {
            temp.delete();
            return "Could not save it.";
        }
        Log.d(TAG, "Imported " + target + " (" + target.length() + " bytes)");
        return null;
    }

    /** `MZ`, which is the first two bytes of every PE ever built. */
    private static boolean isWindowsDll(File file) {
        try (java.io.InputStream in = new java.io.FileInputStream(file)) {
            return in.read() == 'M' && in.read() == 'Z';
        } catch (java.io.IOException e) {
            return false;
        }
    }

    /** Smaller than this and it is certainly not the shader DLL. */
    private static final long MIN_DLL_BYTES = 64 * 1024;

    private static final String DLL_NAME = "Lossless.dll";

    /**
     * Finds the user's copy of the shader DLL.
     *
     * The shared folder first, because that is where we ask for it and it is
     * the only location a user can reach without a container running. Upstream's
     * own search is for Steam on a Linux desktop and finds nothing here.
     *
     * Drive C is still searched afterwards, for the case where somebody really
     * did install the application into the container — that works too, and
     * refusing to look would be perverse.
     */
    private static File findLosslessDll(Context context, Container container, File rootDir) {
        String pinned = container.getExtra("frameGenerationDll");
        if (!pinned.isEmpty()) {
            File file = new File(pinned);
            if (file.isFile()) return file;
        }

        File imported = importedDll(context);
        if (imported.isFile()) return imported;

        File shared = new File(dllFolder(), DLL_NAME);
        if (shared.isFile()) return shared;

        File driveC = new File(rootDir, ImageFs.WINEPREFIX + "/drive_c");
        String[] likely = {
                "Program Files (x86)/Steam/steamapps/common/Lossless Scaling/Lossless.dll",
                "Program Files/Steam/steamapps/common/Lossless Scaling/Lossless.dll",
                "Program Files/Lossless Scaling/Lossless.dll",
                "Lossless Scaling/Lossless.dll",
                "Lossless.dll",
        };
        for (String path : likely) {
            File file = new File(driveC, path);
            if (file.isFile()) return file;
        }

        /*
         * No blind walk of drive C.
         *
         * There was one, bounded to five levels, and it ran on every launch of
         * every container with the switch on and no DLL yet — which is exactly
         * the state somebody is in between enabling the setting and copying the
         * file. Walking an installed Windows drive to fail is a pause before a
         * game starts, paid repeatedly, for a file that has four known homes and
         * a pinned path for anywhere else.
         */
        return null;
    }

    // ----------------------------------------------------------------- config

    /**
     * The TOML the layer reads, at the path it reads it from.
     *
     * `$HOME/.config/bionic-fg/conf.toml`, and the guest's home is the
     * container, so this lands inside it and travels with it. The layer watches
     * the file, which is why the console's switch can take effect without a
     * relaunch.
     *
     * `multiplier` is the number of frames shown per drawn one, matching the
     * container's own setting — 0 would load the layer and leave it idle, which
     * is a reasonable default for a menu but not for a switch that has already
     * been turned on.
     */
    /**
     * Rewrites the layer's configuration, taking effect on the running game.
     *
     * <h2>Why writing a file is the whole mechanism</h2>
     *
     * The layer stats its config and reloads when the mtime moves — its own log
     * lines are `config changed; requesting swapchain recreate` and `config
     * hot-reloaded mult=%d flow=%.2f model=%d via context rebuild`. So there is
     * no command channel to build and no IPC to invent: the file <em>is</em> the
     * interface, and a session that wants to change frame generation writes it
     * again.
     *
     * <p>Public for that reason. {@code prepare} calls it once before the guest
     * starts, and the console calls it whenever somebody moves a control, which
     * are the same operation against a layer that may or may not be listening
     * yet.
     *
     * <h2>Two things the layer will not do</h2>
     *
     * It provisions swapchain images for a bounded multiplier when the swapchain
     * is created, so raising the multiplier far mid-session can be refused —
     * it logs `config rebuild failed; keeping old config` and carries on with
     * what it had. And a swapchain it has already abandoned over fence timeouts
     * stays abandoned: its own message says a hot reload will not re-enable it.
     * Neither is worth blocking a control over, but both are why this returns
     * nothing and promises nothing.
     */
    public static void writeConfig(File rootDir, Container container) {
        boolean enabled = isEnabled(container);
        int multiplier = intExtra(container, "frameGenerationMultiplier", 2);
        int flowPercent = intExtra(container, "frameGenerationFlowScale", 75);
        int model = intExtra(container, "frameGenerationModel", 0);
        int fpsLimit = intExtra(container, "frameGenerationFpsLimit", 0);

        StringBuilder toml = new StringBuilder();
        toml.append("# Written by wemu (per-container frame generation)\n");
        /*
         * The key that makes the switch a switch.
         *
         * It was never written, and the layer's own default is on — which is
         * why residency and interpolation used to be the same thing, and why
         * the only way to express "off" was to keep the layer out of the
         * process entirely.
         */
        toml.append("enabled = ").append(enabled).append('\n');
        toml.append("multiplier = ").append(Math.max(2, Math.min(4, multiplier))).append('\n');
        toml.append("flow_scale = ")
                .append(String.format(java.util.Locale.ROOT, "%.2f",
                        Math.max(0.25f, Math.min(1f, flowPercent / 100f))))
                .append('\n');
        /*
         * The container's own values, which these two lines used to discard.
         *
         * They were written as constants — `model = 0` and no cap — while the
         * editor went on offering five interpolation models and a generated-frame
         * limit and storing whatever was chosen. Both settings persisted, neither
         * reached the layer, and the symptom is a control that appears to do
         * nothing because it did nothing.
         */
        toml.append("model = ").append(Math.max(0, Math.min(4, model))).append('\n');
        toml.append("fps_limit_enabled = ").append(fpsLimit > 0).append('\n');
        toml.append("fps_limit = ").append(Math.max(0, fpsLimit)).append('\n');

        File configFile = new File(rootDir,
                ImageFs.HOME_PATH + "/.config/bionic-fg/conf.toml");
        File parent = configFile.getParentFile();
        if (parent != null) parent.mkdirs();
        FileUtils.writeString(configFile, toml.toString());
        Log.d(TAG, "Frame generation config: enabled=" + enabled
                + " multiplier=" + multiplier
                + " flow=" + flowPercent + "% model=" + model
                + " fpsLimit=" + (fpsLimit > 0 ? String.valueOf(fpsLimit) : "off"));
    }

    /** The layer's config for this container, for a session that wants to rewrite it. */
    public static File configFile(Context context) {
        return new File(ImageFs.find(context).getRootDir(),
                ImageFs.HOME_PATH + "/.config/bionic-fg/conf.toml");
    }

    private static int intExtra(Container container, String key, int fallback) {
        String value = container.getExtra(key);
        if (value.isEmpty()) return fallback;
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }
}
