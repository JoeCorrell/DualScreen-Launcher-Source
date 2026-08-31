plugins {
    alias(libs.plugins.wemu.android.library)
    alias(libs.plugins.wemu.android.library.compose)
}

android {
    namespace = "com.thor.core.display"
}

dependencies {
    api(projects.core.model)
    implementation(projects.core.common)
    implementation(projects.core.designsystem)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)

    testImplementation(libs.junit4)
    testImplementation(libs.truth)
}
