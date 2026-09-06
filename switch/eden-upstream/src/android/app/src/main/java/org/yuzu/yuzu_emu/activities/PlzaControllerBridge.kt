package org.yuzu.yuzu_emu.activities

import android.view.KeyEvent
import android.view.MotionEvent
import java.lang.ref.WeakReference
import com.dualscreen.companion.api.CompanionControllerTarget

internal typealias PlzaControllerTarget = CompanionControllerTarget

internal object PlzaControllerBridge {
    private var target = WeakReference<PlzaControllerTarget>(null)

    fun attach(next: PlzaControllerTarget) {
        target = WeakReference(next)
    }

    fun detach(previous: PlzaControllerTarget) {
        if (target.get() === previous) target.clear()
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean =
        target.get()?.onPlzaControllerKey(event) == true

    fun dispatchMotionEvent(event: MotionEvent): Boolean =
        target.get()?.onPlzaControllerMotion(event) == true
}
