-keep class com.dualscreen.companion.api.** { *; }
# External DEX shares the launcher's Kotlin runtime, including helpers not used by host code.
-keep class kotlin.** { *; }
