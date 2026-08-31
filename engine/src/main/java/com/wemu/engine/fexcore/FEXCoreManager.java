package com.wemu.engine.fexcore;

import android.content.Context;

import com.wemu.engine.R;
import com.wemu.engine.contents.ContentProfile;
import com.wemu.engine.contents.ContentsManager;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The FEX builds available to a container.
 *
 * This used to take a `Spinner` and fill it in, which meant the only way to ask
 * "which FEX versions are installed" was to own a view and then read back out of
 * its adapter. A Compose screen has no spinner to hand it, so the question is
 * now answered with a list and the presentation is the caller's business.
 */
public abstract class FEXCoreManager {

    /**
     * The bundled versions, followed by anything the user has installed.
     *
     * Order matters: the bundled entries come first because a version string
     * saved in a container is matched against this list by name, and a user
     * install that shadows a bundled name should not change what an existing
     * container resolves to.
     */
    public static List<String> getVersions(Context context, ContentsManager contentsManager) {
        String[] bundled = context.getResources().getStringArray(R.array.fexcore_version_entries);
        List<String> versions = new ArrayList<>(Arrays.asList(bundled));

        for (ContentProfile profile :
                contentsManager.getInstalledProfiles(ContentProfile.ContentType.CONTENT_TYPE_FEXCORE)) {
            String entryName = ContentsManager.getEntryName(profile);
            int firstDashIndex = entryName.indexOf('-');
            // An entry with no dash would make `substring(0)` return the whole
            // name rather than throw, which is the sane outcome — but the
            // original's `indexOf('-') + 1` reached that by accident.
            versions.add(entryName.substring(firstDashIndex + 1));
        }
        return versions;
    }
}
