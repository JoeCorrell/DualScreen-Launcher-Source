plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.serialization)
    id("kotlin-parcelize")
    id("androidx.navigation.safeargs.kotlin") version "2.8.9"
}

android {
    namespace = "org.yuzu.yuzu_emu"
    compileSdk = 36

    defaultConfig {
        minSdk = 26
        consumerProguardFiles("consumer-rules.pro")
        buildConfigField("boolean", "BOTW_DUALSCREEN_ENABLED", "true")
        buildConfigField("boolean", "EDEN_DUALSCREEN_RUNTIME", "true")
        buildConfigField("boolean", "EDEN_EMBEDDED_RUNTIME", "true")
        buildConfigField("int", "BOTW_PLAYER_PIPELINE_MODE", "3")
        buildConfigField("int", "SSHD_PLAYER_PIPELINE_MODE", "3")
        buildConfigField("int", "BOTW_SNAPSHOT_POLL_MODE", "2")
        resValue("string", "app_name", "Eden DS Runtime")
        resValue("string", "app_name_suffixed", "Eden DS Runtime")
    }

    buildFeatures {
        buildConfig = true
        viewBinding = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets.named("main") {
        java.srcDir("../eden-upstream/src/android/app/src/main/java")
        res.srcDir("../eden-upstream/src/android/app/src/main/res")
        assets.srcDir("../eden-upstream/src/android/app/src/main/assets")
        // Eden DS keeps the companion artwork/data in its dualscreen flavor. The embedded
        // runtime has no Android flavor of its own, so include that source set explicitly.
        assets.srcDir("../eden-upstream/src/android/app/src/dualscreen/assets")
    }

}

val verifyDualScreenAssets by tasks.registering {
    val dualScreenAssets = layout.projectDirectory.dir(
        "../eden-upstream/src/android/app/src/dualscreen/assets"
    )
    inputs.dir(dualScreenAssets)
    doLast {
        val required = listOf(
            "botw/player/index.json",
            "botw/ui/map/locations.json",
            "plza/map_texture_00.png",
            "plza/map_icons/player.png",
            "plza/item_names.tsv",
            "plza/pokedex.tsv",
            "sshd/player/index.json",
        )
        val missing = required.filterNot { dualScreenAssets.file(it).asFile.isFile }
        check(missing.isEmpty()) {
            "Missing Eden DS dual-screen flavor assets: ${missing.joinToString()}"
        }
        check(dualScreenAssets.asFileTree.files.size >= 829) {
            "Eden DS dual-screen asset tree is incomplete: " +
                "${dualScreenAssets.asFileTree.files.size} files"
        }
    }
}

tasks.named("preBuild").configure {
    dependsOn(verifyDualScreenAssets)
}

dependencies {
    implementation("com.google.android.filament:filament-android:1.75.0")
    implementation("com.google.android.filament:gltfio-android:1.75.0")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.recyclerview:recyclerview:1.4.0")
    implementation("androidx.constraintlayout:constraintlayout:2.2.1")
    implementation("androidx.fragment:fragment-ktx:1.8.6")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.8.7")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("io.coil-kt:coil:2.7.0")
    implementation("androidx.core:core-splashscreen:1.0.1")
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:2.17.2")
    implementation("androidx.window:window:1.3.0")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    implementation("org.commonmark:commonmark:0.22.0")
    implementation("androidx.navigation:navigation-fragment-ktx:2.8.9")
    implementation("androidx.navigation:navigation-ui-ktx:2.8.9")
    implementation("info.debatty:java-string-similarity:2.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.8.1")
    implementation("androidx.compose.ui:ui-graphics-android:1.7.8")
    implementation("androidx.compose.ui:ui-text-android:1.7.8")
    implementation("net.swiftzer.semver:semver:2.0.0")
}
