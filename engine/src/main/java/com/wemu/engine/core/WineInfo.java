package com.wemu.engine.core;

import android.content.Context;
import android.os.Parcel;
import android.os.Parcelable;
import android.util.Log;

import androidx.annotation.NonNull;

import com.wemu.engine.R;
import com.wemu.engine.contents.ContentProfile;
import com.wemu.engine.contents.ContentsManager;
import com.wemu.engine.xenvironment.ImageFs;

import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class WineInfo implements Parcelable {
    public static final WineInfo MAIN_WINE_VERSION = new WineInfo("proton", "9.0", "arm64ec");
    private static final Pattern pattern = Pattern.compile("^(wine|proton)\\-([0-9\\.]+)\\-?([0-9\\.]+)?\\-(x86|x86_64|arm64ec)$");
    public final String version;
    public final String type;
    public String subversion;
    public final String path;
    private String arch;

    public WineInfo(String type, String version, String arch) {
        this.type = type;
        this.version = version;
        this.subversion = null;
        this.arch = arch;
        this.path = null;
    }

    public WineInfo(String type, String version, String subversion, String arch, String path) {
        this.type = type;
        this.version = version;
        this.subversion = subversion != null && !subversion.isEmpty() ? subversion : null;
        this.arch = arch;
        this.path = path;
    }

    public WineInfo(String type, String version, String arch, String path) {
        this.type = type;
        this.version = version;
        this.arch = arch;
        this.path = path;
    }

    private WineInfo(Parcel in) {
        type = in.readString();
        version = in.readString();
        subversion = in.readString();
        arch = in.readString();
        path = in.readString();
    }

    public String getArch() {
        return arch;
    }

    public void setArch(String arch) {
        this.arch = arch;
    }

    public boolean isWin64() {
        return arch.equals("x86_64") || arch.equals("arm64ec");
    }

    public boolean isArm64EC() { return arch.equals("arm64ec"); }

    public String identifier() {
        if (type.equals("proton"))
            return "proton-" + fullVersion() + "-"+ arch;
        else
            return "wine-" + fullVersion() + "-" + arch;
    }

    public String fullVersion() {
        return version+(subversion != null ? "-"+subversion : "");
    }

    @NonNull
    @Override
    public String toString() {
        if (type.equals("proton"))
            return "Proton "+fullVersion()+(this == MAIN_WINE_VERSION ? " (Custom)" : "");
        else
            return "Wine "+fullVersion()+(this == MAIN_WINE_VERSION ? " (Custom)" : "");
    }

    @Override
    public int describeContents() {
        return 0;
    }

    public static final Parcelable.Creator<WineInfo> CREATOR = new Parcelable.Creator<WineInfo>() {
        public WineInfo createFromParcel(Parcel in) {
            return new WineInfo(in);
        }

        public WineInfo[] newArray(int size) {
            return new WineInfo[size];
        }
    };

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(type);
        dest.writeString(version);
        dest.writeString(subversion);
        dest.writeString(arch);
        dest.writeString(path);
    }

    @NonNull
    public static WineInfo fromIdentifier(Context context, ContentsManager contentsManager, String identifier) {
        ImageFs imageFs = ImageFs.find(context);
        if (identifier == null || identifier.trim().isEmpty()) {
            throw new IllegalArgumentException("Wine runtime identifier is missing");
        }
        String requestedIdentifier = identifier.trim();

        Log.d("WineInfo", "Creating WineInfo from identifier " + requestedIdentifier);

        ContentProfile wineProfile = contentsManager != null
                ? contentsManager.getProfileByEntryName(requestedIdentifier) : null;
        if (wineProfile == null && contentsManager != null) {
            // A manager can be constructed before its local index is populated.
            // Refresh once before declaring an explicitly selected runtime gone.
            contentsManager.syncContents();
            wineProfile = contentsManager.getProfileByEntryName(requestedIdentifier);
        }

        final String runtimeIdentifier;
        final File runtimeDirectory;
        if (wineProfile != null) {
            if (wineProfile.type != ContentProfile.ContentType.CONTENT_TYPE_WINE
                    && wineProfile.type != ContentProfile.ContentType.CONTENT_TYPE_PROTON) {
                throw new IllegalArgumentException(
                        "Selected content is not a Wine runtime: " + requestedIdentifier);
            }
            String typePrefix = wineProfile.type.toString().toLowerCase() + "-";
            String profileVersion = wineProfile.verName.toLowerCase();
            runtimeIdentifier = profileVersion.startsWith(typePrefix)
                    ? profileVersion : typePrefix + profileVersion;
            runtimeDirectory = contentsManager.getInstallDir(context, wineProfile);
        } else {
            runtimeIdentifier = requestedIdentifier.toLowerCase();
            File bundled = new File(imageFs.getRootDir(), "opt/" + runtimeIdentifier);
            boolean knownBundled = runtimeIdentifier.equals(MAIN_WINE_VERSION.identifier());
            if (!knownBundled) {
                String[] wineVersions = context.getResources().getStringArray(R.array.wine_entries);
                for (String wineVersion : wineVersions) {
                    if (wineVersion.equalsIgnoreCase(runtimeIdentifier)) {
                        knownBundled = true;
                        break;
                    }
                }
            }
            if (!knownBundled && !ProtonPackageManager.isInstalled(context, runtimeIdentifier)) {
                throw new IllegalStateException(
                        "Selected Wine runtime is not installed: " + requestedIdentifier);
            }
            runtimeDirectory = bundled;
        }

        Matcher matcher = pattern.matcher(runtimeIdentifier);
        if (!matcher.matches()) {
            throw new IllegalArgumentException(
                    "Unsupported Wine runtime identifier: " + requestedIdentifier);
        }

        File wineBinary = new File(runtimeDirectory, "bin/wine");
        if (!runtimeDirectory.isDirectory() || !wineBinary.isFile()) {
            throw new IllegalStateException(
                    "Selected Wine runtime is incomplete: " + requestedIdentifier
                            + " (missing " + wineBinary + ")");
        }

        return new WineInfo(matcher.group(1), matcher.group(2), matcher.group(3),
                matcher.group(4), runtimeDirectory.getPath());
    }

    public static boolean isMainWineVersion(String wineVersion) {
        return wineVersion == null ||wineVersion.equals(MAIN_WINE_VERSION.identifier());
    }
}
