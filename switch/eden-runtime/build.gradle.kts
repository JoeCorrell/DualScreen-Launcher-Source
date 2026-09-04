import java.nio.ByteBuffer
import java.nio.ByteOrder

plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.serialization)
    id("kotlin-parcelize")
    id("androidx.navigation.safeargs.kotlin") version "2.8.9"
}

val botwBasePlayerAssetNames = listOf(
    "Link.glb",
    "Armor_Default_Head.glb",
    "Armor_Default_Upper.glb",
    "Armor_Default_Lower.glb",
)
val botwPlayerAssetsDirectory = providers.gradleProperty("botwPlayerAssetsDir")
val generatedBotwPlayerAssets = layout.buildDirectory.dir("generated/botwPlayerAssets")
val prepareBotwBasePlayerAssets by tasks.registering(Sync::class) {
    botwPlayerAssetsDirectory.orNull?.let { sourceDirectory ->
        from(sourceDirectory) {
            include(botwBasePlayerAssetNames)
            include("Armor_*.glb")
            into("botw/player")
        }
    }
    into(generatedBotwPlayerAssets)
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
        // BOTW models are generated from the builder's own RomFS and must not live in source
        // control. Supplying -PbotwPlayerAssetsDir packages Link plus every armor model so clean
        // installs can render equipped armor; external caches can still override these assets.
        assets.srcDir(generatedBotwPlayerAssets)
    }

}

val verifyDualScreenAssets by tasks.registering {
    val dualScreenAssets = layout.projectDirectory.dir(
        "../eden-upstream/src/android/app/src/dualscreen/assets"
    )
    inputs.dir(dualScreenAssets)
    dependsOn(prepareBotwBasePlayerAssets)
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

val verifyBotwBasePlayerAssets by tasks.registering {
    val dualScreenAssets = layout.projectDirectory.dir(
        "../eden-upstream/src/android/app/src/dualscreen/assets"
    )
    val playerIndex = dualScreenAssets.file("botw/player/index.json")
    inputs.file(playerIndex)
    dependsOn(prepareBotwBasePlayerAssets)
    doLast {
        val generatedRoot = generatedBotwPlayerAssets.get().dir("botw/player")
        fun playerAsset(name: String): File? =
            dualScreenAssets.file("botw/player/$name").asFile.takeIf { it.isFile }
                ?: generatedRoot.file(name).asFile.takeIf { it.isFile }
        fun hasPlayerAsset(name: String): Boolean = playerAsset(name) != null
        fun readGlbDocument(file: File): Map<*, *> {
            val bytes = file.readBytes()
            check(bytes.size >= 20) { "shorter than a GLB JSON header" }
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            check(bytes.copyOfRange(0, 4).contentEquals("glTF".toByteArray(Charsets.US_ASCII))) {
                "invalid GLB magic"
            }
            check(buffer.getInt(4) == 2 && buffer.getInt(8) == bytes.size) {
                "invalid GLB version or length"
            }
            val jsonLength = buffer.getInt(12)
            check(
                buffer.getInt(16) == 0x4E4F534A &&
                    jsonLength >= 2 &&
                    20L + jsonLength.toLong() <= bytes.size.toLong()
            ) { "invalid GLB JSON chunk" }
            val json = String(bytes, 20, jsonLength, Charsets.UTF_8).trimEnd(' ', '\u0000')
            return groovy.json.JsonSlurper().parseText(json) as? Map<*, *>
                ?: error("GLB JSON is not an object")
        }

        val missingBase = botwBasePlayerAssetNames.filterNot(::hasPlayerAsset)
        check(missingBase.isEmpty()) {
            "A release without BOTW's base player GLBs leaves the bottom-screen Link preview " +
                "blank. Supply the generated model directory with " +
                "-PbotwPlayerAssetsDir=<directory>: ${missingBase.joinToString()}"
        }

        val indexRoot = groovy.json.JsonSlurper().parse(playerIndex.asFile) as? Map<*, *>
            ?: error("BOTW player model index is not a JSON object")
        val actors = indexRoot["actors"] as? Map<*, *>
            ?: error("BOTW player model index has no actors object")
        val requiredArmor = actors.entries.asSequence()
            .filter { (actor, _) -> actor.toString().startsWith("Armor_") }
            .mapNotNull { (_, entry) ->
                when (entry) {
                    is String -> entry
                    is Map<*, *> -> entry["file"] as? String
                    else -> null
                }
            }
            .filter { file -> file.startsWith("Armor_") && file.endsWith(".glb") }
            .toSortedSet()
        check(requiredArmor.isNotEmpty()) {
            "BOTW player model index contains no armor GLBs"
        }
        val missingArmor = requiredArmor.filterNot(::hasPlayerAsset)
        check(missingArmor.isEmpty()) {
            "A release without every indexed BOTW armor GLB renders Link without equipped " +
                "armor on clean installs. Generate the preview cache from the builder's RomFS " +
                "and supply -PbotwPlayerAssetsDir=<directory>: ${missingArmor.joinToString()}"
        }

        val invalidPlayerBodies = (requiredArmor + "Link.glb").mapNotNull { name ->
            val file = playerAsset(name) ?: return@mapNotNull null
            val failure = runCatching {
                val document = readGlbDocument(file)
                check((document["meshes"] as? Collection<*>)?.isNotEmpty() == true) {
                    "contains no meshes"
                }
                check((document["skins"] as? Collection<*>)?.isNotEmpty() == true) {
                    "contains no skins"
                }
                val extras = document["extras"] as? Map<*, *>
                val pose = extras?.get("botwPose") as? Map<*, *>
                check(
                    pose?.get("animation") == "Nml_Wait" &&
                        ((pose?.get("changedAnimatedNodes") as? Number)?.toInt() ?: 0) > 0
                ) { "has no validated baked Nml_Wait pose" }
                val images = document["images"] as? Collection<*> ?: emptyList<Any>()
                check(images.all { image ->
                    image is Map<*, *> &&
                        image.containsKey("bufferView") &&
                        image["mimeType"] == "image/png"
                }) { "contains a non-embedded or non-PNG texture" }
            }.exceptionOrNull()
            failure?.let { "$name: ${it.message ?: it.javaClass.simpleName}" }
        }
        check(invalidPlayerBodies.isEmpty()) {
            "BOTW Link/armor preview GLBs are not release-safe. Raw bind-pose armor stretches " +
                "on clean installs; regenerate it through Convert-BotwModel.ps1: " +
                invalidPlayerBodies.joinToString()
        }
    }
}

tasks.named("preBuild").configure {
    dependsOn(verifyDualScreenAssets)
}

tasks.matching { it.name == "preReleaseBuild" }.configureEach {
    dependsOn(verifyBotwBasePlayerAssets)
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
