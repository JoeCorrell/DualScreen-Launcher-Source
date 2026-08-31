/*
 * The emulation stack, with its interface removed.
 *
 * What is here is the emulation stack: the X server and its protocol, the
 * container manager, the Wine/Box64/FEX plumbing, the Vulkan and GL renderers,
 * the input-control model, and the native library those are bound to. What is
 * not here is every fragment, dialog, adapter and layout that used to drive it —
 * that is `:app`'s job now, in Compose.
 *
 * The package was inherited from the project this forked from, and the reason
 * given for keeping it was that the native library exported symbols named after
 * it — JNI resolves a C function from a fully qualified Java name, so renaming
 * the package renames every entry point in the .so.
 *
 * That was true and it was not a reason to stop. The entry points were renamed
 * with the packages, one library at a time, checking the built .so each time
 * rather than trusting the build: a mismatch between the two halves compiles
 * perfectly and fails at runtime with an UnsatisfiedLinkError naming a method
 * that plainly exists.
 *
 * `applicationId` is a separate question and is still the one the device knows.
 * Every engine-owned path lives under /data/data/<applicationId>, so that one
 * cannot move without stranding every container on the device.
 */

plugins {
    alias(libs.plugins.wemu.android.library)
}

android {
    namespace = "com.wemu.engine"

    defaultConfig {
        externalNativeBuild {
            cmake {
                cppFlags("")
            }
        }

        // The prebuilt Wine, Box64 and driver payloads in `assets` are arm64
        // only, so a build for any other ABI would produce an APK that installs
        // and then cannot start a container.
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    ndkVersion = "29.0.14206865"

    // The engine unpacks its assets at runtime by streaming them out of the APK;
    // several are already compressed, and letting aapt try again wastes build
    // time and gains nothing.
    androidResources {
        noCompress += listOf("tzst", "zst", "so", "dat")
    }

    packaging {
        jniLibs {
            // The loader has to find these on disk: `libadrenotools` opens the
            // GPU driver by path to swap in a user-supplied one, which it cannot
            // do for a library that only exists inside the APK.
            useLegacyPackaging = true
        }
    }
}

/*
 * The two payloads too large to keep in a repository were downloaded here, at
 * build time, into `assets`. They are not any more.
 *
 * `imagefs` is the Linux root filesystem every container is built from and
 * Proton is the Wine build that runs inside it — 185 MB and 64 MB packed, and
 * about 1.5 GB unpacked. Baking them into the APK meant every install carried
 * them whether or not that device would ever create a container, and every
 * update re-shipped a quarter of a gigabyte to change a Compose file.
 *
 * `GuestPayloads` fetches them at first boot instead, from these same URLs and
 * in these same parts, and the installer unpacks them with exactly the code that
 * unpacked them from `assets`. An update over an existing install does not
 * re-download: the image is already unpacked and `ImageFsInstaller.LATEST_VERSION`
 * is what decides otherwise.
 *
 * The URLs live in `GuestPayloads` now. If they move, that is the one place to
 * change — a build that silently produced an APK without them was the failure
 * mode this arrangement removes.
 */

dependencies {
    // `api` throughout: `:app` drives these types directly — it holds a
    // `Container`, reads an `XServer`, and hands a `ControlsProfile` to the
    // input layer — so hiding them behind `implementation` would only mean
    // restating every one of them in `:app`'s own dependency list.
    api(libs.androidx.appcompat)
    api(libs.google.material)
    api(libs.androidx.preference)

    testImplementation(libs.junit4)

    // commons-compress reads the .tzst and .txz payloads. It finds the actual
    // codecs by class at runtime, so zstd-jni and xz have to be present even
    // though no source file names them.
    api(libs.commons.compress)
    runtimeOnly(libs.xz)

    // zstd-jni as an AAR, and the `@aar` is load-bearing.
    //
    // Its POM says `packaging=jar`, so a plain coordinate resolves the desktop
    // JAR: macOS dylibs, Windows DLLs, and a glibc linux/aarch64 build that
    // bionic cannot load. AGP then strips shared objects out of java resources,
    // so even the library's own extract-and-load fallback finds nothing.
    //
    // The result is an UnsatisfiedLinkError on the first
    // ZstdCompressorInputStream — an Error, so it goes straight past
    // TarCompressorUtils's `catch (IOException)` and surfaces as "the system
    // files could not be unpacked" before the app has drawn anything.
    //
    // The AAR carries jni/arm64-v8a/libzstd-jni-<version>.so, built against
    // bionic, which lands in lib/arm64-v8a/ and loads by name. A version
    // catalog cannot express an artifact type, hence the coordinate by hand.
    val zstd = libs.zstd.jni.get()
    runtimeOnly(
        "${zstd.module.group}:${zstd.module.name}:${zstd.versionConstraint.requiredVersion}@aar",
    )

    //
    // A soft synthesiser for MIDI audio was here, described as being for older
    // titles that use it for their music. It was never wired to anything: the
    // two classes that would have driven it, MidiHandler and MidiManager, were
    // referenced from nowhere in the app, the engine or the layouts, and a jar
    // is reachable only from Java. So MIDI has never played in this fork, and
    // the jar was 345KB on the classpath to prove it.
    //
    // Restoring the capability means writing the wiring, not restoring the
    // dependency; both are in the history if that day comes.
}
