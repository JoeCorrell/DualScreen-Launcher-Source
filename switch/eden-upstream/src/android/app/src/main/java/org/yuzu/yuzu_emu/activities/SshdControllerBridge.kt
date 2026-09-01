package org.yuzu.yuzu_emu.activities

import android.view.KeyEvent
import android.view.MotionEvent
import java.lang.ref.WeakReference

internal interface SshdControllerTarget {
    fun onSshdControllerKey(event: KeyEvent): Boolean
    fun onSshdControllerMotion(event: MotionEvent): Boolean
}

internal object SshdControllerBridge {
    private var target = WeakReference<SshdControllerTarget>(null)

    fun attach(next: SshdControllerTarget) {
        target = WeakReference(next)
    }

    fun detach(previous: SshdControllerTarget) {
        if (target.get() === previous) target.clear()
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean =
        target.get()?.onSshdControllerKey(event) == true

    fun dispatchMotionEvent(event: MotionEvent): Boolean =
        target.get()?.onSshdControllerMotion(event) == true
}
