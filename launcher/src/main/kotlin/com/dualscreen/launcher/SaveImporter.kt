package com.dualscreen.launcher

import android.content.Context
import android.net.Uri
import android.os.StatFs
import android.provider.OpenableColumns
import android.util.Log
import com.wemu.engine.container.ContainerManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.FilterInputStream
import java.io.InputStream
import java.util.Locale
import java.util.UUID
import java.util.concurrent.atomic.AtomicLong
import java.util.zip.ZipInputStream

enum class SaveImportStage(val label: String) {
    VALIDATING("Validating save ZIP"),
    EXTRACTING("Extracting saves"),
    BACKING_UP("Preserving existing saves"),
    PUBLISHING("Publishing imported saves"),
    COMPLETE("Save import complete"),
    FAILED("Save import failed"),
}

data class SaveImportProgress(
    val gameId: String,
    val stage: SaveImportStage,
    val fraction: Float,
    val detail: String,
    val log: List<String>,
    val running: Boolean,
    val error: String? = null,
    val updateSequence: Long = 0L,
)

internal fun latestSaveImport(states: Collection<SaveImportProgress>): SaveImportProgress? =
    states.filter(SaveImportProgress::running).maxByOrNull(SaveImportProgress::updateSequence)
        ?: states.maxByOrNull(SaveImportProgress::updateSequence)

data class ImportedSave(
    val destination: File,
    val backup: File?,
    val files: Int,
    val bytes: Long,
)

/** Transactional importer for the catalog's fixed Wine-profile save roots. */
class SaveImporter(private val context: Context) {
    private val lock = Mutex()
    private val sequence = AtomicLong(0L)
    private val mutableProgress = MutableStateFlow<Map<String, SaveImportProgress>>(emptyMap())
    val progress: StateFlow<Map<String, SaveImportProgress>> = mutableProgress.asStateFlow()

    suspend fun import(
        game: SupportedGame,
        installed: InstalledGame,
        archive: Uri,
    ): Result<ImportedSave> = lock.withLock {
        begin(game)
        val result = runCatching {
            withContext(Dispatchers.IO) { importInternal(game, installed, archive) }
        }
        result.exceptionOrNull()?.let { if (it is CancellationException) throw it }
        result.onFailure { failure ->
                val previous = mutableProgress.value[game.id]
                publish(
                    game = game,
                    stage = SaveImportStage.FAILED,
                    fraction = previous?.fraction ?: 0f,
                    detail = "Save import stopped",
                    running = false,
                    error = failure.message ?: failure.javaClass.simpleName,
                )
            }
    }

    fun clear() {
        mutableProgress.value = emptyMap()
    }

    private fun importInternal(
        game: SupportedGame,
        installed: InstalledGame,
        archive: Uri,
    ): ImportedSave {
        check(game.id == installed.gameId) { "The selected save does not match this game" }
        publish(game, SaveImportStage.VALIDATING, 0.04f, "Resolving the tested container")
        val container = ContainerManager(context).getContainerById(installed.containerId)
            ?: error("The tested game container is missing")
        val userRoot = File(container.rootDir, ".wine/drive_c/users/xuser").canonicalFile
        check(userRoot.isDirectory || userRoot.mkdirs()) { "The Wine user profile is unavailable" }
        val destination = safeProfileDirectory(userRoot, game.saveDirectory)
        val parent = destination.parentFile ?: error("The save directory has no parent")
        check(parent.isDirectory || parent.mkdirs()) { "Could not create the save directory parent" }

        val raw = File(parent, ".dualscreen-import-${game.id}-raw")
        val payload = File(parent, ".dualscreen-import-${game.id}-payload")
        val rollback = File(parent, ".dualscreen-rollback-${game.id}")
        val backupRoot = File(
            userRoot,
            "Documents/Dualscreen Launcher Save Backups/${game.id}",
        ).canonicalFile
        check(backupRoot.path.startsWith(userRoot.path + File.separator)) { "Unsafe backup path" }
        recoverInterruptedImport(destination, raw, payload, rollback, backupRoot)

        var oldMoved = false
        var newPublished = false
        try {
            publish(game, SaveImportStage.VALIDATING, 0.10f, "Inspecting ZIP entries")
            val archiveBytes = archiveLength(archive)
            val availableBytes = StatFs(parent.absolutePath).availableBytes
            val extractionBudget = minOf(
                MAX_UNPACKED_BYTES,
                extractionByteBudget(availableBytes, STORAGE_RESERVE),
            )
            if (archiveBytes > extractionBudget) {
                error("Not enough free space to import this save archive")
            }
            val extracted = extractArchive(game, archive, raw, archiveBytes, extractionBudget)
            val selected = selectSavePayloadRoot(
                raw = raw,
                destinationLeaf = destination.name,
                stripSingleRoot = game.stripSingleSaveArchiveRoot,
            )
            check(selected.renameTo(payload)) { "Could not normalize the save ZIP layout" }
            if (raw.exists()) check(raw.deleteRecursively()) { "Could not clear the ZIP wrapper directory" }

            publish(
                game,
                SaveImportStage.BACKING_UP,
                0.88f,
                if (destination.exists()) "Preserving the current saves" else "No existing saves to preserve",
            )
            if (destination.exists()) {
                check(destination.isDirectory) { "The configured save path is not a directory" }
                check(destination.renameTo(rollback)) { "Could not preserve the current saves" }
                oldMoved = true
            }

            publish(game, SaveImportStage.PUBLISHING, 0.94f, "Publishing imported saves atomically")
            check(payload.renameTo(destination)) { "Could not publish the imported saves" }
            newPublished = true

            val backup = if (rollback.exists()) {
                nextBackupDirectory(backupRoot).also { target ->
                    check(target.parentFile?.mkdirs() == true || target.parentFile?.isDirectory == true) {
                        "Could not create the save-backup directory"
                    }
                    check(rollback.renameTo(target)) { "Could not archive the previous saves" }
                }
            } else {
                null
            }
            oldMoved = false

            val result = ImportedSave(
                destination = destination,
                backup = backup,
                files = extracted.files,
                bytes = extracted.bytes,
            )
            publish(
                game,
                SaveImportStage.COMPLETE,
                1f,
                "Imported ${result.files} save files" +
                    (backup?.let { "; previous saves backed up" } ?: ""),
                running = false,
            )
            return result
        } catch (failure: Throwable) {
            val cleanupFailure = runCatching {
                if (newPublished && destination.exists()) {
                    check(destination.deleteRecursively()) { "Could not remove the incomplete save import" }
                }
                if (oldMoved && rollback.exists() && !destination.exists()) {
                    check(rollback.renameTo(destination)) { "Could not restore the previous saves" }
                }
                if (raw.exists()) check(raw.deleteRecursively()) { "Could not clear save ZIP staging" }
                if (payload.exists()) check(payload.deleteRecursively()) { "Could not clear save payload staging" }
            }.exceptionOrNull()
            if (cleanupFailure != null) {
                failure.addSuppressed(cleanupFailure)
                Log.e(LOG_TAG, "Save import rollback was incomplete", cleanupFailure)
            }
            throw failure
        }
    }

    private fun extractArchive(
        game: SupportedGame,
        archive: Uri,
        raw: File,
        archiveBytes: Long,
        extractionBudget: Long,
    ): ExtractionResult {
        check(!raw.exists() && raw.mkdirs()) { "Could not create the save staging directory" }
        val input = context.contentResolver.openInputStream(archive)
            ?: error("The selected save ZIP could not be opened")
        val counted = CountingInputStream(input)
        var files = 0
        var unpacked = 0L
        var lastReportedBytes = 0L
        val windowsPaths = hashSetOf<String>()

        val buffer = ByteArray(BUFFER_SIZE)
        ZipInputStream(counted.buffered(BUFFER_SIZE)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val target = safeZipTarget(raw, entry.name)
                val pathKey = target.relativeTo(raw).invariantSeparatorsPath.lowercase(Locale.ROOT)
                check(windowsPaths.add(pathKey) || (entry.isDirectory && target.isDirectory)) {
                    "The ZIP contains duplicate Windows path: ${entry.name}"
                }
                if (entry.isDirectory) {
                    check(target.isDirectory || target.mkdirs()) { "Could not create ${entry.name}" }
                    zip.closeEntry()
                    continue
                }
                check(++files <= MAX_FILES) { "The save ZIP contains too many files" }
                check(target.extension.lowercase(Locale.ROOT) !in BLOCKED_EXTENSIONS) {
                    "Executable content is not allowed in a save ZIP: ${entry.name}"
                }
                check(target.parentFile?.isDirectory == true || target.parentFile?.mkdirs() == true) {
                    "Could not create a save subdirectory"
                }
                FileOutputStream(target).use { output ->
                    while (true) {
                        val count = zip.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                        unpacked += count
                        check(unpacked <= extractionBudget) {
                            "The save ZIP is larger than the available storage"
                        }
                    }
                }
                if (entry.time > 0) target.setLastModified(entry.time)
                zip.closeEntry()
                if (counted.count - lastReportedBytes >= REPORT_INTERVAL_BYTES) {
                    lastReportedBytes = counted.count
                    val local = if (archiveBytes > 0) {
                        (counted.count.toFloat() / archiveBytes).coerceIn(0f, 1f)
                    } else {
                        (files / 250f).coerceIn(0f, 0.95f)
                    }
                    publish(
                        game,
                        SaveImportStage.EXTRACTING,
                        0.12f + 0.72f * local,
                        "Extracting ${entry.name.substringAfterLast('/')} ($files files)",
                        appendLog = false,
                    )
                }
            }
        }
        check(files > 0) { "The selected ZIP contains no save files" }
        publish(game, SaveImportStage.EXTRACTING, 0.84f, "Validated $files extracted files")
        return ExtractionResult(files, unpacked)
    }

    private fun recoverInterruptedImport(
        destination: File,
        raw: File,
        payload: File,
        rollback: File,
        backupRoot: File,
    ) {
        if (!destination.exists() && rollback.exists()) {
            check(rollback.renameTo(destination)) { "Could not restore interrupted save backup" }
        } else if (destination.exists() && rollback.exists()) {
            val recovered = nextBackupDirectory(backupRoot)
            check(recovered.parentFile?.mkdirs() == true || recovered.parentFile?.isDirectory == true) {
                "Could not preserve interrupted save backup"
            }
            check(rollback.renameTo(recovered)) { "Could not preserve interrupted save backup" }
        }
        if (raw.exists()) check(raw.deleteRecursively()) { "Could not clear interrupted save staging" }
        if (payload.exists()) check(payload.deleteRecursively()) { "Could not clear interrupted save payload" }
    }

    private fun archiveLength(uri: Uri): Long = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getLong(0) else -1L
        } ?: -1L
    }.getOrDefault(-1L)

    private fun begin(game: SupportedGame) {
        publish(
            game,
            SaveImportStage.VALIDATING,
            0f,
            "Opening save ZIP",
            running = true,
        )
    }

    private fun publish(
        game: SupportedGame,
        stage: SaveImportStage,
        fraction: Float,
        detail: String,
        running: Boolean = true,
        error: String? = null,
        appendLog: Boolean = true,
    ) {
        mutableProgress.update { current ->
            val previous = current[game.id]
            val safeFraction = if (previous?.running == true) {
                maxOf(previous.fraction, fraction)
            } else {
                fraction
            }.coerceIn(0f, 1f)
            val line = "[SAVE ${(safeFraction * 100).toInt().toString().padStart(3)}%] $detail"
            val lines = when {
                previous == null -> listOf(line)
                appendLog && previous.log.lastOrNull() != line -> (previous.log + line).takeLast(MAX_LOG_LINES)
                else -> previous.log
            }
            current + (game.id to SaveImportProgress(
                gameId = game.id,
                stage = stage,
                fraction = safeFraction,
                detail = detail,
                log = lines,
                running = running,
                error = error,
                updateSequence = sequence.incrementAndGet(),
            ))
        }
    }

    private data class ExtractionResult(val files: Int, val bytes: Long)

    private class CountingInputStream(input: InputStream) : FilterInputStream(input) {
        var count: Long = 0L
            private set

        override fun read(): Int = super.read().also { if (it >= 0) count++ }

        override fun read(buffer: ByteArray, offset: Int, length: Int): Int =
            super.read(buffer, offset, length).also { if (it > 0) count += it }
    }

    private companion object {
        const val BUFFER_SIZE = 512 * 1024
        const val MAX_FILES = 25_000
        const val MAX_UNPACKED_BYTES = 8L * 1024L * 1024L * 1024L
        const val REPORT_INTERVAL_BYTES = 512L * 1024L
        const val MAX_LOG_LINES = 160
        const val STORAGE_RESERVE = 256L * 1024L * 1024L
        const val LOG_TAG = "DualscreenSaveImport"
        val BLOCKED_EXTENSIONS = setOf("exe", "dll", "com", "bat", "cmd", "msi", "lnk", "desktop")
    }
}

internal fun safeProfileDirectory(userRoot: File, relative: String): File {
    val segments = relative.replace('\\', '/').split('/').filter(String::isNotBlank)
    require(segments.isNotEmpty() && segments.none { it == "." || it == ".." || ':' in it }) {
        "Unsafe predefined save directory"
    }
    val root = userRoot.canonicalFile
    val destination = segments.fold(root, ::File).canonicalFile
    require(destination.path.startsWith(root.path + File.separator)) { "Unsafe predefined save directory" }
    return destination
}

internal fun safeZipTarget(root: File, entryName: String): File {
    require('\u0000' !in entryName) { "The ZIP contains an invalid filename" }
    val normalized = entryName.replace('\\', '/')
    require(!normalized.startsWith('/')) { "The ZIP contains an absolute path: $entryName" }
    val segments = normalized.split('/').filter(String::isNotBlank)
    require(segments.isNotEmpty() && segments.none { it == "." || it == ".." || ':' in it }) {
        "The ZIP contains an unsafe path: $entryName"
    }
    val canonicalRoot = root.canonicalFile
    val target = segments.fold(canonicalRoot, ::File).canonicalFile
    require(target.path.startsWith(canonicalRoot.path + File.separator)) {
        "The ZIP contains an unsafe path: $entryName"
    }
    return target
}

internal fun selectSavePayloadRoot(
    raw: File,
    destinationLeaf: String,
    stripSingleRoot: Boolean,
): File {
    val named = raw.walkTopDown()
        .filter(File::isDirectory)
        .filter { it != raw && it.name.equals(destinationLeaf, ignoreCase = true) }
        .sortedBy { it.relativeTo(raw).invariantSeparatorsPath.count { char -> char == '/' } }
        .firstOrNull { directory -> directory.walkTopDown().any(File::isFile) }
    if (named != null) return named
    if (stripSingleRoot) {
        val children = raw.listFiles().orEmpty().filterNot { it.name.startsWith(".") }
        if (children.size == 1 && children.single().isDirectory) return children.single()
    }
    return raw
}

private fun nextBackupDirectory(root: File): File =
    File(root, "${System.currentTimeMillis()}-${UUID.randomUUID().toString().take(8)}")
