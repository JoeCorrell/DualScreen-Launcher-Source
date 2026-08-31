plugins {
    alias(libs.plugins.wemu.android.library)
    alias(libs.plugins.kotlin.serialization)
}

android {
    namespace = "com.thor.core.model"
}

dependencies {
    api(libs.kotlinx.serialization.json)

    testImplementation(libs.junit4)
    testImplementation(libs.truth)
}
