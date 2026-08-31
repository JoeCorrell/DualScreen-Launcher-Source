package com.dualscreen.launcher

import android.content.Context
import android.content.Intent
import android.app.PendingIntent
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

internal sealed interface LauncherUpdateState {
    data object Checking : LauncherUpdateState
    data class Current(val version: String) : LauncherUpdateState
    data class Available(
        val currentVersion: String,
        val latestVersion: String,
        val downloadUrl: String,
        val sha256: String? = null,
    ) : LauncherUpdateState
    data class Downloading(val latestVersion: String, val fraction: Float) : LauncherUpdateState
    data class Preparing(val latestVersion: String) : LauncherUpdateState
    data class AwaitingConfirmation(val latestVersion: String) : LauncherUpdateState
    data class PermissionRequired(val update: Available) : LauncherUpdateState
    data class Failed(val currentVersion: String, val reason: String) : LauncherUpdateState
    data class Unavailable(val currentVersion: String, val reason: String) : LauncherUpdateState
}

internal data class LauncherRelease(
    val version: String,
    val releaseUrl: String,
    val apkUrl: String?,
    val apkSha256: String?,
) {
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
            var apkSha256: String? = null
            if (assets != null) {
                for (index in 0 until assets.length()) {
                    val asset = assets.getJSONObject(index)
                    val name = asset.optString("name")
                    val url = asset.optString("browser_download_url")
                    if (name.endsWith(".apk", ignoreCase = true) &&
                        url.startsWith("$TRUSTED_LAUNCHER_REPOSITORY/releases/download/")
                    ) {
                        apkUrl = url
                        apkSha256 = asset.optString("digest")
                            .removePrefix("sha256:")
                            .lowercase()
                            .takeIf { it.matches(Regex("[0-9a-f]{64}")) }
                        break
                    }
                }
            }
            return LauncherRelease(version, releaseUrl, apkUrl, apkSha256)
        }
    }
}

internal class LauncherUpdateChecker(private val context: Context) {
    private val mutex = Mutex()
    private val mutableState = MutableStateFlow<LauncherUpdateState>(LauncherUpdateState.Checking)
    val state: StateFlow<LauncherUpdateState> = mutableState.asStateFlow()
    private var lastCheckAtMs = 0L
    private var permissionPromptShown = false

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
                    val apk = release.apkUrl
                    if (apk == null) {
                        LauncherUpdateState.Unavailable(currentVersion, "The latest release has no installable APK")
                    } else {
                        LauncherUpdateState.Available(
                            currentVersion = currentVersion,
                            latestVersion = release.version,
                            downloadUrl = apk,
                            sha256 = release.apkSha256,
                        )
                    }
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

    suspend fun downloadAndInstall() = mutex.withLock {
        val available = when (val current = mutableState.value) {
            is LauncherUpdateState.Available -> current
            is LauncherUpdateState.PermissionRequired -> current.update
            else -> return@withLock
        }
        if (!canRequestPackageInstalls()) {
            mutableState.value = LauncherUpdateState.PermissionRequired(available)
            if (!permissionPromptShown) {
                permissionPromptShown = true
                openInstallerPermissionSettings()
            }
            return@withLock
        }
        permissionPromptShown = false
        runCatching {
            val apk = withContext(Dispatchers.IO) { downloadApk(available) }
            try {
                mutableState.value = LauncherUpdateState.Preparing(available.latestVersion)
                withContext(Dispatchers.IO) {
                    validateDownloadedApk(apk, available.latestVersion, available.sha256)
                    stagePackageInstall(apk, available.latestVersion)
                }
            } finally {
                withContext(Dispatchers.IO) { apk.delete() }
            }
        }.onFailure { failure ->
            Log.e(TAG, "In-app update failed", failure)
            mutableState.value = LauncherUpdateState.Failed(
                available.currentVersion,
                failure.message?.takeIf(String::isNotBlank) ?: "Update installation failed",
            )
        }
    }

    fun requestInstallerPermission() {
        permissionPromptShown = true
        openInstallerPermissionSettings()
    }

    private fun canRequestPackageInstalls(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.O ||
            context.packageManager.canRequestPackageInstalls()

    private fun openInstallerPermissionSettings() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        runCatching {
            context.startActivity(
                Intent(
                    Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:${context.packageName}"),
                ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }.recoverCatching {
            context.startActivity(
                Intent(Settings.ACTION_SECURITY_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }.onFailure { failure ->
            Log.e(TAG, "Could not open Android's installer permission screen", failure)
            mutableState.value = LauncherUpdateState.Failed(
                installedVersionName(),
                "Android's app-install permission screen is unavailable",
            )
        }
    }

    internal fun handleInstallStatus(intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val version = intent.getStringExtra(EXTRA_UPDATE_VERSION).orEmpty()
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                mutableState.value = LauncherUpdateState.AwaitingConfirmation(version)
                @Suppress("DEPRECATION")
                val confirmation = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
                confirmation?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)?.let(context::startActivity)
            }
            PackageInstaller.STATUS_SUCCESS -> mutableState.value = LauncherUpdateState.Current(version)
            else -> {
                val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
                    ?: "Android rejected the update"
                mutableState.value = LauncherUpdateState.Failed(installedVersionName(), message)
            }
        }
    }

    private fun downloadApk(update: LauncherUpdateState.Available): File {
        val directory = File(context.cacheDir, "updates").apply {
            check(isDirectory || mkdirs()) { "Could not create update storage" }
        }
        val target = File(directory, "dualscreen-launcher-${update.latestVersion}.apk")
        val temporary = File(directory, ".${target.name}.download")
        temporary.delete()
        val connection = (URL(update.downloadUrl).openConnection() as HttpURLConnection).apply {
            connectTimeout = UPDATE_CONNECT_TIMEOUT_MS
            readTimeout = UPDATE_READ_TIMEOUT_MS
            instanceFollowRedirects = true
            setRequestProperty("Accept", "application/vnd.android.package-archive")
            setRequestProperty("User-Agent", "DualScreen-Launcher/${installedVersionName()}")
        }
        try {
            check(connection.responseCode in 200..299) {
                "Update download returned HTTP ${connection.responseCode}"
            }
            val expected = connection.contentLengthLong
            check(expected <= 0L || expected <= MAX_APK_BYTES) { "Update APK is unexpectedly large" }
            var written = 0L
            var lastPercent = -1
            connection.inputStream.buffered(256 * 1024).use { input ->
                FileOutputStream(temporary).buffered(256 * 1024).use { output ->
                    val buffer = ByteArray(256 * 1024)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        written += count
                        check(written <= MAX_APK_BYTES) { "Update APK exceeds the safety limit" }
                        output.write(buffer, 0, count)
                        val percent = if (expected > 0L) ((written * 100L) / expected).toInt() else 0
                        if (percent != lastPercent) {
                            lastPercent = percent
                            mutableState.value = LauncherUpdateState.Downloading(
                                update.latestVersion,
                                (percent / 100f).coerceIn(0f, 1f),
                            )
                        }
                    }
                }
            }
            check(written > 0L && (expected <= 0L || written == expected)) { "Update download was incomplete" }
            if (target.exists()) check(target.delete()) { "Could not replace an old update" }
            check(temporary.renameTo(target)) { "Could not publish the downloaded update" }
            return target
        } finally {
            connection.disconnect()
            temporary.delete()
        }
    }

    @Suppress("DEPRECATION")
    private fun validateDownloadedApk(apk: File, version: String, expectedSha256: String?) {
        expectedSha256?.let { expected ->
            check(sha256(apk) == expected) { "Downloaded APK failed SHA-256 verification" }
        }
        val flags = PackageManager.GET_SIGNATURES
        val archive = context.packageManager.getPackageArchiveInfo(apk.absolutePath, flags)
            ?: error("Downloaded file is not a valid Android package")
        check(archive.packageName == context.packageName) { "Update APK belongs to a different app" }
        check(archive.versionName?.removePrefix("v") == version.removePrefix("v")) {
            "Update APK version does not match the release"
        }
        val current = context.packageManager.getPackageInfo(context.packageName, flags)
        check(archive.versionCode > current.versionCode) {
            "Update APK version code is not newer than the installed launcher"
        }
        val currentSignatures = current.signatures.orEmpty().map { signatureDigest(it.toByteArray()) }.toSet()
        val updateSignatures = archive.signatures.orEmpty().map { signatureDigest(it.toByteArray()) }.toSet()
        check(currentSignatures.isNotEmpty() && currentSignatures == updateSignatures) {
            "Update APK is not signed by the installed launcher"
        }
    }

    private fun stagePackageInstall(apk: File, version: String) {
        val installer = context.packageManager.packageInstaller
        val parameters = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
        }
        val sessionId = installer.createSession(parameters)
        var committed = false
        try {
            installer.openSession(sessionId).use { session ->
                apk.inputStream().buffered(256 * 1024).use { input ->
                    session.openWrite("launcher.apk", 0L, apk.length()).use { output ->
                        input.copyTo(output, 256 * 1024)
                        session.fsync(output)
                    }
                }
                val resultIntent = Intent(context, LauncherUpdateReceiver::class.java)
                    .setAction(ACTION_UPDATE_RESULT)
                    .putExtra(EXTRA_UPDATE_VERSION, version)
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                val pending = PendingIntent.getBroadcast(context, sessionId, resultIntent, flags)
                mutableState.value = LauncherUpdateState.AwaitingConfirmation(version)
                session.commit(pending.intentSender)
                committed = true
            }
        } finally {
            if (!committed) runCatching { installer.abandonSession(sessionId) }
        }
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered(256 * 1024).use { input ->
            val buffer = ByteArray(256 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun signatureDigest(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

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
        const val UPDATE_CONNECT_TIMEOUT_MS = 15_000
        const val UPDATE_READ_TIMEOUT_MS = 30_000
        const val MAX_APK_BYTES = 1024L * 1024L * 1024L
        const val ACTION_UPDATE_RESULT = "com.dualscreen.launcher.UPDATE_RESULT"
        const val EXTRA_UPDATE_VERSION = "updateVersion"
    }
}

private const val TRUSTED_LAUNCHER_REPOSITORY =
    "https://github.com/JoeCorrell/DualScreen-Launcher"
