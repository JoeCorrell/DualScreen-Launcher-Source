package org.yuzu.yuzu_emu.activities

import android.view.KeyEvent
import android.view.MotionEvent
import java.lang.ref.WeakReference

internal interface XcxControllerTarget {
    fun onXcxControllerKey(event: KeyEvent): Boolean
    fun onXcxControllerMotion(event: MotionEvent): Boolean
}

/** Routes controller navigation to the non-focusable XCX bottom-screen Presentation. */
internal object XcxControllerBridge {
    private var target = WeakReference<XcxControllerTarget>(null)

    fun attach(next: XcxControllerTarget) {
        target = WeakReference(next)
    }

    fun detach(previous: XcxControllerTarget) {
        if (target.get() === previous) target.clear()
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean =
        target.get()?.onXcxControllerKey(event) == true

    fun dispatchMotionEvent(event: MotionEvent): Boolean =
        target.get()?.onXcxControllerMotion(event) == true
}
