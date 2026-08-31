package com.dualscreen.launcher

internal enum class GameInstallState {
    NOT_INSTALLED,
    INSTALLING,
    IMPORTING_SAVE,
    READY,
    ERROR,
}

internal fun resolveGameInstallState(
    installed: InstalledGame?,
    progress: InstallProgress?,
    saveProgress: SaveImportProgress?,
): GameInstallState = when {
    progress?.running == true -> GameInstallState.INSTALLING
    saveProgress?.running == true -> GameInstallState.IMPORTING_SAVE
    progress?.error != null || saveProgress?.error != null -> GameInstallState.ERROR
    installed != null -> GameInstallState.READY
    else -> GameInstallState.NOT_INSTALLED
}

internal fun gameArtwork(gameId: String): Int = when (gameId) {
    "skyrim-se" -> R.drawable.art_skyrim
    "fallout3" -> R.drawable.art_fallout3
    "fallout4" -> R.drawable.art_fallout4
    "grim-dawn" -> R.drawable.art_grim_dawn
    "deus-ex-hrdc" -> R.drawable.art_deus_ex
    "dead-island-de" -> R.drawable.art_dead_island
    "valheim" -> R.drawable.art_valheim
    "stardew-valley" -> R.drawable.art_stardew
    else -> R.drawable.ic_launcher
}

internal fun gameShortTitle(gameId: String): String = when (gameId) {
    "skyrim-se" -> "Skyrim Special Edition"
    "fallout3" -> "Fallout 3"
    "fallout4" -> "Fallout 4"
    "grim-dawn" -> "Grim Dawn"
    "deus-ex-hrdc" -> "Deus Ex: Human Revolution"
    "dead-island-de" -> "Dead Island Definitive"
    "valheim" -> "Valheim"
    "stardew-valley" -> "Stardew Valley"
    else -> gameId
}

internal fun formatBytes(bytes: Long): String {
    if (bytes <= 0) return "0 B"
    val units = listOf("B", "KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var index = 0
    while (value >= 1024 && index < units.lastIndex) {
        value /= 1024
        index++
    }
    return if (index == 0) "${value.toLong()} ${units[index]}"
    else "%.1f %s".format(value, units[index])
}
