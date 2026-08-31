#include "OpenGLRendererContext.h"
#include "OpenGLPixelFormat.h"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#define GLR_TAG "Wemu_OpenGL"
#define GLR_E(...) __android_log_print(ANDROID_LOG_ERROR, GLR_TAG, __VA_ARGS__)
#define GLR_W(...) __android_log_print(ANDROID_LOG_WARN,  GLR_TAG, __VA_ARGS__)
#define GLR_I(...) __android_log_print(ANDROID_LOG_INFO,  GLR_TAG, __VA_ARGS__)

namespace {
constexpr int kPresentMailbox = 1;
constexpr int kPresentFifo = 2;
constexpr auto kSceneLockTimeout = std::chrono::milliseconds(250);
constexpr auto kLifecycleLockTimeout = std::chrono::milliseconds(500);
constexpr auto kLifecycleWaitTimeout = std::chrono::milliseconds(1000);
constexpr auto kInitializationWaitTimeout = std::chrono::milliseconds(2500);

constexpr char kVertexShader[] = R"GLSL(#version 300 es
precision highp float;
uniform vec4 uRect; // left, top, right, bottom in GL clip space
out highp vec2 vTexCoord;
void main() {
    if (gl_VertexID == 0) {
        gl_Position = vec4(uRect.x, uRect.y, 0.0, 1.0);
        vTexCoord = vec2(0.0, 0.0);
    } else if (gl_VertexID == 1) {
        gl_Position = vec4(uRect.x, uRect.w, 0.0, 1.0);
        vTexCoord = vec2(0.0, 1.0);
    } else if (gl_VertexID == 2) {
        gl_Position = vec4(uRect.z, uRect.y, 0.0, 1.0);
        vTexCoord = vec2(1.0, 0.0);
    } else {
        gl_Position = vec4(uRect.z, uRect.w, 0.0, 1.0);
        vTexCoord = vec2(1.0, 1.0);
    }
}
)GLSL";

/*
 * One shader covers the normal compositor and its inexpensive screen effects.
 * Keeping the path single-pass is intentional on a mobile tile renderer: an
 * intermediate full-screen target would cost bandwidth even when the source
 * window already matches the panel.
 */
constexpr char kFragmentShader[] = R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uTexture;
uniform vec2 uSourceSize;
uniform int uFilterMode;
uniform int uSwapRB;
uniform int uUseAlpha;
uniform int uStretchMode;
uniform int uPostFX;
uniform float uSharpness;
in highp vec2 vTexCoord;
layout(location = 0) out vec4 outColor;

vec2 sourceUv() {
    if (uStretchMode == 0) return vTexCoord;
    float x = vTexCoord.x * 2.0 - 1.0;
    float curved = sin(1.57079632679 * x);
    return vec2(clamp(mix(x, curved, 0.22) * 0.5 + 0.5, 0.0, 1.0), vTexCoord.y);
}

vec4 sampleSource(vec2 uv) {
    vec4 c = texture(uTexture, clamp(uv, vec2(0.0), vec2(1.0)));
    return uSwapRB != 0 ? c.bgra : c;
}

vec3 reconstructed(vec2 uv) {
    vec3 c = sampleSource(uv).rgb;
    if (uFilterMode < 2) return c;
    vec2 t = 1.0 / max(uSourceSize, vec2(1.0));
    vec3 n = sampleSource(uv + vec2(0.0, -t.y)).rgb;
    vec3 s = sampleSource(uv + vec2(0.0,  t.y)).rgb;
    vec3 w = sampleSource(uv + vec2(-t.x, 0.0)).rgb;
    vec3 e = sampleSource(uv + vec2( t.x, 0.0)).rgb;
    vec3 ring = (n + s + w + e) * 0.25;
    float amount = uFilterMode == 2 ? 0.20 : (uFilterMode == 3 ? 0.34 : 0.27);
    amount *= 0.35 + 0.65 * clamp(uSharpness, 0.0, 1.0);
    vec3 result = c + (c - ring) * amount;
    if (uFilterMode == 4) {
        vec3 d = sampleSource(uv + vec2(-t.x, -t.y)).rgb
               + sampleSource(uv + vec2( t.x, -t.y)).rgb
               + sampleSource(uv + vec2(-t.x,  t.y)).rgb
               + sampleSource(uv + vec2( t.x,  t.y)).rgb;
        result += (c - d * 0.25) * amount * 0.25;
    }
    return clamp(result, 0.0, 1.0);
}

vec3 applyEffect(vec2 uv, vec3 c) {
    if (uPostFX == 1) {
        float con = 1.0 + uSharpness * 0.12;
        float sat = 1.0 + uSharpness * 0.20;
        c = clamp((c - 0.5) * con + 0.5, 0.0, 1.0);
        float gray = dot(c, vec3(0.299, 0.587, 0.114));
        return clamp(mix(vec3(gray), c, sat), 0.0, 1.0);
    }
    if (uPostFX == 2) {
        float scan = 0.82 + 0.18 * sin(uv.y * uSourceSize.y * 3.14159265);
        vec3 ca = c;
        vec2 d = vec2(0.0025, 0.0);
        ca.r = reconstructed(clamp(uv * 1.0025 - d, 0.0, 1.0)).r;
        ca.b = reconstructed(clamp(uv / 1.0025 + d, 0.0, 1.0)).b;
        return ca * scan;
    }
    if (uPostFX == 3) {
        return clamp(pow(max(c, vec3(0.0)), vec3(0.90)) * 1.06, 0.0, 1.0);
    }
    if (uPostFX == 4) {
        float y = dot(c, vec3(0.299, 0.587, 0.114));
        return clamp(mix(vec3(y), c, 1.12) * vec3(1.02, 1.0, 0.98), 0.0, 1.0);
    }
    return c;
}

void main() {
    vec2 uv = sourceUv();
    vec4 source = sampleSource(uv);
    vec3 rgb = applyEffect(uv, reconstructed(uv));
    outColor = vec4(rgb, uUseAlpha != 0 ? source.a : 1.0);
}
)GLSL";
}

OpenGLRendererContext::OpenGLRendererContext(ANativeWindow* window, int containerWidth,
                                             int containerHeight)
    : containerWidth_(std::max(containerWidth, 1)),
      containerHeight_(std::max(containerHeight, 1)),
      pendingWindow_(window) {
    if (!window) throw std::runtime_error("OpenGL renderer has no window");
    try {
        worker_ = std::thread(&OpenGLRendererContext::workerMain, this);
    } catch (...) {
        ANativeWindow_release(pendingWindow_);
        pendingWindow_ = nullptr;
        throw;
    }
    const auto initDeadline = std::chrono::steady_clock::now() + kInitializationWaitTimeout;
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_until(initDeadline)
            || !lifecycleCv_.wait_until(lock, initDeadline,
                                       [this] { return initFinished_; })) {
        // The object and worker remain owned together. Returning a provisional
        // handle is safe; public calls use timed locks, and nativeDestroy will
        // retain the context if the vendor call never comes back.
        GLR_E("OpenGL initialization exceeded 2500 ms; returning provisional renderer");
        return;
    }
    if (!initialized_) {
        stopping_.store(true, std::memory_order_release);
        lock.unlock();
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        throw std::runtime_error("Could not initialize EGL/OpenGL ES 3 renderer");
    }
}

OpenGLRendererContext::~OpenGLRendererContext() {
    // JNI only deletes after a successful bounded shutdown. Keep a blocking
    // last resort for exceptional direct/native construction paths: destroying
    // a joinable std::thread would terminate the process, while detaching it
    // would let the worker access a freed context.
    if (!shutdown(2000) && worker_.joinable()) worker_.join();
}

bool OpenGLRendererContext::shutdown(uint32_t timeoutMs) {
    stopping_.store(true, std::memory_order_release);
    cv_.notify_all();
    lifecycleCv_.notify_all();
    {
        std::unique_lock<std::mutex> exitLock(exitMutex_);
        if (!exitCv_.wait_for(exitLock, std::chrono::milliseconds(timeoutMs),
                             [this] { return workerExited_; })) {
            GLR_E("Renderer worker did not stop within %u ms", timeoutMs);
            return false;
        }
    }
    if (worker_.joinable()) worker_.join();
    return true;
}

bool OpenGLRendererContext::lockScene(std::unique_lock<std::timed_mutex>& lock,
                                      const char* operation) const {
    if (lock.try_lock_for(kSceneLockTimeout)) return true;
    GLR_W("%s: renderer busy for %lld ms; dropping request", operation,
          static_cast<long long>(kSceneLockTimeout.count()));
    return false;
}

bool OpenGLRendererContext::hasExtension(const char* list, const char* extension) {
    if (!list || !extension || !*extension || std::strchr(extension, ' ')) return false;
    const size_t length = std::strlen(extension);
    for (const char* p = list; (p = std::strstr(p, extension)); p += length) {
        if ((p == list || p[-1] == ' ') && (p[length] == '\0' || p[length] == ' ')) return true;
    }
    return false;
}

GLuint OpenGLRendererContext::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    GLR_E("Shader compilation failed: %s", log.data());
    glDeleteShader(shader);
    return 0;
}

GLuint OpenGLRendererContext::linkProgram(GLuint vertex, GLuint fragment) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return program;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
    glGetProgramInfoLog(program, length, nullptr, log.data());
    GLR_E("Program link failed: %s", log.data());
    glDeleteProgram(program);
    return 0;
}

bool OpenGLRendererContext::initializeEgl(ANativeWindow* window) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        GLR_E("eglInitialize failed: 0x%x", eglGetError());
        ANativeWindow_release(window);
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint count = 0;
    if (!eglChooseConfig(display_, configAttributes, &config_, 1, &count) || count != 1) {
        GLR_E("No RGBA8 GLES3 window config: 0x%x", eglGetError());
        ANativeWindow_release(window);
        return false;
    }
    EGLint minimumSwapInterval = 1;
    eglGetConfigAttrib(display_, config_, EGL_MIN_SWAP_INTERVAL, &minimumSwapInterval);
    mailboxSupported_ = minimumSwapInterval <= 0;
    const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
        GLR_E("eglCreateContext failed: 0x%x", eglGetError());
        ANativeWindow_release(window);
        return false;
    }
    if (!createWindowSurface(window)) return false;

    eglCreateImageKHR_ = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            eglGetProcAddress("eglCreateImageKHR"));
    eglDestroyImageKHR_ = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR"));
    eglGetNativeClientBufferANDROID_ = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
            eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    glEGLImageTargetTexture2DOES_ = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    const char* displayExtensions = eglQueryString(display_, EGL_EXTENSIONS);
    ahbImportSupported_ = eglCreateImageKHR_ && eglDestroyImageKHR_
            && eglGetNativeClientBufferANDROID_ && glEGLImageTargetTexture2DOES_
            && hasExtension(displayExtensions, "EGL_ANDROID_image_native_buffer");
    if (!ahbImportSupported_) GLR_W("AHardwareBuffer EGLImage import unavailable; CPU windows still work");
    return createGlObjects();
}

bool OpenGLRendererContext::createWindowSurface(ANativeWindow* window) {
    if (!window || display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
        if (window) ANativeWindow_release(window);
        return false;
    }
    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) {
        GLR_E("Could not attach EGL window surface: 0x%x", eglGetError());
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
        ANativeWindow_release(window);
        return false;
    }
    window_ = window;
    EGLint width = 0, height = 0;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    surfaceDetached_ = false;
    eglSwapInterval(display_, presentMode_ == kPresentMailbox && mailboxSupported_ ? 0 : 1);
    appliedPresentMode_ = presentMode_;
    return true;
}

void OpenGLRendererContext::destroyWindowSurface() {
    if (display_ != EGL_NO_DISPLAY) eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
    surfaceDetached_ = true;
}

bool OpenGLRendererContext::createGlObjects() {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program_ = linkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program_) return false;
    glGenVertexArrays(1, &vao_);
    rectLocation_ = glGetUniformLocation(program_, "uRect");
    textureLocation_ = glGetUniformLocation(program_, "uTexture");
    sourceSizeLocation_ = glGetUniformLocation(program_, "uSourceSize");
    filterModeLocation_ = glGetUniformLocation(program_, "uFilterMode");
    swapRBLocation_ = glGetUniformLocation(program_, "uSwapRB");
    alphaLocation_ = glGetUniformLocation(program_, "uUseAlpha");
    stretchLocation_ = glGetUniformLocation(program_, "uStretchMode");
    postFXLocation_ = glGetUniformLocation(program_, "uPostFX");
    sharpnessLocation_ = glGetUniformLocation(program_, "uSharpness");
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    glVendor_ = vendor ? vendor : "unknown";
    glRenderer_ = renderer ? renderer : "unknown";
    glVersion_ = version ? version : "unknown";
    GLR_I("OpenGL compositor initialized: vendor=%s renderer=%s version=%s AHB=%s",
          glVendor_.c_str(), glRenderer_.c_str(), glVersion_.c_str(),
          ahbImportSupported_ ? "yes" : "no");
    return true;
}

void OpenGLRendererContext::destroyImportedTexture(ImportedTexture& imported) {
    if (imported.texture) glDeleteTextures(1, &imported.texture);
    if (imported.image != EGL_NO_IMAGE_KHR && eglDestroyImageKHR_)
        eglDestroyImageKHR_(display_, imported.image);
    if (imported.buffer) AHardwareBuffer_release(imported.buffer);
    imported = {};
}

void OpenGLRendererContext::destroyGlObjects() {
    for (auto& pair : textures_) if (pair.second.cpu.texture) glDeleteTextures(1, &pair.second.cpu.texture);
    textures_.clear();
    for (auto& pair : imported_) destroyImportedTexture(pair.second);
    imported_.clear();
    windowBuffers_.clear();
    if (cursor_.texture) glDeleteTextures(1, &cursor_.texture);
    cursor_ = {};
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    vao_ = program_ = 0;
}

void OpenGLRendererContext::destroyEgl() {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT && surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(display_, surface_, surface_, context_);
        destroyGlObjects();
    } else {
        for (auto& pair : imported_) {
            if (pair.second.image != EGL_NO_IMAGE_KHR && eglDestroyImageKHR_)
                eglDestroyImageKHR_(display_, pair.second.image);
            if (pair.second.buffer) AHardwareBuffer_release(pair.second.buffer);
        }
        imported_.clear();
    }
    for (AHardwareBuffer* buffer : pendingAcquiredBuffers_) AHardwareBuffer_release(buffer);
    pendingAcquiredBuffers_.clear();
    destroyWindowSurface();
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    context_ = EGL_NO_CONTEXT;
    if (display_ != EGL_NO_DISPLAY) eglTerminate(display_);
    display_ = EGL_NO_DISPLAY;
    if (pendingWindow_) {
        ANativeWindow_release(pendingWindow_);
        pendingWindow_ = nullptr;
    }
}

void OpenGLRendererContext::workerMain() {
    {
        std::lock_guard<std::timed_mutex> lock(mutex_);
        ANativeWindow* initial = pendingWindow_;
        pendingWindow_ = nullptr;
        try {
            initialized_ = initializeEgl(initial);
        } catch (const std::exception& failure) {
            GLR_E("OpenGL initialization threw: %s", failure.what());
            if (initial && window_ != initial) ANativeWindow_release(initial);
            initialized_ = false;
        } catch (...) {
            GLR_E("OpenGL initialization threw an unknown exception");
            if (initial && window_ != initial) ANativeWindow_release(initial);
            initialized_ = false;
        }
        initFinished_ = true;
        dirty_ = initialized_;
        if (!initialized_) rendererFailed_.store(true, std::memory_order_release);
    }
    lifecycleCv_.notify_all();

    std::unique_lock<std::timed_mutex> lock(mutex_);
    while (!stopping_.load(std::memory_order_acquire)) {
        cv_.wait(lock, [this] {
            return stopping_.load(std::memory_order_acquire)
                    || detachRequested_ || attachRequested_
                    || (dirty_ && !surfaceDetached_);
        });
        if (stopping_.load(std::memory_order_acquire)) break;

        if (detachRequested_) {
            detachRequested_ = false;
            destroyWindowSurface();
            lifecycleCompleted_ = lifecycleRequest_;
            lifecycleCv_.notify_all();
        }
        if (attachRequested_) {
            attachRequested_ = false;
            ANativeWindow* next = pendingWindow_;
            pendingWindow_ = nullptr;
            // A timed-out detach callback may have left the previous EGL
            // surface attached. Never overwrite its handles or window ref.
            if (!surfaceDetached_) destroyWindowSurface();
            lastAttachResult_ = createWindowSurface(next);
            lifecycleCompleted_ = lifecycleRequest_;
            dirty_ = lastAttachResult_;
            lifecycleCv_.notify_all();
        }
        if (dirty_ && !surfaceDetached_ && surface_ != EGL_NO_SURFACE) {
            dirty_ = false;
            try {
                renderFrame();
            } catch (const std::exception& failure) {
                GLR_E("OpenGL frame failed: %s", failure.what());
                rendererFailed_.store(true, std::memory_order_release);
            } catch (...) {
                GLR_E("OpenGL frame failed with an unknown exception");
                rendererFailed_.store(true, std::memory_order_release);
            }
        }
    }
    destroyEgl();
    lock.unlock();
    {
        std::lock_guard<std::mutex> exitLock(exitMutex_);
        workerExited_ = true;
    }
    exitCv_.notify_all();
}

void OpenGLRendererContext::requestRenderLocked() {
    dirty_ = true;
    cv_.notify_one();
}

void OpenGLRendererContext::onSurfaceResized(int width, int height) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "resize")) return;
    pendingWidth_ = std::max(width, 1);
    pendingHeight_ = std::max(height, 1);
    surfaceWidth_ = pendingWidth_;
    surfaceHeight_ = pendingHeight_;
    requestRenderLocked();
}

void OpenGLRendererContext::detachSurface() {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(kLifecycleLockTimeout)) {
        GLR_W("detachSurface: renderer busy; deferring teardown to shutdown");
        return;
    }
    if (surfaceDetached_) return;
    const uint64_t request = ++lifecycleRequest_;
    detachRequested_ = true;
    cv_.notify_one();
    if (!lifecycleCv_.wait_for(lock, kLifecycleWaitTimeout, [this, request] {
            return stopping_.load(std::memory_order_acquire) || lifecycleCompleted_ >= request;
        })) {
        GLR_W("detachSurface: lifecycle request timed out");
    }
}

bool OpenGLRendererContext::reattachSurface(ANativeWindow* window) {
    if (!window) return false;
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(kLifecycleLockTimeout)) {
        GLR_W("reattachSurface: renderer busy; rejecting replacement surface");
        ANativeWindow_release(window);
        return false;
    }
    if (stopping_.load(std::memory_order_acquire)
            || rendererFailed_.load(std::memory_order_acquire)) {
        ANativeWindow_release(window);
        return false;
    }
    if (pendingWindow_) ANativeWindow_release(pendingWindow_);
    pendingWindow_ = window;
    const uint64_t request = ++lifecycleRequest_;
    attachRequested_ = true;
    cv_.notify_one();
    if (!lifecycleCv_.wait_for(lock, kLifecycleWaitTimeout, [this, request] {
            return stopping_.load(std::memory_order_acquire) || lifecycleCompleted_ >= request;
        })) {
        GLR_W("reattachSurface: lifecycle request timed out");
        return false;
    }
    return !stopping_.load(std::memory_order_acquire) && lastAttachResult_;
}

void OpenGLRendererContext::getSurfaceSize(int& width, int& height) const {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "getSurfaceSize")) {
        width = height = 0;
        return;
    }
    width = surfaceWidth_;
    height = surfaceHeight_;
}

bool OpenGLRendererContext::updateWindowContent(int64_t id, const void* pixels, short width,
                                                 short height, short stride, int, int) {
    if (!pixels || width <= 0 || height <= 0
            || stopping_.load(std::memory_order_acquire)
            || rendererFailed_.load(std::memory_order_acquire)) return false;
    const int sourceStride = stride > 0 ? stride : width;
    if (sourceStride < width) return false;
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "CPU window update")) return false;
    WindowTexture& window = textures_[id];
    window.activeBuffer = nullptr;
    CpuTexture& texture = window.cpu;
    texture.width = width;
    texture.height = height;
    texture.pixels.resize(static_cast<size_t>(width) * height * 4);
    const uint8_t* source = static_cast<const uint8_t*>(pixels);
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    if (sourceStride == width) {
        std::memcpy(texture.pixels.data(), source, rowBytes * height);
    } else {
        for (int row = 0; row < height; ++row)
            std::memcpy(texture.pixels.data() + static_cast<size_t>(row) * rowBytes,
                        source + static_cast<size_t>(row) * sourceStride * 4, rowBytes);
    }
    texture.dirty = true;
    requestRenderLocked();
    return true;
}

bool OpenGLRendererContext::updateWindowContentAHB(int64_t id, AHardwareBuffer* buffer,
                                                    const void* fallbackPixels, short width,
                                                    short height, short fallbackStride, int, int) {
    if (!buffer || width <= 0 || height <= 0
            || stopping_.load(std::memory_order_acquire)
            || rendererFailed_.load(std::memory_order_acquire)) return false;
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "AHB window update")) return false;
    // Do not claim a zero-copy presentation on devices whose EGL stack cannot
    // import Android buffers. Java will immediately submit the same pixels to
    // the CPU path instead.
    if (!ahbImportSupported_) return false;
    if (!imported_.count(buffer) && !pendingAcquiredBuffers_.count(buffer)) {
        AHardwareBuffer_acquire(buffer);
        pendingAcquiredBuffers_.insert(buffer);
    }
    WindowTexture& window = textures_[id];
    window.activeBuffer = buffer;
    const int sourceStride = fallbackStride > 0 ? fallbackStride : width;
    if (fallbackPixels && sourceStride >= width) {
        CpuTexture& fallback = window.cpu;
        fallback.width = width;
        fallback.height = height;
        fallback.pixels.resize(static_cast<size_t>(width) * height * 4);
        const uint8_t* source = static_cast<const uint8_t*>(fallbackPixels);
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        for (int row = 0; row < height; ++row) {
            std::memcpy(fallback.pixels.data() + static_cast<size_t>(row) * rowBytes,
                        source + static_cast<size_t>(row) * sourceStride * 4, rowBytes);
        }
        fallback.dirty = true;
    }
    windowBuffers_[id].insert(buffer);
    deferredBufferDeletes_.erase(
        std::remove(deferredBufferDeletes_.begin(), deferredBufferDeletes_.end(), buffer),
        deferredBufferDeletes_.end());
    requestRenderLocked();
    return true;
}

void OpenGLRendererContext::updateCursorImage(const void* pixels, short width, short height,
                                               short hotX, short hotY) {
    if (!pixels || width <= 0 || height <= 0) return;
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "cursor image update")) return;
    cursor_.width = width;
    cursor_.height = height;
    cursor_.pixels.resize(static_cast<size_t>(width) * height * 4);
    std::memcpy(cursor_.pixels.data(), pixels, cursor_.pixels.size());
    cursor_.dirty = true;
    cursorHotX_ = hotX;
    cursorHotY_ = hotY;
    requestRenderLocked();
}

void OpenGLRendererContext::updatePointerPosition(short x, short y) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "pointer update")) return;
    pointerX_ = x;
    pointerY_ = y;
    if (cursorVisible_) requestRenderLocked();
}

void OpenGLRendererContext::setCursorVisible(bool visible) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "cursor visibility")) return;
    cursorVisible_ = visible;
    requestRenderLocked();
}

void OpenGLRendererContext::setRootColor(int argb) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "root color")) return;
    rootColor_ = argb & 0x00ffffff;
    requestRenderLocked();
}

void OpenGLRendererContext::setRenderList(const int64_t* ids, const int* xs, const int* ys, int count) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "render list")) return;
    renderList_.clear();
    renderList_.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) renderList_.push_back({ids[i], xs[i], ys[i]});
    requestRenderLocked();
}

void OpenGLRendererContext::releaseWindowBuffers(int64_t id) {
    auto found = windowBuffers_.find(id);
    if (found == windowBuffers_.end()) return;
    for (AHardwareBuffer* buffer : found->second) {
        bool usedElsewhere = false;
        for (const auto& owner : windowBuffers_) {
            if (owner.first != id && owner.second.count(buffer)) { usedElsewhere = true; break; }
        }
        if (!usedElsewhere) {
            deferredBufferDeletes_.push_back(buffer);
        }
    }
    windowBuffers_.erase(found);
}

void OpenGLRendererContext::removeWindow(int64_t id) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "remove window")) return;
    auto texture = textures_.find(id);
    if (texture != textures_.end()) {
        if (texture->second.cpu.texture) deferredTextureDeletes_.push_back(texture->second.cpu.texture);
        textures_.erase(texture);
    }
    releaseWindowBuffers(id);
    renderList_.erase(std::remove_if(renderList_.begin(), renderList_.end(),
        [id](const RenderEntry& entry) { return entry.id == id; }), renderList_.end());
    requestRenderLocked();
}

void OpenGLRendererContext::setTransformAndScissor(float offsetX, float offsetY,
                                                    float scaleX, float scaleY,
                                                    bool hasScissor, int x, int y,
                                                    int width, int height) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "transform/scissor")) return;
    offsetX_ = offsetX; offsetY_ = offsetY;
    scaleX_ = scaleX; scaleY_ = scaleY;
    hasScissor_ = hasScissor;
    scissorX_ = x; scissorY_ = y;
    scissorWidth_ = width; scissorHeight_ = height;
    requestRenderLocked();
}

void OpenGLRendererContext::uploadCpuTexture(CpuTexture& texture) {
    if (!texture.dirty || texture.pixels.empty()) return;
    if (!texture.texture || texture.storageWidth != texture.width
            || texture.storageHeight != texture.height) {
        if (texture.texture) glDeleteTextures(1, &texture.texture);
        glGenTextures(1, &texture.texture);
        glBindTexture(GL_TEXTURE_2D, texture.texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, texture.width, texture.height);
        texture.storageWidth = texture.width;
        texture.storageHeight = texture.height;
        applySampler(texture.texture);
    }
    glBindTexture(GL_TEXTURE_2D, texture.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height,
                    GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels.data());
    texture.dirty = false;
}

OpenGLRendererContext::ImportedTexture* OpenGLRendererContext::importBuffer(AHardwareBuffer* buffer) {
    auto current = imported_.find(buffer);
    if (current != imported_.end()) return &current->second;
    if (!ahbImportSupported_) return nullptr;

    AHardwareBuffer_Desc descriptor{};
    AHardwareBuffer_describe(buffer, &descriptor);
    EGLClientBuffer client = eglGetNativeClientBufferANDROID_(buffer);
    if (!client) {
        GLR_E("AHardwareBuffer native client buffer import failed; disabling zero-copy path");
        ahbImportSupported_ = false;
        return nullptr;
    }
    const EGLint attributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR image = eglCreateImageKHR_(display_, EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID, client, attributes);
    if (image == EGL_NO_IMAGE_KHR) {
        GLR_E("AHardwareBuffer EGLImage import failed: 0x%x", eglGetError());
        ahbImportSupported_ = false;
        return nullptr;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, image);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        eglDestroyImageKHR_(display_, image);
        GLR_E("AHardwareBuffer texture binding failed");
        ahbImportSupported_ = false;
        return nullptr;
    }
    applySampler(texture);
    if (!pendingAcquiredBuffers_.erase(buffer)) AHardwareBuffer_acquire(buffer);
    ImportedTexture imported{};
    imported.texture = texture;
    imported.image = image;
    imported.buffer = buffer;
    imported.width = static_cast<int>(descriptor.width);
    imported.height = static_cast<int>(descriptor.height);
    imported.sourceNeedsRedBlueSwap =
            wemu::opengl::importedBufferNeedsRedBlueSwap(descriptor.format);
    auto inserted = imported_.emplace(buffer, imported);
    if (!wemu::opengl::isKnownWindowBufferFormat(descriptor.format)) {
        GLR_W("Imported AHardwareBuffer %p uses unknown format=%u; assuming X11 BGRA bytes",
              buffer, descriptor.format);
    } else {
        // Logged once per imported buffer (not per frame) so a device report
        // proves which side of the format boundary was selected.
        GLR_I("Imported AHardwareBuffer %p (%ux%u format=%u, native-sample-swap-rb=%s)",
              buffer, descriptor.width, descriptor.height, descriptor.format,
              imported.sourceNeedsRedBlueSwap ? "yes" : "no");
    }
    return &inserted.first->second;
}

void OpenGLRendererContext::applySampler(GLuint texture) const {
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = filterMode_ == 0 ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void OpenGLRendererContext::drawTexture(GLuint texture, int x, int y, int width, int height,
                                        bool sourceNeedsRedBlueSwap,
                                        bool alpha, bool applyEffects) {
    if (!texture || width <= 0 || height <= 0) return;
    float left = (offsetX_ + static_cast<float>(x) * scaleX_) / containerWidth_ * 2.0f - 1.0f;
    float right = (offsetX_ + static_cast<float>(x + width) * scaleX_) / containerWidth_ * 2.0f - 1.0f;
    float top = 1.0f - (offsetY_ + static_cast<float>(y) * scaleY_) / containerHeight_ * 2.0f;
    float bottom = 1.0f - (offsetY_ + static_cast<float>(y + height) * scaleY_) / containerHeight_ * 2.0f;
    if (stretchMode_ == 1 && !alpha) { left = -1.0f; right = 1.0f; }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform4f(rectLocation_, left, top, right, bottom);
    glUniform2f(sourceSizeLocation_, static_cast<float>(width), static_cast<float>(height));
    glUniform1i(filterModeLocation_, alpha ? 1 : filterMode_);
    // Correct the source format exactly once. CPU uploads and R8 AHBs contain
    // X11 BGRA bytes in RGBA storage and need a shader swap. A B8 AHB is already
    // exposed by EGL as logical RGBA and must not be swapped again. The saved
    // option remains an explicit user override of that native interpretation.
    // It applies to guest windows only: the cursor is compositor-owned BGRA,
    // not content from the game whose broken channel order is being overridden.
    const bool effectiveSwap = alpha
            ? sourceNeedsRedBlueSwap
            : wemu::opengl::effectiveRedBlueSwap(sourceNeedsRedBlueSwap, swapRB_);
    glUniform1i(swapRBLocation_, effectiveSwap ? 1 : 0);
    glUniform1i(alphaLocation_, alpha ? 1 : 0);
    glUniform1i(stretchLocation_, applyEffects ? stretchMode_ : 0);
    glUniform1i(postFXLocation_, applyEffects ? postFXMode_ : 0);
    glUniform1f(sharpnessLocation_, sharpness_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void OpenGLRendererContext::renderFrame() {
    if (rendererFailed_.load(std::memory_order_acquire)) return;
    if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT) return;
    if (surfaceWidth_ <= 0 || surfaceHeight_ <= 0) return;

    if (!deferredTextureDeletes_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(deferredTextureDeletes_.size()),
                         deferredTextureDeletes_.data());
        deferredTextureDeletes_.clear();
    }
    for (AHardwareBuffer* buffer : deferredBufferDeletes_) {
        auto imported = imported_.find(buffer);
        if (imported != imported_.end()) {
            destroyImportedTexture(imported->second);
            imported_.erase(imported);
        } else if (pendingAcquiredBuffers_.erase(buffer)) {
            AHardwareBuffer_release(buffer);
        }
    }
    deferredBufferDeletes_.clear();

    if (samplerDirty_) {
        for (auto& pair : textures_)
            if (pair.second.cpu.texture) applySampler(pair.second.cpu.texture);
        for (auto& pair : imported_) applySampler(pair.second.texture);
        if (cursor_.texture) applySampler(cursor_.texture);
        samplerDirty_ = false;
    }

    glViewport(0, 0, surfaceWidth_, surfaceHeight_);
    glDisable(GL_SCISSOR_TEST);
    const float red = static_cast<float>((rootColor_ >> 16) & 0xff) / 255.0f;
    const float green = static_cast<float>((rootColor_ >> 8) & 0xff) / 255.0f;
    const float blue = static_cast<float>(rootColor_ & 0xff) / 255.0f;
    glClearColor(red, green, blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (hasScissor_) {
        glEnable(GL_SCISSOR_TEST);
        const int sx = std::clamp(scissorX_, 0, surfaceWidth_);
        const int sy = std::clamp(scissorY_, 0, surfaceHeight_);
        const int sw = std::clamp(scissorWidth_, 0, surfaceWidth_ - sx);
        const int sh = std::clamp(scissorHeight_, 0, surfaceHeight_ - sy);
        glScissor(sx, surfaceHeight_ - sy - sh, sw, sh);
    }
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glUniform1i(textureLocation_, 0);
    glDisable(GL_BLEND);

    for (const RenderEntry& entry : renderList_) {
        auto found = textures_.find(entry.id);
        if (found == textures_.end()) continue;
        WindowTexture& window = found->second;
        GLuint texture = 0;
        int width = 0, height = 0;
        bool sourceNeedsRedBlueSwap = true; // CPU texture: X11 BGRA bytes in GL_RGBA.
        if (window.activeBuffer) {
            ImportedTexture* imported = importBuffer(window.activeBuffer);
            if (imported) {
                texture = imported->texture;
                width = imported->width;
                height = imported->height;
                sourceNeedsRedBlueSwap = imported->sourceNeedsRedBlueSwap;
            } else {
                // The extension may be advertised even when a vendor rejects a
                // particular allocation. Use the CPU snapshot supplied with
                // the update instead of silently rendering a black window.
                window.activeBuffer = nullptr;
            }
        }
        if (!window.activeBuffer) {
            uploadCpuTexture(window.cpu);
            texture = window.cpu.texture; width = window.cpu.width; height = window.cpu.height;
        }
        drawTexture(texture, entry.x, entry.y, width, height,
                    sourceNeedsRedBlueSwap, false, true);
    }

    if (cursorVisible_ && cursor_.width > 0 && cursor_.height > 0) {
        uploadCpuTexture(cursor_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const int x = std::max(0, static_cast<int>(pointerX_) - cursorHotX_);
        const int y = std::max(0, static_cast<int>(pointerY_) - cursorHotY_);
        drawTexture(cursor_.texture, x, y, cursor_.width, cursor_.height,
                    true, true, false);
        glDisable(GL_BLEND);
    }

    if (appliedPresentMode_ != presentMode_) {
        eglSwapInterval(display_, presentMode_ == kPresentMailbox && mailboxSupported_ ? 0 : 1);
        appliedPresentMode_ = presentMode_;
    }
    if (!eglSwapBuffers(display_, surface_)) {
        const EGLint error = eglGetError();
        GLR_E("eglSwapBuffers failed: 0x%x", error);
        if (error == EGL_BAD_SURFACE || error == EGL_BAD_NATIVE_WINDOW) {
            // Consume the stale native-window ref here. The Java callback can
            // then reattach a fresh surface without overwriting leaked EGL
            // handles even if its earlier detach request timed out.
            destroyWindowSurface();
        } else {
            rendererFailed_.store(true, std::memory_order_release);
            GLR_E("OpenGL compositor entered a failed state; rejecting future frames");
        }
    }
}

void OpenGLRendererContext::setVerboseLog(bool value) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "verbose logging")) return;
    verbose_ = value;
}

void OpenGLRendererContext::dumpRendererInfo() {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "renderer info")) return;
    GLR_I("Renderer info: vendor=%s renderer=%s version=%s surface=%dx%d AHB=%s rb-override=%s",
          glVendor_.c_str(), glRenderer_.c_str(), glVersion_.c_str(),
          surfaceWidth_, surfaceHeight_, ahbImportSupported_ ? "yes" : "no",
          swapRB_ ? "on" : "off");
}

void OpenGLRendererContext::setFilterMode(int mode) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "filter mode")) return;
    filterMode_ = std::clamp(mode, 0, 4);
    samplerDirty_ = true;
    requestRenderLocked();
}

void OpenGLRendererContext::setStretchMode(int mode) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "stretch mode")) return;
    stretchMode_ = mode == 1 ? 1 : 0;
    requestRenderLocked();
}

void OpenGLRendererContext::setPostFXMode(int mode) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "post-FX mode")) return;
    postFXMode_ = std::clamp(mode, 0, 4);
    requestRenderLocked();
}

void OpenGLRendererContext::setSharpness(float value) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "sharpness")) return;
    sharpness_ = std::clamp(value, 0.0f, 1.0f);
    requestRenderLocked();
}

void OpenGLRendererContext::setSwapRB(bool enabled) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "channel swap")) return;
    GLR_I("Guest red/blue override=%s", enabled ? "on" : "off");
    swapRB_ = enabled;
    requestRenderLocked();
}

void OpenGLRendererContext::setPresentMode(int mode) {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lockScene(lock, "present mode")) return;
    presentMode_ = mode == kPresentMailbox ? kPresentMailbox : kPresentFifo;
    requestRenderLocked();
}

std::vector<int> OpenGLRendererContext::getSupportedPresentModes() const {
    return mailboxSupported_ ? std::vector<int>{kPresentMailbox, kPresentFifo}
                             : std::vector<int>{kPresentFifo};
}
