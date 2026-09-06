package com.dualscreen.launcher.switch

import java.io.File

data class SwitchModFolder(val name: String, val path: List<String>) {
    val id: String get() = path.joinToString("/")
    val location: String get() = id.ifEmpty { "." }
}

/** Discovery is independent of Android so wrapped and multi-mod trees can be tested. */
internal object SwitchModFolders {
    val payloadRoots = setOf("exefs", "romfs", "romfslite", "romfs_ext", "cheats")
    private val wrappers = setOf("atmosphere", "contents", "titles", "load", "mods")

    fun safeName(name: String): String {
        require(name.isNotBlank() && name != "." && name != ".." &&
            name.none { it == '/' || it == '\\' || it == '\u0000' }) { "Unsafe mod filename: $name" }
        return name
    }

    fun child(root: File, name: String): File {
        val target = File(root, safeName(name)).canonicalFile
        require(target.parentFile == root.canonicalFile) { "Mod path escapes its destination." }
        return target
    }

    fun publish(staging: File, titleRoot: File, names: List<String>) {
        require(staging.canonicalFile.parentFile == titleRoot.canonicalFile &&
            staging.name.startsWith(".dualscreen-import-")) { "Invalid mod staging directory." }
        require(names.distinct().size == names.size) { "Duplicate mod selection." }
        val targets = names.map { child(titleRoot, it) }
        targets.forEach { require(!it.exists()) { "A mod named ${it.name} is already installed." } }
        val committed = mutableListOf<File>()
        try {
            names.forEachIndexed { index, name ->
                val target = targets[index]
                require(!target.exists() && child(staging, name).renameTo(target)) {
                    "Cannot finish installing $name."
                }
                committed += target
            }
        } catch (failure: Throwable) {
            committed.forEach { it.deleteRecursively() } // Only targets created by this transaction.
            throw failure
        }
    }

    fun <T> discover(
        root: T, titleId: String, name: (T) -> String, directories: (T) -> List<T>,
    ): List<SwitchModFolder> {
        var visited = 0
        val result = mutableListOf<SwitchModFolder>()
        fun visit(node: T, path: List<String>) {
            require(++visited <= 512 && path.size <= 8) { "Folder is too broad. Choose a smaller mod folder." }
            val nodeName = safeName(name(node))
            // Do not offer another game's payload from a multi-title pack.
            if (nodeName.matches(Regex("[0-9a-fA-F]{16}")) && !nodeName.equals(titleId, true)) return
            val children = directories(node)
            if (children.any { name(it).lowercase() in payloadRoots }) {
                val meaningfulName = (listOf(name(root)) + path).lastOrNull {
                    it.lowercase() !in wrappers && !it.matches(Regex("[0-9a-fA-F]{16}"))
                } ?: "Imported Mod"
                result += SwitchModFolder(safeName(meaningfulName), path)
                return // Never scan the potentially huge payload contents.
            }
            children.sortedBy { name(it) }.forEach { visit(it, path + safeName(name(it))) }
        }
        visit(root, emptyList())
        return result
    }
}
