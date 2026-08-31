plugins {
    alias(libs.plugins.wemu.android.library)
    alias(libs.plugins.wemu.android.library.compose)
}

android {
    namespace = "com.wemu.session.secondscreen"
}

dependencies {
    implementation(projects.core.designsystem)
    implementation(projects.core.common)

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.kotlinx.serialization.json)

    implementation("com.squareup.okhttp3:okhttp:5.3.2")

    testImplementation(libs.junit4)
}
