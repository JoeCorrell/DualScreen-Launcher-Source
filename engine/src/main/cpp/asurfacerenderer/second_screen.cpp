/*
 * The Thor's bottom panel as an output for one guest window.
 *
 * Deliberately not a renderer. The guest hands the second monitor's window over
 * as an AHardwareBuffer through DRI3, and there is exactly one full-screen
 * window on that panel — so there is nothing to composite, and compositing it
 * would mean a second Vulkan swapchain, a second render pass and a second
 * present every frame on a GPU that is already the limit on this device. This
 * takes the buffer the guest already produced and hands it to SurfaceFlinger
 * unchanged, which is what the display controller was going to do with it
 * anyway.
 *
 * Separate from ASurfaceRendererContext rather than a mode of it, because that
 * class is the scanout *renderer* — a window map, a converted-buffer pool, a
 * cursor, release-fence bookkeeping — and none of it applies to a single
 * full-screen layer whose buffers belong to somebody else. It also has to work
 * when the container is using the Vulkan compositor, which owns no
 * ASurfaceControl at all.
 */

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/rect.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>   // close(), for the fence descriptor on the refusal paths
#include <cstring>
#include <mutex>

#ifndef AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5
#endif

#define SSLOG(...)   __android_log_print(ANDROID_LOG_INFO,  "asr_second", __VA_ARGS__)
#define SSLOG_E(...) __android_log_print(ANDROID_LOG_ERROR, "asr_second", __VA_ARGS__)

namespace {

typedef void* (*pfn_SCCreateFromWindow)(ANativeWindow*, const char*);
typedef void  (*pfn_SCRelease)(void*);
typedef void* (*pfn_STCreate)();
typedef void  (*pfn_STDelete)(void*);
typedef void  (*pfn_STApply)(void*);
typedef void  (*pfn_STSetBuffer)(void*, void*, AHardwareBuffer*, int);
typedef void  (*pfn_STSetVisibility)(void*, void*, int8_t);
typedef void  (*pfn_STSetZOrder)(void*, void*, int32_t);
typedef void  (*pfn_STSetGeometry)(void*, void*, const ARect*, const ARect*, int32_t);
typedef void  (*pfn_STSetBufferTransparency)(void*, void*, int8_t);
typedef void  (*pfn_STSetOnComplete)(void*, void*, void (*)(void*, void*));
typedef int   (*pfn_STStatsGetPreviousReleaseFenceFd)(void*, void*);

struct Api {
    pfn_SCCreateFromWindow createFromWindow = nullptr;
    pfn_SCRelease          release          = nullptr;
    pfn_STCreate           txCreate         = nullptr;
    pfn_STDelete           txDelete         = nullptr;
    pfn_STApply            txApply          = nullptr;
    pfn_STSetBuffer        setBuffer        = nullptr;
    pfn_STSetVisibility    setVisibility    = nullptr;
    pfn_STSetZOrder        setZOrder        = nullptr;
    pfn_STSetGeometry      setGeometry      = nullptr;
    pfn_STSetBufferTransparency setTransparency = nullptr;
    pfn_STSetOnComplete    setOnComplete    = nullptr;
    pfn_STStatsGetPreviousReleaseFenceFd getPreviousReleaseFenceFd = nullptr;
    bool loaded = false;
};

Api g_api;
std::once_flag g_apiOnce;

/*
 * Resolved by name rather than linked. ASurfaceControl is API 29 and this
 * module is built against a lower minimum, so a missing symbol has to be a
 * runtime answer of "no second output" rather than a library that will not
 * load — which would take the whole renderer with it.
 */
void loadApi() {
    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) {
        SSLOG_E("dlopen(libandroid.so) failed: %s", dlerror());
        return;
    }

    g_api.createFromWindow = (pfn_SCCreateFromWindow)dlsym(lib, "ASurfaceControl_createFromWindow");
    g_api.release          = (pfn_SCRelease)dlsym(lib, "ASurfaceControl_release");
    g_api.txCreate         = (pfn_STCreate)dlsym(lib, "ASurfaceTransaction_create");
    g_api.txDelete         = (pfn_STDelete)dlsym(lib, "ASurfaceTransaction_delete");
    g_api.txApply          = (pfn_STApply)dlsym(lib, "ASurfaceTransaction_apply");
    g_api.setBuffer        = (pfn_STSetBuffer)dlsym(lib, "ASurfaceTransaction_setBuffer");
    g_api.setVisibility    = (pfn_STSetVisibility)dlsym(lib, "ASurfaceTransaction_setVisibility");
    g_api.setZOrder        = (pfn_STSetZOrder)dlsym(lib, "ASurfaceTransaction_setZOrder");
    g_api.setGeometry      = (pfn_STSetGeometry)dlsym(lib, "ASurfaceTransaction_setGeometry");
    g_api.setTransparency  = (pfn_STSetBufferTransparency)
            dlsym(lib, "ASurfaceTransaction_setBufferTransparency");
    g_api.setOnComplete    = (pfn_STSetOnComplete)
            dlsym(lib, "ASurfaceTransaction_setOnComplete");
    g_api.getPreviousReleaseFenceFd =
            (pfn_STStatsGetPreviousReleaseFenceFd)
            dlsym(lib, "ASurfaceTransactionStats_getPreviousReleaseFenceFd");

    g_api.loaded = g_api.createFromWindow && g_api.release && g_api.txCreate
            && g_api.txDelete && g_api.txApply && g_api.setBuffer
            && g_api.setVisibility && g_api.setGeometry;
    if (!g_api.loaded) SSLOG_E("ASurfaceControl entry points unavailable");
}

struct ControlGeneration {
    void* control = nullptr;
    pfn_SCRelease release = nullptr;
    uint64_t generation = 0;

    ~ControlGeneration() {
        if (control && release) release(control);
    }
};

struct CpuRingState {
    struct Slot {
        AHardwareBuffer* buffer = nullptr;
        int releaseFenceFd = -1;
        bool inUse = false;
    };

    std::mutex mutex;
    std::array<Slot, 3> slots;
    int width = 0;
    int height = 0;
    int next = 0;
    bool retired = false;
    uint64_t generation = 0;

    ~CpuRingState() {
        for (Slot& slot : slots) {
            if (slot.releaseFenceFd >= 0) close(slot.releaseFenceFd);
            if (slot.buffer) AHardwareBuffer_release(slot.buffer);
        }
    }
};

struct PresentedCpuSlot {
    std::shared_ptr<CpuRingState> ring;
    int index = -1;

    bool valid() const {
        return ring && index >= 0 && index < static_cast<int>(ring->slots.size());
    }
};

struct CpuReleaseContext {
    std::shared_ptr<ControlGeneration> control;
    PresentedCpuSlot previous;
    pfn_STStatsGetPreviousReleaseFenceFd getPreviousReleaseFenceFd = nullptr;
};

struct Output {
    ANativeWindow* window  = nullptr;
    void*          control = nullptr;
    std::shared_ptr<ControlGeneration> controlGeneration;
    uint64_t       nextGeneration = 1;
    int            panelWidth  = 0;
    int            panelHeight = 0;
    bool           visible = false;
    /*
     * The last buffer handed over, kept only to notice when the source size
     * changes. The geometry is recomputed then, and not on every frame — a
     * transaction that repeats the rectangles it already applied is work
     * SurfaceFlinger still has to do.
     */
    int lastSrcWidth  = 0;
    int lastSrcHeight = 0;

    // Ordinary GDI/X11 windows have CPU pixels rather than a guest-owned AHB.
    // Keep a tiny rotating set for those panels so SurfaceFlinger never reads
    // the buffer the next UI repaint is writing. This path is used only for a
    // claimed second-monitor window; normal game composition is unchanged.
    std::shared_ptr<CpuRingState> cpuRing;
    PresentedCpuSlot currentCpu;
};

Output    g_output;
std::mutex g_lock;

struct CpuPanelHealth {
    uint64_t intervalStartNs = 0;
    uint64_t calls = 0;
    uint64_t accepted = 0;
    uint64_t noSlot = 0;
    uint64_t fenceBusy = 0;
    uint64_t lockFailures = 0;
    uint64_t lockSamples = 0;
    uint64_t lockTotalNs = 0;
    uint64_t lockMaxNs = 0;
    uint64_t copySamples = 0;
    uint64_t copyTotalNs = 0;
    uint64_t copyMaxNs = 0;
    uint64_t copiedBytes = 0;
};

CpuPanelHealth g_cpuHealth;

uint64_t monotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool releaseFenceReadyNoWait(int fd) {
    if (fd < 0) return true;
    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = POLLIN;

    int result;
    do {
        result = poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);

    // POLLNVAL means the descriptor is not safe to hand to AHardwareBuffer.
    return result > 0 && (descriptor.revents & POLLNVAL) == 0;
}

void maybeReportCpuHealthLocked(uint64_t nowNs) {
    constexpr uint64_t kReportIntervalNs = 10'000'000'000ULL;
    if (g_cpuHealth.intervalStartNs == 0) {
        g_cpuHealth.intervalStartNs = nowNs;
        return;
    }
    if (nowNs - g_cpuHealth.intervalStartNs < kReportIntervalNs) return;

    const uint64_t lockAverageUs = g_cpuHealth.lockSamples
            ? g_cpuHealth.lockTotalNs / g_cpuHealth.lockSamples / 1000ULL : 0;
    const uint64_t copyAverageUs = g_cpuHealth.copySamples
            ? g_cpuHealth.copyTotalNs / g_cpuHealth.copySamples / 1000ULL : 0;
    SSLOG("CPU panel health: calls=%" PRIu64 " accepted=%" PRIu64
          " noSlot=%" PRIu64 " fenceBusy=%" PRIu64
          " lockFail=%" PRIu64 " lockUs=%" PRIu64 "/%" PRIu64
          " copyUs=%" PRIu64 "/%" PRIu64 " copiedMiB=%.1f",
          g_cpuHealth.calls, g_cpuHealth.accepted, g_cpuHealth.noSlot,
          g_cpuHealth.fenceBusy, g_cpuHealth.lockFailures, lockAverageUs,
          g_cpuHealth.lockMaxNs / uint64_t{1000}, copyAverageUs,
          g_cpuHealth.copyMaxNs / uint64_t{1000},
          static_cast<double>(g_cpuHealth.copiedBytes) / (1024.0 * 1024.0));

    g_cpuHealth = {};
    g_cpuHealth.intervalStartNs = nowNs;
}

/**
 * Fits the guest's frame to the panel without changing its shape.
 *
 * The bottom screen is not the same aspect as whatever the guest chose to draw,
 * and stretching a HUD is the sort of wrong that looks like a bug in the game.
 * Scaled to the larger fitting size and centred, so the panel keeps black bars
 * rather than a distorted picture.
 */
void computeDestination(int srcWidth, int srcHeight, ARect* out) {
    if (srcWidth <= 0 || srcHeight <= 0
            || g_output.panelWidth <= 0 || g_output.panelHeight <= 0) {
        out->left = out->top = 0;
        out->right = g_output.panelWidth;
        out->bottom = g_output.panelHeight;
        return;
    }

    const float scale = std::min((float)g_output.panelWidth / srcWidth,
                                (float)g_output.panelHeight / srcHeight);
    const int width  = (int)(srcWidth * scale);
    const int height = (int)(srcHeight * scale);
    const int left = (g_output.panelWidth - width) / 2;
    const int top  = (g_output.panelHeight - height) / 2;

    out->left   = left;
    out->top    = top;
    out->right  = left + width;
    out->bottom = top + height;
}

void cpuReleaseComplete(void* opaque, void* stats) {
    std::unique_ptr<CpuReleaseContext> context(
            static_cast<CpuReleaseContext*>(opaque));
    if (!context || !context->previous.valid()) return;

    int releaseFenceFd = -1;
    if (stats && context->getPreviousReleaseFenceFd &&
            context->control && context->control->control) {
        releaseFenceFd = context->getPreviousReleaseFenceFd(
                stats, context->control->control);
    }

    const PresentedCpuSlot previous = context->previous;
    std::lock_guard<std::mutex> lock(previous.ring->mutex);
    CpuRingState::Slot& slot = previous.ring->slots[previous.index];
    if (previous.ring->retired) {
        if (releaseFenceFd >= 0) close(releaseFenceFd);
        return;
    }
    if (slot.releaseFenceFd >= 0) close(slot.releaseFenceFd);
    slot.releaseFenceFd = releaseFenceFd;
    slot.inUse = false;
}

bool attachCpuReleaseCallback(void* transaction,
                              const PresentedCpuSlot& previous) {
    if (!previous.valid()) return true;
    if (!transaction || !g_api.setOnComplete ||
            !g_api.getPreviousReleaseFenceFd ||
            !g_output.controlGeneration) {
        return false;
    }

    auto* context = new (std::nothrow) CpuReleaseContext();
    if (!context) return false;
    context->control = g_output.controlGeneration;
    context->previous = previous;
    context->getPreviousReleaseFenceFd = g_api.getPreviousReleaseFenceFd;
    g_api.setOnComplete(transaction, context, cpuReleaseComplete);
    return true;
}

void releaseCpuBuffersLocked() {
    if (g_output.cpuRing) {
        std::lock_guard<std::mutex> lock(g_output.cpuRing->mutex);
        g_output.cpuRing->retired = true;
    }
    g_output.cpuRing.reset();
}

void retireCurrentCpuLocked() {
    const PresentedCpuSlot previous = g_output.currentCpu;
    if (!previous.valid() || !g_output.control || !g_api.txCreate ||
            !g_api.txApply || !g_api.txDelete || !g_api.setVisibility) {
        return;
    }

    void* transaction = g_api.txCreate();
    if (!transaction) return;
    g_api.setVisibility(transaction, g_output.control, 0);
    if (!attachCpuReleaseCallback(transaction, previous)) {
        // The slot remains quarantined. The ring is released only after the
        // control itself is released, so this fallback cannot race a rewrite.
        SSLOG_E("CPU panel retirement callback unavailable");
    }
    g_api.txApply(transaction);
    // Do not publish the retirement until the transaction has actually been
    // submitted. If transaction allocation failed, currentCpu must continue
    // to describe the buffer that SurfaceFlinger still owns.
    g_output.currentCpu = {};
    g_api.txDelete(transaction);
}

void destroyLocked() {
    retireCurrentCpuLocked();
    // A failed retirement transaction leaves currentCpu accurate until this
    // point. The control is now being torn down, so retaining that slot would
    // only keep an obsolete ring alive across the next attachment generation.
    g_output.currentCpu = {};
    releaseCpuBuffersLocked();

    // Per-frame callbacks retain this generation. Resetting the output's copy
    // therefore cannot release an ASurfaceControl while an old callback still
    // needs it to query that generation's previous release fence.
    g_output.control = nullptr;
    g_output.controlGeneration.reset();
    if (g_output.window) {
        ANativeWindow_release(g_output.window);
        g_output.window = nullptr;
    }
    g_output.visible = false;
    g_output.lastSrcWidth = 0;
    g_output.lastSrcHeight = 0;
}

bool ensureCpuBuffersLocked(int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    if (!g_api.setOnComplete || !g_api.getPreviousReleaseFenceFd) {
        SSLOG_E("CPU panel release-fence callbacks unavailable");
        return false;
    }
    if (g_output.cpuRing && g_output.cpuRing->width == width &&
            g_output.cpuRing->height == height) return true;

    std::shared_ptr<CpuRingState> replacement;
    try {
        replacement = std::make_shared<CpuRingState>();
    } catch (...) {
        SSLOG_E("CPU panel ring allocation failed: %dx%d", width, height);
        return false;
    }
    replacement->width = width;
    replacement->height = height;
    replacement->generation = g_output.controlGeneration
            ? g_output.controlGeneration->generation : 0;

    AHardwareBuffer_Desc desc = {};
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
    for (CpuRingState::Slot& slot : replacement->slots) {
        if (AHardwareBuffer_allocate(&desc, &slot.buffer) != 0 || !slot.buffer) {
            SSLOG_E("CPU panel buffer allocation failed: %dx%d", width, height);
            return false;
        }
    }

    releaseCpuBuffersLocked();
    g_output.cpuRing = std::move(replacement);
    SSLOG("CPU-backed second-screen buffers ready: %dx%d generation=%" PRIu64,
          width, height, g_output.cpuRing->generation);
    return true;
}

bool setBufferLocked(AHardwareBuffer* buffer, int fenceFd,
                     int srcWidth, int srcHeight) {
    if (!g_api.loaded || !g_output.control || !buffer) {
        if (fenceFd >= 0) close(fenceFd);
        return false;
    }

    void* transaction = g_api.txCreate();
    if (!transaction) {
        if (fenceFd >= 0) close(fenceFd);
        return false;
    }
    const PresentedCpuSlot previousCpu = g_output.currentCpu;
    g_api.setBuffer(transaction, g_output.control, buffer, fenceFd);

    if (srcWidth != g_output.lastSrcWidth || srcHeight != g_output.lastSrcHeight) {
        ARect source { 0, 0, srcWidth, srcHeight };
        ARect destination;
        computeDestination(srcWidth, srcHeight, &destination);
        g_api.setGeometry(transaction, g_output.control, &source, &destination, 0);
        g_output.lastSrcWidth  = srcWidth;
        g_output.lastSrcHeight = srcHeight;
        SSLOG("Second screen source %dx%d shown at %d,%d-%d,%d",
              srcWidth, srcHeight, destination.left, destination.top,
              destination.right, destination.bottom);
    }

    if (!g_output.visible) {
        if (g_api.setZOrder) g_api.setZOrder(transaction, g_output.control, 1);
        if (g_api.setTransparency) g_api.setTransparency(transaction, g_output.control, 0);
        g_api.setVisibility(transaction, g_output.control, 1);
        g_output.visible = true;
    }
    if (!attachCpuReleaseCallback(transaction, previousCpu)) {
        // Direct DRI3 presentation remains zero-copy. Only the displaced CPU
        // slot is quarantined if callback registration is unavailable.
        SSLOG_E("CPU release callback unavailable while switching to direct scanout");
    }
    g_api.txApply(transaction);
    // The direct buffer only becomes the authoritative presentation after the
    // transaction has been submitted. Until then currentCpu must continue to
    // identify the CPU slot presently owned by SurfaceFlinger.
    g_output.currentCpu = {};
    g_api.txDelete(transaction);
    return true;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_SecondScreenOutput_nativeAttach(
        JNIEnv* env, jclass, jobject surface, jint panelWidth, jint panelHeight)
{
    std::call_once(g_apiOnce, loadApi);
    if (!g_api.loaded) return JNI_FALSE;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        SSLOG_E("nativeAttach: surface has no native window");
        return JNI_FALSE;
    }

    std::lock_guard<std::mutex> guard(g_lock);
    destroyLocked();

    void* control = g_api.createFromWindow(window, "wemu-second-screen");
    if (!control) {
        ANativeWindow_release(window);
        SSLOG_E("nativeAttach: ASurfaceControl_createFromWindow failed");
        return JNI_FALSE;
    }

    std::shared_ptr<ControlGeneration> generation;
    try {
        generation = std::make_shared<ControlGeneration>();
    } catch (...) {
        g_api.release(control);
        ANativeWindow_release(window);
        SSLOG_E("nativeAttach: control-generation allocation failed");
        return JNI_FALSE;
    }
    generation->control = control;
    generation->release = g_api.release;
    generation->generation = g_output.nextGeneration++;

    g_output.window      = window;
    g_output.control     = control;
    g_output.controlGeneration = std::move(generation);
    g_output.panelWidth  = panelWidth;
    g_output.panelHeight = panelHeight;
    g_cpuHealth = {};
    SSLOG("Second screen attached: %dx%d", panelWidth, panelHeight);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_SecondScreenOutput_nativeDetach(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(g_lock);
    destroyLocked();
}

/**
 * Shows one guest frame on the panel.
 *
 * The fence is the guest's: the buffer is not finished being written when this
 * is called, and handing SurfaceFlinger the fence is what lets it wait on the
 * GPU rather than making us wait on the CPU. Ownership of the descriptor passes
 * with the transaction, so it must not be closed once it has been handed over.
 *
 * Ownership before that point is ours, and the early returns are where that
 * matters. A caller that hands over a real fence and is refused -- because the
 * panel has just detached, or because a transaction could not be allocated --
 * has already given the descriptor away. Returning without closing it leaks one
 * per frame, and the detach case is exactly the one that repeats. Today both
 * callers pass -1, so this closes nothing; it is here so that the first caller
 * to pass a real fence does not have to discover the rule the hard way.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_SecondScreenOutput_nativeSetBuffer(
        JNIEnv*, jclass, jlong ahbPtr, jint fenceFd, jint srcWidth, jint srcHeight)
{
    std::lock_guard<std::mutex> guard(g_lock);
    return setBufferLocked(reinterpret_cast<AHardwareBuffer*>(ahbPtr), fenceFd,
                           srcWidth, srcHeight) ? JNI_TRUE : JNI_FALSE;
#if 0
    if (!g_api.loaded || !g_output.control || ahbPtr == 0) {
        if (fenceFd >= 0) close(fenceFd);
        return JNI_FALSE;
    }

    void* transaction = g_api.txCreate();
    if (!transaction) {
        if (fenceFd >= 0) close(fenceFd);
        return JNI_FALSE;
    }

    g_api.setBuffer(transaction, g_output.control,
                    reinterpret_cast<AHardwareBuffer*>(ahbPtr), fenceFd);

    if (srcWidth != g_output.lastSrcWidth || srcHeight != g_output.lastSrcHeight) {
        ARect source { 0, 0, srcWidth, srcHeight };
        ARect destination;
        computeDestination(srcWidth, srcHeight, &destination);
        g_api.setGeometry(transaction, g_output.control, &source, &destination, 0);
        g_output.lastSrcWidth  = srcWidth;
        g_output.lastSrcHeight = srcHeight;
        SSLOG("Second screen source %dx%d shown at %d,%d-%d,%d",
              srcWidth, srcHeight, destination.left, destination.top,
              destination.right, destination.bottom);
    }

    if (!g_output.visible) {
        g_api.setZOrder(transaction, g_output.control, 1);
        /*
         * Opaque. The guest's frame has an alpha channel it does not mean —
         * a window drawn with alpha 0 in its unused corners would let the
         * console show through, and blending a full-screen layer costs the
         * display controller a composition pass it does not otherwise need.
         */
        if (g_api.setTransparency) g_api.setTransparency(transaction, g_output.control, 0);
        g_api.setVisibility(transaction, g_output.control, 1);
        g_output.visible = true;
    }

    g_api.txApply(transaction);
    g_api.txDelete(transaction);
    return JNI_TRUE;
#endif
}

/**
 * Copies one ordinary X11/GDI window into a lower-panel scanout buffer.
 * Skyrim's panel deliberately avoids a second D3D device and swapchain, which
 * can disturb the primary DXVK/frame-generation swapchain on this device.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_renderer_SecondScreenOutput_nativeSetPixels(
        JNIEnv* env, jclass, jobject pixels, jint srcWidth, jint srcHeight,
        jint srcStride)
{
    if (!pixels || srcWidth <= 0 || srcHeight <= 0 || srcStride < srcWidth)
        return JNI_FALSE;
    auto* source = static_cast<uint8_t*>(env->GetDirectBufferAddress(pixels));
    const jlong capacity = env->GetDirectBufferCapacity(pixels);
    const uint64_t required = static_cast<uint64_t>(srcStride) *
                              static_cast<uint64_t>(srcHeight) * 4u;
    if (!source || capacity < 0 || static_cast<uint64_t>(capacity) < required)
        return JNI_FALSE;

    std::lock_guard<std::mutex> guard(g_lock);
    const uint64_t callStartedNs = monotonicNs();
    maybeReportCpuHealthLocked(callStartedNs);
    ++g_cpuHealth.calls;
    if (!g_api.loaded || !g_output.control ||
            !ensureCpuBuffersLocked(srcWidth, srcHeight)) return JNI_FALSE;

    const std::shared_ptr<CpuRingState> ring = g_output.cpuRing;
    PresentedCpuSlot next;
    AHardwareBuffer* target = nullptr;
    int releaseFenceFd = -1;
    {
        std::lock_guard<std::mutex> ringGuard(ring->mutex);
        if (ring->retired) return JNI_FALSE;
        for (size_t offset = 0; offset < ring->slots.size(); ++offset) {
            const int index = (ring->next + static_cast<int>(offset)) %
                              static_cast<int>(ring->slots.size());
            CpuRingState::Slot& slot = ring->slots[index];
            if (slot.inUse || !slot.buffer) continue;
            if (slot.releaseFenceFd >= 0 &&
                    !releaseFenceReadyNoWait(slot.releaseFenceFd)) {
                ++g_cpuHealth.fenceBusy;
                continue;
            }

            slot.inUse = true;
            releaseFenceFd = slot.releaseFenceFd;
            slot.releaseFenceFd = -1;
            target = slot.buffer;
            next.ring = ring;
            next.index = index;
            ring->next = (index + 1) % static_cast<int>(ring->slots.size());
            break;
        }
    }
    if (!next.valid() || !target) {
        // SurfaceFlinger still owns every buffer. Dropping one UI repaint is
        // preferable to either blocking the game thread or tearing a buffer
        // out from under the display controller.
        ++g_cpuHealth.noSlot;
        return JNI_FALSE;
    }

    AHardwareBuffer_Desc desc = {};
    AHardwareBuffer_describe(target, &desc);
    if (desc.width < static_cast<uint32_t>(srcWidth) ||
            desc.height < static_cast<uint32_t>(srcHeight) ||
            desc.stride < static_cast<uint32_t>(srcWidth)) {
        std::lock_guard<std::mutex> ringGuard(ring->mutex);
        CpuRingState::Slot& slot = ring->slots[next.index];
        slot.releaseFenceFd = releaseFenceFd;
        slot.inUse = false;
        SSLOG_E("CPU panel buffer has invalid dimensions/stride");
        return JNI_FALSE;
    }

    void* destination = nullptr;
    const uint64_t lockStartedNs = monotonicNs();
    const int lockResult = AHardwareBuffer_lock(
            target, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
            releaseFenceFd, nullptr, &destination);
    const uint64_t lockDurationNs = monotonicNs() - lockStartedNs;
    ++g_cpuHealth.lockSamples;
    g_cpuHealth.lockTotalNs += lockDurationNs;
    g_cpuHealth.lockMaxNs = std::max(g_cpuHealth.lockMaxNs, lockDurationNs);
    if (lockResult != 0) {
        ++g_cpuHealth.lockFailures;
        // AHardwareBuffer_lock takes ownership of the input fence. A failed
        // asynchronous lock leaves synchronization state implementation-
        // defined, so quarantine this slot instead of risking a CPU/display
        // write race. The other slots continue to serve frames.
        SSLOG_E("CPU panel buffer lock failed");
        return JNI_FALSE;
    }
    if (!destination) {
        int unlockFenceFd = -1;
        if (AHardwareBuffer_unlock(target, &unlockFenceFd) == 0) {
            std::lock_guard<std::mutex> ringGuard(ring->mutex);
            CpuRingState::Slot& slot = ring->slots[next.index];
            slot.releaseFenceFd = unlockFenceFd;
            slot.inUse = false;
        } else if (unlockFenceFd >= 0) {
            close(unlockFenceFd);
        }
        SSLOG_E("CPU panel buffer lock returned no address");
        return JNI_FALSE;
    }

    const size_t sourcePitch = static_cast<size_t>(srcStride) * 4u;
    const size_t destinationPitch = static_cast<size_t>(desc.stride) * 4u;
    const size_t copyBytes = static_cast<size_t>(srcWidth) * 4u;
    const uint64_t copyStartedNs = monotonicNs();
    for (int y = 0; y < srcHeight; ++y) {
        std::memcpy(static_cast<uint8_t*>(destination) + y * destinationPitch,
                    source + y * sourcePitch, copyBytes);
    }
    const uint64_t copyDurationNs = monotonicNs() - copyStartedNs;
    ++g_cpuHealth.copySamples;
    g_cpuHealth.copyTotalNs += copyDurationNs;
    g_cpuHealth.copyMaxNs = std::max(g_cpuHealth.copyMaxNs, copyDurationNs);
    g_cpuHealth.copiedBytes += copyBytes * static_cast<size_t>(srcHeight);

    int acquireFenceFd = -1;
    if (AHardwareBuffer_unlock(target, &acquireFenceFd) != 0) {
        if (acquireFenceFd >= 0) close(acquireFenceFd);
        SSLOG_E("CPU panel buffer unlock failed");
        return JNI_FALSE;
    }

    void* transaction = g_api.txCreate();
    if (!transaction) {
        // This buffer was never handed to SurfaceFlinger. Preserve the CPU
        // completion fence so its next lock waits before writing it again.
        std::lock_guard<std::mutex> ringGuard(ring->mutex);
        CpuRingState::Slot& slot = ring->slots[next.index];
        slot.releaseFenceFd = acquireFenceFd;
        slot.inUse = false;
        return JNI_FALSE;
    }

    const PresentedCpuSlot previous = g_output.currentCpu;
    // Ownership of the CPU-completion fence passes with setBuffer.
    g_api.setBuffer(transaction, g_output.control, target, acquireFenceFd);

    if (srcWidth != g_output.lastSrcWidth || srcHeight != g_output.lastSrcHeight) {
        ARect sourceRect { 0, 0, srcWidth, srcHeight };
        ARect destinationRect;
        computeDestination(srcWidth, srcHeight, &destinationRect);
        g_api.setGeometry(transaction, g_output.control, &sourceRect,
                          &destinationRect, 0);
        g_output.lastSrcWidth = srcWidth;
        g_output.lastSrcHeight = srcHeight;
        SSLOG("Second screen source %dx%d shown at %d,%d-%d,%d",
              srcWidth, srcHeight, destinationRect.left, destinationRect.top,
              destinationRect.right, destinationRect.bottom);
    }

    if (!g_output.visible) {
        if (g_api.setZOrder) g_api.setZOrder(transaction, g_output.control, 1);
        if (g_api.setTransparency) {
            g_api.setTransparency(transaction, g_output.control, 0);
        }
        g_api.setVisibility(transaction, g_output.control, 1);
        g_output.visible = true;
    }
    if (!attachCpuReleaseCallback(transaction, previous)) {
        // The previous slot deliberately remains in-use if the callback could
        // not be installed; reusing it without its release fence is unsafe.
        SSLOG_E("CPU panel release callback unavailable");
    }
    g_api.txApply(transaction);
    // Publish only after submission. This keeps teardown and a subsequent
    // presentation from treating an unsubmitted slot as the displayed one.
    g_output.currentCpu = next;
    g_api.txDelete(transaction);
    ++g_cpuHealth.accepted;
    return JNI_TRUE;
}

/** Hides the layer without giving up the control, for a window that has gone. */
extern "C" JNIEXPORT void JNICALL
Java_com_wemu_engine_renderer_SecondScreenOutput_nativeSetVisible(
        JNIEnv*, jclass, jboolean visible)
{
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_api.loaded || !g_output.control) return;
    if (g_output.visible == (visible == JNI_TRUE)) return;

    void* transaction = g_api.txCreate();
    if (!transaction) return;
    g_api.setVisibility(transaction, g_output.control, visible == JNI_TRUE ? 1 : 0);
    g_api.txApply(transaction);
    g_api.txDelete(transaction);
    g_output.visible = visible == JNI_TRUE;
}
