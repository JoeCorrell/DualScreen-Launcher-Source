#include <jni.h>

#include <cstddef>

#include "jni/sshd_companion.h"

// Keep this bridge independent from native.h and its full emulator dependency
// graph. The implementation is exported by libyuzu-android.so; binding its
// Itanium ABI symbol here gives the sidecar the same singleton reference
// without redeclaring EmulationSession's complete class definition.
extern EmulationSession& GetEmulationSessionInstance()
    __asm__("_ZN16EmulationSession11GetInstanceEv");

// Unique v4 entry points keep the embedded sidecar independent from the older
// SSHD JNI methods already present in the base emulator library.
extern "C" JNIEXPORT jint JNICALL
Java_org_yuzu_yuzu_1emu_NativeLibrary_fillSshdCompanionSnapshotV4(
    JNIEnv* env, [[maybe_unused]] jobject instance, jobject output) {
    if (!output) {
        return -1;
    }
    void* address = env->GetDirectBufferAddress(output);
    const jlong capacity = env->GetDirectBufferCapacity(output);
    if (!address || capacity <= 0) {
        return -1;
    }
    return SshdCompanion::FillSnapshot(GetEmulationSessionInstance(), address,
                                       static_cast<std::size_t>(capacity));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_yuzu_yuzu_1emu_NativeLibrary_copySshdCompanionMapImageV1(
    JNIEnv* env, [[maybe_unused]] jobject instance, jobject output) {
    if (!output) {
        return -1;
    }
    void* address = env->GetDirectBufferAddress(output);
    const jlong capacity = env->GetDirectBufferCapacity(output);
    if (!address || capacity <= 0) {
        return -1;
    }
    return SshdCompanion::CopyMapImage(GetEmulationSessionInstance(), address,
                                       static_cast<std::size_t>(capacity));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_yuzu_yuzu_1emu_NativeLibrary_performSshdCompanionActionV4(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject instance, jint action,
    jint argument) {
    return SshdCompanion::PerformAction(GetEmulationSessionInstance(), action, argument)
               ? JNI_TRUE
               : JNI_FALSE;
}
