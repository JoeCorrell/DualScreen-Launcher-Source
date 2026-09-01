using UnityEngine;
using UnityEngine.Scripting;

namespace ValheimPerformance
{
    /// <summary>
    /// How far the CPU is allowed to run ahead of the GPU, and how the frames in
    /// between are paid for.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The game explicitly sets <c>QualitySettings.maxQueuedFrames = 2</c> in
    /// <c>GraphicsSettingsManager.Initialize</c>. That is a sensible low-latency
    /// desktop default. On a translated D3D-to-Vulkan stack, a third frame can
    /// keep submission and rendering overlapped when two is too shallow.
    /// </para>
    /// <para>
    /// Here a draw call goes through DXVK, then Wine's Vulkan loader, then
    /// Turnip, then SurfaceFlinger, and the round trip from submit to retire is
    /// far longer than the game was written to expect. The render thread can
    /// hit a shallow queue limit and block waiting for a frame to retire. This
    /// is workload- and driver-dependent, so the value remains configurable
    /// rather than being presented as a universal throughput increase.
    /// </para>
    /// <para>
    /// Raising the queue costs latency, and honestly: one extra queued frame is
    /// roughly one frame of extra input delay. On a handheld running a
    /// third-person survival game that is a trade worth making, but it is a
    /// trade, so it is a setting and it says so.
    /// </para>
    /// <para>
    /// The upload budget is the second half. A larger persistent ring can avoid
    /// resize and synchronization work as zone assets arrive. The time slice is
    /// kept at Unity's two-millisecond default unless deliberately changed: a
    /// larger slice can finish loading sooner, but consumes more of each frame.
    /// Nothing here changes a pixel.
    /// </para>
    /// </remarks>
    internal static class Pipeline
    {
        private static bool _loggedQueue;
        private static bool _captured;
        private static int _originalQueue;
        private static int _originalUploadSlice;
        private static int _originalUploadBuffer;
        private static bool _originalPersistentBuffer;
        private static ulong _originalGcSlice;
        private static bool _queueApplied;
        private static bool _uploadApplied;
        private static bool _gcApplied;
        private static int _appliedQueueValue;
        private static int _appliedUploadSlice;
        private static int _appliedUploadBuffer;
        private static bool _appliedPersistentBuffer;
        private static ulong _appliedGcSlice;
        private static bool _capturedInitializedQueue;

        public static void Apply()
        {
            Capture();
            ApplyQueueDepth();
            ApplyUploadBudget();
            ApplyIncrementalGc();
        }

        /// <summary>
        /// Called after Valheim initializes graphics and writes its queue depth.
        /// This prevents an opt-in override applied during early plugin startup
        /// from treating Unity's pre-initialization value as the restore target.
        /// </summary>
        public static void CaptureGameQueueDepth()
        {
            Capture();
            int current = QualitySettings.maxQueuedFrames;
            if (!_capturedInitializedQueue || !_queueApplied
                || current != _appliedQueueValue)
            {
                _originalQueue = current;
                _capturedInitializedQueue = true;
            }
        }

        private static void Capture()
        {
            if (_captured) return;
            _originalQueue = QualitySettings.maxQueuedFrames;
            _originalUploadSlice = QualitySettings.asyncUploadTimeSlice;
            _originalUploadBuffer = QualitySettings.asyncUploadBufferSize;
            _originalPersistentBuffer = QualitySettings.asyncUploadPersistentBuffer;
            _originalGcSlice = GarbageCollector.incrementalTimeSliceNanoseconds;
            _captured = true;
        }

        /// <summary>
        /// Applies an optional queue-depth override.
        /// </summary>
        /// <remarks>
        /// Valheim writes <c>maxQueuedFrames = 2</c> during graphics
        /// initialization. The plugin reapplies an opt-in override from the
        /// game's graphics-settings event, not from a polling loop.
        /// </remarks>
        private static void ApplyQueueDepth()
        {
            int wanted = Plugin.QueuedFrames.Value;
            if (wanted <= 0)
            {
                if (_queueApplied
                    && QualitySettings.maxQueuedFrames == _appliedQueueValue)
                    QualitySettings.maxQueuedFrames = _originalQueue;
                else
                    _originalQueue = QualitySettings.maxQueuedFrames;
                _queueApplied = false;
                return;
            }

            wanted = Mathf.Clamp(wanted, 1, 4);
            if (!_queueApplied)
                _originalQueue = QualitySettings.maxQueuedFrames;
            _queueApplied = true;
            _appliedQueueValue = wanted;
            if (QualitySettings.maxQueuedFrames == wanted) return;

            QualitySettings.maxQueuedFrames = wanted;
            if (_loggedQueue) return;

            _loggedQueue = true;
            Plugin.Log.LogInfo("[Perf] Render queue depth " + wanted
                               + " (the game's low-latency default is 2).");
        }

        /// <summary>
        /// Gives streamed texture and mesh uploads a larger persistent ring.
        /// </summary>
        /// <remarks>
        /// The buffer is a persistent ring, so the size is memory actually held
        /// for the process lifetime. Thirty-two megabytes is small next to what
        /// the game already reserves for its own atlases and large enough that
        /// a zone's worth of textures does not have to arrive in instalments.
        /// </remarks>
        private static void ApplyUploadBudget()
        {
            if (!Plugin.WidenUploadBudget.Value)
            {
                if (_uploadApplied)
                {
                    RestoreUploadBudgetIfOwned();
                }
                else
                {
                    _originalUploadSlice = QualitySettings.asyncUploadTimeSlice;
                    _originalUploadBuffer = QualitySettings.asyncUploadBufferSize;
                    _originalPersistentBuffer =
                        QualitySettings.asyncUploadPersistentBuffer;
                }
                _uploadApplied = false;
                return;
            }

            int slice = Mathf.Clamp(Plugin.UploadTimeSlice.Value, 1, 33);
            int buffer = Mathf.Clamp(Plugin.UploadBufferSize.Value, 2, 512);
            if (!_uploadApplied)
            {
                _originalUploadSlice = QualitySettings.asyncUploadTimeSlice;
                _originalUploadBuffer = QualitySettings.asyncUploadBufferSize;
                _originalPersistentBuffer = QualitySettings.asyncUploadPersistentBuffer;
            }
            _uploadApplied = true;
            _appliedUploadSlice = slice;
            _appliedUploadBuffer = buffer;
            _appliedPersistentBuffer = true;

            if (QualitySettings.asyncUploadTimeSlice != slice)
                QualitySettings.asyncUploadTimeSlice = slice;

            if (QualitySettings.asyncUploadBufferSize != buffer)
                QualitySettings.asyncUploadBufferSize = buffer;

            if (!QualitySettings.asyncUploadPersistentBuffer)
                QualitySettings.asyncUploadPersistentBuffer = true;
        }

        /// <summary>
        /// Spreads collection across frames instead of into one of them.
        /// </summary>
        /// <remarks>
        /// <para>
        /// Only does anything when the runtime was built with incremental
        /// collection, which is why it asks first rather than assuming. Where it
        /// is available, the slice is the ceiling on how long a single frame may
        /// spend collecting; a smaller slice means more frames doing a little
        /// rather than one frame doing all of it.
        /// </para>
        /// <para>
        /// This does not reduce total collection work and is not meant to. It
        /// converts a periodic dropped frame into a slightly lower steady rate,
        /// which is the trade that makes a handheld feel smooth.
        /// </para>
        /// </remarks>
        private static void ApplyIncrementalGc()
        {
            if (!Plugin.SpreadGarbageCollection.Value || !GarbageCollector.isIncremental)
            {
                if (_gcApplied
                    && GarbageCollector.incrementalTimeSliceNanoseconds
                        == _appliedGcSlice)
                    GarbageCollector.incrementalTimeSliceNanoseconds = _originalGcSlice;
                else
                    _originalGcSlice = GarbageCollector.incrementalTimeSliceNanoseconds;
                _gcApplied = false;
                return;
            }

            ulong wanted = (ulong)Mathf.Clamp(Plugin.GcSliceMicroseconds.Value, 100, 8000) * 1000UL;
            if (!_gcApplied)
                _originalGcSlice = GarbageCollector.incrementalTimeSliceNanoseconds;
            _gcApplied = true;
            _appliedGcSlice = wanted;
            if (GarbageCollector.incrementalTimeSliceNanoseconds != wanted)
                GarbageCollector.incrementalTimeSliceNanoseconds = wanted;
        }

        public static void Restore()
        {
            if (!_captured) return;
            if (_queueApplied
                && QualitySettings.maxQueuedFrames == _appliedQueueValue)
                QualitySettings.maxQueuedFrames = _originalQueue;
            if (_uploadApplied)
                RestoreUploadBudgetIfOwned();
            if (_gcApplied
                && GarbageCollector.incrementalTimeSliceNanoseconds == _appliedGcSlice)
                GarbageCollector.incrementalTimeSliceNanoseconds = _originalGcSlice;
            _queueApplied = _uploadApplied = _gcApplied = false;
        }

        private static void RestoreUploadBudgetIfOwned()
        {
            if (QualitySettings.asyncUploadTimeSlice == _appliedUploadSlice)
                QualitySettings.asyncUploadTimeSlice = _originalUploadSlice;
            if (QualitySettings.asyncUploadBufferSize == _appliedUploadBuffer)
                QualitySettings.asyncUploadBufferSize = _originalUploadBuffer;
            if (QualitySettings.asyncUploadPersistentBuffer
                == _appliedPersistentBuffer)
            {
                QualitySettings.asyncUploadPersistentBuffer =
                    _originalPersistentBuffer;
            }
        }
    }
}
