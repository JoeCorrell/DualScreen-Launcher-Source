package com.dualscreen.launcher.switch

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.util.UUID
import org.yuzu.yuzu_emu.companion.ExternalCompanionPackages

internal class SwitchModImporter(private val context: Context) {
    private fun root(uri: Uri) = requireNotNull(DocumentFile.fromTreeUri(context, uri)) {
        "The selected folder is unavailable. Select it again to grant access."
    }.also { require(it.isDirectory && it.canRead()) { "Cannot read the selected mod folder." } }

    fun discover(uri: Uri, titleId: String): List<SwitchModFolder> = (SwitchModFolders.discover(
        root(uri), titleId, { it.name ?: error("A mod folder has no name.") },
        { directory ->
            require(directory.canRead()) { "Cannot read ${directory.name}." }
            directory.listFiles().filter { it.isDirectory }
        },
    ) + discoverPackages(root(uri))).also { require(it.isNotEmpty()) { "No mods found. Select a folder containing a signed .dsmod package or extracted exefs/romfs mods." } }

    private fun discoverPackages(root: DocumentFile): List<SwitchModFolder> {
        val result = mutableListOf<SwitchModFolder>()
        var visited = 0
        fun scan(dir: DocumentFile, path: List<String>, depth: Int) {
            if (depth > 4) return
            require(++visited <= 1000) { "Too many mod folders" }
            dir.listFiles().forEach { child ->
                val name = child.name ?: return@forEach
                val parts = path + SwitchModFolders.safeName(name)
                if (child.isFile && name.endsWith(".dsmod", true)) result += SwitchModFolder(name, parts)
                else if (child.isDirectory && name.lowercase() !in SwitchModFolders.payloadRoots) scan(child, parts, depth + 1)
            }
        }
        scan(root, emptyList(), 0)
        return result
    }

    fun install(uri: Uri, titleId: String, selected: List<SwitchModFolder>, loadRoot: File, progress: (Float, String) -> Unit) {
        require(selected.isNotEmpty()) { "Select at least one mod." }
        val available = discover(uri, titleId)
        require(selected.all { it in available }) { "The selected mod folders changed. Choose them again." }
        val packages = selected.filter { it.path.lastOrNull()?.endsWith(".dsmod", true) == true }
        if (packages.isNotEmpty()) {
            require(selected.size == 1) { "Install one signed dual-screen package at a time, separately from ordinary mods" }
            var source = root(uri)
            packages.single().path.forEach { part -> source = requireNotNull(source.findFile(SwitchModFolders.safeName(part))) }
            val version = requireNotNull(context.contentResolver.openInputStream(source.uri)).use {
                ExternalCompanionPackages.install(context, titleId, it)
            }
            progress(1f, "Dual-screen mod $version installed; activates on next game launch")
            return
        }
        require(selected.map { it.name.lowercase() }.distinct().size == selected.size) {
            "Selected mods have duplicate names. Install one version at a time."
        }
        val titleRoot = SwitchModFolders.child(loadRoot, titleId.uppercase())
        require(titleRoot.isDirectory || titleRoot.mkdirs()) { "Cannot write to Eden's configured mod directory: $titleRoot" }
        val targets = selected.map { SwitchModFolders.child(titleRoot, it.name) }
        targets.forEach { require(!it.exists()) { "A mod named ${it.name} is already installed. Remove it first if you want to replace it." } }
        // Nested staging contains no immediate payload roots, so Eden cannot load it.
        val staging = SwitchModFolders.child(titleRoot, ".dualscreen-import-${UUID.randomUUID()}")
        require(staging.mkdir()) { "Cannot create mod staging directory." }
        var files = 0
        var bytes = 0L
        fun copy(source: DocumentFile, target: File, depth: Int) {
            require(depth <= 64) { "Mod directory nesting is too deep." }
            require(source.canRead()) { "Cannot read ${source.name}." }
            if (source.isDirectory) {
                require(target.mkdir()) { "Cannot create ${target.name}." }
                source.listFiles().forEach { child ->
                    copy(child, SwitchModFolders.child(target, child.name ?: error("Unnamed mod file")), depth + 1)
                }
            } else {
                require(source.isFile && ++files <= 100_000) { "Invalid mod entry or too many files." }
                require(!target.exists()) { "Duplicate mod filename: ${target.name}" }
                context.contentResolver.openInputStream(source.uri).use { input ->
                    requireNotNull(input) { "Cannot open ${source.name}." }
                    target.outputStream().buffered().use { output ->
                        val buffer = ByteArray(64 * 1024)
                        var copied = 0L
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            bytes += count
                            copied += count
                            require(bytes <= 16L * 1024 * 1024 * 1024) { "Selected mods exceed the 16 GiB import limit." }
                            output.write(buffer, 0, count)
                        }
                        val expected = source.length()
                        require(expected == 0L || expected == copied) { "Incomplete copy of ${source.name}." }
                    }
                }
            }
        }
        try {
            selected.forEachIndexed { index, mod ->
                var source = root(uri)
                // Resolve inside the original granted tree. fromTreeUri(childUri)
                // would silently select the tree root again instead of the child.
                mod.path.forEach { part ->
                    source = requireNotNull(source.findFile(SwitchModFolders.safeName(part))) { "Mod folder disappeared." }
                }
                val target = SwitchModFolders.child(staging, mod.name)
                require(target.mkdir()) { "Cannot stage ${mod.name}." }
                val before = files
                source.listFiles().filter { it.isDirectory && it.name?.lowercase() in SwitchModFolders.payloadRoots }
                    .forEach { payload ->
                        copy(payload, SwitchModFolders.child(target, payload.name!!.lowercase()), 0)
                    }
                require(files > before) { "${mod.name} contains no mod files." }
                progress((index + 1f) / selected.size * 0.9f, "Copied ${mod.name}")
            }
            SwitchModFolders.publish(staging, titleRoot, selected.map { it.name })
        } finally {
            staging.deleteRecursively() // Validated, uniquely created staging only.
        }
    }
}
