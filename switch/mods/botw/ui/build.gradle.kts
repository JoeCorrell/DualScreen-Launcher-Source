plugins { alias(libs.plugins.android.library); alias(libs.plugins.kotlin.android) }
android {
    namespace = "com.dualscreen.mods.botw"
    compileSdk = 36
    defaultConfig { minSdk = 26 }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}
dependencies { compileOnly("com.google.android.filament:filament-android:1.75.0"); compileOnly("com.google.android.filament:gltfio-android:1.75.0"); compileOnly(project(":switch:companion-api")) }
