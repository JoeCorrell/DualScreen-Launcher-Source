package org.yuzu.yuzu_emu.activities

import android.view.KeyEvent
import android.view.MotionEvent
import java.lang.ref.WeakReference

internal interface BotwControllerTarget {
    fun onBotwControllerKey(event: KeyEvent): Boolean
    fun onBotwControllerMotion(event: MotionEvent): Boolean
}

internal object BotwControllerBridge {
    private var target = WeakReference<BotwControllerTarget>(null)

    fun attach(next: BotwControllerTarget) {
        target = WeakReference(next)
    }

    fun detach(previous: BotwControllerTarget) {
        if (target.get() === previous) target.clear()
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean =
        target.get()?.onBotwControllerKey(event) == true

    fun dispatchMotionEvent(event: MotionEvent): Boolean =
        target.get()?.onBotwControllerMotion(event) == true
}
