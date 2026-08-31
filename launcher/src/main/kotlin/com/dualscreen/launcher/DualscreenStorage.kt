package com.dualscreen.launcher

import android.os.Environment
import android.system.Os
import android.util.Log
import com.wemu.engine.container.Container
import java.io.File
import java.nio.file.Files
import java.util.UUID

/**
 * User-visible launcher storage.
 *
 * The Wine prefix and runtime stay app-private. Game files, saves, editable
 * configuration and diagnostics live under /Dualscreen whenever shared storage
 * is writable. Wine profile paths are linked to the public save/config files so
 * there is one authoritative copy rather than a fragile periodic mirror.
 */
class DualscreenStorage {
    val root: File = File(Environment.getExternalStorageDirectory(), ROOT_NAME)
    val gamesRoot: File get() = File(root, "Games")
    val containersRoot: File get() = File(root, "Containers")

    fun gameHome(game: SupportedGame): File = File(gamesRoot, game.storageName)
    fun gameFiles(game: SupportedGame): File = File(gameHome(game), "Game")
    fun mods(game: SupportedGame): File = File(gameHome(game), "Mods")
    fun saves(game: SupportedGame): File = File(gameHome(game), "Saves")
    fun config(game: SupportedGame): File = File(gameHome(game), "Config")
    fun logs(game: SupportedGame): File = File(gameHome(game), "Logs")

    fun ensureCatalogLayout(): Boolean = runCatching {
        check(root.isDirectory || root.mkdirs()) { "Could not create ${root.absolutePath}" }
        check(gamesRoot.isDirectory || gamesRoot.mkdirs()) { "Could not create ${gamesRoot.absolutePath}" }
        check(containersRoot.isDirectory || containersRoot.mkdirs()) {
            "Could not create ${containersRoot.absolutePath}"
        }
        GameCatalog.games.forEach(::ensureGameLayout)
        writeIfMissing(
            File(root, "README.txt"),
            "Dualscreen Launcher user files\n\n" +
                "Games contains each title's game files, mods, saves, configuration, and logs.\n" +
                "Containers contains readable snapshots of the managed presets. The protected Wine " +
                "runtime remains inside app storage because moving it would break permissions and links.\n",
        )
        true
    }.onFailure { Log.w(TAG, "Shared storage is unavailable", it) }.getOrDefault(false)

    fun ensureGameLayout(game: SupportedGame) {
        val home = gameHome(game)
        check(home.isDirectory || home.mkdirs()) { "Could not create ${home.absolutePath}" }
        listOf(mods(game), config(game), logs(game)).forEach { directory ->
            check(directory.isDirectory || directory.mkdirs()) { "Could not create ${directory.absolutePath}" }
        }
        writeIfMissing(
            File(home, "README.txt"),
            buildString {
                appendLine(game.title)
                appendLine()
                appendLine("Game   - the installed Windows game and deployed dual-screen mod")
                appendLine("Mods   - mod downloads or backups; install ready-to-use files into Game as required")
                appendLine("Saves  - the live Wine save directory")
                appendLine("Config - live user-editable configuration files exposed by the launcher")
                appendLine("Logs   - latest setup and launch diagnostics")
                appendLine()
                appendLine("Recommended game version: ${game.requiredBuild}")
            },
        )
        writeIfMissing(
            File(mods(game), "README.txt"),
            "Use this folder for mod downloads and backups. Install each mod's files into the paths " +
                "specified by that mod, usually inside ../Game.\n",
        )
    }

    /** Returns public Game storage, falling back only when shared storage is unavailable. */
    fun gameFilesOrLegacy(container: Container, game: SupportedGame): File {
        if (ensureCatalogLayout()) return gameFiles(game)
        return File(container.rootDir, ".wine/drive_c/Games/${game.id}")
    }

    fun installLog(game: SupportedGame): File {
        ensureGameLayout(game)
        return File(logs(game), "install-latest.log")
    }

    fun launchLog(game: SupportedGame): File {
        ensureGameLayout(game)
        return File(logs(game), "launch-latest.log")
    }

    fun exportContainerSnapshot(container: Container) = runCatching {
        check(ensureCatalogLayout()) { "Shared storage is unavailable" }
        val directory = File(containersRoot, safeStorageSegment(container.name, "Preset"))
        check(directory.isDirectory || directory.mkdirs()) { "Could not create ${directory.absolutePath}" }
        val source = container.configFile
        if (source.isFile) copyFileAtomically(source, File(directory, "container.json"))
        writeIfMissing(
            File(directory, "README.txt"),
            "This is a readable snapshot of the managed preset. The active Wine prefix remains " +
                "protected inside the app; editing this snapshot does not modify the running container.\n",
        )
    }.onFailure { Log.w(TAG, "Could not export container snapshot", it) }

    /**
     * Makes the public Saves directory authoritative while preserving any
     * existing prefix saves transactionally. If linking is unavailable, the
     * untouched private directory is returned so a game can never lose saves.
     */
    fun prepareSaveDirectory(container: Container, game: SupportedGame): File {
        if (!ensureCatalogLayout()) return privateSaveDirectory(container, game)
        val public = saves(game)
        val private = privateSaveDirectory(container, game)
        private.parentFile?.let { check(it.isDirectory || it.mkdirs()) }

        if (Files.isSymbolicLink(private.toPath())) {
            val current = runCatching { private.canonicalFile }.getOrNull()
            if (current == public.canonicalFile) {
                check(public.isDirectory || public.mkdirs())
                return public
            }
            check(private.delete()) { "Could not replace an outdated save link" }
        }

        if (!private.exists()) {
            check(public.isDirectory || public.mkdirs()) { "Could not create ${public.absolutePath}" }
            return if (createDirectoryLink(private, public)) public else private.apply { mkdirs() }
        }
        if (!private.isDirectory) return private

        val publicHasFiles = public.isDirectory && public.walkTopDown().any(File::isFile)
        val privateHasFiles = private.walkTopDown().any(File::isFile)
        val exposurePlan = saveExposurePlan(publicHasFiles, privateHasFiles)
        if (exposurePlan == SaveExposurePlan.PRESERVE_PRIVATE) {
            // Never merge two independent save histories automatically.
            Log.w(TAG, "Both public and private saves exist for ${game.id}; preserving private saves")
            return private
        }
        if (exposurePlan == SaveExposurePlan.PREFER_PUBLIC) {
            // A manually imported/public save history wins over an empty Wine
            // placeholder. Previously that harmless empty directory prevented
            // the public saves from ever becoming visible to the game.
            check(private.deleteRecursively()) { "Could not remove the empty private save directory" }
            if (createDirectoryLink(private, public)) return public
            if (Files.isSymbolicLink(private.toPath())) private.delete()
            copyTreeVerified(public, private)
            return private
        }
        if (public.exists()) check(public.deleteRecursively()) { "Could not prepare public save storage" }
        val staging = File(gameHome(game), ".Saves-migrating-${UUID.randomUUID()}")
        val backup = File(private.parentFile, ".${private.name}-dualscreen-backup")
        copyTreeVerified(private, staging)
        check(staging.renameTo(public)) { "Could not publish public saves" }
        if (backup.exists()) check(backup.deleteRecursively()) { "Could not clear an old save backup" }
        check(private.renameTo(backup)) { "Could not preserve private saves before linking" }
        if (!createDirectoryLink(private, public)) {
            public.deleteRecursively()
            check(backup.renameTo(private)) { "Could not restore private saves" }
            return private
        }
        check(backup.deleteRecursively()) { "Could not clear migrated private saves" }
        return public
    }

    /** Moves immediate profile configuration files to public storage and links them in place. */
    fun exposeConfigurationFiles(container: Container, game: SupportedGame) = runCatching {
        if (!ensureCatalogLayout()) return@runCatching
        val privateSave = privateSaveDirectory(container, game)
        val profile = privateSave.parentFile ?: return@runCatching
        if (!profile.isDirectory) return@runCatching
        val publicConfig = config(game).apply { check(isDirectory || mkdirs()) }
        profile.listFiles().orEmpty()
            .filter(File::isFile)
            .filterNot { Files.isSymbolicLink(it.toPath()) || it.name.startsWith(".dualscreen-") }
            .forEach { source ->
                val target = File(publicConfig, source.name)
                copyFileAtomically(source, target)
                val backup = File(profile, ".${source.name}.dualscreen-backup")
                if (backup.exists()) backup.delete()
                check(source.renameTo(backup)) { "Could not preserve ${source.name}" }
                if (createFileLink(source, target)) {
                    backup.delete()
                } else {
                    target.delete()
                    check(backup.renameTo(source)) { "Could not restore ${source.name}" }
                }
            }
    }.onFailure { Log.w(TAG, "Could not expose ${game.id} configuration", it) }

    private fun privateSaveDirectory(container: Container, game: SupportedGame): File {
        val userRoot = File(container.rootDir, ".wine/drive_c/users/xuser")
        val segments = game.saveDirectory.replace('\\', '/').split('/').filter(String::isNotBlank)
        require(segments.isNotEmpty() && segments.none { it == "." || it == ".." || ':' in it })
        return segments.fold(userRoot, ::File)
    }

    private fun createDirectoryLink(link: File, target: File): Boolean = runCatching {
        link.parentFile?.let { check(it.isDirectory || it.mkdirs()) }
        Os.symlink(target.absolutePath, link.absolutePath)
        check(link.isDirectory)
        true
    }.onFailure {
        if (Files.isSymbolicLink(link.toPath())) link.delete()
        Log.w(TAG, "Could not link ${link.absolutePath} to shared storage", it)
    }
        .getOrDefault(false)

    private fun createFileLink(link: File, target: File): Boolean = runCatching {
        Os.symlink(target.absolutePath, link.absolutePath)
        check(link.isFile)
        true
    }.onFailure {
        if (Files.isSymbolicLink(link.toPath())) link.delete()
        Log.w(TAG, "Could not link ${link.absolutePath} to shared storage", it)
    }
        .getOrDefault(false)

    private fun copyTreeVerified(source: File, target: File) {
        check(!target.exists() && target.mkdirs()) { "Could not create migration staging" }
        var sourceFiles = 0L
        var sourceBytes = 0L
        source.walkTopDown().forEach { item ->
            val relative = item.relativeTo(source).invariantSeparatorsPath
            if (relative.isEmpty()) return@forEach
            check(!Files.isSymbolicLink(item.toPath())) { "Save directory contains an unsupported link" }
            val output = File(target, relative)
            if (item.isDirectory) {
                check(output.isDirectory || output.mkdirs()) { "Could not create $relative" }
            } else {
                output.parentFile?.mkdirs()
                item.inputStream().buffered().use { input ->
                    output.outputStream().buffered().use(input::copyTo)
                }
                sourceFiles++
                sourceBytes += item.length()
            }
        }
        val targetFiles = target.walkTopDown().filter(File::isFile).toList()
        check(targetFiles.size.toLong() == sourceFiles && targetFiles.sumOf(File::length) == sourceBytes) {
            "Save migration verification failed"
        }
    }

    private fun copyFileAtomically(source: File, target: File) {
        target.parentFile?.let { check(it.isDirectory || it.mkdirs()) }
        val temporary = File(target.parentFile, ".${target.name}-${UUID.randomUUID()}.tmp")
        try {
            source.inputStream().buffered().use { input ->
                temporary.outputStream().buffered().use(input::copyTo)
            }
            check(temporary.length() == source.length()) { "File copy verification failed" }
            Files.move(
                temporary.toPath(),
                target.toPath(),
                java.nio.file.StandardCopyOption.REPLACE_EXISTING,
            )
        } finally {
            temporary.delete()
        }
    }

    private fun writeIfMissing(file: File, text: String) {
        if (!file.exists()) file.writeText(text)
    }

    companion object {
        const val ROOT_NAME = "Dualscreen"
        private const val TAG = "DualscreenStorage"
    }
}

internal fun safeStorageSegment(value: String, fallback: String): String =
    value.replace(Regex("[^A-Za-z0-9 ._-]"), "")
        .trim()
        .trim(' ', '.')
        .ifBlank { fallback }

internal enum class SaveExposurePlan { PRESERVE_PRIVATE, PREFER_PUBLIC, MIGRATE_PRIVATE }

internal fun saveExposurePlan(publicHasFiles: Boolean, privateHasFiles: Boolean): SaveExposurePlan =
    when {
        publicHasFiles && privateHasFiles -> SaveExposurePlan.PRESERVE_PRIVATE
        publicHasFiles -> SaveExposurePlan.PREFER_PUBLIC
        else -> SaveExposurePlan.MIGRATE_PRIVATE
    }
