package com.dualscreen.launcher.switch

import org.json.JSONObject

/** A newer global catalog can still contain an older APK-only routing descriptor.
 * Keep a migration declared by this launcher; package revisions live inside signed .dsmod files.
 * Never apply that migration to a different title or hide an invalid remote descriptor.
 */
internal fun selectSwitchModManifest(resolved: JSONObject, bundled: JSONObject?): JSONObject =
    if (bundled?.optInt("schema") == 1 && resolved.optInt("schema") == 1 &&
        bundled.optString("titleId").isNotBlank() &&
        bundled.optString("titleId").equals(resolved.optString("titleId"), ignoreCase = true) &&
        bundled.optString("delivery") == "external-module" &&
        resolved.optString("delivery") == "launcher-apk"
    ) bundled else resolved
