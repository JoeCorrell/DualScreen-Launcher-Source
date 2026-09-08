package com.dualscreen.launcher

/** The user-verified graphics versions belong to Dishonored, not its shared WFC prefix. */
internal fun supportedGameDirectXConfig(gameId: String, current: String): String? {
    if (gameId != "dishonored") return null
    val options = linkedMapOf<String, String>()
    current.split(',').filter { it.isNotBlank() }.forEach { token ->
        val pair = token.split('=', limit = 2)
        require(pair.size == 2) { "Invalid DirectX configuration" }
        options[pair[0]] = pair[1]
    }
    options["version"] = "DXVK-2.6-2-gplasync-0"
    options["vkd3dVersion"] = "VKD3D-2.9-0"
    return options.entries.joinToString(",") { "${it.key}=${it.value}" }
}
