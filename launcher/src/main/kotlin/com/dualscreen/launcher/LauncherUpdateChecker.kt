package com.dualscreen.launcher

import android.content.Context
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL

internal sealed interface LauncherUpdateState {
    data object Checking : LauncherUpdateState
    data class Current(val version: String) : LauncherUpdateState
    data class Available(
        val currentVersion: String,
        val latestVersion: String,
        val downloadUrl: String,
    ) : LauncherUpdateState
    data class Unavailable(val currentVersion: String, val reason: String) : LauncherUpdateState
}

internal data class LauncherRelease(
    val version: String,
    val releaseUrl: String,
    val apkUrl: String?,
) {
    val preferredDownloadUrl: String
        get() = apkUrl ?: releaseUrl

    companion object {
        fun parse(text: String): LauncherRelease {
            val json = JSONObject(text.trimStart('\uFEFF'))
            val version = json.getString("tag_name").trim().removePrefix("v").removePrefix("V")
            require(version.isNotBlank()) { "The latest release has no version" }
            val releaseUrl = json.getString("html_url").trim()
            require(releaseUrl.startsWith("$TRUSTED_LAUNCHER_REPOSITORY/releases/")) {
                "The update release URL is not trusted"
            }
            val assets = json.optJSONArray("assets")
            var apkUrl: String? = null
            if (assets != null) {
                for (index in 0 until assets.length()) {
                    val asset = assets.getJSONObject(index)
                    val name = asset.optString("name")
                    val url = asset.optString("browser_download_url")
                    if (name.endsWith(".apk", ignoreCase = true) &&
                        url.startsWith("$TRUSTED_LAUNCHER_REPOSITORY/releases/download/")
                    ) {
                        apkUrl = url
                        break
                    }
                }
            }
            return LauncherRelease(version, releaseUrl, apkUrl)
        }
    }
}

internal class LauncherUpdateChecker(private val context: Context) {
    private val mutex = Mutex()
    private val mutableState = MutableStateFlow<LauncherUpdateState>(LauncherUpdateState.Checking)
    val state: StateFlow<LauncherUpdateState> = mutableState.asStateFlow()
    private var lastCheckAtMs = 0L

    suspend fun check(force: Boolean = false) = mutex.withLock {
        val currentVersion = installedVersionName()
        val now = SystemClock.elapsedRealtime()
        if (!force && now - lastCheckAtMs < CHECK_INTERVAL_MS &&
            mutableState.value !is LauncherUpdateState.Checking
        ) {
            return@withLock
        }
        lastCheckAtMs = now
        if (mutableState.value !is LauncherUpdateState.Available) {
            mutableState.value = LauncherUpdateState.Checking
        }
        runCatching { withContext(Dispatchers.IO) { fetchLatestRelease() } }
            .onSuccess { release ->
                mutableState.value = if (isNewerVersion(release.version, currentVersion)) {
                    LauncherUpdateState.Available(
                        currentVersion = currentVersion,
                        latestVersion = release.version,
                        downloadUrl = release.preferredDownloadUrl,
                    )
                } else {
                    LauncherUpdateState.Current(currentVersion)
                }
            }
            .onFailure { failure ->
                Log.w(TAG, "Could not check for launcher updates", failure)
                mutableState.value = LauncherUpdateState.Unavailable(
                    currentVersion,
                    failure.message?.takeIf(String::isNotBlank) ?: "Update check failed",
                )
            }
    }

    private fun fetchLatestRelease(): LauncherRelease {
        val connection = (URL(LATEST_RELEASE_API).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout = READ_TIMEOUT_MS
            instanceFollowRedirects = true
            setRequestProperty("Accept", "application/vnd.github+json")
            setRequestProperty("User-Agent", "DualScreen-Launcher/${installedVersionName()}")
            setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
        }
        return try {
            check(connection.responseCode in 200..299) {
                "GitHub returned HTTP ${connection.responseCode}"
            }
            val bytes = connection.inputStream.use(::readLimited)
            check(bytes.size <= MAX_RESPONSE_BYTES) { "GitHub release response is too large" }
            LauncherRelease.parse(bytes.toString(Charsets.UTF_8))
        } finally {
            connection.disconnect()
        }
    }

    private fun readLimited(input: java.io.InputStream): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        var remaining = MAX_RESPONSE_BYTES + 1
        while (remaining > 0) {
            val count = input.read(buffer, 0, minOf(buffer.size, remaining))
            if (count < 0) break
            output.write(buffer, 0, count)
            remaining -= count
        }
        return output.toByteArray()
    }

    @Suppress("DEPRECATION")
    private fun installedVersionName(): String = runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull()?.takeIf(String::isNotBlank) ?: "0"

    private companion object {
        const val TAG = "LauncherUpdate"
        const val LATEST_RELEASE_API =
            "https://api.github.com/repos/JoeCorrell/DualScreen-Launcher/releases/latest"
        const val CHECK_INTERVAL_MS = 60_000L
        const val CONNECT_TIMEOUT_MS = 5_000
        const val READ_TIMEOUT_MS = 8_000
        const val MAX_RESPONSE_BYTES = 512 * 1024
    }
}

private const val TRUSTED_LAUNCHER_REPOSITORY =
    "https://github.com/JoeCorrell/DualScreen-Launcher"
