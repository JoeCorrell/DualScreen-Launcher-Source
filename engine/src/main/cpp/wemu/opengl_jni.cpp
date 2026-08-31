#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include <vector>

#include "OpenGLRendererContext.h"

namespace {
inline OpenGLRendererContext* renderer(jlong handle) {
    return reinterpret_cast<OpenGLRendererContext*>(handle);
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeInit(
        JNIEnv* env, jobject, jobject surface, jint width, jint height,
        jstring, jstring, jstring) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return 0;
    try {
        return reinterpret_cast<jlong>(new OpenGLRendererContext(window, width, height));
    } catch (...) {
        // The context consumes the ANativeWindow on every constructor path.
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeResize(
        JNIEnv*, jobject, jlong handle, jint width, jint height) {
    if (auto* r = renderer(handle)) r->onSurfaceResized(width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeDestroy(
        JNIEnv*, jobject, jlong handle) {
    auto* context = renderer(handle);
    if (!context) return;
    if (!context->shutdown(2000)) {
        // A vendor eglSwapBuffers call cannot be cancelled safely. Retaining
        // this failed-session context avoids both a Java teardown hang and a
        // detached worker dereferencing freed renderer state.
        __android_log_print(ANDROID_LOG_ERROR, "Wemu_OpenGL",
                "nativeDestroy: renderer did not quiesce; retaining native context");
        return;
    }
    delete context;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeDetachSurface(
        JNIEnv*, jobject, jlong handle) {
    if (auto* r = renderer(handle)) r->detachSurface();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeReattachSurface(
        JNIEnv* env, jobject, jlong handle, jobject surface) {
    auto* r = renderer(handle);
    if (!r || !surface) return JNI_FALSE;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    return r->reattachSurface(window) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeGetSwapchainSize(
        JNIEnv* env, jobject, jlong handle) {
    jintArray result = env->NewIntArray(2);
    if (!result) return nullptr;
    int width = 0, height = 0;
    if (auto* r = renderer(handle)) r->getSurfaceSize(width, height);
    const jint values[] = {width, height};
    env->SetIntArrayRegion(result, 0, 2, values);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeUpdateWindowContent(
        JNIEnv* env, jobject, jlong handle, jlong id, jobject buffer,
        jshort width, jshort height, jshort stride, jint x, jint y) {
    auto* r = renderer(handle);
    if (!r || !buffer || width <= 0 || height <= 0) return JNI_FALSE;
    void* pixels = env->GetDirectBufferAddress(buffer);
    const int effectiveStride = stride > 0 ? stride : width;
    if (effectiveStride < width) return JNI_FALSE;
    const jlong required = static_cast<jlong>(effectiveStride) * height * 4;
    if (pixels && env->GetDirectBufferCapacity(buffer) >= required)
        return r->updateWindowContent(id, pixels, width, height, stride, x, y)
                ? JNI_TRUE : JNI_FALSE;
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeUpdateWindowContentAHB(
        JNIEnv* env, jobject, jlong handle, jlong id, jlong buffer, jobject fallback,
        jshort width, jshort height, jshort fallbackStride, jint x, jint y) {
    auto* r = renderer(handle);
    if (!r || !buffer || width <= 0 || height <= 0) return JNI_FALSE;
    const int effectiveStride = fallbackStride > 0 ? fallbackStride : width;
    const jlong required = static_cast<jlong>(effectiveStride) * height * 4;
    void* fallbackPixels = nullptr;
    if (fallback && effectiveStride >= width
            && env->GetDirectBufferCapacity(fallback) >= required) {
        fallbackPixels = env->GetDirectBufferAddress(fallback);
    }
    return r->updateWindowContentAHB(id, reinterpret_cast<AHardwareBuffer*>(buffer),
            fallbackPixels, width, height, fallbackStride, x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetTransformAndScissor(
        JNIEnv*, jobject, jlong handle, jfloat ox, jfloat oy, jfloat sx, jfloat sy,
        jboolean hasScissor, jint x, jint y, jint width, jint height) {
    if (auto* r = renderer(handle)) r->setTransformAndScissor(
        ox, oy, sx, sy, hasScissor == JNI_TRUE, x, y, width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetPointerPos(
        JNIEnv*, jobject, jlong handle, jshort x, jshort y) {
    if (auto* r = renderer(handle)) r->updatePointerPosition(x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetCursorVisible(
        JNIEnv*, jobject, jlong handle, jboolean visible) {
    if (auto* r = renderer(handle)) r->setCursorVisible(visible == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetRootColor(
        JNIEnv*, jobject, jlong handle, jint color) {
    if (auto* r = renderer(handle)) r->setRootColor(color);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeUpdateCursorImage(
        JNIEnv* env, jobject, jlong handle, jobject buffer, jshort width, jshort height,
        jshort hotX, jshort hotY) {
    auto* r = renderer(handle);
    if (!r || !buffer || width <= 0 || height <= 0) return;
    void* pixels = env->GetDirectBufferAddress(buffer);
    if (pixels && env->GetDirectBufferCapacity(buffer) >= static_cast<jlong>(width) * height * 4)
        r->updateCursorImage(pixels, width, height, hotX, hotY);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetRenderList(
        JNIEnv* env, jobject, jlong handle, jlongArray ids, jintArray xs,
        jintArray ys, jint count) {
    auto* r = renderer(handle);
    if (!r) return;
    if (count <= 0 || !ids || !xs || !ys) {
        r->setRenderList(nullptr, nullptr, nullptr, 0);
        return;
    }
    if (env->GetArrayLength(ids) < count || env->GetArrayLength(xs) < count
            || env->GetArrayLength(ys) < count) return;
    std::vector<jlong> idValues(static_cast<size_t>(count));
    std::vector<jint> xValues(static_cast<size_t>(count));
    std::vector<jint> yValues(static_cast<size_t>(count));
    env->GetLongArrayRegion(ids, 0, count, idValues.data());
    env->GetIntArrayRegion(xs, 0, count, xValues.data());
    env->GetIntArrayRegion(ys, 0, count, yValues.data());
    if (env->ExceptionCheck()) return;
    std::vector<int64_t> nativeIds(static_cast<size_t>(count));
    std::vector<int> nativeXs(static_cast<size_t>(count));
    std::vector<int> nativeYs(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        nativeIds[static_cast<size_t>(i)] = static_cast<int64_t>(idValues[static_cast<size_t>(i)]);
        nativeXs[static_cast<size_t>(i)] = static_cast<int>(xValues[static_cast<size_t>(i)]);
        nativeYs[static_cast<size_t>(i)] = static_cast<int>(yValues[static_cast<size_t>(i)]);
    }
    r->setRenderList(nativeIds.data(), nativeXs.data(), nativeYs.data(), count);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeRemoveWindow(
        JNIEnv*, jobject, jlong handle, jlong id) {
    if (auto* r = renderer(handle)) r->removeWindow(id);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetVerboseLog(
        JNIEnv*, jobject, jlong handle, jboolean verbose) {
    if (auto* r = renderer(handle)) r->setVerboseLog(verbose == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeDumpRendererInfo(
        JNIEnv*, jobject, jlong handle) {
    if (auto* r = renderer(handle)) r->dumpRendererInfo();
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetFilterMode(
        JNIEnv*, jobject, jlong handle, jint mode) {
    if (auto* r = renderer(handle)) r->setFilterMode(mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetStretchMode(
        JNIEnv*, jobject, jlong handle, jint mode) {
    if (auto* r = renderer(handle)) r->setStretchMode(mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetPostFXMode(
        JNIEnv*, jobject, jlong handle, jint mode) {
    if (auto* r = renderer(handle)) r->setPostFXMode(mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetSharpness(
        JNIEnv*, jobject, jlong handle, jfloat sharpness) {
    if (auto* r = renderer(handle)) r->setSharpness(sharpness);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetSwapRB(
        JNIEnv*, jobject, jlong handle, jboolean enabled) {
    if (auto* r = renderer(handle)) r->setSwapRB(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeSetPresentMode(
        JNIEnv*, jobject, jlong handle, jint mode) {
    if (auto* r = renderer(handle)) r->setPresentMode(mode);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_wemu_engine_renderer_OpenGLRenderer_nativeGetSupportedPresentModes(
        JNIEnv* env, jobject, jlong handle) {
    std::vector<int> modes;
    if (auto* r = renderer(handle)) modes = r->getSupportedPresentModes();
    jintArray result = env->NewIntArray(static_cast<jsize>(modes.size()));
    if (!result) return nullptr;
    if (!modes.empty()) env->SetIntArrayRegion(result, 0, static_cast<jsize>(modes.size()), modes.data());
    return result;
}
