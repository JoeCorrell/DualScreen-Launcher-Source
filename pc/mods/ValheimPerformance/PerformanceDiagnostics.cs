using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Opt-in, low-overhead frame diagnostics for controlled A/B measurements.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <see cref="Tick"/> must be called once from the plugin's Update method.
    /// No sample buffers are allocated until diagnostics are enabled. Counter
    /// entry points also reduce to a volatile read while collection is disabled.
    /// </para>
    /// <para>
    /// Reports deliberately use Unity's unscaled frame delta. Pauses, loading
    /// stalls and other real frames therefore remain visible in the result.
    /// </para>
    /// </remarks>
    internal static class PerformanceDiagnostics
    {
        private const float MinimumWindowSeconds = 5f;
        private const float MaximumWindowSeconds = 600f;

        // This covers even an uncapped 1,000 FPS for the configured window. If
        // that extraordinary rate is exceeded, the percentile buffer becomes a
        // ring while the FPS, mean, maximum and hitch counts still cover every
        // frame in the window.
        private const int SamplesPerSecondCapacity = 1000;

        private static float[]? _samples;
        private static float[]? _sorted;
        private static FrameTiming[]? _frameTiming;
        private static int _sampleCount;
        private static int _sampleWriteIndex;
        private static long _frameCount;
        private static double _elapsedSeconds;
        private static double _totalMilliseconds;
        private static float _maximumMilliseconds;
        private static double _cpuFrameMilliseconds;
        private static double _gpuFrameMilliseconds;
        private static double _maximumCpuFrameMilliseconds;
        private static double _maximumGpuFrameMilliseconds;
        private static long _cpuTimingCount;
        private static long _gpuTimingCount;
        private static long _over25Milliseconds;
        private static long _over33Milliseconds;
        private static long _over50Milliseconds;
        private static float _windowSeconds;
        private static bool _active;

        private static int _gc0AtStart;
        private static int _gc1AtStart;
        private static int _gc2AtStart;
        private static long _managedBytesAtStart;

        // An int is used instead of a bool so patch callbacks can test the gate
        // safely from any Unity/physics worker thread.
        private static int _collectingCounters;

        private static long _collisionContactHits;
        private static long _collisionContactBufferAllocations;
        private static long _collisionContactFallbacks;
        private static long _characterColorSkips;
        private static long _characterColorUpdates;
        private static long _dryWaterSkips;
        private static long _dryWaterQueries;
        private static long _smokeSkips;
        private static long _smokeUpdates;
        private static long _lightSkips;
        private static long _lightUpdates;
        private static long _equipmentSkips;
        private static long _equipmentUpdates;
        private static long _autoPickupQueries;
        private static long _autoPickupBufferGrowths;
        private static long _waterStaticSkips;
        private static long _waterStaticUpdates;
        private static long _weaponTrailFrames;
        private static long _weaponTrailBufferGrowths;
        private static long _modifierFastPaths;
        private static long _frictionSkips;
        private static long _frictionRuns;
        private static long _packageWrites;
        private static long _packageReads;
        private static long _packageFallbacks;
        private static long _sharedMaterialReads;

        /// <summary>Records one unscaled frame and emits a report when due.</summary>
        public static void Tick()
        {
            if (!Plugin.DiagnosticsEnabled.Value)
            {
                if (_active) Stop();
                return;
            }

            float requestedWindow = Mathf.Clamp(
                (float)Plugin.DiagnosticsWindowSeconds.Value,
                MinimumWindowSeconds,
                MaximumWindowSeconds);

            if (!_active || !Mathf.Approximately(requestedWindow, _windowSeconds))
            {
                BeginWindow(requestedWindow, clearCounters: true);
                return;
            }

            float milliseconds = Time.unscaledDeltaTime * 1000f;
            if (!(milliseconds > 0f) || float.IsNaN(milliseconds)
                || float.IsInfinity(milliseconds))
            {
                return;
            }

            AddFrame(milliseconds);
            AddEngineTiming();
            if (_elapsedSeconds >= _windowSeconds)
            {
                Report();
                BeginWindow(_windowSeconds, clearCounters: false);
            }
        }

        private static void BeginWindow(float seconds, bool clearCounters)
        {
            // Initial enable and a live window-size change need a clean boundary.
            // A normal report rollover keeps collection live so a worker callback
            // cannot disappear into a small disabled gap between two windows.
            if (clearCounters) Volatile.Write(ref _collectingCounters, 0);
            _windowSeconds = seconds;
            EnsureCapacity(seconds);
            ResetFrameSamples();

            _gc0AtStart = GC.CollectionCount(0);
            _gc1AtStart = GC.CollectionCount(1);
            _gc2AtStart = GC.CollectionCount(2);
            _managedBytesAtStart = GC.GetTotalMemory(false);

            if (clearCounters) ClearCounters();
            _active = true;
            Volatile.Write(ref _collectingCounters, 1);
        }

        private static void Stop()
        {
            Volatile.Write(ref _collectingCounters, 0);
            _active = false;
            ResetFrameSamples();
            ClearCounters();
        }

        private static void EnsureCapacity(float seconds)
        {
            int wanted = Math.Max(1024,
                (int)Math.Ceiling(seconds * SamplesPerSecondCapacity));
            if (_samples != null && _sorted != null
                && _samples.Length >= wanted && _sorted.Length >= wanted)
            {
                return;
            }

            _samples = new float[wanted];
            _sorted = new float[wanted];
            _frameTiming = new FrameTiming[1];
        }

        private static void ResetFrameSamples()
        {
            _sampleCount = 0;
            _sampleWriteIndex = 0;
            _frameCount = 0;
            _elapsedSeconds = 0d;
            _totalMilliseconds = 0d;
            _maximumMilliseconds = 0f;
            _cpuFrameMilliseconds = 0d;
            _gpuFrameMilliseconds = 0d;
            _maximumCpuFrameMilliseconds = 0d;
            _maximumGpuFrameMilliseconds = 0d;
            _cpuTimingCount = 0;
            _gpuTimingCount = 0;
            _over25Milliseconds = 0;
            _over33Milliseconds = 0;
            _over50Milliseconds = 0;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void AddFrame(float milliseconds)
        {
            float[] samples = _samples!;
            samples[_sampleWriteIndex] = milliseconds;
            _sampleWriteIndex++;
            if (_sampleWriteIndex == samples.Length) _sampleWriteIndex = 0;
            if (_sampleCount < samples.Length) _sampleCount++;

            _frameCount++;
            _elapsedSeconds += milliseconds / 1000d;
            _totalMilliseconds += milliseconds;
            if (milliseconds > _maximumMilliseconds)
                _maximumMilliseconds = milliseconds;
            if (milliseconds > 25f) _over25Milliseconds++;
            if (milliseconds > 33f) _over33Milliseconds++;
            if (milliseconds > 50f) _over50Milliseconds++;
        }

        private static void AddEngineTiming()
        {
            FrameTiming[]? timing = _frameTiming;
            if (timing == null) return;

            try
            {
                FrameTimingManager.CaptureFrameTimings();
                if (FrameTimingManager.GetLatestTimings(1, timing) == 0) return;

                double cpu = timing[0].cpuFrameTime;
                if (cpu > 0d && !double.IsNaN(cpu) && !double.IsInfinity(cpu))
                {
                    _cpuFrameMilliseconds += cpu;
                    _cpuTimingCount++;
                    if (cpu > _maximumCpuFrameMilliseconds)
                        _maximumCpuFrameMilliseconds = cpu;
                }

                double gpu = timing[0].gpuFrameTime;
                if (gpu > 0d && !double.IsNaN(gpu) && !double.IsInfinity(gpu))
                {
                    _gpuFrameMilliseconds += gpu;
                    _gpuTimingCount++;
                    if (gpu > _maximumGpuFrameMilliseconds)
                        _maximumGpuFrameMilliseconds = gpu;
                }
            }
            catch
            {
                // Some translated backends do not expose frame timings. The
                // frame-time and GC report remains valid without them.
                _frameTiming = null;
            }
        }

        private static void Report()
        {
            if (_sampleCount <= 0 || _frameCount <= 0 || _elapsedSeconds <= 0d)
                return;

            float[] sorted = _sorted!;
            Array.Copy(_samples!, 0, sorted, 0, _sampleCount);
            Array.Sort(sorted, 0, _sampleCount);

            double fps = _frameCount / _elapsedSeconds;
            double mean = _totalMilliseconds / _frameCount;
            float median = Percentile(sorted, _sampleCount, 0.50d);
            float p95 = Percentile(sorted, _sampleCount, 0.95d);
            float p99 = Percentile(sorted, _sampleCount, 0.99d);
            double onePercentLow = SlowestFractionFps(sorted, _sampleCount, 0.01d);
            double pointOnePercentLow = SlowestFractionFps(sorted, _sampleCount, 0.001d);

            int gc0 = GC.CollectionCount(0) - _gc0AtStart;
            int gc1 = GC.CollectionCount(1) - _gc1AtStart;
            int gc2 = GC.CollectionCount(2) - _gc2AtStart;
            long managedDelta = GC.GetTotalMemory(false) - _managedBytesAtStart;
            CounterSnapshot counters = TakeCounterSnapshot();

            var report = new StringBuilder(512);
            report.AppendFormat(CultureInfo.InvariantCulture,
                "[PerfDiag] {0:0.0}s/{1}f FPS {2:0.0} | ms mean {3:0.00} "
                + "med {4:0.00} p95 {5:0.00} p99 {6:0.00} max {7:0.00} "
                + "| lows 1% {8:0.0} 0.1% {9:0.0} "
                + "| hitches >25/>33/>50 {10}/{11}/{12} "
                + "| GC {13}/{14}/{15} heap {16:+0.00;-0.00;0.00} MiB",
                _elapsedSeconds, _frameCount, fps, mean, median, p95, p99,
                _maximumMilliseconds, onePercentLow, pointOnePercentLow,
                _over25Milliseconds, _over33Milliseconds, _over50Milliseconds,
                gc0, gc1, gc2, managedDelta / 1048576d);

            if (_frameCount > _sampleCount)
            {
                report.AppendFormat(CultureInfo.InvariantCulture,
                    " | percentile sample latest {0}/{1}", _sampleCount, _frameCount);
            }

            if (_cpuTimingCount > 0)
            {
                report.AppendFormat(CultureInfo.InvariantCulture,
                    " | engine CPU avg/max {0:0.00}/{1:0.00} ms",
                    _cpuFrameMilliseconds / _cpuTimingCount,
                    _maximumCpuFrameMilliseconds);
            }
            if (_gpuTimingCount > 0)
            {
                report.AppendFormat(CultureInfo.InvariantCulture,
                    " | engine GPU avg/max {0:0.00}/{1:0.00} ms",
                    _gpuFrameMilliseconds / _gpuTimingCount,
                    _maximumGpuFrameMilliseconds);
            }

            AppendCounters(report, counters);
            Plugin.Log.LogInfo(report.ToString());
        }

        private static float Percentile(float[] sorted, int count, double percentile)
        {
            int index = (int)Math.Ceiling(percentile * count) - 1;
            if (index < 0) index = 0;
            if (index >= count) index = count - 1;
            return sorted[index];
        }

        private static double SlowestFractionFps(float[] sorted, int count,
                                                  double fraction)
        {
            int slowCount = Math.Max(1, (int)Math.Ceiling(count * fraction));
            int first = count - slowCount;
            double milliseconds = 0d;
            for (int i = first; i < count; i++) milliseconds += sorted[i];
            return milliseconds > 0d ? 1000d / (milliseconds / slowCount) : 0d;
        }

        private static void AppendCounters(StringBuilder report, CounterSnapshot c)
        {
            AppendCounter(report, "contacts", c.CollisionContactHits,
                c.CollisionContactBufferAllocations, c.CollisionContactFallbacks,
                "hit/alloc/fallback");
            AppendCounter(report, "colors", c.CharacterColorSkips,
                c.CharacterColorUpdates, "skip/run");
            AppendCounter(report, "dry-water", c.DryWaterSkips,
                c.DryWaterQueries, "skip/query");
            AppendCounter(report, "smoke", c.SmokeSkips,
                c.SmokeUpdates, "skip/run");
            AppendCounter(report, "lights", c.LightSkips,
                c.LightUpdates, "skip/run");
            AppendCounter(report, "equipment", c.EquipmentSkips,
                c.EquipmentUpdates, "skip/run");
            AppendCounter(report, "pickup", c.AutoPickupQueries,
                c.AutoPickupBufferGrowths, "query/grow");
            AppendCounter(report, "water-static", c.WaterStaticSkips,
                c.WaterStaticUpdates, "skip/run");
            AppendCounter(report, "weapon-trail", c.WeaponTrailFrames,
                c.WeaponTrailBufferGrowths, "frame/grow");
            AppendCounter(report, "modifiers", c.ModifierFastPaths, "direct");
            AppendCounter(report, "friction", c.FrictionSkips,
                c.FrictionRuns, "skip/run");
            AppendCounter(report, "zpackage", c.PackageWrites,
                c.PackageReads, c.PackageFallbacks, "write/read/fallback");
            AppendCounter(report, "shared-material", c.SharedMaterialReads, "read");
        }

        private static void AppendCounter(StringBuilder report, string name,
                                          long value, string label)
        {
            if (value == 0) return;
            report.AppendFormat(CultureInfo.InvariantCulture,
                " | {0} {1} {2}", name, value, label);
        }

        private static void AppendCounter(StringBuilder report, string name,
                                          long first, long second, string labels)
        {
            if ((first | second) == 0) return;
            report.AppendFormat(CultureInfo.InvariantCulture,
                " | {0} {1}/{2} {3}", name, first, second, labels);
        }

        private static void AppendCounter(StringBuilder report, string name,
                                          long first, long second, long third,
                                          string labels)
        {
            if ((first | second | third) == 0) return;
            report.AppendFormat(CultureInfo.InvariantCulture,
                " | {0} {1}/{2}/{3} {4}", name, first, second, third, labels);
        }

        private struct CounterSnapshot
        {
            public long CollisionContactHits;
            public long CollisionContactBufferAllocations;
            public long CollisionContactFallbacks;
            public long CharacterColorSkips;
            public long CharacterColorUpdates;
            public long DryWaterSkips;
            public long DryWaterQueries;
            public long SmokeSkips;
            public long SmokeUpdates;
            public long LightSkips;
            public long LightUpdates;
            public long EquipmentSkips;
            public long EquipmentUpdates;
            public long AutoPickupQueries;
            public long AutoPickupBufferGrowths;
            public long WaterStaticSkips;
            public long WaterStaticUpdates;
            public long WeaponTrailFrames;
            public long WeaponTrailBufferGrowths;
            public long ModifierFastPaths;
            public long FrictionSkips;
            public long FrictionRuns;
            public long PackageWrites;
            public long PackageReads;
            public long PackageFallbacks;
            public long SharedMaterialReads;
        }

        private static CounterSnapshot TakeCounterSnapshot()
        {
            return new CounterSnapshot
            {
                CollisionContactHits = Interlocked.Exchange(ref _collisionContactHits, 0),
                CollisionContactBufferAllocations = Interlocked.Exchange(
                    ref _collisionContactBufferAllocations, 0),
                CollisionContactFallbacks = Interlocked.Exchange(
                    ref _collisionContactFallbacks, 0),
                CharacterColorSkips = Interlocked.Exchange(ref _characterColorSkips, 0),
                CharacterColorUpdates = Interlocked.Exchange(ref _characterColorUpdates, 0),
                DryWaterSkips = Interlocked.Exchange(ref _dryWaterSkips, 0),
                DryWaterQueries = Interlocked.Exchange(ref _dryWaterQueries, 0),
                SmokeSkips = Interlocked.Exchange(ref _smokeSkips, 0),
                SmokeUpdates = Interlocked.Exchange(ref _smokeUpdates, 0),
                LightSkips = Interlocked.Exchange(ref _lightSkips, 0),
                LightUpdates = Interlocked.Exchange(ref _lightUpdates, 0),
                EquipmentSkips = Interlocked.Exchange(ref _equipmentSkips, 0),
                EquipmentUpdates = Interlocked.Exchange(ref _equipmentUpdates, 0),
                AutoPickupQueries = Interlocked.Exchange(ref _autoPickupQueries, 0),
                AutoPickupBufferGrowths = Interlocked.Exchange(
                    ref _autoPickupBufferGrowths, 0),
                WaterStaticSkips = Interlocked.Exchange(ref _waterStaticSkips, 0),
                WaterStaticUpdates = Interlocked.Exchange(ref _waterStaticUpdates, 0),
                WeaponTrailFrames = Interlocked.Exchange(ref _weaponTrailFrames, 0),
                WeaponTrailBufferGrowths = Interlocked.Exchange(
                    ref _weaponTrailBufferGrowths, 0),
                ModifierFastPaths = Interlocked.Exchange(ref _modifierFastPaths, 0),
                FrictionSkips = Interlocked.Exchange(ref _frictionSkips, 0),
                FrictionRuns = Interlocked.Exchange(ref _frictionRuns, 0),
                PackageWrites = Interlocked.Exchange(ref _packageWrites, 0),
                PackageReads = Interlocked.Exchange(ref _packageReads, 0),
                PackageFallbacks = Interlocked.Exchange(ref _packageFallbacks, 0),
                SharedMaterialReads = Interlocked.Exchange(ref _sharedMaterialReads, 0)
            };
        }

        private static void ClearCounters()
        {
            TakeCounterSnapshot();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Increment(ref long counter)
        {
            if (Volatile.Read(ref _collectingCounters) != 0)
                Interlocked.Increment(ref counter);
        }

        public static void CountCollisionContactHit() =>
            Increment(ref _collisionContactHits);
        public static void CountCollisionContactBufferAllocation() =>
            Increment(ref _collisionContactBufferAllocations);
        public static void CountCollisionContactFallback() =>
            Increment(ref _collisionContactFallbacks);
        public static void CountCharacterColorSkip() =>
            Increment(ref _characterColorSkips);
        public static void CountCharacterColorUpdate() =>
            Increment(ref _characterColorUpdates);
        public static void CountDryWaterSkip() => Increment(ref _dryWaterSkips);
        public static void CountDryWaterQuery() => Increment(ref _dryWaterQueries);
        public static void CountSmokeSkip() => Increment(ref _smokeSkips);
        public static void CountSmokeUpdate() => Increment(ref _smokeUpdates);
        public static void CountLightSkip() => Increment(ref _lightSkips);
        public static void CountLightUpdate() => Increment(ref _lightUpdates);
        public static void CountEquipmentSkip() => Increment(ref _equipmentSkips);
        public static void CountEquipmentUpdate() => Increment(ref _equipmentUpdates);
        public static void CountAutoPickupQuery() => Increment(ref _autoPickupQueries);
        public static void CountAutoPickupBufferGrowth() =>
            Increment(ref _autoPickupBufferGrowths);
        public static void CountWaterStaticSkip() => Increment(ref _waterStaticSkips);
        public static void CountWaterStaticUpdate() => Increment(ref _waterStaticUpdates);
        public static void CountWeaponTrailFrame() => Increment(ref _weaponTrailFrames);
        public static void CountWeaponTrailBufferGrowth() =>
            Increment(ref _weaponTrailBufferGrowths);
        public static void CountModifierFastPath() => Increment(ref _modifierFastPaths);
        public static void CountFrictionSkip() => Increment(ref _frictionSkips);
        public static void CountFrictionRun() => Increment(ref _frictionRuns);
        public static void CountPackageWrite() => Increment(ref _packageWrites);
        public static void CountPackageRead() => Increment(ref _packageReads);
        public static void CountPackageFallback() => Increment(ref _packageFallbacks);
        public static void CountSharedMaterialRead() => Increment(ref _sharedMaterialReads);
    }
}
