# Eden's native core calls these entry points and model constructors through JNI.
-keep class org.yuzu.yuzu_emu.NativeLibrary { *; }
-keep class org.yuzu.yuzu_emu.utils.GameMetadata { *; }
-keep class org.yuzu.yuzu_emu.model.** { *; }

# The native core resolves these classes, nested enums, fields, constructors,
# and callbacks by their literal JVM names during JNI_OnLoad. R8 previously
# removed DiskShaderCacheProgress$LoadCallbackStage because only the outer
# Kotlin object carries @Keep. Loading libyuzu-android.so from the production
# key/firmware flow then aborted the entire process before the operation could
# report an error. Preserve Eden's Java/Kotlin ABI as a unit: it is an embedded
# native runtime, not an ordinary shrinkable UI dependency.
-keep class org.yuzu.yuzu_emu.** { *; }

# The native ID cache constructs kotlin.Pair directly with FindClass/GetMethodID.
# R8 sees no Java-side constructor call in the embedded frontend and otherwise
# removes/renames Pair in release builds, causing an unrecoverable abort from
# JNI_OnLoad before any Switch screen or importer can open.
-keep class kotlin.Pair { *; }

# Signed companion DEX files use the host's Filament runtime through its public API.
# Those references are invisible to the launcher's shrinker.
-keep class com.google.android.filament.** { *; }

-keepclasseswithmembers,includedescriptorclasses class * {
    native <methods>;
}
