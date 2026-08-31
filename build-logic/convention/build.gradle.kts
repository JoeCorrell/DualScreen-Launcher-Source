/*
 * Convention plugins for wemu.
 *
 * Every module applies one of the `wemu.*` plugins instead of restating the
 * Android DSL. Loki's build-logic is the model, minus the Hilt, Room and
 * feature plugins this app has no use for.
 */

plugins {
    `kotlin-dsl`
}

group = "com.wemu.buildlogic"

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    compileOnly(libs.android.gradlePlugin)
    compileOnly(libs.kotlin.gradlePlugin)
    compileOnly(libs.compose.gradlePlugin)
}

gradlePlugin {
    plugins {
        register("androidApplication") {
            id = "wemu.android.application"
            implementationClass = "WemuAndroidApplicationConventionPlugin"
        }
        register("androidApplicationCompose") {
            id = "wemu.android.application.compose"
            implementationClass = "WemuAndroidApplicationComposeConventionPlugin"
        }
        register("androidLibrary") {
            id = "wemu.android.library"
            implementationClass = "WemuAndroidLibraryConventionPlugin"
        }
        register("androidLibraryCompose") {
            id = "wemu.android.library.compose"
            implementationClass = "WemuAndroidLibraryComposeConventionPlugin"
        }
    }
}
