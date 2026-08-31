import com.android.build.api.dsl.LibraryExtension
import wemu.configureKotlinAndroid
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

/**
 * An Android library module.
 *
 * Deliberately does not set `targetSdk`: a library's value is ignored at merge
 * time, and setting it invites the belief that a module can opt out of the
 * application's. It cannot.
 */
class WemuAndroidLibraryConventionPlugin : Plugin<Project> {

    override fun apply(target: Project) = with(target) {
        pluginManager.apply("com.android.library")
        pluginManager.apply("org.jetbrains.kotlin.android")

        extensions.configure<LibraryExtension> {
            configureKotlinAndroid(this)

            // Nothing here ships instrumented tests, and the release variant of
            // a library is the only one the app consumes.
            defaultConfig.consumerProguardFiles("consumer-rules.pro")
        }
    }
}
