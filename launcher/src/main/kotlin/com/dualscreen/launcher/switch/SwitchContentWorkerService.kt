package com.dualscreen.launcher.switch

import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.RemoteException
import android.util.Log
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeout
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.model.InstallResult
import org.yuzu.yuzu_emu.model.PatchType
import org.yuzu.yuzu_emu.utils.GameMetadata
import org.yuzu.yuzu_emu.utils.GameHelper

/**
 * Runs all untrusted NSP parsing in a dedicated process. Eden's parser is native code: a corrupt
 * package, provider bug, or native regression can abort a process before Kotlin can catch it. A
 * remote service makes that failure recoverable and keeps the launcher/UI process alive.
 */
internal class SwitchContentWorkerService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val incoming = Messenger(Handler(Looper.getMainLooper(), ::onMessage))

    override fun onBind(intent: Intent?): IBinder = incoming.binder

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    private fun onMessage(message: Message): Boolean {
        if (message.what != MSG_REQUEST || message.replyTo == null) return false
        val request = message.data
        val reply = message.replyTo
        scope.launch {
            runCatching {
                initializeRuntime()
                when (request.getString(KEY_ACTION)) {
                    ACTION_INSPECT_BASE -> inspectBase(request)
                    ACTION_INSTALL_CONTENT -> installContent(request, reply)
                    ACTION_QUERY_PATCHES -> queryPatches(request)
                    ACTION_REMOVE_CONTENT -> removeContent(request)
                    else -> error("Unknown Switch content operation.")
                }
            }.onSuccess { result -> send(reply, MSG_SUCCESS, result) }
                .onFailure { failure ->
                    send(
                        reply,
                        MSG_FAILURE,
                        Bundle().apply {
                            putString(KEY_ERROR, failure.message ?: "Eden could not read that NSP.")
                        },
                    )
                }
        }
        return true
    }

    private fun initializeRuntime() {
        if (YuzuApplication.isInitialized()) return
        EdenRuntimeAbi.verify(classLoader)
        val root = File(getExternalFilesDir(null) ?: filesDir, "Switch/Eden")
        YuzuApplication.initialize(application, root)
    }

    private fun inspectBase(request: Bundle): Bundle {
        val uri = requireNotNull(request.getString(KEY_URI)) { "The selected NSP is unavailable." }
        require(GameMetadata.getIsValid(uri)) {
            "This NSP is not a bootable base game, or the required keys are unavailable."
        }
        return Bundle().apply {
            putString(KEY_PROGRAM_ID, GameMetadata.getProgramId(uri))
            putString(KEY_TITLE, GameMetadata.getTitle(uri))
            putString(KEY_VERSION, GameMetadata.getVersion(uri, true))
            putByteArray(KEY_ICON, GameMetadata.getIcon(uri))
        }
    }

    private fun installContent(request: Bundle, reply: Messenger): Bundle {
        val uri = requireNotNull(request.getString(KEY_URI)) { "The selected NSP is unavailable." }
        val expectedType = request.getInt(KEY_CONTENT_TYPE, -1)
        val expectedBaseId = requireNotNull(request.getString(KEY_BASE_TITLE_ID)).uppercase()
        val records = parsePackageRecords(NativeLibrary.inspectContentPackage(uri))
        require(records.isNotEmpty()) { "The selected file is not a readable Switch content package." }
        require(records.any { it.type == expectedType && it.baseTitleId == expectedBaseId }) {
            val detected = records.joinToString { "${it.kindLabel} ${it.titleId}" }
            "This package does not match the selected game/content type. Detected: $detected"
        }
        require(records.none { it.baseTitleId != expectedBaseId }) {
            "The package contains content for another game and was not installed."
        }

        val progressCallback = EdenInstallProgressCallback { max, progress ->
                val fraction = if (max <= 0L) 0f else (progress.toFloat() / max).coerceIn(0f, 1f)
                send(
                    reply,
                    MSG_PROGRESS,
                    Bundle().apply {
                        putFloat(KEY_PROGRESS, fraction)
                        putString(KEY_DETAIL, "Installing verified Switch content")
                    },
                )
        }
        @Suppress("UNCHECKED_CAST")
        val nativeProgressCallback = progressCallback as (Long, Long) -> Boolean
        val result = InstallResult.from(
            NativeLibrary.installFileToNand(uri, nativeProgressCallback),
        )
        require(result == InstallResult.Success || result == InstallResult.Overwrite) {
            when (result) {
                InstallResult.BaseInstallAttempted -> "Select the base-game control for base NSP files."
                else -> "Eden could not install this content package."
            }
        }
        // InstallEntry refreshes this worker's RegisteredCache. Query it before reporting success
        // so a copied-but-unreadable update can never be presented to the user as installed.
        val patches = decodePatchBundle(queryPatches(request))
        val expectedKind = when (expectedType) {
            TITLE_TYPE_UPDATE -> SwitchContentKind.UPDATE
            TITLE_TYPE_DLC -> SwitchContentKind.DLC
            else -> error("Unsupported installed content type.")
        }
        val installedPatches = patches.filter { it.kind == expectedKind }
        require(installedPatches.any { it.enabled }) {
            "Eden copied the package but could not activate its ${expectedKind.name.lowercase()} content. Check the keys and NSP integrity."
        }
        Log.i(
            TAG,
            "Verified ${expectedKind.name.lowercase()} after NAND install: " +
                installedPatches.joinToString { "${it.name} ${it.version} enabled=${it.enabled}" },
        )
        return encodePatches(patches)
    }

    private fun queryPatches(request: Bundle): Bundle {
        val baseUri = requireNotNull(request.getString(KEY_BASE_URI)) { "The base game is unavailable." }
        val programId = requireNotNull(request.getString(KEY_PROGRAM_ID)) { "The title ID is unavailable." }
        GameHelper.restoreContentForGame(Game("", baseUri, programId))
        val patches = NativeLibrary.getPatchesForFile(baseUri, programId).orEmpty().mapNotNull { patch ->
            val kind = runCatching {
                when (PatchType.from(patch.type)) {
                    PatchType.Update -> SwitchContentKind.UPDATE
                    PatchType.DLC -> SwitchContentKind.DLC
                    PatchType.Mod -> SwitchContentKind.MOD
                }
            }.getOrNull() ?: return@mapNotNull null
            SwitchPatch(
                name = patch.name,
                version = patch.version,
                kind = kind,
                enabled = patch.enabled,
                removable = patch.isRemovable,
            )
        }.distinctBy { listOf(it.kind, it.name, it.version) }
        Log.i(
            TAG,
            "Resolved patches programId=$programId: " +
                patches.joinToString { "${it.kind}:${it.name}:${it.version}:enabled=${it.enabled}" },
        )
        return encodePatches(patches)
    }

    private fun decodePatchBundle(bundle: Bundle): List<SwitchPatch> {
        val names = bundle.getStringArrayList(KEY_PATCH_NAMES).orEmpty()
        val versions = bundle.getStringArrayList(KEY_PATCH_VERSIONS).orEmpty()
        val kinds = bundle.getStringArrayList(KEY_PATCH_KINDS).orEmpty()
        val enabled = bundle.getBooleanArray(KEY_PATCH_ENABLED) ?: booleanArrayOf()
        val removable = bundle.getBooleanArray(KEY_PATCH_REMOVABLE) ?: booleanArrayOf()
        return names.indices.mapNotNull { index ->
            val kind = kinds.getOrNull(index)?.let {
                runCatching { SwitchContentKind.valueOf(it) }.getOrNull()
            } ?: return@mapNotNull null
            SwitchPatch(
                name = names[index],
                version = versions.getOrElse(index) { "" },
                kind = kind,
                enabled = enabled.getOrElse(index) { true },
                removable = removable.getOrElse(index) { false },
            )
        }
    }

    private fun removeContent(request: Bundle): Bundle {
        val programId = requireNotNull(request.getString(KEY_PROGRAM_ID)) { "The title ID is unavailable." }
        val kind = requireNotNull(request.getString(KEY_PATCH_KIND)) { "The content type is unavailable." }
        when (SwitchContentKind.valueOf(kind)) {
            SwitchContentKind.UPDATE -> NativeLibrary.removeUpdate(programId)
            SwitchContentKind.DLC -> NativeLibrary.removeDLC(programId)
            SwitchContentKind.MOD -> NativeLibrary.removeMod(
                programId,
                requireNotNull(request.getString(KEY_PATCH_NAME)) { "The mod name is unavailable." },
            )
            SwitchContentKind.BASE -> error("The base game is managed separately.")
        }
        return queryPatches(request)
    }

    private fun encodePatches(patches: List<SwitchPatch>) = Bundle().apply {
        putStringArrayList(KEY_PATCH_NAMES, ArrayList(patches.map(SwitchPatch::name)))
        putStringArrayList(KEY_PATCH_VERSIONS, ArrayList(patches.map(SwitchPatch::version)))
        putStringArrayList(KEY_PATCH_KINDS, ArrayList(patches.map { it.kind.name }))
        putBooleanArray(KEY_PATCH_ENABLED, patches.map(SwitchPatch::enabled).toBooleanArray())
        putBooleanArray(KEY_PATCH_REMOVABLE, patches.map(SwitchPatch::removable).toBooleanArray())
    }

    private fun send(target: Messenger, what: Int, data: Bundle) {
        runCatching {
            target.send(Message.obtain(null, what).apply { this.data = data })
        }
    }

    private data class PackageRecord(val type: Int, val titleId: String, val baseTitleId: String) {
        val kindLabel: String
            get() = when (type) {
                TITLE_TYPE_APPLICATION -> "base"
                TITLE_TYPE_UPDATE -> "update"
                TITLE_TYPE_DLC -> "DLC"
                else -> "unknown"
            }
    }

    private fun parsePackageRecords(encodedRecords: String): List<PackageRecord> = encodedRecords
        .split(';')
        .mapNotNull { encoded ->
            val fields = encoded.split('|')
            if (fields.size != 3) null else PackageRecord(
                type = fields[0].toIntOrNull() ?: return@mapNotNull null,
                titleId = fields[1].uppercase(),
                baseTitleId = fields[2].uppercase(),
            )
        }

    private companion object {
        const val TAG = "EdenContentWorker"
        const val MSG_REQUEST = 1
        const val MSG_PROGRESS = 2
        const val MSG_SUCCESS = 3
        const val MSG_FAILURE = 4
        const val ACTION_INSPECT_BASE = "inspect_base"
        const val ACTION_INSTALL_CONTENT = "install_content"
        const val ACTION_QUERY_PATCHES = "query_patches"
        const val ACTION_REMOVE_CONTENT = "remove_content"
        const val KEY_ACTION = "action"
        const val KEY_URI = "uri"
        const val KEY_CONTENT_TYPE = "content_type"
        const val KEY_BASE_TITLE_ID = "base_title_id"
        const val KEY_BASE_URI = "base_uri"
        const val KEY_PROGRAM_ID = "program_id"
        const val KEY_TITLE = "title"
        const val KEY_VERSION = "version"
        const val KEY_ICON = "icon"
        const val KEY_PROGRESS = "progress"
        const val KEY_DETAIL = "detail"
        const val KEY_ERROR = "error"
        const val KEY_PATCH_NAME = "patch_name"
        const val KEY_PATCH_KIND = "patch_kind"
        const val KEY_PATCH_NAMES = "patch_names"
        const val KEY_PATCH_VERSIONS = "patch_versions"
        const val KEY_PATCH_KINDS = "patch_kinds"
        const val KEY_PATCH_ENABLED = "patch_enabled"
        const val KEY_PATCH_REMOVABLE = "patch_removable"
        const val TITLE_TYPE_APPLICATION = 0x80
        const val TITLE_TYPE_UPDATE = 0x81
        const val TITLE_TYPE_DLC = 0x82

        fun baseRequest(uri: Uri): Bundle = Bundle().apply {
            putString(KEY_ACTION, ACTION_INSPECT_BASE)
            putString(KEY_URI, uri.toString())
        }

        fun contentRequest(
            uri: Uri,
            type: Int,
            baseTitleId: String,
            baseUri: String,
            programId: String,
        ): Bundle = Bundle().apply {
            putString(KEY_ACTION, ACTION_INSTALL_CONTENT)
            putString(KEY_URI, uri.toString())
            putInt(KEY_CONTENT_TYPE, type)
            putString(KEY_BASE_TITLE_ID, baseTitleId)
            putString(KEY_BASE_URI, baseUri)
            putString(KEY_PROGRAM_ID, programId)
        }

        fun patchRequest(baseUri: String, programId: String): Bundle = Bundle().apply {
            putString(KEY_ACTION, ACTION_QUERY_PATCHES)
            putString(KEY_BASE_URI, baseUri)
            putString(KEY_PROGRAM_ID, programId)
        }

        fun removeRequest(
            baseUri: String,
            programId: String,
            patch: SwitchPatch,
        ): Bundle = patchRequest(baseUri, programId).apply {
            putString(KEY_ACTION, ACTION_REMOVE_CONTENT)
            putString(KEY_PATCH_NAME, patch.name)
            putString(KEY_PATCH_KIND, patch.kind.name)
        }
    }

    internal data class BaseMetadata(
        val programId: String,
        val title: String,
        val version: String,
        val icon: ByteArray,
    )

    internal object Client {
        suspend fun inspectBase(context: Context, uri: Uri): BaseMetadata {
            val result = withTimeout(BASE_INSPECTION_TIMEOUT_MS) {
                request(context, baseRequest(uri))
            }
            return BaseMetadata(
                programId = result.getString(KEY_PROGRAM_ID).orEmpty(),
                title = result.getString(KEY_TITLE).orEmpty(),
                version = result.getString(KEY_VERSION).orEmpty(),
                icon = result.getByteArray(KEY_ICON) ?: byteArrayOf(),
            )
        }

        suspend fun installContent(
            context: Context,
            uri: Uri,
            type: Int,
            baseTitleId: String,
            baseUri: String,
            programId: String,
            onProgress: (Float, String) -> Unit,
        ): List<SwitchPatch> = withTimeout(CONTENT_INSTALL_TIMEOUT_MS) {
            decodePatches(
                request(
                    context,
                    contentRequest(uri, type, baseTitleId, baseUri, programId),
                    onProgress,
                ),
            )
        }

        suspend fun queryPatches(
            context: Context,
            baseUri: String,
            programId: String,
        ): List<SwitchPatch> = withTimeout(BASE_INSPECTION_TIMEOUT_MS) {
            decodePatches(request(context, patchRequest(baseUri, programId)))
        }

        suspend fun removeContent(
            context: Context,
            baseUri: String,
            programId: String,
            patch: SwitchPatch,
        ): List<SwitchPatch> = withTimeout(BASE_INSPECTION_TIMEOUT_MS) {
            decodePatches(request(context, removeRequest(baseUri, programId, patch)))
        }

        private fun decodePatches(bundle: Bundle): List<SwitchPatch> {
            val names = bundle.getStringArrayList(KEY_PATCH_NAMES).orEmpty()
            val versions = bundle.getStringArrayList(KEY_PATCH_VERSIONS).orEmpty()
            val kinds = bundle.getStringArrayList(KEY_PATCH_KINDS).orEmpty()
            val enabled = bundle.getBooleanArray(KEY_PATCH_ENABLED) ?: booleanArrayOf()
            val removable = bundle.getBooleanArray(KEY_PATCH_REMOVABLE) ?: booleanArrayOf()
            return names.indices.mapNotNull { index ->
                val kind = kinds.getOrNull(index)?.let { runCatching { SwitchContentKind.valueOf(it) }.getOrNull() }
                    ?: return@mapNotNull null
                SwitchPatch(
                    name = names[index],
                    version = versions.getOrElse(index) { "" },
                    kind = kind,
                    enabled = enabled.getOrElse(index) { true },
                    removable = removable.getOrElse(index) { false },
                )
            }
        }

        private suspend fun request(
            context: Context,
            request: Bundle,
            onProgress: (Float, String) -> Unit = { _, _ -> },
        ): Bundle = suspendCancellableCoroutine { continuation ->
            val finished = AtomicBoolean(false)
            var remoteBinder: IBinder? = null
            lateinit var connection: ServiceConnection
            lateinit var deathRecipient: IBinder.DeathRecipient

            fun finish(result: Result<Bundle>) {
                if (!finished.compareAndSet(false, true)) return
                remoteBinder?.let { binder -> runCatching { binder.unlinkToDeath(deathRecipient, 0) } }
                runCatching { context.unbindService(connection) }
                result.fold(
                    onSuccess = { continuation.resume(it) },
                    onFailure = { continuation.resumeWithException(it) },
                )
            }

            val reply = Messenger(
                Handler(Looper.getMainLooper()) { message ->
                    when (message.what) {
                        MSG_PROGRESS -> onProgress(
                            message.data.getFloat(KEY_PROGRESS),
                            message.data.getString(KEY_DETAIL).orEmpty(),
                        )
                        MSG_SUCCESS -> finish(Result.success(message.data))
                        MSG_FAILURE -> finish(
                            Result.failure(
                                IllegalStateException(
                                    message.data.getString(KEY_ERROR) ?: "Eden could not process that NSP.",
                                ),
                            ),
                        )
                    }
                    true
                },
            )
            deathRecipient = IBinder.DeathRecipient {
                Handler(Looper.getMainLooper()).post {
                    finish(
                        Result.failure(
                            IllegalStateException(
                                "Eden's NSP parser stopped unexpectedly. The launcher stayed open; verify the NSP and keys.",
                            ),
                        ),
                    )
                }
            }
            connection = object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName, service: IBinder) {
                    remoteBinder = service
                    runCatching {
                        service.linkToDeath(deathRecipient, 0)
                        Messenger(service).send(Message.obtain(null, MSG_REQUEST).apply {
                            data = request
                            replyTo = reply
                        })
                    }.onFailure { finish(Result.failure(it)) }
                }

                override fun onServiceDisconnected(name: ComponentName) {
                    finish(Result.failure(RemoteException("Eden content worker disconnected.")))
                }

                override fun onBindingDied(name: ComponentName) {
                    finish(Result.failure(RemoteException("Eden content worker stopped.")))
                }

                override fun onNullBinding(name: ComponentName) {
                    finish(Result.failure(RemoteException("Eden content worker was unavailable.")))
                }
            }

            val bound = context.bindService(
                Intent(context, SwitchContentWorkerService::class.java),
                connection,
                Context.BIND_AUTO_CREATE,
            )
            if (!bound) finish(Result.failure(IllegalStateException("Could not start Eden's content worker.")))
            continuation.invokeOnCancellation {
                if (finished.compareAndSet(false, true)) runCatching { context.unbindService(connection) }
            }
        }

        private const val BASE_INSPECTION_TIMEOUT_MS = 120_000L
        private const val CONTENT_INSTALL_TIMEOUT_MS = 4 * 60 * 60 * 1_000L
    }
}
