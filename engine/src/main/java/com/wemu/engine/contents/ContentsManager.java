package com.wemu.engine.contents;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.util.Log;

import androidx.annotation.NonNull;

import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.ArchivePathPolicy;
import com.wemu.engine.core.TarCompressorUtils;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;

public class ContentsManager {
    public static final String PROFILE_NAME = "profile.json";
    /**
     * The content index this build reads by default.
     *
     * Not the fork's own, which carries 54 entries of which exactly three are
     * Wine or Proton — one Proton 9, one Proton 10, one Wine 9.20 — so the part
     * of Downloads that matters most is the part that is empty. This one
     * carries 158 entries and 26 Wine and Proton builds, including Proton 10
     * and 11 and GE-Proton 11 for arm64ec, in the same schema.
     *
     * It also publishes builds tagged `sdk28` and `sdk35`, which matters here
     * more than it looks: this app targets SDK 28 deliberately (see
     * `libs.versions.toml`), and the two are not interchangeable.
     */
    public static final String REMOTE_PROFILES = "https://raw.githubusercontent.com/The412Banner/winlator-contents/main/contents.json";

    /**
     * The index this used to point at.
     *
     * Kept so a stored preference holding it can be recognised and replaced.
     * Changing a default does nothing to a value already written down — a
     * lesson this project learned the hard way when two rendering flags stayed
     * on in every container that had been saved while they defaulted to on.
     */
    public static final String LEGACY_REMOTE_PROFILES = "https://raw.githubusercontent.com/StevenMXZ/Winlator-Contents/main/contents.json";
    public static final String[] DXVK_TRUST_FILES = {"${system32}/d3d8.dll", "${system32}/d3d9.dll", "${system32}/d3d10.dll", "${system32}/d3d10_1.dll",
            "${system32}/d3d10core.dll", "${system32}/d3d11.dll", "${system32}/dxgi.dll", "${syswow64}/d3d8.dll", "${syswow64}/d3d9.dll", "${syswow64}/d3d10.dll",
            "${syswow64}/d3d10_1.dll", "${syswow64}/d3d10core.dll", "${syswow64}/d3d11.dll", "${syswow64}/dxgi.dll"};
    public static final String[] VKD3D_TRUST_FILES = {"${system32}/d3d12core.dll", "${system32}/d3d12.dll",
            "${syswow64}/d3d12core.dll", "${syswow64}/d3d12.dll"};
    public static final String[] BOX64_TRUST_FILES = {"${bindir}/box64"};
    public static final String[] WOWBOX64_TRUST_FILES = {"${system32}/wowbox64.dll"};
    public static final String[] FEXCORE_TRUST_FILES = {"${system32}/libwow64fex.dll", "${system32}/libarm64ecfex.dll"};
    private Map<String, String> dirTemplateMap;
    private Map<ContentProfile.ContentType, List<String>> trustedFilesMap;

    private SharedPreferences preferences;

    public enum InstallFailedReason {
        ERROR_NOSPACE,
        ERROR_BADTAR,
        ERROR_NOPROFILE,
        ERROR_BADPROFILE,
        ERROR_MISSINGFILES,
        ERROR_EXIST,
        ERROR_UNTRUSTPROFILE,
        ERROR_UNKNOWN
    }

    public enum ContentDirName {
        CONTENT_MAIN_DIR_NAME("contents"),
        CONTENT_WINE_DIR_NAME("wine"),
        CONTENT_DXVK_DIR_NAME("dxvk"),
        CONTENT_VKD3D_DIR_NAME("vkd3d"),
        CONTENT_BOX64_DIR_NAME("box64");

        private String name;

        ContentDirName(String name) {
            this.name = name;
        }

        @NonNull
        @Override
        public String toString() {
            return name;
        }
    }

    private final Context context;

    private HashMap<ContentProfile.ContentType, List<ContentProfile>> profilesMap;

    private ArrayList<ContentProfile> remoteProfiles;

    public ContentsManager(Context context) {
        this.context = context;
        this.preferences = context.getSharedPreferences("contents_manager_prefs", Context.MODE_PRIVATE);
    }

    // Method to mark the graphics driver as installed
    public void setGraphicsDriverInstalled(String driverVersion, boolean installed) {
        preferences.edit().putBoolean("graphics_driver_installed_" + driverVersion, installed).apply();
    }

    public interface OnInstallFinishedCallback {
        void onFailed(InstallFailedReason reason, Exception e);

        void onSucceed(ContentProfile profile);
    }

    public void setRemoteProfiles(String json) {
        try {
            remoteProfiles = new ArrayList<>();
            JSONArray content = new JSONArray(json);
            for (int i = 0; i < content.length(); i++) {
                try {
                    JSONObject object = content.getJSONObject(i);
                    ContentProfile remoteProfile = new ContentProfile();
                    remoteProfile.remoteUrl = object.getString("remoteUrl");
                    remoteProfile.type = ContentProfile.ContentType.getTypeByName(object.getString("type"));
                    remoteProfile.verName = object.getString("verName");
                    remoteProfile.verCode = object.getInt("verCode");
                    if (remoteProfile.type == null
                            || !ArchivePathPolicy.isSafePathComponent(remoteProfile.verName)
                            || remoteProfile.verCode < 0) {
                        Log.w("ContentsManager", "Ignoring unsafe remote content profile");
                        continue;
                    }
                    remoteProfiles.add(remoteProfile);
                } catch (JSONException e) {
                    e.printStackTrace();
                }
            }
        } catch (JSONException e) {
            e.printStackTrace();
        }
        syncContents();
    }

    public void syncContents() {
        profilesMap = new HashMap<>();

        // Ensure all content types are initialized in the profilesMap
        for (ContentProfile.ContentType type : ContentProfile.ContentType.values()) {
            profilesMap.put(type, new LinkedList<>());
        }

        for (ContentProfile.ContentType type : ContentProfile.ContentType.values()) {
            List<ContentProfile> profiles = profilesMap.get(type);


            // Load local profiles
            File typeFile = getContentTypeDir(context, type);
            File[] fileList = typeFile.listFiles();
            if (fileList != null) {
                for (File file : fileList) {
                    File proFile = new File(file, PROFILE_NAME);
                    if (proFile.exists() && proFile.isFile()) {
                        ContentProfile profile = readProfile(proFile);
                        if (profile != null) {
                            profiles.add(profile);
                            Log.d("ContentsManager", "Local profile loaded: " + profile.verName);
                        } else {
                            Log.w("ContentsManager", "Invalid local profile at: " + proFile.getAbsolutePath());
                        }
                    }
                }
            }

            // Add remote profiles for this type
            if (remoteProfiles != null) {
                for (ContentProfile remote : remoteProfiles) {
                    if (remote.type == type) {
                        boolean exists = false;
                        for (ContentProfile profile : profiles) {
                            if (profile.verName.equals(remote.verName) && profile.verCode == remote.verCode) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            profiles.add(remote);
                            Log.d("ContentsManager", "Remote profile added: " + remote.verName);
                        }
                    }
                }
            }
        }
    }

    public void extraContentFile(Uri uri, OnInstallFinishedCallback callback) {
        cleanTmpDir(context);

        File file = getTmpDir(context);

        TarCompressorUtils.Type detectedType = TarCompressorUtils.detectType(context, uri);
        boolean ret;
        if (detectedType != null) {
            ret = TarCompressorUtils.extract(detectedType, context, uri, file);
        } else {
            // Retain compatibility with unusual streams that do not expose a
            // recognizable signature, but isolate every decoder attempt.
            ret = TarCompressorUtils.extract(TarCompressorUtils.Type.XZ, context, uri, file);
            if (!ret) {
                cleanTmpDir(context);
                file = getTmpDir(context);
                ret = TarCompressorUtils.extract(TarCompressorUtils.Type.ZSTD, context, uri, file);
            }
            if (!ret) {
                cleanTmpDir(context);
                file = getTmpDir(context);
                ret = TarCompressorUtils.extract(TarCompressorUtils.Type.GZIP, context, uri, file);
            }
        }
        if (!ret) {
            cleanTmpDir(context);
            callback.onFailed(InstallFailedReason.ERROR_BADTAR, null);
            return;
        }

        File proFile = new File(file, PROFILE_NAME);
        if (!proFile.exists()) {
            callback.onFailed(InstallFailedReason.ERROR_NOPROFILE, null);
            return;
        }

        ContentProfile profile = readProfile(proFile);
        if (profile == null) {
            callback.onFailed(InstallFailedReason.ERROR_BADPROFILE, null);
            return;
        }

        String imagefsPath = context.getFilesDir().getAbsolutePath() + "/imagefs";
        for (ContentProfile.ContentFile contentFile : profile.fileList) {
            File tmpFile;
            try {
                tmpFile = ArchivePathPolicy.resolveEntry(file, contentFile.source);
                ArchivePathPolicy.requireContainedOutput(file, tmpFile);
            } catch (IOException unsafe) {
                callback.onFailed(InstallFailedReason.ERROR_UNTRUSTPROFILE, unsafe);
                return;
            }
            if (!tmpFile.isFile() || Files.isSymbolicLink(tmpFile.toPath())) {
                callback.onFailed(InstallFailedReason.ERROR_MISSINGFILES, null);
                return;
            }

            String realPath = getPathFromTemplate(contentFile.target);
            if (!isSubPath(imagefsPath, realPath) || isSubPath(ContentsManager.getContentDir(context).getAbsolutePath(), realPath) || realPath.contains("dosdevices")) {
                callback.onFailed(InstallFailedReason.ERROR_UNTRUSTPROFILE, null);
                return;
            }
        }

        if (profile.type == ContentProfile.ContentType.CONTENT_TYPE_WINE || profile.type == ContentProfile.ContentType.CONTENT_TYPE_PROTON) {
            File bin;
            File lib;
            File cp;
            try {
                bin = ArchivePathPolicy.resolveEntry(file, profile.wineBinPath);
                lib = ArchivePathPolicy.resolveEntry(file, profile.wineLibPath);
                cp = ArchivePathPolicy.resolveEntry(file, profile.winePrefixPack);
                ArchivePathPolicy.requireContainedOutput(file, bin);
                ArchivePathPolicy.requireContainedOutput(file, lib);
                ArchivePathPolicy.requireContainedOutput(file, cp);
            } catch (IOException unsafe) {
                callback.onFailed(InstallFailedReason.ERROR_UNTRUSTPROFILE, unsafe);
                return;
            }

            if (!bin.isDirectory() || Files.isSymbolicLink(bin.toPath())
                    || !lib.isDirectory() || Files.isSymbolicLink(lib.toPath())
                    || !cp.isFile() || Files.isSymbolicLink(cp.toPath())) {
                callback.onFailed(InstallFailedReason.ERROR_MISSINGFILES, null);
                return;
            }
        }

        callback.onSucceed(profile);
    }

    public void finishInstallContent(ContentProfile profile, OnInstallFinishedCallback callback) {
        final File installPath;
        try {
            installPath = getInstallDir(context, profile);
        } catch (IllegalArgumentException unsafe) {
            callback.onFailed(InstallFailedReason.ERROR_UNTRUSTPROFILE, unsafe);
            return;
        }
        if (installPath.exists()) {
            callback.onFailed(InstallFailedReason.ERROR_EXIST, null);
            return;
        }

        File installParent = installPath.getParentFile();
        if (installParent == null
                || (!installParent.isDirectory() && !installParent.mkdirs())) {
            callback.onFailed(InstallFailedReason.ERROR_UNKNOWN, null);
            return;
        }

        if (!getTmpDir(context).renameTo(installPath)) {
            callback.onFailed(InstallFailedReason.ERROR_UNKNOWN, null);
            // Was missing, so a failed move reported failure *and then* success.
            // A caller that treats the two as exclusive — anything bridging this
            // to a coroutine, for one — is resumed twice and throws.
            return;
        }

        callback.onSucceed(profile);
    }

    public ContentProfile readProfile(File file) {
        try {
            ContentProfile profile = new ContentProfile();
            JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
            String typeName = profileJSONObject.getString(ContentProfile.MARK_TYPE);
            String verName = profileJSONObject.getString(ContentProfile.MARK_VERSION_NAME);
            int verCode = profileJSONObject.getInt(ContentProfile.MARK_VERSION_CODE);
            String desc = profileJSONObject.getString(ContentProfile.MARK_DESC);
            ContentProfile.ContentType type = ContentProfile.ContentType.getTypeByName(typeName);
            if (type == null || !ArchivePathPolicy.isSafePathComponent(verName)
                    || verCode < 0) return null;

            JSONArray fileJSONArray = profileJSONObject.getJSONArray(ContentProfile.MARK_FILE_LIST);
            List<ContentProfile.ContentFile> fileList = new ArrayList<>();
            for (int i = 0; i < fileJSONArray.length(); i++) {
                JSONObject contentFileJSONObject = fileJSONArray.getJSONObject(i);
                ContentProfile.ContentFile contentFile = new ContentProfile.ContentFile();
                contentFile.source = contentFileJSONObject.getString(ContentProfile.MARK_FILE_SOURCE);
                contentFile.target = contentFileJSONObject.getString(ContentProfile.MARK_FILE_TARGET);
                if (!isSafeRelativeEntry(contentFile.source)) return null;
                fileList.add(contentFile);
            }
            if (typeName.equals(ContentProfile.ContentType.CONTENT_TYPE_WINE.toString()) || typeName.equals(ContentProfile.ContentType.CONTENT_TYPE_PROTON.toString())) {
                JSONObject wineJSONObject = profileJSONObject.getJSONObject(ContentProfile.MARK_WINE);
                profile.wineLibPath = wineJSONObject.getString(ContentProfile.MARK_WINE_LIBPATH);
                profile.wineBinPath = wineJSONObject.getString(ContentProfile.MARK_WINE_BINPATH);
                profile.winePrefixPack = wineJSONObject.getString(ContentProfile.MARK_WINE_PREFIX_PACK);
                if (!isSafeRelativeEntry(profile.wineLibPath)
                        || !isSafeRelativeEntry(profile.wineBinPath)
                        || !isSafeRelativeEntry(profile.winePrefixPack)) return null;
            }

            profile.type = type;
            profile.verName = verName;
            profile.verCode = verCode;
            profile.desc = desc;
            profile.fileList = fileList;
            return profile;
        } catch (Exception e) {
            return null;
        }
    }

    public List<ContentProfile> getProfiles(ContentProfile.ContentType type) {
        if (profilesMap != null)
            return profilesMap.get(type);
        return null;
    }

    public List<ContentProfile> getInstalledProfiles(ContentProfile.ContentType type) {
        List<ContentProfile> installedProfiles = new ArrayList<>();
        List<ContentProfile> profiles = getProfiles(type);
        if (profiles == null) return installedProfiles;

        for (ContentProfile profile : profiles) {
            if (profile.remoteUrl == null) installedProfiles.add(profile);
        }
        return installedProfiles;
    }

    public static File getInstallDir(Context context, ContentProfile profile) {
        if (profile == null || profile.type == null
                || !ArchivePathPolicy.isSafePathComponent(profile.verName)
                || profile.verCode < 0) {
            throw new IllegalArgumentException("Unsafe content profile identity");
        }
        return new File(getContentTypeDir(context, profile.type), profile.verName + "-" + profile.verCode);
    }

    public static File getContentDir(Context context) {
        return new File(context.getFilesDir(), ContentDirName.CONTENT_MAIN_DIR_NAME.toString());
    }

    public static File getContentTypeDir(Context context, ContentProfile.ContentType type) {
        return new File(getContentDir(context), type.toString());
    }

    public static File getTmpDir(Context context) {
        return new File(context.getFilesDir(), "tmp/" + ContentDirName.CONTENT_MAIN_DIR_NAME);
    }

    public static File getSourceFile(Context context, ContentProfile profile, String path) {
        return new File(getInstallDir(context, profile), path);
    }

    public static void cleanTmpDir(Context context) {
        File file = getTmpDir(context);
        FileUtils.delete(file);
        file.mkdirs();
    }

    public List<ContentProfile.ContentFile> getUnTrustedContentFiles(ContentProfile profile) {
        createTrustedFilesMap();
        List<ContentProfile.ContentFile> files = new ArrayList<>();
        for (ContentProfile.ContentFile contentFile : profile.fileList) {
            if (!trustedFilesMap.get(profile.type).contains(
                    Paths.get(getPathFromTemplate(contentFile.target)).toAbsolutePath().normalize().toString()))
                files.add(contentFile);
        }
        return files;
    }

    private boolean isSubPath(String parent, String child) {
        try {
            return new File(child).getCanonicalFile().toPath()
                    .startsWith(new File(parent).getCanonicalFile().toPath());
        } catch (IOException e) {
            return false;
        }
    }

    private void createDirTemplateMap() {
        if (dirTemplateMap == null) {
            dirTemplateMap = new HashMap<>();
            String imagefsPath = context.getFilesDir().getAbsolutePath() + "/imagefs";
            String drivecPath = imagefsPath + "/home/xuser/.wine/drive_c";
            dirTemplateMap.put("${libdir}", imagefsPath + "/usr/lib");
            dirTemplateMap.put("${system32}", drivecPath + "/windows/system32");
            dirTemplateMap.put("${syswow64}", drivecPath + "/windows/syswow64");
            dirTemplateMap.put("${bindir}", imagefsPath + "/usr/bin");
            dirTemplateMap.put("${sharedir}", imagefsPath + "/usr/share");
        }
    }

    private void createTrustedFilesMap() {
        if (trustedFilesMap == null) {
            trustedFilesMap = new HashMap<>();
            for (ContentProfile.ContentType type : ContentProfile.ContentType.values()) {
                List<String> pathList = new ArrayList<>();
                trustedFilesMap.put(type, pathList);

                String[] paths = switch (type) {
                    case CONTENT_TYPE_DXVK -> DXVK_TRUST_FILES;
                    case CONTENT_TYPE_VKD3D -> VKD3D_TRUST_FILES;
                    case CONTENT_TYPE_BOX64 -> BOX64_TRUST_FILES;
                    case CONTENT_TYPE_WOWBOX64 -> WOWBOX64_TRUST_FILES;
                    case CONTENT_TYPE_FEXCORE -> FEXCORE_TRUST_FILES;
                    default -> new String[0];
                };
                for (String path : paths)
                    pathList.add(Paths.get(getPathFromTemplate(path)).toAbsolutePath().normalize().toString());
            }
        }
    }

    private String getPathFromTemplate(String path) {
        createDirTemplateMap();
        String realPath = path;
        for (String key : dirTemplateMap.keySet()) {
            realPath = realPath.replace(key, dirTemplateMap.get(key));
        }
        return realPath;
    }

    public void removeContent(ContentProfile profile) {
        if (profilesMap.get(profile.type).contains(profile)) {
            FileUtils.delete(getInstallDir(context, profile));
            profilesMap.get(profile.type).remove(profile);
            syncContents();
        }
    }

    public static String getEntryName(ContentProfile profile) {
        return profile.type.toString() + '-' + profile.verName + '-' + profile.verCode;
    }

    /**
     * Finds an installed profile by the name {@link #getEntryName} produces.
     *
     * <h2>Why this accepts the type tag twice</h2>
     *
     * The engine does not agree with itself about what it passes here.
     * {@code WemuSession} and {@code WineInfo} hand over the entry name
     * whole — {@code Proton-11.0-3-arm64ec-4}. {@code ContainerBootstrap} and
     * {@code GuestProgramLauncherComponent} instead build {@code "dxvk-" +
     * version} from a stored version that is *already* an entry name, giving
     * {@code dxvk-DXVK-3.0-0}.
     *
     * The old parse read that second form as version name "DXVK-3.0", matched
     * nothing, and returned null — and every caller reads null as "not a user
     * profile, fall back to the bundled asset". So a DXVK the user had
     * downloaded, selected, and could see named in the editor was silently
     * replaced at launch by the one in the APK, with no error anywhere: the
     * fallback then tried to extract {@code dxwrapper/dxvk-DXVK-3.0-0.tzst},
     * which does not exist, and {@link TarCompressorUtils#extract} returns
     * false for a missing asset. The prefix simply kept whatever DXVK was last
     * written into it. The same held for VKD3D and Box64.
     * {@code FEXCoreManager.getVersions} is the one place that ever
     * compensated, by stripping the tag on the way out.
     *
     * <h2>Why it retries rather than stripping up front</h2>
     *
     * A leading tag is not proof of duplication: the installed FEXCore profile
     * genuinely has {@code verName == "FEXCore-2608-stable"}, so its correct
     * entry name contains the tag twice by right. Stripping unconditionally
     * would break the one type that already worked. Matching first and only
     * then retrying without the tag is strictly additive — it can find
     * profiles that used to resolve to null, and cannot change any lookup that
     * already succeeded.
     */
    public ContentProfile getProfileByEntryName(String entryName) {
        if (entryName == null) return null;

        int firstDash = entryName.indexOf('-');
        if (firstDash < 0) return null;

        ContentProfile.ContentType type =
                ContentProfile.ContentType.getTypeByName(entryName.substring(0, firstDash));
        if (type == null) return null;

        String remainder = entryName.substring(firstDash + 1);

        ContentProfile match = findProfile(type, remainder);
        if (match != null) return match;

        String tag = type.toString() + '-';
        if (remainder.regionMatches(true, 0, tag, 0, tag.length())) {
            return findProfile(type, remainder.substring(tag.length()));
        }
        return null;
    }

    /** Resolves the {@code <version name>-<version code>} tail within one type. */
    private ContentProfile findProfile(ContentProfile.ContentType type, String versionAndCode) {
        int lastDash = versionAndCode.lastIndexOf('-');
        if (lastDash < 0) return null;

        int versionCode;
        try {
            versionCode = Integer.parseInt(versionAndCode.substring(lastDash + 1));
        } catch (NumberFormatException e) {
            return null;
        }

        String versionName = versionAndCode.substring(0, lastDash);
        if (profilesMap == null) return null;
        List<ContentProfile> profiles = profilesMap.get(type);
        if (profiles == null) return null;

        for (ContentProfile profile : profiles) {
            if (versionName.equals(profile.verName) && versionCode == profile.verCode) return profile;
        }
        return null;
    }

    public boolean applyContent(ContentProfile profile) {
        if (profile == null || profile.type == null || profile.fileList == null) return false;
        if (profile.type != ContentProfile.ContentType.CONTENT_TYPE_WINE
                && profile.type != ContentProfile.ContentType.CONTENT_TYPE_PROTON) {
            for (ContentProfile.ContentFile contentFile : profile.fileList) {
                File targetFile = new File(getPathFromTemplate(contentFile.target));
                File installDir;
                File sourceFile;
                try {
                    installDir = getInstallDir(context, profile).getCanonicalFile();
                    sourceFile = ArchivePathPolicy.resolveEntry(installDir, contentFile.source);
                    ArchivePathPolicy.requireContainedOutput(installDir, sourceFile);
                } catch (IOException | IllegalArgumentException unsafe) {
                    Log.e("ContentsManager", "Unsafe content source", unsafe);
                    return false;
                }

                String imagefsPath = new File(context.getFilesDir(), "imagefs").getAbsolutePath();
                String targetPath = targetFile.getAbsolutePath();
                if (!isSubPath(imagefsPath, targetPath)
                        || isSubPath(getContentDir(context).getAbsolutePath(), targetPath)
                        || targetPath.toLowerCase().contains("dosdevices")
                        || !sourceFile.isFile()
                        || Files.isSymbolicLink(sourceFile.toPath())) return false;

                if ((targetFile.exists() || Files.isSymbolicLink(targetFile.toPath()))
                        && !FileUtils.delete(targetFile)) return false;
                if (!FileUtils.copy(sourceFile, targetFile) || !targetFile.isFile()) return false;

                if (profile.type == ContentProfile.ContentType.CONTENT_TYPE_BOX64) {
                    FileUtils.chmod(targetFile, 0771);
                }
            }
        } else {
            // TODO: do nothing?
        }
        return true;
    }

    /**
     * Verifies that an installed component profile is also materialized in the
     * active container. Comparing source and target sizes is cheap enough for
     * launch-time validation and catches missing or stale component DLLs.
     */
    public boolean isContentApplied(ContentProfile profile) {
        if (profile == null || profile.type == null || profile.fileList == null
                || profile.type == ContentProfile.ContentType.CONTENT_TYPE_WINE
                || profile.type == ContentProfile.ContentType.CONTENT_TYPE_PROTON) {
            return false;
        }
        for (ContentProfile.ContentFile contentFile : profile.fileList) {
            final File sourceFile;
            final File targetFile;
            try {
                File installDir = getInstallDir(context, profile).getCanonicalFile();
                sourceFile = ArchivePathPolicy.resolveEntry(installDir, contentFile.source);
                ArchivePathPolicy.requireContainedOutput(installDir, sourceFile);
                targetFile = new File(getPathFromTemplate(contentFile.target)).getCanonicalFile();
            } catch (IOException | IllegalArgumentException unsafe) {
                Log.w("ContentsManager", "Could not verify content materialization", unsafe);
                return false;
            }
            if (!sourceFile.isFile() || !targetFile.isFile()
                    || sourceFile.length() != targetFile.length()) return false;
        }
        return true;
    }

    private static boolean isSafeRelativeEntry(String path) {
        if (path == null || path.isEmpty()) return false;
        try {
            File syntheticRoot = new File(System.getProperty("java.io.tmpdir", "/tmp"),
                    "wemu-profile-root");
            ArchivePathPolicy.resolveEntry(syntheticRoot, path);
            return true;
        } catch (IOException | RuntimeException unsafe) {
            return false;
        }
    }
}
