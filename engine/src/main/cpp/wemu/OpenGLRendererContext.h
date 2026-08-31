#pragma once

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * GLES 3 compositor for the X server.
 *
 * EGL and every GL object live on worker_. Public calls only update a guarded
 * scene snapshot and wake that worker. This is important because X damage,
 * Surface callbacks and teardown arrive on different Java threads.
 */
class OpenGLRendererContext final {
public:
    OpenGLRendererContext(ANativeWindow* window, int containerWidth, int containerHeight);
    ~OpenGLRendererContext();

    OpenGLRendererContext(const OpenGLRendererContext&) = delete;
    OpenGLRendererContext& operator=(const OpenGLRendererContext&) = delete;

    bool shutdown(uint32_t timeoutMs);
    void onSurfaceResized(int width, int height);
    void detachSurface();
    bool reattachSurface(ANativeWindow* window);
    void getSurfaceSize(int& width, int& height) const;

    bool updateWindowContent(int64_t id, const void* pixels, short width, short height,
                             short stride, int x, int y);
    bool updateWindowContentAHB(int64_t id, AHardwareBuffer* buffer,
                                const void* fallbackPixels, short width,
                                short height, short fallbackStride, int x, int y);
    void updateCursorImage(const void* pixels, short width, short height,
                           short hotX, short hotY);
    void updatePointerPosition(short x, short y);
    void setCursorVisible(bool visible);
    void setRootColor(int argb);
    void setRenderList(const int64_t* ids, const int* xs, const int* ys, int count);
    void removeWindow(int64_t id);
    void setTransformAndScissor(float offsetX, float offsetY, float scaleX, float scaleY,
                                bool hasScissor, int x, int y, int width, int height);

    void setVerboseLog(bool value);
    void dumpRendererInfo();
    void setFilterMode(int mode);
    void setStretchMode(int mode);
    void setPostFXMode(int mode);
    void setSharpness(float value);
    void setSwapRB(bool enabled);
    void setPresentMode(int mode);
    std::vector<int> getSupportedPresentModes() const;

private:
    struct RenderEntry { int64_t id; int x; int y; };
    struct CpuTexture {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        int storageWidth = 0;
        int storageHeight = 0;
        bool dirty = false;
        std::vector<uint8_t> pixels;
    };
    struct ImportedTexture {
        GLuint texture = 0;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        AHardwareBuffer* buffer = nullptr;
        int width = 0;
        int height = 0;
        bool sourceNeedsRedBlueSwap = true;
    };
    struct WindowTexture {
        CpuTexture cpu;
        AHardwareBuffer* activeBuffer = nullptr;
    };

    void workerMain();
    bool initializeEgl(ANativeWindow* window);
    bool createWindowSurface(ANativeWindow* window);
    void destroyWindowSurface();
    bool createGlObjects();
    void destroyGlObjects();
    void destroyEgl();
    void renderFrame();
    void requestRenderLocked();
    void uploadCpuTexture(CpuTexture& texture);
    ImportedTexture* importBuffer(AHardwareBuffer* buffer);
    void releaseWindowBuffers(int64_t id);
    void destroyImportedTexture(ImportedTexture& imported);
    void drawTexture(GLuint texture, int x, int y, int width, int height,
                     bool sourceNeedsRedBlueSwap, bool alpha, bool applyEffects);
    void applySampler(GLuint texture) const;
    static GLuint compileShader(GLenum type, const char* source);
    static GLuint linkProgram(GLuint vertex, GLuint fragment);
    static bool hasExtension(const char* list, const char* extension);
    bool lockScene(std::unique_lock<std::timed_mutex>& lock, const char* operation) const;

    const int containerWidth_;
    const int containerHeight_;

    mutable std::timed_mutex mutex_;
    std::condition_variable_any cv_;
    std::condition_variable_any lifecycleCv_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> rendererFailed_{false};
    std::mutex exitMutex_;
    std::condition_variable exitCv_;
    bool workerExited_ = false;
    bool dirty_ = false;
    bool initialized_ = false;
    bool initFinished_ = false;
    bool surfaceDetached_ = false;

    ANativeWindow* pendingWindow_ = nullptr;
    bool detachRequested_ = false;
    bool attachRequested_ = false;
    uint64_t lifecycleRequest_ = 0;
    uint64_t lifecycleCompleted_ = 0;
    bool lastAttachResult_ = false;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    ANativeWindow* window_ = nullptr;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID_ = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = nullptr;
    bool ahbImportSupported_ = false;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint rectLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint sourceSizeLocation_ = -1;
    GLint filterModeLocation_ = -1;
    GLint swapRBLocation_ = -1;
    GLint alphaLocation_ = -1;
    GLint stretchLocation_ = -1;
    GLint postFXLocation_ = -1;
    GLint sharpnessLocation_ = -1;

    int surfaceWidth_ = 0;
    int surfaceHeight_ = 0;
    int pendingWidth_ = 0;
    int pendingHeight_ = 0;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    bool hasScissor_ = false;
    int scissorX_ = 0;
    int scissorY_ = 0;
    int scissorWidth_ = 0;
    int scissorHeight_ = 0;
    short pointerX_ = 0;
    short pointerY_ = 0;
    bool cursorVisible_ = false;
    short cursorHotX_ = 0;
    short cursorHotY_ = 0;
    CpuTexture cursor_;
    int rootColor_ = 0;
    int filterMode_ = 0;
    int stretchMode_ = 0;
    int postFXMode_ = 0;
    float sharpness_ = 0.5f;
    bool swapRB_ = false;
    int presentMode_ = 2;
    int appliedPresentMode_ = -1;
    bool mailboxSupported_ = false;
    bool samplerDirty_ = false;
    bool verbose_ = false;

    std::unordered_map<int64_t, WindowTexture> textures_;
    std::unordered_map<AHardwareBuffer*, ImportedTexture> imported_;
    std::unordered_set<AHardwareBuffer*> pendingAcquiredBuffers_;
    std::unordered_map<int64_t, std::unordered_set<AHardwareBuffer*>> windowBuffers_;
    std::vector<RenderEntry> renderList_;
    std::vector<GLuint> deferredTextureDeletes_;
    std::vector<AHardwareBuffer*> deferredBufferDeletes_;
    std::string glVendor_;
    std::string glRenderer_;
    std::string glVersion_;
};
