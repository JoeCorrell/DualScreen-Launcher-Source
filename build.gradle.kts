plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}

tasks.register<Delete>("cleanAll") {
    delete(rootProject.layout.buildDirectory)
    subprojects.forEach { delete(it.layout.buildDirectory) }
}
