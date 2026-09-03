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

fun gradleBuildConfigString(name: String): String =
    providers.gradleProperty(name).orNull.orEmpty()
        .replace("\\", "\\\\")
        .replace("\"", "\\\"")

android {
    namespace = "com.dualscreen.launcher"

    defaultConfig {
        applicationId = "com.dualscreen.launcher"
        versionCode = providers.gradleProperty("launcherVersionCode").orNull?.toIntOrNull() ?: 25
        versionName = providers.gradleProperty("launcherVersionName").orNull ?: "1.6.13"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
        // Both values are safe to ship: the URL identifies the service and the
        // public key only verifies its short-lived access leases. All secrets
        // remain in the entitlement service.
        buildConfigField(
            "String",
            "ACCESS_API_BASE_URL",
            "\"${gradleBuildConfigString("accessApiBaseUrl")}\"",
        )
        buildConfigField(
            "String",
            "ACCESS_LEASE_PUBLIC_KEY",
            "\"${gradleBuildConfigString("accessLeasePublicKey")}\"",
        )
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
            // Minify the release requested for device testing. Resource shrinking stays
            // disabled because the native runtime resolves several packaged resources by
            // name and because it adds a second high-memory analysis pass.
            isMinifyEnabled = true
            isShrinkResources = false
            signingConfig = localReleaseSigning
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    buildFeatures { buildConfig = true }
    androidResources { noCompress += listOf("tzst", "zst", "tgz", "so", "dat", "dll") }
    // Mod payloads are supplied by the signed, remotely updateable first-boot
    // pack. Keep them out of the APK so native mod fixes and prerequisites can
    // be updated without rebuilding the launcher.
    listOf(
        "dead-island-de",
        "deus-ex-hrdc",
        "fallout3",
        "fallout4",
        "grim-dawn",
        "skyrim-se",
        "stardew-valley",
        "transformers-wfc",
        "valheim",
    ).forEach { gameId ->
        sourceSets["main"].assets.exclude("mods/$gameId/payload/**")
    }
    sourceSets["main"].assets.exclude("mods/switch/**/payload/**")
    packaging { jniLibs { useLegacyPackaging = true } }
}

dependencies {
    implementation(projects.engine)
    implementation(projects.secondscreen)
    implementation(projects.core.common)
    implementation(projects.core.model)
    implementation(projects.core.designsystem)
    implementation(projects.core.display)
    implementation(projects.switch.edenRuntime)

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
