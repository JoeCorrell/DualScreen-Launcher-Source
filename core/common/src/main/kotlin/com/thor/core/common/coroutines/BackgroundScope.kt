package com.thor.core.common.coroutines

import com.thor.core.common.log.ThorLog
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/**
 * A process-lifetime scope whose failures are logged rather than fatal.
 *
 * <h2>The mistake this exists to stop repeating</h2>
 *
 * `CoroutineScope(SupervisorJob() + Dispatchers.IO)` reads as the safe
 * construction and is not. A supervisor governs one thing: whether a failing
 * child cancels its siblings. It says nothing about what happens to the failure
 * itself, and the answer is that an uncaught exception in a root `launch` looks
 * for a [CoroutineExceptionHandler] in its context, finds none, and hands
 * itself to the thread's default handler — which on Android ends the process.
 *
 * So every long-running background job built that way was a route to killing
 * the app, and downloads are where it shows: a download is the operation with
 * the most ways to fail and the longest time in which to do it. The network
 * drops, the disk fills, a manifest is truncated, a store returns something
 * unexpected. Each of those should be a message on a row, and each of them was
 * taking the whole app down instead.
 *
 * <h2>What this does not do</h2>
 *
 * It does not make the work correct — a job that fails still fails, and its
 * caller still has to notice. It removes exactly one failure mode: the app
 * dying because nobody was listening.
 *
 * Cancellation is not reported, because it is not a failure. Pausing a download
 * cancels its coroutine by design.
 *
 * <h2>Why it lives in `core:common`</h2>
 *
 * It was in the app, next to the downloads that prompted it. It moved when the
 * second screen became a library that a standalone app also builds: that library
 * needs the same guarantee, and the alternative to sharing it was a second copy
 * of a safety net, which is the kind of duplication that ends with one of the two
 * quietly losing the handler.
 */
fun backgroundScope(
    tag: String,
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
    onFailure: (Throwable) -> Unit = {},
): CoroutineScope = CoroutineScope(
    SupervisorJob() +
        dispatcher +
        CoroutineExceptionHandler { _, failure ->
            if (failure is CancellationException) return@CoroutineExceptionHandler
            ThorLog.e(tag, "Background work failed", failure)
            runCatching { onFailure(failure) }
        },
)
