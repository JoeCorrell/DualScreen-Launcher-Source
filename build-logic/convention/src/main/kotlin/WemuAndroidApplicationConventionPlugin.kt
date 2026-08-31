import com.android.build.api.dsl.ApplicationExtension
import wemu.configureKotlinAndroid
import wemu.intVersion
import wemu.libs
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

/**
 * The application module.
 *
 * `targetSdk` is set here and only here. It is the number that decides whether
 * the engine can execute the binaries it unpacks — see the catalog for why it
 * cannot move — so it belongs on the one module that actually declares it,
 * rather than being restated per build type where it could drift.
 */
class WemuAndroidApplicationConventionPlugin : Plugin<Project> {

    override fun apply(target: Project) = with(target) {
        pluginManager.apply("com.android.application")
        pluginManager.apply("org.jetbrains.kotlin.android")

        extensions.configure<ApplicationExtension> {
            configureKotlinAndroid(this)
            defaultConfig.targetSdk = libs.intVersion("targetSdk")
        }
    }
}
