package com.dualscreen.launcher

import android.content.Context
import android.net.Uri
import android.os.StatFs
import android.os.SystemClock
import android.util.Log
import com.wemu.engine.container.Container
import com.wemu.engine.container.ContainerManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.DataInputStream
import java.io.FilterInputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.io.IOException
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.GZIPInputStream
import java.util.zip.ZipInputStream

class GameInstaller(
    private val context: Context,
    private val registry: InstallRegistry,
) {
    private val preset = PresetContainerManager(context)
    private val installLock = Mutex()
    private val progressSequence = AtomicLong(0L)
    private val mutableProgress = MutableStateFlow<Map<String, InstallProgress>>(emptyMap())
    val progress: StateFlow<Map<String, InstallProgress>> = mutableProgress.asStateFlow()

    suspend fun install(game: SupportedGame, archive: Uri): Result<InstalledGame> =
        installLock.withLock {
            runCatching { installInternal(game, archive) }
                .onFailure { failure ->
                    val previous = mutableProgress.value[game.id]
                    publish(
                        game.id,
                        InstallStage.FAILED,
                        previous?.fraction ?: InstallStage.RUNTIME.baseProgress,
                        "Installation stopped during ${previous?.stage?.label ?: "setup"}",
                        error = failure.message ?: failure.javaClass.simpleName,
                        running = false,
                    )
                }
        }

    /**
     * Brings an already-installed game forward to the payload bundled in this APK.
     * This runs before launch and touches only files listed by the signed-in-build
     * manifest; the user's game archive is never extracted again.
     */
    suspend fun ensureCurrentPayload(game: SupportedGame): Result<InstalledGame?> =
        installLock.withLock {
            runCatching {
                var installed = registry.get(game.id) ?: return@runCatching null
                if (preset.requiresDedicatedContainer(game)) {
                    installed = ensureDedicatedContainer(game, installed)
                }
                val root = File(installed.hostDirectory).canonicalFile
                check(root.isDirectory) { "The installed game directory is missing" }
                val bundled = readPayloadManifest(game)
                val currentFilesMatch = withContext(Dispatchers.IO) {
                    payloadIsCurrent(root, bundled)
                }
                if (installed.payloadVersion == bundled.version && currentFilesMatch) {
                    Log.i(LOG_TAG, "Verified ${game.id} payload ${bundled.version}")
                    publish(
                        game.id,
                        InstallStage.COMPLETE,
                        1f,
                        "${game.title} mod is current",
                        running = false,
                    )
                    return@runCatching installed
                }

                publish(
                    game.id,
                    InstallStage.PATCHING,
                    InstallStage.PATCHING.baseProgress,
                    "Updating ${game.title} dual-screen mod",
                )
                val applied = withContext(Dispatchers.IO) {
                    applyPayload(game, root, 0f, 1f)
                }
                withContext(Dispatchers.IO) { verify(game, root, applied) }
                installed.copy(payloadVersion = applied.version).also(registry::put).also {
                    Log.i(LOG_TAG, "Updated ${game.id} payload to ${applied.version}")
                    publish(
                        game.id,
                        InstallStage.COMPLETE,
                        1f,
                        "${game.title} mod update complete",
                        running = false,
                    )
                }
            }
                .onFailure { failure ->
                    val previous = mutableProgress.value[game.id]
                    publish(
                        game.id,
                        InstallStage.FAILED,
                        previous?.fraction ?: InstallStage.PATCHING.baseProgress,
                        "Dual-screen mod update failed",
                        running = false,
                        error = failure.message ?: failure.javaClass.simpleName,
                    )
                }
        }

    private suspend fun installInternal(game: SupportedGame, archive: Uri): InstalledGame {
        begin(game)
        val installStartedAt = SystemClock.elapsedRealtime()
        val setupStartedAt = installStartedAt
        val container = preset.prepare(game) { stage, local, detail ->
            publish(game.id, stage, stageProgress(stage, local), detail)
        }

        publish(
            game.id,
            InstallStage.VALIDATING,
            0.24f,
            "Container setup ready in ${formatDuration(SystemClock.elapsedRealtime() - setupStartedAt)}",
        )
        publish(game.id, InstallStage.VALIDATING, 0.24f, "Opening the selected ZIP")
        val parent = preset.gameDirectory(container, game).parentFile
            ?: error("The games directory is unavailable")
        withContext(Dispatchers.IO) {
            check(parent.isDirectory || parent.mkdirs()) { "Could not create $parent" }
        }

        val target = File(parent, game.id)
        val rollback = File(parent, ".rollback-${game.id}")
        withContext(Dispatchers.IO) {
            recoverInterruptedPublish(parent, target, rollback, game.id)
        }
        val staging = File(parent, ".installing-${game.id}-${UUID.randomUUID()}")
        var committed = false
        try {
            extract(game, archive, staging)
            val gameRoot = locateGameRoot(staging, game)
                ?: error("This ZIP does not contain a supported ${game.title} installation")
            withContext(Dispatchers.IO) { validateGameBuild(game, gameRoot) }

            publish(game.id, InstallStage.PATCHING, 0.82f, "Preparing verified game prerequisites")
            withContext(Dispatchers.IO) { applyPrerequisites(game, gameRoot) }
            val payloadStart = if (game.prerequisites.isEmpty()) 0f else 0.58f
            val payload = withContext(Dispatchers.IO) {
                applyPayload(game, gameRoot, payloadStart, 1f)
            }

            publish(game.id, InstallStage.CONFIGURING, 0.91f, "Preparing the direct launch entry")
            publish(game.id, InstallStage.VERIFYING, 0.96f, "Checking game and mod files")
            val executable = withContext(Dispatchers.IO) { verify(game, gameRoot, payload) }

            publish(game.id, InstallStage.VERIFYING, 0.985f, "Publishing the verified installation")
            withContext(Dispatchers.IO) {
                if (rollback.exists() && !rollback.deleteRecursively()) {
                    error("Could not clear the previous rollback directory")
                }
                if (target.exists() && !target.renameTo(rollback)) {
                    error("Could not preserve the previous installation")
                }
                if (!gameRoot.renameTo(target)) {
                    if (rollback.exists()) rollback.renameTo(target)
                    error("Could not publish the verified installation")
                }
                committed = true
                if (staging.exists()) staging.deleteRecursively()
            }

            val shortcut = preset.createShortcut(container, game, executable)
            val installedBytes = withContext(Dispatchers.IO) {
                target.walkTopDown().filter(File::isFile).sumOf(File::length)
            }
            val installed = InstalledGame(
                gameId = game.id,
                containerId = container.id,
                shortcutPath = shortcut.file.absolutePath,
                hostDirectory = target.absolutePath,
                installedBytes = installedBytes,
                payloadVersion = payload.version,
            )
            registry.put(installed)
            withContext(Dispatchers.IO) {
                if (rollback.exists()) rollback.deleteRecursively()
            }
            publish(
                game.id,
                InstallStage.COMPLETE,
                1f,
                "${game.title} is ready to play · " +
                    formatDuration(SystemClock.elapsedRealtime() - installStartedAt),
                running = false,
            )
            return installed
        } catch (failure: Throwable) {
            withContext(Dispatchers.IO) {
                if (committed) {
                    if (target.exists()) target.deleteRecursively()
                    if (rollback.exists()) rollback.renameTo(target)
                } else {
                    if (staging.exists()) staging.deleteRecursively()
                    if (!target.exists() && rollback.exists()) rollback.renameTo(target)
                }
            }
            throw failure
        }
    }

    /** Moves a legacy Stardew install out of the shared Games prefix exactly once. */
    private suspend fun ensureDedicatedContainer(
        game: SupportedGame,
        installed: InstalledGame,
    ): InstalledGame {
        val destinationContainer = preset.prepare(game) { stage, local, detail ->
            publish(game.id, stage, stageProgress(stage, local), detail)
        }
        if (destinationContainer.id == installed.containerId) return installed

        return withContext(Dispatchers.IO) {
            val source = File(installed.hostDirectory).canonicalFile
            val destination = preset.gameDirectory(destinationContainer, game).canonicalFile
            check(!(source.isDirectory && destination.exists())) {
                "The dedicated ${game.title} destination already contains files"
            }
            // A process death may happen after the atomic directory move but
            // before the registry commit. In that case finish publishing the
            // destination instead of declaring the intact game missing.
            val activeRoot = when {
                source.isDirectory -> source
                destination.isDirectory -> destination
                else -> error("The installed ${game.title} directory is missing")
            }
            val executable = resolveLaunchExecutable(activeRoot, game)
                ?: error("Missing launch executable: ${game.executableCandidates.joinToString(" or ")}")
            val sourceContainer = ContainerManager(context).getContainerById(installed.containerId)
            val sourceSaveRoot = sourceContainer?.let {
                File(it.rootDir, ".wine/drive_c/users/xuser/${game.saveDirectory}").canonicalFile
            }
            val destinationSaveRoot = File(
                destinationContainer.rootDir,
                ".wine/drive_c/users/xuser/${game.saveDirectory}",
            ).canonicalFile
            var gameMoved = false
            var savesMoved = false
            try {
                destination.parentFile?.let { check(it.isDirectory || it.mkdirs()) }
                if (activeRoot == source) {
                    check(source.renameTo(destination)) {
                        "Could not move ${game.title} into its dedicated container"
                    }
                    gameMoved = true
                }
                if (sourceSaveRoot?.isDirectory == true && !destinationSaveRoot.exists()) {
                    destinationSaveRoot.parentFile?.let { check(it.isDirectory || it.mkdirs()) }
                    check(sourceSaveRoot.renameTo(destinationSaveRoot)) {
                        "Could not move ${game.title} saves into its dedicated container"
                    }
                    savesMoved = true
                }
                val shortcut = preset.createShortcut(destinationContainer, game, executable)
                installed.copy(
                    containerId = destinationContainer.id,
                    shortcutPath = shortcut.file.absolutePath,
                    hostDirectory = destination.absolutePath,
                ).also(registry::put)
            } catch (failure: Throwable) {
                if (savesMoved) destinationSaveRoot.renameTo(checkNotNull(sourceSaveRoot))
                if (gameMoved) destination.renameTo(source)
                throw failure
            }
        }
    }

    /** Repairs only this game's transaction artifacts after an app or device interruption. */
    private fun recoverInterruptedPublish(parent: File, target: File, rollback: File, gameId: String) {
        val canonicalParent = parent.canonicalFile
        fun directChild(file: File): Boolean = file.canonicalFile.parentFile == canonicalParent
        check(directChild(target) && directChild(rollback)) { "Unsafe install transaction path" }

        if (!target.exists() && rollback.exists()) {
            check(rollback.renameTo(target)) { "Could not restore the previous game installation" }
        } else if (target.exists() && rollback.exists()) {
            check(rollback.deleteRecursively()) { "Could not clear a completed install rollback" }
        }

        val stagingPrefix = ".installing-$gameId-"
        canonicalParent.listFiles()
            ?.filter { it.name.startsWith(stagingPrefix) && directChild(it) }
            ?.forEach { stale ->
                check(stale.deleteRecursively()) { "Could not clear an interrupted game extraction" }
            }
    }

    private suspend fun extract(game: SupportedGame, source: Uri, destination: File): Long =
        withContext(Dispatchers.IO) {
            check(destination.mkdirs()) { "Could not create the extraction directory" }
            val startedAt = SystemClock.elapsedRealtime()
            val compressedBytes = context.contentResolver.openFileDescriptor(source, "r")?.use {
                it.statSize.coerceAtLeast(0L)
            } ?: 0L
            val available = StatFs(destination.absolutePath).availableBytes
            if (compressedBytes > 0 && available < compressedBytes + STORAGE_RESERVE) {
                error("Not enough free space to install this archive")
            }
            publish(
                game.id,
                InstallStage.EXTRACTING,
                InstallStage.EXTRACTING.baseProgress,
                "Extracting the selected game archive",
            )

            val result = extractSeekable(game, source, destination, available)
                ?: extractStreaming(game, source, destination, compressedBytes, available)
            val elapsedMs = (SystemClock.elapsedRealtime() - startedAt).coerceAtLeast(1L)
            val bytesPerSecond = result.bytes * 1_000L / elapsedMs
            publish(
                game.id,
                InstallStage.EXTRACTING,
                stageProgress(InstallStage.EXTRACTING, 1f),
                "Archive extracted · ${result.entries} entries · ${formatBytes(result.bytes)} · " +
                    "${formatDuration(elapsedMs)} · ${formatBytes(bytesPerSecond)}/s",
            )
            result.bytes
        }

    /**
     * Uses the ZIP central directory when the Android document provider exposes
     * a seekable descriptor. Independent entries can then inflate concurrently
     * without copying the multi-gigabyte archive into app storage first.
     */
    private fun extractSeekable(
        game: SupportedGame,
        source: Uri,
        destination: File,
        available: Long,
    ): ExtractionResult? {
        val descriptor = context.contentResolver.openFileDescriptor(source, "r") ?: return null
        descriptor.use { parcel ->
            val zip = try {
                ZipFile("/proc/self/fd/${parcel.fd}")
            } catch (unavailable: IOException) {
                Log.i(LOG_TAG, "ZIP provider is not seekable; using streaming extraction")
                return null
            }
            zip.use { archive ->
                val root = destination.canonicalFile
                val windowsPaths = mutableMapOf<String, Boolean>()
                val files = ArrayList<SeekableZipEntry>()
                var entries = 0
                var expectedBytes = 0L
                val enumeration = archive.entries()
                while (enumeration.hasMoreElements()) {
                    val entry = enumeration.nextElement()
                    check(++entries <= MAX_ENTRIES) { "Archive contains too many files" }
                    val relative = normalizeGameArchiveEntry(entry.name)
                    val pathKey = relative.lowercase(Locale.ROOT).trimEnd('/')
                    val previousWasDirectory = windowsPaths[pathKey]
                    check(previousWasDirectory == null || (previousWasDirectory && entry.isDirectory)) {
                        "Archive contains a duplicate Windows path: ${entry.name}"
                    }
                    windowsPaths[pathKey] = entry.isDirectory
                    val output = File(root, relative)
                    if (entry.isDirectory) {
                        check(output.isDirectory || output.mkdirs()) { "Could not create $relative" }
                    } else {
                        check(entry.size >= 0L) { "Archive entry has no declared size: ${entry.name}" }
                        expectedBytes = Math.addExact(expectedBytes, entry.size)
                        check(expectedBytes <= available - STORAGE_RESERVE) {
                            "The archive is larger than the available storage"
                        }
                        check(output.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
                            "Could not create the directory for $relative"
                        }
                        files += SeekableZipEntry(entry, output)
                    }
                }
                check(entries > 0) { "The selected ZIP is empty" }
                if (files.isEmpty()) return ExtractionResult(entries, 0L)

                val workers = minOf(PARALLEL_EXTRACTION_WORKERS, files.size)
                publish(
                    game.id,
                    InstallStage.EXTRACTING,
                    InstallStage.EXTRACTING.baseProgress,
                    "Fast extraction · $workers workers · ${formatBytes(expectedBytes)}",
                )
                val extracted = AtomicLong(0L)
                val lastPercent = AtomicInteger(-1)
                val nextFile = AtomicInteger(0)
                val progressLock = Any()
                val executor = Executors.newFixedThreadPool(workers) { task ->
                    Thread(task, "GameZipExtractor").apply { priority = Thread.NORM_PRIORITY }
                }
                try {
                    val futures = List(workers) {
                        executor.submit<Unit> {
                            val buffer = ByteArray(EXTRACTION_BUFFER_SIZE)
                            while (true) {
                                val index = nextFile.getAndIncrement()
                                if (index >= files.size) break
                                val item = files[index]
                                archive.getInputStream(item.entry).use { input ->
                                    FileOutputStream(item.output).use { output ->
                                        while (true) {
                                            if (Thread.currentThread().isInterrupted) {
                                                throw InterruptedException("Game extraction cancelled")
                                            }
                                            val count = input.read(buffer)
                                            if (count < 0) break
                                            output.write(buffer, 0, count)
                                            val completed = extracted.addAndGet(count.toLong())
                                            reportExtractionProgress(
                                                game,
                                                item.output.name,
                                                completed,
                                                expectedBytes,
                                                lastPercent,
                                                progressLock,
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                    futures.forEach { future ->
                        try {
                            future.get()
                        } catch (failure: ExecutionException) {
                            throw failure.cause ?: failure
                        }
                    }
                } finally {
                    executor.shutdownNow()
                    executor.awaitTermination(EXTRACTOR_STOP_TIMEOUT_SECONDS, TimeUnit.SECONDS)
                }
                check(extracted.get() == expectedBytes) {
                    "Archive extraction was incomplete: expected $expectedBytes bytes, wrote ${extracted.get()}"
                }
                return ExtractionResult(entries, extracted.get())
            }
        }
    }

    /** Safe fallback for cloud/document providers which expose a pipe, not a file. */
    private fun extractStreaming(
        game: SupportedGame,
        source: Uri,
        destination: File,
        compressedBytes: Long,
        available: Long,
    ): ExtractionResult {
        val raw = context.contentResolver.openInputStream(source)
            ?: error("The selected archive could not be opened")
        val counted = CountingInputStream(raw)
        val root = destination.canonicalFile
        var written = 0L
        var entries = 0
        val lastPercent = AtomicInteger(-1)
        val windowsPaths = mutableMapOf<String, Boolean>()
        ZipInputStream(counted.buffered(INPUT_BUFFER_SIZE)).use { zip ->
            val buffer = ByteArray(EXTRACTION_BUFFER_SIZE)
            while (true) {
                val entry = zip.nextEntry ?: break
                check(++entries <= MAX_ENTRIES) { "Archive contains too many files" }
                val relative = normalizeGameArchiveEntry(entry.name)
                val pathKey = relative.lowercase(Locale.ROOT).trimEnd('/')
                val previousWasDirectory = windowsPaths[pathKey]
                check(previousWasDirectory == null || (previousWasDirectory && entry.isDirectory)) {
                    "Archive contains a duplicate Windows path: ${entry.name}"
                }
                windowsPaths[pathKey] = entry.isDirectory
                val output = File(root, relative)
                if (entry.isDirectory) {
                    check(output.isDirectory || output.mkdirs()) { "Could not create $relative" }
                } else {
                    check(output.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
                        "Could not create the directory for $relative"
                    }
                    FileOutputStream(output).use { file ->
                        while (true) {
                            val count = zip.read(buffer)
                            if (count < 0) break
                            file.write(buffer, 0, count)
                            written += count
                            check(written <= available - STORAGE_RESERVE) {
                                "The archive is larger than the available storage"
                            }
                            if (compressedBytes > 0) {
                                reportExtractionProgress(
                                    game,
                                    output.name,
                                    counted.count,
                                    compressedBytes,
                                    lastPercent,
                                    this,
                                )
                            }
                        }
                    }
                }
                zip.closeEntry()
            }
        }
        check(entries > 0) { "The selected ZIP is empty" }
        return ExtractionResult(entries, written)
    }

    private fun reportExtractionProgress(
        game: SupportedGame,
        fileName: String,
        completed: Long,
        total: Long,
        lastPercent: AtomicInteger,
        lock: Any,
    ) {
        val percent = percentOf(completed, total)
        synchronized(lock) {
            if (percent <= lastPercent.get()) return
            lastPercent.set(percent)
            publish(
                game.id,
                InstallStage.EXTRACTING,
                stageProgress(InstallStage.EXTRACTING, percent / 100f),
                "Extracting · $fileName",
                addLog = percent == 100 || (percent > 0 && percent % 5 == 0),
            )
        }
    }

    private fun locateGameRoot(staging: File, game: SupportedGame): File? {
        fun valid(root: File): Boolean = game.requiredFiles.all {
            resolveWindowsFile(root, it)?.isFile == true
        }
        if (valid(staging)) return staging

        val candidates = mutableListOf<File>()
        fun visit(directory: File, depth: Int) {
            if (depth > MAX_WRAPPER_DEPTH || candidates.size >= MAX_GAME_ROOT_CANDIDATES) return
            if (valid(directory)) {
                candidates += directory
                return
            }
            directory.listFiles()
                ?.asSequence()
                ?.filter { it.isDirectory }
                ?.filterNot { it.name.startsWith(".") }
                ?.forEach { visit(it, depth + 1) }
        }
        visit(staging, 0)
        if (candidates.size <= 1) return candidates.singleOrNull()

        // Some archives contain documentation or backup wrappers with another
        // executable. Prefer the one that passes the complete build contract.
        val compatible = candidates.filter { candidate ->
            runCatching { validateGameBuild(game, candidate) }.isSuccess
        }
        return (compatible.ifEmpty { candidates }).minByOrNull { candidate ->
            candidate.relativeTo(staging).invariantSeparatorsPath.count { it == '/' }
        }
    }

    private fun validateGameBuild(game: SupportedGame, root: File) {
        game.requiredFiles.forEach { path ->
            check(resolveWindowsFile(root, path)?.isFile == true) { "Missing game file: $path" }
        }
        game.fileRules.forEach { rule ->
            val file = resolveWindowsFile(root, rule.path)
                ?.takeIf(File::isFile)
                ?: error("Missing versioned game file: ${rule.path}")
            rule.bytes?.let { expected ->
                check(file.length() == expected) {
                    unsupportedBuildSizeMessage(game, rule.path, expected, file.length())
                }
            }
            rule.sha256?.let { expected ->
                val observed = sha256(file)
                check(observed == expected) {
                    "Unsupported ${rule.path}: SHA-256 ${observed.take(12)}... does not match the " +
                        "verified build required by this dual-screen mod"
                }
            }
            if (rule.sha256AnyOf.isNotEmpty()) {
                val observed = sha256(file)
                check(observed in rule.sha256AnyOf) {
                    "Unsupported ${rule.path}: SHA-256 ${observed.take(12)}... is not one of " +
                        "the verified builds supported by this dual-screen mod"
                }
            }
        }
        if (game.configurationLauncherCandidates.isNotEmpty()) {
            check(resolveConfigurationLauncherExecutable(root, game) != null) {
                "Missing settings launcher: ${game.configurationLauncherCandidates.joinToString(" or ")}"
            }
        }
    }

    private fun applyPrerequisites(game: SupportedGame, root: File) {
        game.prerequisites.forEachIndexed { index, prerequisite ->
            val span = 0.58f / game.prerequisites.size
            val start = span * index
            publish(
                game.id,
                InstallStage.PATCHING,
                stageProgress(InstallStage.PATCHING, start),
                "Downloading ${prerequisite.name} ${prerequisite.version}",
            )
            val archive = downloadPrerequisite(game, prerequisite) { percent ->
                val local = start + span * (percent / 100f) * 0.82f
                publish(
                    game.id,
                    InstallStage.PATCHING,
                    stageProgress(InstallStage.PATCHING, local),
                    "Downloading ${prerequisite.name}",
                    addLog = percent % 10 == 0,
                )
            }
            try {
                publish(
                    game.id,
                    InstallStage.PATCHING,
                    stageProgress(InstallStage.PATCHING, start + span * 0.86f),
                    "Installing ${prerequisite.name} ${prerequisite.version}",
                )
                extractPrerequisite(archive, root, prerequisite)
                prerequisite.installedFiles.forEach { path ->
                    check(resolveWindowsFile(root, path)?.isFile == true) {
                        "${prerequisite.name} did not install required file: $path"
                    }
                }
                publish(
                    game.id,
                    InstallStage.PATCHING,
                    stageProgress(InstallStage.PATCHING, start + span),
                    "Installed ${prerequisite.name} ${prerequisite.version}",
                )
            } finally {
                archive.delete()
            }
        }
    }

    private fun downloadPrerequisite(
        game: SupportedGame,
        prerequisite: PrerequisitePackage,
        progress: (Int) -> Unit,
    ): File {
        val target = File(context.cacheDir, "prerequisite-${game.id}-${safeName(prerequisite.name)}.zip")
        if (target.isFile && target.length() == prerequisite.bytes && sha256(target) == prerequisite.sha256) {
            progress(100)
            return target
        }
        target.delete()
        val partial = File(target.parentFile, target.name + ".partial")
        partial.delete()

        val connection = (URL(prerequisite.url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 20_000
            readTimeout = 90_000
            instanceFollowRedirects = true
            requestMethod = "GET"
            setRequestProperty("Accept", "application/octet-stream")
            connect()
        }
        try {
            check(connection.responseCode in 200..299) {
                "${prerequisite.name} download failed with HTTP ${connection.responseCode}"
            }
            val advertised = connection.contentLengthLong
            check(advertised <= 0 || advertised == prerequisite.bytes) {
                "${prerequisite.name} download size changed unexpectedly"
            }
            var received = 0L
            var lastPercent = -1
            connection.inputStream.buffered(BUFFER_SIZE).use { input ->
                FileOutputStream(partial).buffered(BUFFER_SIZE).use { output ->
                    val buffer = ByteArray(BUFFER_SIZE)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        received += count
                        check(received <= prerequisite.bytes) {
                            "${prerequisite.name} download exceeded its pinned size"
                        }
                        output.write(buffer, 0, count)
                        val percent = percentOf(received, prerequisite.bytes)
                        if (percent != lastPercent) {
                            lastPercent = percent
                            progress(percent)
                        }
                    }
                }
            }
            check(received == prerequisite.bytes) { "${prerequisite.name} download is incomplete" }
            check(sha256(partial) == prerequisite.sha256) {
                "${prerequisite.name} package failed SHA-256 verification"
            }
            check(partial.renameTo(target)) { "Could not publish ${prerequisite.name} package" }
            return target
        } finally {
            connection.disconnect()
            if (!target.isFile) partial.delete()
        }
    }

    private fun extractPrerequisite(
        archive: File,
        destination: File,
        prerequisite: PrerequisitePackage,
    ) {
        val nestedSuffix = prerequisite.nestedZipSuffix
        if (nestedSuffix == null) {
            FileInputStream(archive).buffered(BUFFER_SIZE).use { input ->
                ZipInputStream(input).use { zip -> extractOverlay(zip, destination) }
            }
            return
        }

        val nested = File(context.cacheDir, "nested-${safeName(prerequisite.name)}.zip")
        nested.delete()
        try {
            var found = false
            FileInputStream(archive).buffered(BUFFER_SIZE).use { input ->
                ZipInputStream(input).use { outer ->
                    while (true) {
                        val entry = outer.nextEntry ?: break
                        val name = entry.name.replace('\\', '/')
                        if (!entry.isDirectory && name.endsWith(nestedSuffix, ignoreCase = true)) {
                            check(!found) { "${prerequisite.name} package contains duplicate install data" }
                            var written = 0L
                            FileOutputStream(nested).buffered(BUFFER_SIZE).use { output ->
                                val buffer = ByteArray(BUFFER_SIZE)
                                while (true) {
                                    val count = outer.read(buffer)
                                    if (count < 0) break
                                    written += count
                                    check(written <= MAX_PREREQUISITE_BYTES) {
                                        "${prerequisite.name} install data is too large"
                                    }
                                    output.write(buffer, 0, count)
                                }
                            }
                            found = true
                        }
                        outer.closeEntry()
                    }
                }
            }
            check(found && nested.isFile) { "${prerequisite.name} package has no Windows install data" }
            FileInputStream(nested).buffered(BUFFER_SIZE).use { input ->
                ZipInputStream(input).use { zip -> extractOverlay(zip, destination) }
            }
        } finally {
            nested.delete()
        }
    }

    private fun extractOverlay(zip: ZipInputStream, destination: File) {
        val canonicalRoot = destination.canonicalFile
        var entries = 0
        var written = 0L
        val buffer = ByteArray(BUFFER_SIZE)
        while (true) {
            val entry = zip.nextEntry ?: break
            entries++
            check(entries <= MAX_PREREQUISITE_ENTRIES) { "Prerequisite package contains too many files" }
            val relative = entry.name.replace('\\', '/').trimStart('/')
            check(relative.isNotBlank()) { "Prerequisite package contains an empty path" }
            val output = File(canonicalRoot, relative).canonicalFile
            check(output.path.startsWith(canonicalRoot.path + File.separator)) {
                "Prerequisite package contains an unsafe path: ${entry.name}"
            }
            if (entry.isDirectory) {
                check(output.isDirectory || output.mkdirs()) { "Could not create $relative" }
            } else {
                check(output.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
                    "Could not create the directory for $relative"
                }
                FileOutputStream(output).buffered(BUFFER_SIZE).use { file ->
                    while (true) {
                        val count = zip.read(buffer)
                        if (count < 0) break
                        written += count
                        check(written <= MAX_PREREQUISITE_BYTES) {
                            "Prerequisite package expands beyond the safety limit"
                        }
                        file.write(buffer, 0, count)
                    }
                }
            }
            zip.closeEntry()
        }
        check(entries > 0) { "Prerequisite package is empty" }
    }

    private fun applyPayload(
        game: SupportedGame,
        root: File,
        progressStart: Float,
        progressEnd: Float,
    ): PayloadManifest {
        val manifest = readPayloadManifest(game)
        val canonicalRoot = root.canonicalFile
        val patchTargets = manifest.patches.groupBy(PayloadPatch::target)
        val operationCount = manifest.files.size + patchTargets.size + manifest.removals.size
        check(operationCount > 0) { "The mod payload manifest is empty" }
        var completed = 0
        fun report(detail: String) {
            completed++
            publish(
                game.id,
                InstallStage.PATCHING,
                stageProgress(
                    InstallStage.PATCHING,
                    progressStart +
                        (progressEnd - progressStart) * (completed.toFloat() / operationCount),
                ),
                detail,
            )
        }

        manifest.files.forEach { entry ->
            val target = File(canonicalRoot, entry.target).canonicalFile
            checkPayloadTarget(canonicalRoot, target)
            check(target.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
                "Could not create the mod directory"
            }
            writeAssetAtomically(game, entry, target)
            report("Applied ${entry.target}")
        }

        patchTargets.forEach { (relativeTarget, candidates) ->
            val target = File(canonicalRoot, relativeTarget).canonicalFile
            checkPayloadTarget(canonicalRoot, target)
            check(target.isFile) { "Binary patch target is missing: $relativeTarget" }
            val observed = sha256(target)
            val alreadyCurrent = candidates.any { it.outputSha256 == observed }
            if (!alreadyCurrent) {
                val selected = candidates.firstOrNull { it.inputSha256 == observed }
                    ?: error(
                        "Unsupported $relativeTarget: SHA-256 ${observed.take(12)}... has no " +
                            "matching native patch",
                    )
                applyBinaryPatch(game, selected, target)
            }
            check(candidates.any { it.outputSha256 == sha256(target) }) {
                "Native binary patch verification failed: $relativeTarget"
            }
            report("Applied $relativeTarget")
        }

        // Retired loader files are removed only after every replacement has
        // been atomically installed and verified. A rejected/corrupt native
        // patch therefore cannot dismantle the currently working mod first.
        manifest.removals.forEach { relativeTarget ->
            val target = File(canonicalRoot, relativeTarget).canonicalFile
            checkPayloadTarget(canonicalRoot, target)
            check(!target.exists() || target.isFile) {
                "Refusing to recursively remove a mod payload directory: $relativeTarget"
            }
            if (target.isFile) {
                check(target.delete()) { "Could not remove retired mod file: $relativeTarget" }
            }
            target.parentFile?.takeIf { parent ->
                parent != canonicalRoot && parent.isDirectory && parent.list()?.isEmpty() == true
            }?.delete()
            report("Removed retired $relativeTarget")
        }
        return manifest
    }

    private fun payloadIsCurrent(root: File, manifest: PayloadManifest): Boolean {
        val canonicalRoot = root.canonicalFile
        val filesCurrent = manifest.files.all { entry ->
            val target = File(canonicalRoot, entry.target).canonicalFile
            runCatching { checkPayloadTarget(canonicalRoot, target) }.isSuccess &&
                target.isFile && sha256(target) == entry.sha256
        }
        if (!filesCurrent) return false
        if (manifest.removals.any { relativeTarget ->
                val target = File(canonicalRoot, relativeTarget).canonicalFile
                runCatching { checkPayloadTarget(canonicalRoot, target) }.isFailure || target.exists()
            }
        ) return false
        return manifest.patches.groupBy(PayloadPatch::target).all { (relativeTarget, patches) ->
            val target = File(canonicalRoot, relativeTarget).canonicalFile
            if (runCatching { checkPayloadTarget(canonicalRoot, target) }.isFailure ||
                !target.isFile
            ) {
                false
            } else {
                val observed = sha256(target)
                patches.any { it.outputSha256 == observed }
            }
        }
    }

    private fun checkPayloadTarget(root: File, target: File) {
        check(target.path.startsWith(root.path + File.separator)) {
            "Mod payload contains an unsafe target"
        }
    }

    private fun writeAssetAtomically(game: SupportedGame, entry: PayloadFile, target: File) {
        val temporary = File(
            checkNotNull(target.parentFile),
            ".${target.name}.dualscreen-${UUID.randomUUID()}.tmp",
        )
        try {
            PayloadAssetStore.open(context, "mods/${game.id}/payload/${entry.source}").use { input ->
                FileOutputStream(temporary).use { output ->
                    input.copyTo(output, BUFFER_SIZE)
                    output.fd.sync()
                }
            }
            check(sha256(temporary) == entry.sha256) {
                "Mod asset failed verification: ${entry.source}"
            }
            replaceAtomically(temporary, target)
        } finally {
            temporary.delete()
        }
    }

    /** Applies the small delta only to the exact game assembly it was built for. */
    private fun applyBinaryPatch(game: SupportedGame, patch: PayloadPatch, target: File) {
        val assetPath = "mods/${game.id}/payload/${patch.source}"
        val patchHash = PayloadAssetStore.open(context, assetPath).use(::sha256)
        check(patchHash == patch.patchSha256) { "Native patch asset failed verification" }
        check(target.length() <= MAX_BINARY_PATCH_INPUT) { "Native patch input is unexpectedly large" }
        check(sha256(target) == patch.inputSha256) { "Native patch input changed during deployment" }

        val temporary = File(
            checkNotNull(target.parentFile),
            ".${target.name}.dualscreen-${UUID.randomUUID()}.tmp",
        )
        try {
            PayloadAssetStore.open(context, assetPath).buffered(BUFFER_SIZE).use { compressed ->
                DataInputStream(GZIPInputStream(compressed, BUFFER_SIZE)).use { input ->
                    val magic = ByteArray(BINARY_PATCH_MAGIC.size).also(input::readFully)
                    check(magic.contentEquals(BINARY_PATCH_MAGIC)) { "Native patch header is invalid" }
                    val inputLength = java.lang.Long.reverseBytes(input.readLong())
                    val inputHash = ByteArray(32).also(input::readFully).toHex()
                    val outputLength = java.lang.Long.reverseBytes(input.readLong())
                    val outputHash = ByteArray(32).also(input::readFully).toHex()
                    check(inputLength == target.length() && inputHash == patch.inputSha256) {
                        "Native patch does not match this game assembly"
                    }
                    check(outputLength in 1..MAX_BINARY_PATCH_OUTPUT &&
                        outputHash == patch.outputSha256
                    ) { "Native patch declares an invalid output" }

                    RandomAccessFile(target, "r").use { source ->
                        FileOutputStream(temporary).use { output ->
                            val buffer = ByteArray(BUFFER_SIZE)
                            var produced = 0L
                            while (true) {
                                when (val opcode = input.readUnsignedByte()) {
                                    0 -> {
                                        val offset = java.lang.Long.reverseBytes(input.readLong())
                                        val length = Integer.reverseBytes(input.readInt())
                                        check(offset >= 0 && length > 0 &&
                                            offset <= inputLength - length &&
                                            produced <= outputLength - length
                                        ) { "Native patch contains an invalid copy operation" }
                                        source.seek(offset)
                                        var remaining = length
                                        while (remaining > 0) {
                                            val count = source.read(buffer, 0, minOf(buffer.size, remaining))
                                            check(count > 0) { "Native patch source ended unexpectedly" }
                                            output.write(buffer, 0, count)
                                            remaining -= count
                                        }
                                        produced += length
                                    }
                                    1 -> {
                                        val length = Integer.reverseBytes(input.readInt())
                                        check(length > 0 && produced <= outputLength - length) {
                                            "Native patch contains an invalid literal operation"
                                        }
                                        var remaining = length
                                        while (remaining > 0) {
                                            val count = minOf(buffer.size, remaining)
                                            input.readFully(buffer, 0, count)
                                            output.write(buffer, 0, count)
                                            remaining -= count
                                        }
                                        produced += length
                                    }
                                    255 -> {
                                        check(produced == outputLength) {
                                            "Native patch output length is incomplete"
                                        }
                                        break
                                    }
                                    else -> error("Native patch contains unknown opcode $opcode")
                                }
                            }
                            output.fd.sync()
                        }
                    }
                }
            }
            check(temporary.length() <= MAX_BINARY_PATCH_OUTPUT &&
                sha256(temporary) == patch.outputSha256
            ) { "Native patch output failed verification" }
            replaceAtomically(temporary, target)
        } finally {
            temporary.delete()
        }
    }

    private fun replaceAtomically(source: File, target: File) {
        try {
            Files.move(
                source.toPath(),
                target.toPath(),
                StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING,
            )
        } catch (_: AtomicMoveNotSupportedException) {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
    }

    private fun sha256(input: InputStream): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            digest.update(buffer, 0, count)
        }
        return digest.digest().toHex()
    }

    private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }

    private fun readPayloadManifest(game: SupportedGame): PayloadManifest {
        val manifestPath = "mods/${game.id}/manifest.json"
        val manifestText = runCatching {
            context.assets.open(manifestPath).bufferedReader().use { it.readText() }
        }.getOrElse { error("No deployable dual-screen mod is bundled for ${game.title}") }
        val json = JSONObject(manifestText.trimStart('\uFEFF'))
        val files = json.optJSONArray("files")
        val patches = json.optJSONArray("patches")
        val removals = json.optJSONArray("remove")
        val manifest = PayloadManifest(
            version = json.getString("version"),
            files = buildList {
                for (index in 0 until (files?.length() ?: 0)) {
                    val entry = checkNotNull(files).getJSONObject(index)
                    add(
                        PayloadFile(
                            source = entry.getString("source"),
                            target = entry.getString("target"),
                            sha256 = entry.getString("sha256").lowercase(),
                        ),
                    )
                }
            },
            patches = buildList {
                for (index in 0 until (patches?.length() ?: 0)) {
                    val entry = checkNotNull(patches).getJSONObject(index)
                    add(
                        PayloadPatch(
                            source = entry.getString("source"),
                            target = entry.getString("target"),
                            patchSha256 = entry.getString("sha256").lowercase(),
                            inputSha256 = entry.getString("inputSha256").lowercase(),
                            outputSha256 = entry.getString("outputSha256").lowercase(),
                        ),
                    )
                }
            },
            removals = buildList {
                for (index in 0 until (removals?.length() ?: 0)) {
                    add(checkNotNull(removals).getString(index))
                }
            },
        )
        check(manifest.files.isNotEmpty() || manifest.patches.isNotEmpty() ||
            manifest.removals.isNotEmpty()
        ) {
            "The mod payload manifest is empty"
        }
        return manifest
    }

    private fun verify(game: SupportedGame, root: File, payload: PayloadManifest): String {
        validateGameBuild(game, root)
        game.prerequisites.forEach { prerequisite ->
            prerequisite.installedFiles.forEach { path ->
                check(resolveWindowsFile(root, path)?.isFile == true) {
                    "Installed prerequisite verification failed: $path"
                }
            }
        }
        val executable = resolveLaunchExecutable(root, game)
            ?: error("Missing launch executable: ${game.executableCandidates.joinToString(" or ")}")
        payload.files.forEach { file ->
            val installed = File(root, file.target)
            check(installed.isFile && sha256(installed) == file.sha256) {
                "Installed mod verification failed: ${file.target}"
            }
        }
        payload.patches.groupBy(PayloadPatch::target).forEach { (target, patches) ->
            val installed = File(root, target)
            val observed = installed.takeIf(File::isFile)?.let(::sha256)
            check(observed != null && patches.any { it.outputSha256 == observed }) {
                "Installed native patch verification failed: $target"
            }
        }
        payload.removals.forEach { target ->
            check(!File(root, target).exists()) { "Retired mod file is still installed: $target" }
        }
        return executable
    }

    private fun begin(game: SupportedGame) {
        runCatching {
            installLogFile(game.id).apply {
                parentFile?.mkdirs()
                writeText("${formatLogLine(InstallStage.RUNTIME.baseProgress, "Starting ${game.title}")}\n")
            }
        }.onFailure { Log.w(LOG_TAG, "Could not create install log for ${game.id}", it) }
        mutableProgress.update { current ->
            val initialFraction = InstallStage.RUNTIME.baseProgress
            current + (game.id to InstallProgress(
                gameId = game.id,
                stage = InstallStage.RUNTIME,
                fraction = initialFraction,
                detail = "Starting installation",
                log = listOf(formatLogLine(initialFraction, "Starting ${game.title}")),
                running = true,
                updateSequence = progressSequence.incrementAndGet(),
            ))
        }
    }

    private fun publish(
        gameId: String,
        stage: InstallStage,
        fraction: Float,
        detail: String,
        error: String? = null,
        running: Boolean = true,
        addLog: Boolean = true,
    ) {
        var persistedLine: String? = null
        mutableProgress.update { current ->
            val previous = current[gameId] ?: InstallProgress(gameId)
            val stableFraction = monotonicInstallFraction(previous, fraction)
            val logLine = formatLogLine(stableFraction, detail)
            val nextLog = if (!addLog || previous.log.lastOrNull() == logLine) previous.log else {
                persistedLine = logLine
                (previous.log + logLine).takeLast(MAX_LOG_LINES)
            }
            current + (gameId to previous.copy(
                stage = stage,
                fraction = stableFraction,
                detail = detail,
                log = nextLog,
                running = running,
                error = error,
                updateSequence = progressSequence.incrementAndGet(),
            ))
        }
        val lines = buildList {
            persistedLine?.let(::add)
            error?.let { add("ERROR  $it") }
        }
        if (lines.isNotEmpty()) {
            runCatching {
                installLogFile(gameId).appendText(lines.joinToString(separator = "\n", postfix = "\n"))
            }.onFailure { Log.w(LOG_TAG, "Could not persist install log for $gameId", it) }
        }
    }

    private fun installLogFile(gameId: String): File =
        File(context.filesDir, "install-logs/${safeName(gameId)}-latest.log")

    private fun stageProgress(stage: InstallStage, local: Float): Float {
        val next = InstallStage.entries.getOrNull(stage.ordinal + 1)?.baseProgress ?: 1f
        return stage.baseProgress + (next - stage.baseProgress) * local.coerceIn(0f, 1f)
    }

    private fun formatLogLine(fraction: Float, detail: String): String =
        formatInstallLogLine(fraction, detail)

    private fun percentOf(completed: Long, total: Long): Int {
        if (completed <= 0L || total <= 0L) return 0
        return (((completed * 100L) + total - 1L) / total).toInt().coerceIn(1, 100)
    }

    private fun formatBytes(bytes: Long): String = when {
        bytes >= 1024L * 1024L * 1024L -> "%.1f GiB".format(bytes / (1024.0 * 1024.0 * 1024.0))
        bytes >= 1024L * 1024L -> "%.1f MiB".format(bytes / (1024.0 * 1024.0))
        bytes >= 1024L -> "%.1f KiB".format(bytes / 1024.0)
        else -> "$bytes B"
    }

    private fun formatDuration(milliseconds: Long): String {
        val seconds = milliseconds.coerceAtLeast(0L) / 1_000.0
        return if (seconds < 60.0) "%.1fs".format(seconds) else {
            val wholeSeconds = seconds.toLong()
            "${wholeSeconds / 60}m ${wholeSeconds % 60}s"
        }
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered(BUFFER_SIZE).use { input ->
            val buffer = ByteArray(BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun safeName(value: String): String = value.replace(Regex("[^A-Za-z0-9._-]"), "_")

    private data class PayloadFile(val source: String, val target: String, val sha256: String)
    private data class PayloadPatch(
        val source: String,
        val target: String,
        val patchSha256: String,
        val inputSha256: String,
        val outputSha256: String,
    )
    private data class PayloadManifest(
        val version: String,
        val files: List<PayloadFile>,
        val patches: List<PayloadPatch>,
        val removals: List<String>,
    )
    private data class SeekableZipEntry(val entry: ZipEntry, val output: File)
    private data class ExtractionResult(val entries: Int, val bytes: Long)

    private class CountingInputStream(input: InputStream) : FilterInputStream(input) {
        var count: Long = 0
            private set

        override fun read(): Int = super.read().also { if (it >= 0) count++ }

        override fun read(buffer: ByteArray, offset: Int, length: Int): Int =
            super.read(buffer, offset, length).also { if (it > 0) count += it }
    }

    companion object {
        private const val BUFFER_SIZE = 128 * 1024
        private const val INPUT_BUFFER_SIZE = 1024 * 1024
        private const val EXTRACTION_BUFFER_SIZE = 1024 * 1024
        private const val PARALLEL_EXTRACTION_WORKERS = 3
        private const val EXTRACTOR_STOP_TIMEOUT_SECONDS = 5L
        private const val MAX_ENTRIES = 250_000
        private const val MAX_WRAPPER_DEPTH = 8
        private const val MAX_GAME_ROOT_CANDIDATES = 32
        private const val MAX_LOG_LINES = 600
        private const val STORAGE_RESERVE = 768L * 1024 * 1024
        private const val MAX_PREREQUISITE_ENTRIES = 65_536
        private const val MAX_PREREQUISITE_BYTES = 768L * 1024 * 1024
        private const val MAX_BINARY_PATCH_INPUT = 128L * 1024 * 1024
        private const val MAX_BINARY_PATCH_OUTPUT = 128L * 1024 * 1024
        private val BINARY_PATCH_MAGIC = byteArrayOf(
            'D'.code.toByte(),
            'S'.code.toByte(),
            'D'.code.toByte(),
            'P'.code.toByte(),
            '1'.code.toByte(),
            0,
            0,
            0,
        )
        private const val LOG_TAG = "DualscreenInstaller"
    }
}

internal fun normalizeGameArchiveEntry(entryName: String): String {
    require('\u0000' !in entryName) { "Archive contains an invalid filename" }
    val normalized = entryName.replace('\\', '/')
    require(!normalized.startsWith('/')) { "Archive contains an absolute path: $entryName" }
    val segments = normalized.split('/').filter(String::isNotBlank)
    require(segments.isNotEmpty() && segments.none { it == "." || it == ".." || ':' in it }) {
        "Archive contains an unsafe path: $entryName"
    }
    return segments.joinToString("/") + if (normalized.endsWith('/')) "/" else ""
}

internal fun unsupportedBuildSizeMessage(
    game: SupportedGame,
    path: String,
    expected: Long,
    observed: Long,
): String = if (game.id == "fallout3" && observed == 15_044_024L && expected == 16_855_040L) {
    "Fallout 3 1.7.0.3 was detected ($observed-byte $path). The dual-screen bridge requires " +
        "Fallout 3 1.7.0.4 ($expected bytes). Update the game first, then create a new ZIP from " +
        "that updated game folder."
} else {
    "Unsupported $path: expected $expected bytes, found $observed"
}
