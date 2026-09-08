package org.yuzu.yuzu_emu.activities

import android.view.KeyEvent
import android.view.MotionEvent
import java.lang.ref.WeakReference

internal typealias BotwControllerTarget = com.dualscreen.companion.api.CompanionControllerTarget

internal object BotwControllerBridge {
    private var target = WeakReference<BotwControllerTarget>(null)

    fun attach(next: BotwControllerTarget) {
        target = WeakReference(next)
    }

    fun detach(previous: BotwControllerTarget) {
        if (target.get() === previous) target.clear()
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean =
        target.get()?.onPlzaControllerKey(event) == true

    fun dispatchMotionEvent(event: MotionEvent): Boolean =
        target.get()?.onPlzaControllerMotion(event) == true
}
