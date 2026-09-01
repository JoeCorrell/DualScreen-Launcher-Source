package com.dualscreen.launcher.switch

import androidx.annotation.Keep

/**
 * JNI adapter for Eden's content installer.
 *
 * Eden's native bridge invokes Kotlin's erased Function2 method with boxed Double values even
 * though NativeLibrary's generated Kotlin declaration describes the values as Long. A normal
 * Kotlin lambda therefore inserts Long casts and throws before it can return a Boolean, after
 * which JNI aborts while reading the null return value. Keeping this adapter erased at Number
 * makes the bridge compatible with both Eden's current Double values and older Long values.
 */
@Keep
internal class EdenInstallProgressCallback(
    private val onProgress: (maximum: Long, progress: Long) -> Unit,
) : kotlin.Function2<Any?, Any?, Boolean> {
    override fun invoke(maximum: Any?, progress: Any?): Boolean {
        onProgress((maximum as? Number)?.toLong() ?: 0L, (progress as? Number)?.toLong() ?: 0L)
        return false
    }
}
