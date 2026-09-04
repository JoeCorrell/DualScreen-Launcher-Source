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

val remotelyManagedPayloads = listOf(
    "dead-island-de",
    "deus-ex-hrdc",
    "fallout3",
    "fallout4",
    "grim-dawn",
    "skyrim-se",
    "stardew-valley",
    "transformers-wfc",
    "valheim",
)
val filteredLauncherAssets = layout.buildDirectory.dir("generated/filteredLauncherAssets")
val prepareFilteredLauncherAssets by tasks.registering(Sync::class) {
    from(layout.projectDirectory.dir("src/main/assets")) {
        remotelyManagedPayloads.forEach { gameId ->
            exclude("mods/$gameId/payload/**")
        }
        exclude("mods/switch/**/payload/**")
    }
    into(filteredLauncherAssets)
}

android {
    namespace = "com.dualscreen.launcher"

    defaultConfig {
        applicationId = "com.dualscreen.launcher"
        versionCode = providers.gradleProperty("launcherVersionCode").orNull?.toIntOrNull() ?: 27
        versionName = providers.gradleProperty("launcherVersionName").orNull ?: "1.6.15"
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
        debug {
            isMinifyEnabled = false
            // Device-test builds must update the installed release-signed
            // launcher in place; using Android's generated debug key makes an
            // otherwise valid APK fail with INSTALL_FAILED_UPDATE_INCOMPATIBLE.
            signingConfig = localReleaseSigning
        }
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
    // AndroidSourceDirectorySet's exclude filter is not applied by AGP's asset
    // merger. Stage a filtered tree instead, otherwise stale native payloads
    // (including WFC's redistributables) silently ship inside every APK and can
    // override the signed first-boot pack. Isaac is intentionally not in this
    // list, so its native DLL, Lua loader, and assets ship with this release.
    sourceSets["main"].assets.setSrcDirs(listOf(filteredLauncherAssets.get().asFile))
    packaging { jniLibs { useLegacyPackaging = true } }
}

tasks.named("preBuild").configure {
    dependsOn(prepareFilteredLauncherAssets)
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
