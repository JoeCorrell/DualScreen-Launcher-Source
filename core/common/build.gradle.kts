plugins {
    alias(libs.plugins.wemu.android.library)
}

android {
    namespace = "com.thor.core.common"
}

dependencies {
    implementation(libs.androidx.core.ktx)
    api(libs.kotlinx.coroutines.android)

    testImplementation(libs.junit4)
    testImplementation(libs.truth)
}
