-keepclasseswithmembers class * {
    native <methods>;
}
-keep class com.github.luben.zstd.** { *; }
-keep class com.wemu.engine.** { *; }
-keep class com.wemu.runtime.** { *; }
# Eden's JNI content installer invokes this erased Function2 with boxed Double values. Keep the
# concrete Number-compatible bridge intact so R8 cannot merge it back into a Long-specialized
# Kotlin lambda and crash the isolated content worker.
-keep class com.dualscreen.launcher.switch.EdenInstallProgressCallback { *; }
