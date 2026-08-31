package com.wemu.engine.container;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.wemu.engine.R;
import com.wemu.engine.contents.GuestPayloads;
import com.wemu.engine.contents.ContentsManager;
import com.wemu.engine.core.Callback;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.MSLink;
import com.wemu.engine.core.OnExtractFileListener;
import com.wemu.engine.core.TarCompressorUtils;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.xenvironment.ImageFs;

import java.util.Arrays;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class ContainerManager {
    /** Serializes mutations across the several manager instances the app owns. */
    private static final Object STORAGE_LOCK = new Object();
    /** One process-lifetime worker instead of leaking one executor per operation. */
    private static final ExecutorService CONTAINER_IO = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "Wemu-container-io");
        thread.setDaemon(true);
        return thread;
    });

    private final ArrayList<Container> containers = new ArrayList<>();
    private int maxContainerId = 0;
    private final File homeDir;
    private final Context context;

    private boolean isInitialized = false; // New flag to track initialization

    public ContainerManager(Context context) {
        this.context = context;
        File rootDir = ImageFs.find(context).getRootDir();
        homeDir = new File(rootDir, "home");
        loadContainers();
        isInitialized = true;
    }

    // Check if the ContainerManager is fully initialized
    public boolean isInitialized() {
        return isInitialized;
    }

    /**
     * Re-reads the containers from disk.
     *
     * The list is a snapshot taken in the constructor, and more than one manager
     * exists — `:app` keeps one behind its container repository and another
     * behind its shortcut repository. A container created through the first was
     * therefore invisible to the second for the rest of the process, which is
     * not an abstract problem: making a container and then adding a game to it
     * is the ordinary first-run path, and `getContainerById` answered null for
     * the container the user had just made.
     *
     * `loadContainers` clears before it rebuilds, so this is safe to call as
     * often as a lookup needs it.
     */
    public void reload() {
        synchronized (STORAGE_LOCK) {
            loadContainersLocked();
        }
    }

    public ArrayList<Container> getContainers() {
        synchronized (STORAGE_LOCK) {
            return new ArrayList<>(containers);
        }
    }

    // Load containers from the home directory
    private void loadContainers() {
        synchronized (STORAGE_LOCK) {
            loadContainersLocked();
        }
    }

    private void loadContainersLocked() {
        containers.clear();
        maxContainerId = 0;

        File[] files = homeDir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isDirectory() && !FileUtils.isSymlink(file)
                        && file.getName().startsWith(ImageFs.USER + "-")) {
                    try {
                        String idText = file.getName().substring((ImageFs.USER + "-").length());
                        if (idText.isEmpty()) continue;
                        int id = Integer.parseInt(idText);
                        if (id <= 0) continue;
                        Container container = new Container(id, this);

                        container.setRootDir(file);
                        File configFile = container.getConfigFile();
                        if (!configFile.isFile() || configFile.length() == 0) continue;
                        JSONObject data = new JSONObject(FileUtils.readString(configFile));
                        container.loadData(data);
                        containers.add(container);
                        maxContainerId = Math.max(maxContainerId, container.id);
                    } catch (JSONException | RuntimeException e) {
                        // One interrupted or hand-edited config must not hide all
                        // of the healthy containers that follow it.
                        Log.e("ContainerManager", "Skipping unreadable container " + file, e);
                    }
                }
            }
        }
        containers.sort(Comparator.comparingInt(container -> container.id));
    }


    public Context getContext() {
        return context;
    }


    public void activateContainer(Container container) {
        if (container == null) throw new IllegalArgumentException("Container is missing");
        synchronized (STORAGE_LOCK) {
            container.setRootDir(new File(homeDir, ImageFs.USER+"-"+container.id));
            if (!container.getRootDir().isDirectory()) {
                throw new IllegalStateException("Container directory is missing: "
                        + container.getRootDir());
            }

            File link = new File(homeDir, ImageFs.USER);
            String target = "./"+ImageFs.USER+"-"+container.id;

        /*
         * `File.delete` cannot remove a directory that has anything in it, and
         * it reports that by returning false, which the original ignored — as
         * does `FileUtils.symlink`, whose `Os.symlink` then fails EEXIST into an
         * empty catch. Both failures are silent, so the link keeps pointing
         * wherever it did and the container that was asked for is never reached.
         *
         * That matters for repairing an install rather than a fresh one: if this
         * ever ran without the link in place, the extractions created
         * `home/xuser` as a real directory and filled it. It is not a container,
         * nothing reads it, and while it is there no container can be activated.
         *
         * `FileUtils.delete` clears a real directory and deliberately does not
         * descend into a symlink, so the case where this is already a link to a
         * real container removes the link and not the container behind it.
         */
            if (FileUtils.isSymlink(link)) {
                if (!link.delete()) {
                    throw new IllegalStateException(
                            "Could not replace active container link " + link);
                }
            } else if (link.exists()) {
                Log.w("ContainerManager", "Replacing real directory " + link
                        + " with a link to " + target);
                if (!FileUtils.delete(link)) {
                    throw new IllegalStateException("Could not remove " + link
                            + "; container " + container.id + " is not active");
                }
            }

            FileUtils.symlink(target, link.getPath());

            if (!FileUtils.isSymlink(link) || !target.equals(FileUtils.readSymlink(link))) {
                throw new IllegalStateException("Could not link " + link + " to " + target);
            }
        }
    }

    public void createContainerAsync(final JSONObject data, ContentsManager contentsManager, Callback<Container> callback) {
        final Handler handler = new Handler(Looper.getMainLooper());
        CONTAINER_IO.execute(() -> {
            final Container container = createContainer(data, contentsManager);
            handler.post(() -> callback.call(container));
        });
    }

    public void duplicateContainerAsync(Container container, Runnable callback) {
        final Handler handler = new Handler(Looper.getMainLooper());
        CONTAINER_IO.execute(() -> {
            duplicateContainer(container);
            handler.post(callback);
        });
    }

    public void removeContainerAsync(Container container, Runnable callback) {
        final Handler handler = new Handler(Looper.getMainLooper());
        CONTAINER_IO.execute(() -> {
            removeContainer(container);
            handler.post(callback);
        });
    }

    private Container createContainer(JSONObject data, ContentsManager contentsManager) {
        synchronized (STORAGE_LOCK) {
            File containerDir = null;
            try {
                loadContainersLocked();
                AllocatedDirectory allocation = allocateContainerDirectoryLocked();
                int id = allocation.id;
                containerDir = allocation.directory;
                JSONObject containerData = new JSONObject(data.toString());
                containerData.put("id", id);

                Container container = new Container(id, this);
                container.setRootDir(containerDir);
                container.loadData(containerData);

                container.setWineVersion(containerData.optString(
                        "wineVersion", WineInfo.MAIN_WINE_VERSION.identifier()));

                if (!extractContainerPatternFile(container, container.getWineVersion(),
                        contentsManager, containerDir, null)) {
                    FileUtils.delete(containerDir);
                    return null;
                }

//            // Extract the selected graphics driver files
//            String driverVersion = container.getGraphicsDriverVersion();
//            if (!extractGraphicsDriverFiles(driverVersion, containerDir, null)) {
//                FileUtils.delete(containerDir);
//                return null;
//            }

                if (!container.saveData()) {
                    FileUtils.delete(containerDir);
                    return null;
                }
                maxContainerId = Math.max(maxContainerId, id);
                containers.add(container);
                return container;
            } catch (JSONException | RuntimeException | IOException e) {
                Log.e("ContainerManager", "Could not create container", e);
                if (containerDir != null) FileUtils.delete(containerDir);
            }
            return null;
        }
    }


    private void duplicateContainer(Container srcContainer) {
        synchronized (STORAGE_LOCK) {
            File dstDir = null;
            try {
                if (srcContainer == null) {
                    throw new IllegalArgumentException("Container is missing");
                }
                loadContainersLocked();
                Container currentSource = getContainerByIdLocked(srcContainer.id);
                if (currentSource == null) {
                    Log.e("ContainerManager", "Could not duplicate missing container "
                            + srcContainer.id);
                    return;
                }
                if ("1".equals(currentSource.getExtra("wemuOfficialSteamRuntime"))) {
                    // A managed prefix is trusted as one exact runtime/container tuple.
                    // Copying its marker would mint a second trusted identity; stripping
                    // it would leave copied Steam state running outside the preflight.
                    // Neither is a valid duplicate, so managed containers are immutable
                    // through this generic operation.
                    Log.e("ContainerManager", "Refusing to duplicate managed Steam container "
                            + srcContainer.id);
                    return;
                }
                AllocatedDirectory allocation = allocateContainerDirectoryLocked();
                int id = allocation.id;
                dstDir = allocation.directory;

                if (!FileUtils.copy(currentSource.getRootDir(), dstDir,
                        file -> FileUtils.chmod(file, 0771))) {
                    FileUtils.delete(dstDir);
                    return;
                }

                Container dstContainer = new Container(id, this);
                dstContainer.setRootDir(dstDir);
                // Load the complete in-memory schema so every ordinary field,
                // performance option and future extra survives duplication.
                dstContainer.loadData(new JSONObject(currentSource.toJson().toString()));
                dstContainer.setName(currentSource.getName() + " ("
                        + context.getString(R.string._copy) + ")");
                if (!dstContainer.saveData()) {
                    FileUtils.delete(dstDir);
                    return;
                }

                maxContainerId = Math.max(maxContainerId, id);
                containers.add(dstContainer);
            } catch (JSONException | IOException | RuntimeException e) {
                Log.e("ContainerManager", "Could not duplicate container "
                        + (srcContainer != null ? srcContainer.id : "null"), e);
                if (dstDir != null) FileUtils.delete(dstDir);
            }
        }
    }


    private void removeContainer(Container container) {
        synchronized (STORAGE_LOCK) {
            if (container == null) return;
            loadContainersLocked();
            Container current = getContainerByIdLocked(container.id);
            if (current == null) return;
            if (FileUtils.delete(current.getRootDir())) {
                containers.remove(current);
                removeActiveLinkIfItTargets(container.id);
            } else {
                Log.e("ContainerManager", "Could not remove container " + container.id);
            }
        }
    }

    public ArrayList<Shortcut> loadShortcuts() {
        ArrayList<Shortcut> shortcuts = new ArrayList<>();
        for (Container container : getContainers()) {
            File desktopDir = container.getDesktopDir();
            ArrayList<File> files = new ArrayList<>();
            File[] desktopFiles = desktopDir.listFiles();
            if (desktopFiles != null) files.addAll(Arrays.asList(desktopFiles));
            for (File file : files) {
                    String fileName = file.getName();
                    if (fileName.endsWith(".lnk")) {
                        String filePath = file.getPath();
                        File desktopFile = new File(filePath.substring(0, filePath.lastIndexOf(".")) + ".desktop");
                        if (!desktopFile.exists()) {
                            MSLink.createDesktopFile(file, context);
                            shortcuts.add(new Shortcut(container, desktopFile));
                        }
                    }
                    else if (fileName.endsWith(".desktop")) shortcuts.add(new Shortcut(container, file));
            }
        }

        shortcuts.sort(Comparator.comparing(a -> a.name));
        return shortcuts;
    }

    public int getNextContainerId() {
        synchronized (STORAGE_LOCK) {
            loadContainersLocked();
            return maxContainerId + 1;
        }
    }

    public Container getContainerById(int id) {
        synchronized (STORAGE_LOCK) {
            loadContainersLocked();
            return getContainerByIdLocked(id);
        }
    }

    private void extractCommonDlls(WineInfo wineInfo, String srcName, String dstName, File containerDir, OnExtractFileListener onExtractFileListener) throws JSONException {
        File srcDir = new File(wineInfo.path + "/lib/wine/" + srcName);

        File[] srcfiles = srcDir.listFiles(file -> file.isFile());
        if (srcfiles == null) throw new JSONException("Missing Wine files");

        for (File file : srcfiles) {
            String dllName = file.getName();
            if (dllName.equals("iexplore.exe") && wineInfo.isArm64EC() && srcName.equals("aarch64-windows"))
                file = new File(wineInfo.path + "/lib/wine/" + "i386-windows/iexplore.exe");
            if (dllName.equals("tabtip.exe") || dllName.equals("icu.dll"))
                continue;
            File dstFile = new File(containerDir, ".wine/drive_c/windows/" + dstName + "/" + dllName);
            if (dstFile.exists()) continue;
            if (onExtractFileListener != null ) {
                dstFile = onExtractFileListener.onExtractFile(dstFile, 0);
                if (dstFile == null) continue;
            }
            if (!FileUtils.copy(file, dstFile) || !dstFile.isFile()) {
                throw new JSONException("Could not provision Wine file " + dllName);
            }
        }
    }

    public boolean extractContainerPatternFile(Container container, String wineVersion, ContentsManager contentsManager, File containerDir, OnExtractFileListener onExtractFileListener) {
        WineInfo wineInfo = WineInfo.fromIdentifier(context, contentsManager, wineVersion);
        if (wineInfo.path == null || wineInfo.path.isEmpty()) return false;
        String containerPattern = wineVersion + "_container_pattern.tzst";
        File containerPatternFile = new File(wineInfo.path + "/prefixPack.txz");
        /*
         * An installed Wine/Proton profile owns its prefix pack. Prefer it over
         * the network payload catalog: entry-name identifiers such as
         * Proton-9.0-x86_64-0 do not have a matching remote payload name and
         * used to spend a launch on a guaranteed 404 before using this file.
         */
        boolean installedProfile = contentsManager.getProfileByEntryName(wineVersion) != null;
        boolean result = installedProfile && containerPatternFile.isFile()
                && TarCompressorUtils.extract(TarCompressorUtils.Type.XZ,
                        containerPatternFile, containerDir, onExtractFileListener);

        if (!result)
            result = GuestPayloads.extractOrFetch(context, containerPattern, containerDir,
                    onExtractFileListener);

        if (!result && !installedProfile)
            result = TarCompressorUtils.extract(TarCompressorUtils.Type.XZ,
                    containerPatternFile, containerDir, onExtractFileListener);

        if (result) {
            try {
                if (wineInfo.isArm64EC())
                    extractCommonDlls(wineInfo, "aarch64-windows", "system32", containerDir, onExtractFileListener); // arm64ec only
                else
                    extractCommonDlls(wineInfo, "x86_64-windows", "system32", containerDir, onExtractFileListener);

                extractCommonDlls(wineInfo, "i386-windows", "syswow64", containerDir, onExtractFileListener);
            }
            catch (JSONException e) {
                return false;
            }
        }
   
        return result;
    }

    public Container getContainerForShortcut(Shortcut shortcut) {
        if (shortcut == null) return null;
        synchronized (STORAGE_LOCK) {
            loadContainersLocked();
            return getContainerByIdLocked(shortcut.getContainerId());
        }
    }

    private Container getContainerByIdLocked(int id) {
        for (Container container : containers) if (container.id == id) return container;
        return null;
    }

    /** mkdir is the reservation, so a second process cannot claim the same id. */
    private AllocatedDirectory allocateContainerDirectoryLocked() throws IOException {
        if (!homeDir.isDirectory() && !homeDir.mkdirs()) {
            throw new IOException("Could not create container home " + homeDir);
        }
        int id = Math.max(1, maxContainerId + 1);
        while (id > 0) {
            File candidate = new File(homeDir, ImageFs.USER + "-" + id);
            if (candidate.mkdir()) return new AllocatedDirectory(id, candidate);
            if (!candidate.exists()) {
                throw new IOException("Could not reserve container directory " + candidate);
            }
            if (id == Integer.MAX_VALUE) break;
            id++;
        }
        throw new IOException("No container id is available");
    }

    private void removeActiveLinkIfItTargets(int id) {
        File link = new File(homeDir, ImageFs.USER);
        String expected = "./" + ImageFs.USER + "-" + id;
        if (FileUtils.isSymlink(link) && expected.equals(FileUtils.readSymlink(link))
                && !link.delete()) {
            Log.w("ContainerManager", "Removed container " + id
                    + " but could not remove its active link " + link);
        }
    }

    private static final class AllocatedDirectory {
        final int id;
        final File directory;

        AllocatedDirectory(int id, File directory) {
            this.id = id;
            this.directory = directory;
        }
    }

    // Utility method to run on UI thread
    private void runOnUiThread(Runnable action) {
        new Handler(Looper.getMainLooper()).post(action);
    }



}
