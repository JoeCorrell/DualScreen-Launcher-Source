plugins {
    alias(libs.plugins.wemu.android.application)
    alias(libs.plugins.wemu.android.application.compose)
}

android {
    namespace = "com.dualscreen.launcher"

    defaultConfig {
        applicationId = "com.dualscreen.launcher"
        versionCode = providers.gradleProperty("launcherVersionCode").orNull?.toIntOrNull() ?: 2
        versionName = providers.gradleProperty("launcherVersionName").orNull ?: "0.2.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
    }

    buildTypes {
        debug { isMinifyEnabled = false }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    androidResources { noCompress += listOf("tzst", "zst", "tgz", "so", "dat", "dll") }
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
