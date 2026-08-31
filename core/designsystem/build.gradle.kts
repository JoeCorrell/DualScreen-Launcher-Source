plugins {
    alias(libs.plugins.wemu.android.library)
    alias(libs.plugins.wemu.android.library.compose)
}

android {
    namespace = "com.thor.core.designsystem"
}

dependencies {
    // `api`, not `implementation`: every consumer of the design system composes
    // against these model types by name — a screen cannot call `ThorTheme` and
    // then not be able to mention `CornerStyle`.
    api(projects.core.model)
    implementation(projects.core.common)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.compose.material.icons.extended)

    testImplementation(libs.junit4)
    testImplementation(libs.truth)
}
