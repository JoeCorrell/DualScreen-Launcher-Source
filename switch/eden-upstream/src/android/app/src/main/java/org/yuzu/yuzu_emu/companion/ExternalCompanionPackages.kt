package org.yuzu.yuzu_emu.companion

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.AtomicFile
import android.view.View
import android.widget.TextView
import com.dualscreen.companion.api.CompanionHost
import com.dualscreen.companion.api.CompanionHostV2
import com.dualscreen.companion.api.CompanionControllerTarget
import com.dualscreen.companion.api.SignedModuleArchive
import com.dualscreen.companion.api.ModuleCodeCache
import dalvik.system.DexClassLoader
import java.io.File
import java.io.InputStream
import java.nio.ByteBuffer
import java.security.MessageDigest
import java.util.UUID
import org.json.JSONObject
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.activities.PlzaControllerBridge
import org.yuzu.yuzu_emu.activities.BotwControllerBridge
import org.yuzu.yuzu_emu.utils.DirectoryInitialization
import org.yuzu.yuzu_emu.utils.Log

/** Signed external native + DEX + assets, pinned together for a complete gameplay session. */
object ExternalCompanionPackages {
    const val HOST_ABI = 1
    private data class Package(val dir: File, val json: JSONObject) {
        val title get() = json.getString("titleId")
        val version get() = json.getString("version")
        val revision get() = json.getLong("revision")
    }
    private data class Loaded(val pkg: Package, val loader: ClassLoader)
    @Volatile private var loaded: Loaded? = null
    private fun root(context: Context) = File(context.filesDir, "companion-modules")
    private fun title(value: String) = value.uppercase().also { require(it.matches(Regex("[0-9A-F]{16}"))) }
    private fun activeFile(context: Context, id: String) = AtomicFile(File(root(context), "active-${title(id)}"))
    private fun hash(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input -> val buffer = ByteArray(65536); while (true) {
            val n = input.read(buffer); if (n < 0) break; digest.update(buffer, 0, n)
        } }
        return SignedModuleArchive.hex(digest.digest())
    }
    @Suppress("DEPRECATION")
    private fun trusted(context: Context): Set<String> {
        val flags = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
        val info = context.packageManager.getPackageInfo(context.packageName, flags)
        val signatures = if (Build.VERSION.SDK_INT >= 28) info.signingInfo!!.apkContentsSigners else info.signatures
        return signatures!!.map { SignedModuleArchive.hex(MessageDigest.getInstance("SHA-256").digest(it.toByteArray())) }.toSet()
    }
    private fun validate(dir: File, expectedTitle: String, cache: File): Package {
        val manifest = File(dir, "module.json")
        require(manifest.length() in 1..262144) { "Invalid module manifest size" }
        val json = JSONObject(manifest.readText())
        require(json.getInt("schema") == 1 && json.getInt("hostAbi") == HOST_ABI) { "This mod needs a different launcher runtime ABI" }
        json.optJSONArray("hostFeatures")?.let { features ->
            repeat(features.length()) { index ->
                require(features.getString(index) == "query64-rgba-v1") { "This mod requires a newer launcher feature" }
            }
        }
        require(json.getString("titleId") == title(expectedTitle)) { "This package is for another game" }
        require(json.getString("version").matches(Regex("[A-Za-z0-9._-]{1,64}")) && json.getLong("revision") > 0)
        require(json.getString("entryClass").matches(Regex("com\\.dualscreen\\.mods\\.[A-Za-z0-9_.]+")))
        require(json.getString("architecture") == "arm64-v8a")
        require(json.getJSONArray("buildIds").length() > 0)
        val files = json.getJSONObject("files")
        require(files.length() in 3..4999)
        fun inventory() = dir.walkTopDown().filter { it.isFile && it.relativeTo(dir).invariantSeparatorsPath != "module.json" }
            .map { it.relativeTo(dir).invariantSeparatorsPath }.toSet()
        val expected = files.keys().asSequence().toSet()
        val extras = inventory() - expected
        if (extras.isNotEmpty()) {
            Log.warning("[CompanionPackage] Unexpected package files: ${extras.sorted().joinToString()}")
            ModuleCodeCache.relocateLegacyArt(dir, cache, extras)
        }
        val actual = inventory()
        require(actual == expected) { "Module file list mismatch: missing ${(expected - actual).joinToString()}" }
        for (name in actual) {
            SignedModuleArchive.safePath(name)
            require(hash(File(dir, name)) == files.getString(name)) { "Module checksum failed: $name" }
        }
        return Package(dir, json)
    }
    private fun installed(context: Context, id: String): Package? {
        val state = activeFile(context, id)
        if (!state.baseFile.exists() && !File(state.baseFile.path + ".bak").exists()) return null
        val key = state.openRead().bufferedReader().use { it.readText() }
        require(key.matches(Regex("[0-9a-f]{64}"))) { "Invalid active module record" }
        return validate(File(root(context), "versions/$key"), id, context.codeCacheDir)
    }
    @Synchronized fun installedVersion(context: Context, id: String): String? = installed(context, id)?.version

    /** Import needs no APK install. A failed verification leaves the active version untouched. */
    @Synchronized fun install(context: Context, id: String, input: InputStream, retainNewer: Boolean = false): String {
        require(!NativeLibrary.isRunning()) { "Close the game before updating its dual-screen mod" }
        val store = root(context); require(store.isDirectory || store.mkdirs())
        val stage = File(store, "staging-${UUID.randomUUID()}"); require(stage.mkdir())
        val archive = File(stage, "package.dsmod")
        val content = File(stage, "content"); require(content.mkdir())
        try {
            archive.outputStream().use { out -> val buffer = ByteArray(65536); var bytes = 0L
                while (true) { val n = input.read(buffer); if (n < 0) break; bytes += n
                    require(bytes <= SignedModuleArchive.MAX_BYTES) { "Module package too large" }; out.write(buffer, 0, n)
                }
            }
            val key = hash(archive)
            SignedModuleArchive.extract(archive, content, trusted(context))
            val candidate = validate(content, id, context.codeCacheDir)
            val previous = installed(context, id)
            if (retainNewer && previous != null && candidate.revision < previous.revision) return previous.version
            require(previous == null || candidate.revision >= previous.revision) { "Refusing an older mod revision" }
            if (previous != null && candidate.revision == previous.revision) {
                require(previous.dir.name == key) { "Different package reused an existing revision" }
                return previous.version
            }
            val versions = File(store, "versions"); require(versions.isDirectory || versions.mkdir())
            val dest = File(versions, key)
            if (!dest.exists()) require(content.renameTo(dest)) { "Cannot publish verified package" }
            else validate(dest, id, context.codeCacheDir)
            // Check native ABI/title/version and the DEX constructor before replacing a working version.
            val ready = prepare(context, Package(dest, candidate.json))
            // Retain previous version for recovery. AtomicFile restores .bak after interrupted writes.
            val state = activeFile(context, id); val out = state.startWrite()
            try { out.write(key.toByteArray()); state.finishWrite(out) } catch (e: Throwable) { state.failWrite(out); throw e }
            loaded = ready
            Log.info("[CompanionPackage] Installed ${candidate.title} ${candidate.version} revision=${candidate.revision} hash=$key")
            return candidate.version
        } finally { stage.deleteRecursively() }
    }

    @Synchronized fun activate(context: Context, id: String) {
        val pkg = installed(context, id) ?: error("Automatic dual-screen mod setup has not finished. Return to the launcher and retry payload setup.")
        if (loaded?.pkg?.dir == pkg.dir) return
        require(!NativeLibrary.isRunning()) { "A mod update takes effect after the game is closed" }
        loaded = prepare(context, pkg)
        Log.info("[CompanionPackage] Activated ${pkg.title} ${pkg.version}; UI/native/assets=${pkg.dir.name}")
    }
    private fun prepare(context: Context, pkg: Package): Loaded {
        // optimizedDirectory is ignored on API 26+: ART otherwise writes oat/ beside
        // ui.jar and breaks the immutable package's strict file-list validation.
        val dex = ModuleCodeCache.verifiedDex(File(pkg.dir, "ui.jar"), context.codeCacheDir,
            pkg.json.getJSONObject("files").getString("ui.jar"))
        val classLoader = DexClassLoader(dex.path, context.codeCacheDir.path, null, context.classLoader)
        val entry = classLoader.loadClass(pkg.json.getString("entryClass"))
        require(View::class.java.isAssignableFrom(entry)) { "Invalid module UI entry" }
        entry.getConstructor(Context::class.java, CompanionHost::class.java)
        require(NativeLibrary.loadExternalCompanion(File(pkg.dir, "lib/arm64-v8a/libmodule.so").path, pkg.title.toLong(16), pkg.version)) { "Native module ABI/load check failed" }
        return Loaded(pkg, classLoader)
    }
    @Synchronized fun createView(context: Context, id: String): View {
        val current = loaded?.takeIf { it.pkg.title == title(id) }
        if (current == null) return TextView(context).apply { text = "Dual-screen mod is not loaded. Return to the launcher and install its signed package."; textSize = 24f }
        val host = object : CompanionHostV2 {
            override fun snapshot(output: ByteBuffer) = if (loaded?.pkg?.dir == current.pkg.dir) NativeLibrary.fillPlzaCompanionSnapshot(output) else 0
            override fun action(operation: Int, item: Int, target: Int) = if (loaded?.pkg?.dir == current.pkg.dir) NativeLibrary.performPlzaCompanionItemAction(operation, item, target) else 15
            override fun romfsAsset(path: String) = if (loaded?.pkg?.dir == current.pkg.dir) NativeLibrary.externalCompanionAsset(path) else null
            override fun openAsset(path: String): InputStream = File(current.pkg.dir, "assets/${SignedModuleArchive.safePath(path)}").inputStream()
            override fun log(level: Int, message: String) { when (level) { 2 -> Log.error(message); 1 -> Log.warning(message); else -> Log.info(message) } }
            override fun query(kind: Int, input: String) = if (loaded?.pkg?.dir == current.pkg.dir) NativeLibrary.externalCompanionQuery(kind, input) else "{}"
            override fun action64(operation: Int, argument: Long) = loaded?.pkg?.dir == current.pkg.dir && NativeLibrary.externalCompanionAction64(operation, argument)
            override fun romfsRevision() = if (loaded?.pkg?.dir == current.pkg.dir) NativeLibrary.getDualScreenRomfsRevision() else ""
            override fun userDataDirectory() = DirectoryInitialization.userDirectory
            override fun attach(target: CompanionControllerTarget) {
                if (current.pkg.title == "01007EF00011E000") BotwControllerBridge.attach(target)
                else PlzaControllerBridge.attach(target)
            }
            override fun detach(target: CompanionControllerTarget) {
                if (current.pkg.title == "01007EF00011E000") BotwControllerBridge.detach(target)
                else PlzaControllerBridge.detach(target)
            }
        }
        return current.loader.loadClass(current.pkg.json.getString("entryClass"))
            .getConstructor(Context::class.java, CompanionHost::class.java).newInstance(context, host) as View
    }
}
