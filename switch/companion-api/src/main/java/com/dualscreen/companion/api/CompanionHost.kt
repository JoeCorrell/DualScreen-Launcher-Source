package com.dualscreen.companion.api

import android.view.KeyEvent
import android.view.MotionEvent
import java.io.InputStream
import java.nio.ByteBuffer

/** ABI 1. Keep names and signatures stable; snapshot/action protocols belong to each package. */
interface CompanionHost {
    fun snapshot(output: ByteBuffer): Int
    fun action(operation: Int, item: Int, target: Int): Int
    fun romfsAsset(path: String): ByteArray?
    fun openAsset(path: String): InputStream
    fun log(level: Int, message: String)
    fun attach(target: CompanionControllerTarget)
    fun detach(target: CompanionControllerTarget)
}
interface CompanionControllerTarget {
    fun onPlzaControllerKey(event: KeyEvent): Boolean
    fun onPlzaControllerMotion(event: MotionEvent): Boolean
}

/** Optional extension; ABI-1 packages continue using CompanionHost unchanged. */
interface CompanionHostV2 : CompanionHost {
    fun query(kind: Int, input: String): String
    fun action64(operation: Int, argument: Long): Boolean
    fun romfsRevision(): String
    fun userDataDirectory(): String?
}
