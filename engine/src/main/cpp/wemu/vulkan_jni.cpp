#include <jni.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../adrenotools/include/adrenotools/driver.h"
#include "VulkanRendererContext.h"

static void* openAdrenotoolsDriver(const char* driverPath, const char* libraryName, const char* nativeLibDir) {
    if (!driverPath || !libraryName || !nativeLibDir) return nullptr;
    if (access(driverPath, F_OK) != 0) {
        __android_log_print(ANDROID_LOG_ERROR,"Wemu_Renderer",
            "openAdrenotoolsDriver: driverPath not accessible: %s", driverPath);
        return nullptr;
    }
    char tmpdir[512];
    snprintf(tmpdir, sizeof(tmpdir), "%stemp", driverPath);
    mkdir(tmpdir, S_IRWXU | S_IRWXG);
    __android_log_print(ANDROID_LOG_DEBUG,"Wemu_Renderer",
        "openAdrenotoolsDriver: driverPath=%s lib=%s nativeLibDir=%s tmp=%s",
        driverPath, libraryName, nativeLibDir, tmpdir);
    setenv("ADRENOTOOLS_DRIVER_PATH", driverPath, 1);
    setenv("ADRENOTOOLS_DRIVER_NAME", libraryName, 1);
    setenv("ADRENOTOOLS_HOOKS_PATH", nativeLibDir, 1);
    const char* redirectDir = getenv("ADRENOTOOLS_REDIRECT_DIR");
    int featureFlags = ADRENOTOOLS_DRIVER_CUSTOM;
    if (redirectDir && redirectDir[0] != '\0') {
        featureFlags |= ADRENOTOOLS_DRIVER_FILE_REDIRECT;
    } else {
        unsetenv("ADRENOTOOLS_DRIVER_FILE_REDIRECT");
    }
    void* handle = adrenotools_open_libvulkan(
        RTLD_LOCAL | RTLD_NOW,
        featureFlags,
        tmpdir,
        nativeLibDir,
        driverPath,
        libraryName,
        (redirectDir && redirectDir[0] != '\0') ? redirectDir : nullptr,
        nullptr);
    if (!handle) {
        __android_log_print(ANDROID_LOG_ERROR,"Wemu_Renderer",
            "openAdrenotoolsDriver: adrenotools_open_libvulkan failed");
    } else {
        __android_log_print(ANDROID_LOG_DEBUG,"Wemu_Renderer",
            "openAdrenotoolsDriver: SUCCESS handle=%p", handle);
    }
    return handle;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeInit(
    JNIEnv* env, jobject, jobject surface, jint w, jint h,
    jstring jDriverPath, jstring jLibraryName, jstring jNativeLibDir)
{
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return 0;
    void* adrenotoolsHandle = nullptr;
    if (jDriverPath && jLibraryName && jNativeLibDir) {
        const char* dp  = env->GetStringUTFChars(jDriverPath,   nullptr);
        const char* lib = env->GetStringUTFChars(jLibraryName,  nullptr);
        const char* nld = env->GetStringUTFChars(jNativeLibDir, nullptr);
        adrenotoolsHandle = openAdrenotoolsDriver(dp, lib, nld);
        env->ReleaseStringUTFChars(jDriverPath,   dp);
        env->ReleaseStringUTFChars(jLibraryName,  lib);
        env->ReleaseStringUTFChars(jNativeLibDir, nld);
    }
    try { return reinterpret_cast<jlong>(new VulkanRendererContext(win, w, h, adrenotoolsHandle)); }
    catch (...) {
        ANativeWindow_release(win);
        if (adrenotoolsHandle) dlclose(adrenotoolsHandle);
        return 0;
    }
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeResize(JNIEnv*, jobject, jlong h, jint w, jint ht) {
    auto* r=reinterpret_cast<VulkanRendererContext*>(h); if (r) r->onSurfaceResized(w,ht);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeDestroy(JNIEnv*, jobject, jlong h) {
    auto* renderer = reinterpret_cast<VulkanRendererContext*>(h);
    if (!renderer) return;
    if (!renderer->shutdown(2000)) {
        /*
         * A detached worker or in-flight GPU submission still references the
         * context. Leaking it is bounded to this failed session and is safer
         * than freeing memory under a vendor thread, which previously crashed
         * the process and blocked every later cleanup.
         */
        __android_log_print(ANDROID_LOG_ERROR, "Wemu_Renderer",
                "nativeDestroy: renderer did not quiesce; retaining native context");
        return;
    }
    delete renderer;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeUpdateWindowContent(
    JNIEnv* env, jobject, jlong handle, jlong id, jobject buf, jshort w, jshort h, jshort stride, jint x, jint y)
{
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r||!buf) return JNI_FALSE;
    if (w <= 0 || h <= 0) return JNI_FALSE;
    const int effectiveStride = stride > 0 ? stride : w;
    if (effectiveStride < w) return JNI_FALSE;
    void* px=env->GetDirectBufferAddress(buf);
    const jlong required = static_cast<jlong>(effectiveStride) * h * 4;
    if (px && required > 0 && env->GetDirectBufferCapacity(buf)>=required)
        return r->updateWindowContent(id,px,w,h,stride,x,y) ? JNI_TRUE : JNI_FALSE;
    return JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeUpdateWindowContentAHB(
    JNIEnv*, jobject, jlong handle, jlong id, jlong ahbPtr, jobject,
    jshort w, jshort h, jshort, jint x, jint y)
{
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r || !ahbPtr) return JNI_FALSE;
    return r->updateWindowContentAHB(id,reinterpret_cast<AHardwareBuffer*>(ahbPtr),w,h,x,y)
            ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetTransformAndScissor(
    JNIEnv*, jobject, jlong handle,
    jfloat ox, jfloat oy, jfloat sx, jfloat sy,
    jboolean hasScissor, jint scX, jint scY, jint scW, jint scH)
{
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setTransformAndScissor(ox, oy, sx, sy, (bool)hasScissor, scX, scY, scW, scH);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetPointerPos(JNIEnv*, jobject, jlong handle, jshort x, jshort y) {
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle); if (r) r->updatePointerPosition(x,y);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetCursorVisible(JNIEnv*, jobject, jlong handle, jboolean v) {
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle); if (r) r->setCursorVisible(v);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetRootColor(JNIEnv*, jobject, jlong handle, jint argb) {
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle); if (r) r->setRootColor(argb);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeUpdateCursorImage(
    JNIEnv* env, jobject, jlong handle, jobject buf, jshort w, jshort h, jshort hotX, jshort hotY)
{
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r||!buf || w <= 0 || h <= 0) return;
    void* px=env->GetDirectBufferAddress(buf);
    if (px && env->GetDirectBufferCapacity(buf)>=(jlong)w*h*4)
        r->updateCursorImage(px,w,h,hotX,hotY);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetRenderList(
    JNIEnv* env, jobject, jlong handle, jlongArray jids, jintArray jxs, jintArray jys, jint count)
{
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r) return;
    if (count<=0||!jids||!jxs||!jys) {
        r->setRenderList(nullptr, nullptr, nullptr, 0);
        return;
    }
    if (env->GetArrayLength(jids) < count || env->GetArrayLength(jxs) < count
            || env->GetArrayLength(jys) < count) {
        __android_log_print(ANDROID_LOG_ERROR,"Wemu_Renderer",
                "nativeSetRenderList: array shorter than count=%d", count);
        return;
    }

    std::vector<jlong> ids(static_cast<size_t>(count));
    std::vector<jint> xs(static_cast<size_t>(count));
    std::vector<jint> ys(static_cast<size_t>(count));
    env->GetLongArrayRegion(jids, 0, count, ids.data());
    env->GetIntArrayRegion(jxs, 0, count, xs.data());
    env->GetIntArrayRegion(jys, 0, count, ys.data());
    if (env->ExceptionCheck()) return;
    std::vector<int64_t> idsCopy(static_cast<size_t>(count));
    std::vector<int> xsCopy(static_cast<size_t>(count));
    std::vector<int> ysCopy(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        idsCopy[static_cast<size_t>(i)] = static_cast<int64_t>(ids[static_cast<size_t>(i)]);
        xsCopy[static_cast<size_t>(i)] = static_cast<int>(xs[static_cast<size_t>(i)]);
        ysCopy[static_cast<size_t>(i)] = static_cast<int>(ys[static_cast<size_t>(i)]);
    }
    r->setRenderList(idsCopy.data(),xsCopy.data(),ysCopy.data(),count);
}
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeRemoveWindow(JNIEnv*, jobject, jlong handle, jlong id) {
    auto* r=reinterpret_cast<VulkanRendererContext*>(handle); if (r) r->removeWindow(id);
}


extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetVerboseLog(JNIEnv*, jobject, jlong handle, jboolean v) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setVerboseLog((bool)v);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeDumpRendererInfo(JNIEnv*, jobject, jlong handle) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->dumpRendererInfo();
}


extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetFilterMode(JNIEnv*, jobject, jlong handle, jint mode) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setFilterMode((int)mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetStretchMode(JNIEnv*, jobject, jlong handle, jint mode) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setStretchMode((int)mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetPostFXMode(JNIEnv*, jobject, jlong handle, jint mode) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setPostFXMode((int)mode);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetSharpness(JNIEnv*, jobject, jlong handle, jfloat s) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setSharpness((float)s);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetSwapRB(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setSwapRB(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeSetPresentMode(JNIEnv*, jobject, jlong handle, jint mode) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->setPresentMode((VkPresentModeKHR)mode);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeGetSupportedPresentModes(JNIEnv* env, jobject, jlong handle) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r) return env->NewIntArray(0);
    auto modes = r->getSupportedPresentModes();
    jintArray arr = env->NewIntArray((jsize)modes.size());
    if (!arr) return nullptr;
    if (!modes.empty()) env->SetIntArrayRegion(arr,0,(jsize)modes.size(),modes.data());
    return arr;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeGetSwapchainSize(JNIEnv* env, jobject, jlong handle) {
    jintArray arr = env->NewIntArray(2);
    if (!arr) return nullptr;
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r) return arr;
    VkExtent2D ext = r->getSwapchainExtent();
    jint vals[2] = {(jint)ext.width, (jint)ext.height};
    env->SetIntArrayRegion(arr, 0, 2, vals);
    return arr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeDetachSurface(JNIEnv*, jobject, jlong handle) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (r) r->detachSurface();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_VulkanRenderer_nativeReattachSurface(JNIEnv* env, jobject, jlong handle, jobject surface) {
    auto* r = reinterpret_cast<VulkanRendererContext*>(handle);
    if (!r || !surface) return JNI_FALSE;
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return JNI_FALSE;
    bool ok = r->reattachSurface(win);
    return (jboolean)ok;
}
