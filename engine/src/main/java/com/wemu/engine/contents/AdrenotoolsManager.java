package com.wemu.engine.contents;

import com.wemu.engine.R;

import android.content.res.AssetManager;
import android.net.Uri;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.core.WemuPaths;
import com.wemu.engine.core.ArchivePathPolicy;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.container.ContainerManager;
import com.wemu.engine.core.DefaultVersion;
import com.wemu.engine.core.DownloadProgressDialog;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.GPUInformation;
import com.wemu.engine.core.GraphicsDriverConfig;
import com.wemu.engine.core.OnExtractFileListener;
import com.wemu.engine.core.TarCompressorUtils;
import com.wemu.engine.xenvironment.ImageFs;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.json.JSONException;
import org.json.JSONObject;

public class AdrenotoolsManager {
    private static final Object DRIVER_INSTALL_LOCK = new Object();
    private static final int MAX_DRIVER_ZIP_ENTRIES = 16_384;
    private static final long MAX_DRIVER_UNPACKED_BYTES = 1L << 30;
    
    private File adrenotoolsContentDir;
    private Context mContext;
    
    public AdrenotoolsManager(Context context) {
        this.mContext = context;
        this.adrenotoolsContentDir = new File(mContext.getFilesDir(), "contents/adrenotools");
        if (!adrenotoolsContentDir.exists())
            adrenotoolsContentDir.mkdirs();
    }
    
    public String getAssetPath(String adrenotoolsDriverId) {
        return "graphics_driver/adrenotools-" + adrenotoolsDriverId + ".tzst";
    }
        
    public String getLibraryName(String adrenoToolsDriverId) {
        String libraryName = "";
        File driverPath = getDriverDirectory(adrenoToolsDriverId);
        if (driverPath == null) return libraryName;
        try {
            File metaProfile = new File(driverPath, "meta.json");
            JSONObject jsonObject = new JSONObject(FileUtils.readString(metaProfile));
            libraryName = jsonObject.getString("libraryName");
        }
        catch (JSONException e) {
        }
        return libraryName;
    }
    
    public String getDriverName(String adrenoToolsDriverId) {
        String driverName = "";
        File driverPath = getDriverDirectory(adrenoToolsDriverId);
        if (driverPath == null) return driverName;
        try {
            File metaProfile = new File(driverPath, "meta.json");
            JSONObject jsonObject = new JSONObject(FileUtils.readString(metaProfile));
            driverName = jsonObject.getString("name");
        }
        catch (JSONException e) {
        }
        return driverName;
    }

    public String getDriverVersion(String adrenoToolsDriverId) {
        String driverVersion = "";
        File driverPath = getDriverDirectory(adrenoToolsDriverId);
        if (driverPath == null) return driverVersion;
        try {
            File metaProfile = new File(driverPath, "meta.json");
            JSONObject jsonObject = new JSONObject(FileUtils.readString(metaProfile));
            driverVersion = jsonObject.getString("driverVersion");
        }
        catch (JSONException e) {
        }
        return driverVersion;
    }

    public String getDriverPath(String adrenotoolsDriverId) {
        File driver = getDriverDirectory(adrenotoolsDriverId);
        return driver != null ? driver.getAbsolutePath() + "/" : "";
    }

    private void reloadContainers(String adrenoToolsDriverId) {
        String driverName = getDriverName(adrenoToolsDriverId);
        ContainerManager containerManager = new ContainerManager(mContext);
        for (Container container : containerManager.getContainers()) {
            HashMap<String, String> config = GraphicsDriverConfig.parse(container.getGraphicsDriverConfig());
            String configuredVersion = config.get("version");
            Log.d("AdrenotoolsManager", "Checking if container driver version "
                    + configuredVersion + " matches " + driverName);
            if (configuredVersion != null && (configuredVersion.equals(adrenoToolsDriverId)
                    || (!driverName.isEmpty() && configuredVersion.contains(driverName)))) {
                Log.d("AdrenotoolsManager", "Found a match for container " + container.getName());
                config.put("version", GPUInformation.isDriverSupported(DefaultVersion.WRAPPER_ADRENO, mContext) ? DefaultVersion.WRAPPER_ADRENO : DefaultVersion.WRAPPER);
                container.setGraphicsDriverConfig(GraphicsDriverConfig.toConfigString(config));
                if (!container.saveData()) {
                    Log.e("AdrenotoolsManager", "Could not persist the replacement driver for container "
                            + container.id);
                }
            }     
        }
        for (Shortcut shortcut : containerManager.loadShortcuts()) {
            HashMap<String, String> config = GraphicsDriverConfig.parse(shortcut.getExtra("graphicsDriverConfig", shortcut.container.getGraphicsDriverConfig()));
            String configuredVersion = config.get("version");
            Log.d("AdrenotoolsManager", "Checking if shortcut driver version "
                    + configuredVersion + " matches " + driverName);
            if (configuredVersion != null && (configuredVersion.equals(adrenoToolsDriverId)
                    || (!driverName.isEmpty() && configuredVersion.contains(driverName)))) {
                Log.d("AdrenotoolsManager", "Found a match for shortcut " + shortcut.name);
                config.put("version", GPUInformation.isDriverSupported(DefaultVersion.WRAPPER_ADRENO, mContext) ? DefaultVersion.WRAPPER_ADRENO : DefaultVersion.WRAPPER);
                shortcut.putExtra("graphicsDriverConfig", GraphicsDriverConfig.toConfigString(config));
                shortcut.saveData();
            }
        }
    }
    
    public void removeDriver(String adrenoToolsDriverId) {
        synchronized (DRIVER_INSTALL_LOCK) {
            File driverPath = getDriverDirectory(adrenoToolsDriverId);
            if (driverPath == null) {
                Log.w("AdrenotoolsManager", "Refusing invalid driver id " + adrenoToolsDriverId);
                return;
            }
            Log.d("AdrenotoolsManager", "Removing driver " + adrenoToolsDriverId);
            reloadContainers(adrenoToolsDriverId);
            if (!FileUtils.delete(driverPath)) {
                Log.e("AdrenotoolsManager", "Could not remove driver " + driverPath);
            }
        }
    }

    public ArrayList<String> enumarateInstalledDrivers() {
        ArrayList<String> driversList = new ArrayList<>();
        
        File[] installed = adrenotoolsContentDir.listFiles();
        if (installed == null) return driversList;
        for (File f : installed) {
            if (!f.isDirectory() || f.getName().equals("tmp")
                    || f.getName().equals("tmp-inner")) continue;
            boolean fromResources = isFromResources(f.getName());
            if (!fromResources && isDriverComplete(f.getName()))
                driversList.add(f.getName());
        }
        return driversList;
    }
    
    /**
     * Whether this driver is one the app itself offers, rather than one the user
     * installed.
     *
     * Answered from the resource array, not by opening the asset. Opening the
     * asset was the same question for exactly as long as the drivers shipped in
     * the APK; once they moved to the payload release it started answering
     * "false" for every built-in driver, which would have quietly listed them
     * all as user-installed in {@code enumarateInstalledDrivers} and skipped
     * them in the installer.
     *
     * The array is the list of drivers this build knows how to obtain, which is
     * what every caller is actually asking about.
     */
    public boolean isFromResources(String adrenotoolsDriverId) {
        boolean isFromResources = false;
        for (String known : mContext.getResources()
                .getStringArray(R.array.wrapper_graphics_driver_version_entries)) {
            if (known.equals(adrenotoolsDriverId)) {
                isFromResources = true;
                break;
            }
        }
        
        return isFromResources;
    }
        
    public boolean extractDriverFromResources(String adrenotoolsDriverId) {
        synchronized (DRIVER_INSTALL_LOCK) {
            return extractDriverFromResourcesLocked(adrenotoolsDriverId, null);
        }
    }

    private boolean extractDriverFromResourcesLocked(
            String adrenotoolsDriverId,
            OnExtractFileListener listener) {
        if (!ArchivePathPolicy.isSafePathComponent(adrenotoolsDriverId)) return false;
        String src = "graphics_driver/adrenotools-" + adrenotoolsDriverId + ".tzst";

        File dst = new File(adrenotoolsContentDir, adrenotoolsDriverId);
        if (isDriverComplete(adrenotoolsDriverId)) return true;
        if (dst.exists() && !FileUtils.delete(dst)) return false;

        if (!dst.mkdirs() && !dst.isDirectory()) return false;
        Log.d("AdrenotoolsManager", "Extracting " + src + " to " + dst.getAbsolutePath());
        boolean hasExtracted = GuestPayloads.extractOrFetch(mContext, src, dst, listener);

        if (!hasExtracted || !isDriverComplete(adrenotoolsDriverId)) {
            FileUtils.delete(dst);
            return false;
        }

        return true;
    }
    
    public boolean extractDriverFromResources(String adrenotoolsDriverId, OnExtractFileListener listener) {
        synchronized (DRIVER_INSTALL_LOCK) {
            return extractDriverFromResourcesLocked(adrenotoolsDriverId, listener);
        }
    }
    
    /**
     * Unpacks a driver package and adopts it if it looks like one.
     *
     * @return the installed driver's name, or an empty string on any failure.
     *
     * Three things the original could not survive, all of which are ordinary in
     * driver zips published to GitHub:
     *
     *  - **A folder inside.** `Files.copy` to `tmp/driver/meta.json` throws
     *    `NoSuchFileException` when `tmp/driver` does not exist, so any package
     *    that was zipped with its containing directory failed outright.
     *  - **Directory entries**, which are not files and must not be copied.
     *  - **A leftover `tmp`.** `File.delete()` does nothing to a non-empty
     *    directory and returns false, so a previous failed install poisoned
     *    every attempt after it.
     */
    public String installDriver(Uri driverUri) {
        synchronized (DRIVER_INSTALL_LOCK) {
            return installDriverLocked(driverUri);
        }
    }

    private String installDriverLocked(Uri driverUri) {
        File tmpDir = new File(adrenotoolsContentDir, "tmp");
        File hoisted = new File(adrenotoolsContentDir, "tmp-inner");
        FileUtils.delete(tmpDir);
        FileUtils.delete(hoisted);
        if (!tmpDir.mkdirs() && !tmpDir.isDirectory()) return "";
        String name = "";

        try (InputStream input = mContext.getContentResolver().openInputStream(driverUri)) {
            if (input == null) throw new IOException("The driver package could not be opened");
            try (ZipInputStream zip = new ZipInputStream(input)) {
                int entryCount = 0;
                long unpackedBytes = 0;
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entryCount > MAX_DRIVER_ZIP_ENTRIES) {
                        throw new IOException("The driver package contains too many entries");
                    }
                    File dstFile = ArchivePathPolicy.resolveEntry(tmpDir, entry.getName());

                    if (entry.isDirectory()) {
                        ArchivePathPolicy.createSafeDirectory(tmpDir, dstFile);
                    } else {
                        ArchivePathPolicy.createSafeParents(tmpDir, dstFile);
                        ArchivePathPolicy.rejectExistingLink(dstFile);
                        try (java.io.FileOutputStream output =
                                     new java.io.FileOutputStream(dstFile)) {
                            byte[] buffer = new byte[32 * 1024];
                            int read;
                            while ((read = zip.read(buffer)) != -1) {
                                unpackedBytes += read;
                                if (unpackedBytes > MAX_DRIVER_UNPACKED_BYTES) {
                                    throw new IOException(
                                            "The driver package expands beyond the 1 GiB limit");
                                }
                                output.write(buffer, 0, read);
                            }
                        }
                    }
                    zip.closeEntry();
                }
            }

            // A package zipped with its containing folder puts everything one
            // level down. Adopt that level as the driver rather than rejecting
            // a package that is otherwise perfectly good.
            File root = descendIntoSingleFolder(tmpDir);
            if (root != tmpDir && new File(root, "meta.json").exists()) {
                if (root.renameTo(hoisted)) {
                    FileUtils.delete(tmpDir);
                    if (!hoisted.renameTo(tmpDir)) {
                        throw new IOException("Could not publish the package's driver directory");
                    }
                }
            }

            if (new File(tmpDir, "meta.json").exists()) {
                name = getDriverName(tmpDir.getName());
                String libraryName = getLibraryName(tmpDir.getName());
                if (!ArchivePathPolicy.isSafePathComponent(name)
                        || name.equals("tmp") || name.equals("tmp-inner")
                        || !ArchivePathPolicy.isSafePathComponent(libraryName)
                        || !new File(tmpDir, libraryName).isFile()) {
                    Log.w("AdrenotoolsManager",
                            "Driver metadata has an unsafe name or missing library");
                    name = "";
                    FileUtils.delete(tmpDir);
                } else {
                    File dst = ArchivePathPolicy.resolveEntry(adrenotoolsContentDir, name);
                    if (dst.exists() || !tmpDir.renameTo(dst)) {
                        name = "";
                        FileUtils.delete(tmpDir);
                    }
                }
            }
            else {
                Log.d("AdrenotoolsManager",
                        "No meta.json in the package; this is not an AdrenoTools driver");
                FileUtils.delete(tmpDir);
            }
        }
        catch (IOException | RuntimeException e) {
            Log.w("AdrenotoolsManager", "Could not unpack the driver package", e);
            FileUtils.delete(tmpDir);
        }
        FileUtils.delete(hoisted);

        return name;
    }

    /**
     * The single directory a package unpacked into, or the directory itself.
     *
     * A zip made with `zip -r driver.zip driver/` puts everything one level
     * down; one made with `cd driver && zip -r ../driver.zip .` does not. Both
     * are published, so both have to work.
     */
    private static File descendIntoSingleFolder(File dir) {
        File[] children = dir.listFiles();
        if (children == null || children.length != 1 || !children[0].isDirectory()) return dir;
        return children[0];
    }
    
    public boolean setDriverById(EnvVars envVars, ImageFs imagefs, String adrenotoolsDriverId) {
        boolean isFromResources = isFromResources(adrenotoolsDriverId);

        if (isFromResources || enumarateInstalledDrivers().contains(adrenotoolsDriverId)) {
            String driverPath = getDriverPath(adrenotoolsDriverId);
            if (driverPath.isEmpty()) return false;
            String libraryName = getLibraryName(adrenotoolsDriverId);

            if (ArchivePathPolicy.isSafePathComponent(libraryName)
                    && new File(driverPath, libraryName).isFile()) {
                envVars.put("ADRENOTOOLS_DRIVER_PATH", driverPath);
                envVars.put("ADRENOTOOLS_HOOKS_PATH", imagefs.getLibDir());
                envVars.put("ADRENOTOOLS_DRIVER_NAME", libraryName);

                File sharedDir = new File(WemuPaths.DEFAULT_SHARED_PATH);
                File qglConfig = new File(sharedDir, "qgl_config.txt");
                if (qglConfig.exists())
                    envVars.put("ADRENOTOOLS_REDIRECT_DIR", sharedDir.getAbsolutePath() + "/");
                return true;
            }
        }
        return false;
    }

    private File getDriverDirectory(String driverId) {
        if (!ArchivePathPolicy.isSafePathComponent(driverId)) return null;
        try {
            return ArchivePathPolicy.resolveEntry(adrenotoolsContentDir, driverId);
        } catch (IOException e) {
            return null;
        }
    }

    private boolean isDriverComplete(String driverId) {
        File directory = getDriverDirectory(driverId);
        if (directory == null || !directory.isDirectory()) return false;
        String libraryName = getLibraryName(driverId);
        return ArchivePathPolicy.isSafePathComponent(libraryName)
                && new File(directory, "meta.json").isFile()
                && new File(directory, libraryName).isFile();
    }
 }
