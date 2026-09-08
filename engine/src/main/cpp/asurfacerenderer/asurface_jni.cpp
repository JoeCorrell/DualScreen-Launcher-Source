#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <unistd.h>
#include "ASurfaceRendererContext.h"
#include <mutex>
#include <shared_mutex>

static JavaVM* g_javaVm = nullptr;
static ASurfaceRendererContext* g_ctx = nullptr;
static std::shared_mutex g_ctxMutex;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_javaVm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeInit(
        JNIEnv* env, jobject, jobject surface, jint w, jint h)
{
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return JNI_FALSE;

    ASurfaceRendererContext* old;
    try {
        std::unique_lock lk(g_ctxMutex);
        auto* ctx = new ASurfaceRendererContext(win, w, h);
        ctx->javaVm = g_javaVm;
        old = g_ctx;
        g_ctx = ctx;
    } catch (...) {
        ANativeWindow_release(win);
        __android_log_print(ANDROID_LOG_ERROR, "ASurfaceRenderer",
                "nativeInit: native renderer construction failed");
        return JNI_FALSE;
    }
    delete old;

    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeDestroy(JNIEnv*, jobject) {
    std::unique_lock lk(g_ctxMutex);
    auto* ctx = g_ctx;
    if (!ctx) return;
    ctx->beginShutdown();
    g_ctx = nullptr;
    delete ctx;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeInitScanout(JNIEnv*, jobject) {
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->initScanout();
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeDestroyScanout(JNIEnv*, jobject) {
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->destroyScanout();
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeSetSfCallbackTarget(
        JNIEnv* env, jobject, jobject rendererRef)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->setSfCallbackTarget(env, rendererRef);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeScanoutSetCursorImage(
        JNIEnv* env, jobject, jobject buf, jshort w, jshort h, jshort stride)
{
    if (!buf || w <= 0 || h <= 0 || stride < w) return;
    void* px = env->GetDirectBufferAddress(buf);
    if (px && env->GetDirectBufferCapacity(buf) >= (jlong)stride * h * 4) {
        std::shared_lock lk(g_ctxMutex);
        if (auto* r = g_ctx) r->scanoutSetCursorImage(px, w, h, stride);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeScanoutSetCursorPos(
        JNIEnv*, jobject, jshort x, jshort y, jshort hotX, jshort hotY, jboolean cursorVisible)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->scanoutSetCursorPos(x, y, hotX, hotY, cursorVisible);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeScanoutSetDst(
        JNIEnv*, jobject, jint x, jint y, jint w, jint h)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->scanoutSetDst(x, y, w, h);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeReattachSurface(JNIEnv* env, jobject, jobject surface) {
    if (!surface) return JNI_FALSE;
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return JNI_FALSE;
    bool ok = false;
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) {
        ok = r->reattachSurface(win);
        if (ok && r->scanoutActive.load()) {
            r->destroyScanout();
        }
    } else ANativeWindow_release(win);
    return (jboolean)ok;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeRegisterWindowSC(
        JNIEnv* env, jobject, jlong contentId, jstring debugName)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) {
        const char* name = nullptr;
        if (debugName) name = env->GetStringUTFChars(debugName, nullptr);
        r->registerWindowSC((int64_t)contentId, name ? name : "(x11_window)");
        if (name) env->ReleaseStringUTFChars(debugName, name);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeUnregisterWindowSC(
        JNIEnv*, jobject, jlong contentId)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->unregisterWindowSC((int64_t)contentId);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeScanoutSetCursorVisibility(
        JNIEnv*, jobject, jboolean visible)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->scanoutSetCursorVisibility(visible);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativePrepareCpuSourceBuffers(
        JNIEnv*, jclass, jlong buffer0, jlong buffer1, jlong buffer2)
{
    std::shared_lock lk(g_ctxMutex);
    auto* r = g_ctx;
    if (!r) return JNI_FALSE;

    const bool ok = r->prepareCpuSourceBuffers(
            reinterpret_cast<AHardwareBuffer*>(buffer0),
            reinterpret_cast<AHardwareBuffer*>(buffer1),
            reinterpret_cast<AHardwareBuffer*>(buffer2));
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeReleaseCpuSourceBuffers(
        JNIEnv*, jclass, jlong buffer0, jlong buffer1, jlong buffer2)
{
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) {
        r->releaseCpuSourceBuffers(
                reinterpret_cast<AHardwareBuffer*>(buffer0),
                reinterpret_cast<AHardwareBuffer*>(buffer1),
                reinterpret_cast<AHardwareBuffer*>(buffer2));
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeSetWindowBuffer(
        JNIEnv* env, jobject, jlong contentId,
        jlong ahbPtr, jint fenceFd, jlong windowId, jlong serial, jobject ahbImage, jint slot,
        jboolean sfCompatMode, jint maximumConvertedBuffers)
{
    std::shared_lock lk(g_ctxMutex);
    auto* r = g_ctx;
    if (!r || !ahbPtr) { if (fenceFd >= 0) close(fenceFd); return JNI_FALSE; }

    return r->setWindowBuffer(
            env,
            (int64_t)contentId,
            reinterpret_cast<AHardwareBuffer*>(ahbPtr),
            (int)fenceFd,
            (int64_t)windowId,
            (int64_t)serial,
            ahbImage,
            (int)slot,
            (bool)sfCompatMode,
            static_cast<size_t>(maximumConvertedBuffers < 2 ? 2
                    : (maximumConvertedBuffers > 8 ? 8 : maximumConvertedBuffers)))
            ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_ASurfaceRenderer_nativeUpdateWindows(
        JNIEnv* env, jobject, jintArray packedRows, jint count) {
    if (!packedRows || count <= 0 || count > 65536 ||
        env->GetArrayLength(packedRows) / 11 < count) return;
    // Copy before locking. Never retain a pinned Java array across compositor
    // calls; context lifetime spans creation, filling and applying the batch.
    std::vector<int32_t> rows(static_cast<size_t>(count) * 11);
    env->GetIntArrayRegion(packedRows, 0, count * 11, rows.data());
    if (env->ExceptionCheck()) return;
    std::shared_lock lk(g_ctxMutex);
    if (auto* r = g_ctx) r->updateWindows(rows.data(), static_cast<size_t>(count));
}
