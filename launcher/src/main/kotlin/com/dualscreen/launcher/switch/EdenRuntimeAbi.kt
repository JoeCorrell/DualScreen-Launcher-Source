package com.dualscreen.launcher.switch

/**
 * Verifies the Java ABI that libyuzu-android resolves by literal class name in JNI_OnLoad.
 *
 * Android aborts the entire process when JNI calls GetMethodID with a null class. Checking the
 * contract before loading NativeLibrary converts a packaging/R8 regression into a normal,
 * user-visible setup failure and keeps the PC launcher usable.
 */
internal object EdenRuntimeAbi {
    private val requiredClasses = listOf(
        "org.yuzu.yuzu_emu.NativeLibrary",
        "org.yuzu.yuzu_emu.disk_shader_cache.DiskShaderCacheProgress",
        "org.yuzu.yuzu_emu.disk_shader_cache.DiskShaderCacheProgress\$LoadCallbackStage",
        "org.yuzu.yuzu_emu.model.GameDir",
        "org.yuzu.yuzu_emu.model.Game",
        "kotlin.Pair",
        "org.yuzu.yuzu_emu.overlay.model.OverlayControlData",
        "org.yuzu.yuzu_emu.model.Patch",
        "org.yuzu.yuzu_emu.features.input.model.PlayerInput",
        "org.yuzu.yuzu_emu.features.input.YuzuInputDevice",
        "org.yuzu.yuzu_emu.applets.keyboard.SoftwareKeyboard",
        "org.yuzu.yuzu_emu.applets.keyboard.SoftwareKeyboard\$KeyboardConfig",
        "org.yuzu.yuzu_emu.applets.keyboard.SoftwareKeyboard\$KeyboardData",
    )

    @Volatile
    private var verified = false

    fun verify(classLoader: ClassLoader) {
        if (verified) return
        synchronized(this) {
            if (verified) return
            val missing = requiredClasses.filter { name ->
                runCatching { Class.forName(name, false, classLoader) }.isFailure
            }
            check(missing.isEmpty()) {
                "The embedded Eden runtime is incomplete (${missing.joinToString()}). " +
                    "Install a complete launcher update before using Switch games."
            }

            val pair = Class.forName("kotlin.Pair", false, classLoader)
            check(pair.declaredConstructors.any { constructor ->
                constructor.parameterTypes.contentEquals(arrayOf(Any::class.java, Any::class.java))
            }) { "The embedded Eden runtime has an incompatible kotlin.Pair ABI." }
            verified = true
        }
    }
}
