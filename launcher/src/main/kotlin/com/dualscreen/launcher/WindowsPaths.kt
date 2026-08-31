package com.dualscreen.launcher

import java.io.File

/** Resolves a catalog path with Windows case semantics on Android's case-sensitive filesystem. */
internal fun resolveWindowsFile(root: File, relativePath: String): File? {
    val segments = relativePath
        .replace('\\', '/')
        .split('/')
        .filter(String::isNotBlank)
    if (segments.isEmpty() || segments.any { it == "." || it == ".." }) return null

    var current = root
    for (segment in segments) {
        if (!current.isDirectory) return null
        val matches = current.listFiles()
            ?.filter { child -> child.name.equals(segment, ignoreCase = true) }
            .orEmpty()
        // Two names that differ only by case are ambiguous on Windows and must
        // not be selected arbitrarily from an untrusted ZIP.
        if (matches.size != 1) return null
        current = matches.single()
    }
    return current
}

internal fun resolveLaunchExecutable(root: File, game: SupportedGame): String? =
    game.executableCandidates.firstNotNullOfOrNull { candidate ->
        resolveWindowsFile(root, candidate)
            ?.takeIf(File::isFile)
            ?.relativeTo(root)
            ?.invariantSeparatorsPath
    }

internal fun resolveConfigurationLauncherExecutable(root: File, game: SupportedGame): String? =
    game.configurationLauncherCandidates.firstNotNullOfOrNull { candidate ->
        resolveWindowsFile(root, candidate)
            ?.takeIf(File::isFile)
            ?.relativeTo(root)
            ?.invariantSeparatorsPath
    }
