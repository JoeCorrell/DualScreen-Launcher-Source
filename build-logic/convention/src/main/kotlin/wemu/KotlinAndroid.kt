package wemu

import com.android.build.api.dsl.CommonExtension
import org.gradle.api.JavaVersion
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.dsl.KotlinAndroidProjectExtension

/**
 * Opt-ins used across the project.
 *
 * `ExperimentalCoroutinesApi` is required by the `flatMapLatest` / `mapLatest`
 * operators the session and display plumbing is built on.
 */
private val LOKI_COMPILER_ARGS = listOf(
    "-opt-in=kotlin.RequiresOptIn",
    "-opt-in=kotlinx.coroutines.ExperimentalCoroutinesApi",
    "-opt-in=kotlinx.coroutines.FlowPreview",
)

/**
 * Shared Android + Kotlin configuration applied to every Android module.
 *
 * Note: these are plain `.kt` sources, not `.gradle.kts` scripts, so Gradle's
 * lazy-property assignment operator is unavailable — `Property.set()` is used
 * throughout.
 */
internal fun Project.configureKotlinAndroid(
    commonExtension: CommonExtension<*, *, *, *, *, *>,
) {
    commonExtension.apply {
        compileSdk = libs.intVersion("compileSdk")

        defaultConfig {
            minSdk = libs.intVersion("minSdk")
        }

        compileOptions {
            sourceCompatibility = JavaVersion.VERSION_17
            targetCompatibility = JavaVersion.VERSION_17
        }

        packaging {
            resources {
                excludes.add("/META-INF/{AL2.0,LGPL2.1}")
                excludes.add("/META-INF/LICENSE*")
                excludes.add("/META-INF/DEPENDENCIES")
                excludes.add("/META-INF/NOTICE*")
                excludes.add("META-INF/*.kotlin_module")
            }
        }

        testOptions {
            unitTests.isIncludeAndroidResources = true
            unitTests.isReturnDefaultValues = true
        }

        lint {
            // The engine targets API 28 while compiling against 35, so every
            // platform call newer than 28 is flagged. The engine guards them
            // itself; failing the build on it would mean annotating a few
            // hundred lines of code this fork did not write.
            abortOnError = false
            checkReleaseBuilds = false
        }
    }

    extensions.configure<KotlinAndroidProjectExtension> {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_17)
            freeCompilerArgs.addAll(LOKI_COMPILER_ARGS)
        }
    }
}
