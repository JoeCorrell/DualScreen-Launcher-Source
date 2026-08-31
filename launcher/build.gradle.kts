import java.util.Properties

plugins {
    alias(libs.plugins.wemu.android.application)
    alias(libs.plugins.wemu.android.application.compose)
}

val releaseSigningPropertiesFile = rootProject.file("keystore.properties")
val releaseSigningProperties = Properties().apply {
    if (releaseSigningPropertiesFile.isFile) {
        releaseSigningPropertiesFile.inputStream().use(::load)
    }
}

android {
    namespace = "com.dualscreen.launcher"

    defaultConfig {
        applicationId = "com.dualscreen.launcher"
        versionCode = providers.gradleProperty("launcherVersionCode").orNull?.toIntOrNull() ?: 13
        versionName = providers.gradleProperty("launcherVersionName").orNull ?: "1.6.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
    }

    val localReleaseSigning = if (releaseSigningPropertiesFile.isFile) {
        signingConfigs.create("release") {
            storeFile = rootProject.file(checkNotNull(releaseSigningProperties.getProperty("storeFile")))
            storePassword = checkNotNull(releaseSigningProperties.getProperty("storePassword"))
            keyAlias = checkNotNull(releaseSigningProperties.getProperty("keyAlias"))
            keyPassword = checkNotNull(releaseSigningProperties.getProperty("keyPassword"))
            enableV1Signing = true
            enableV2Signing = true
        }
    } else {
        null
    }

    buildTypes {
        debug { isMinifyEnabled = false }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            signingConfig = localReleaseSigning
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    androidResources { noCompress += listOf("tzst", "zst", "tgz", "so", "dat", "dll") }
    sourceSets["main"].assets.exclude("mods/**/payload/**")
    packaging { jniLibs { useLegacyPackaging = true } }
}

dependencies {
    implementation(projects.engine)
    implementation(projects.secondscreen)
    implementation(projects.core.common)
    implementation(projects.core.model)
    implementation(projects.core.designsystem)
    implementation(projects.core.display)

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.preference)
    implementation(libs.kotlinx.coroutines.android)

    testImplementation(libs.junit4)
    testImplementation(libs.truth)
    testImplementation(libs.org.json)
}
