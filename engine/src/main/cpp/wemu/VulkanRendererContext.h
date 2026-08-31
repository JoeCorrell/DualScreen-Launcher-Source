#pragma once
#include <vulkan/vulkan.h>
#include <list>
#include <string>
#include <vector>
#include <android/hardware_buffer.h>
#include <vulkan/vulkan_android.h>
struct VkTable {

    PFN_vkCreateInstance CreateInstance;

    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkCreateAndroidSurfaceKHR CreateAndroidSurfaceKHR;

    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers FreeCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdPushConstants CmdPushConstants;
    PFN_vkCmdSetViewport CmdSetViewport;
    PFN_vkCmdSetScissor CmdSetScissor;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdCopyImage CmdCopyImage;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCreateSampler CreateSampler;
    PFN_vkDestroySampler DestroySampler;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkGetFenceStatus GetFenceStatus;

    PFN_vkGetAndroidHardwareBufferPropertiesANDROID GetAndroidHardwareBufferPropertiesANDROID;
};

#include <android/log.h>
#include <string>
#define WLOG_TAG "Wemu_Renderer"
#define RLOG(...) if(verboseLog) __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,__VA_ARGS__)
#define RLOG_E(...) __android_log_print(ANDROID_LOG_ERROR,WLOG_TAG,__VA_ARGS__)


#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct WindowPushConstants      { float ndcX0, ndcY0, ndcX1, ndcY1; int useTexAlpha; };
/*
 * `effectAmount` is not a duplicate of `sharpness`, and the finishing pass is
 * the only place the difference shows. RCAS belongs to FSR and nothing else, so
 * on the SGSR and NIS paths `sharpness` is sent as zero to keep it from running
 * — while `effectAmount` still carries what the user set, so a screen effect
 * that takes a strength is not flattened by whichever upscaler it was combined
 * with. It occupies the slot the per-window `resW` used to, which was only ever
 * read by the post-effect shader this pass replaced.
 */
struct WindowPushConstantsSGSR  { float ndcX0, ndcY0, ndcX1, ndcY1; int useTexAlpha;
                                  float invSrcW, invSrcH, srcW, srcH;
                                  int   effectId;
                                  float effectAmount;
                                  float sharpness; };
struct WindowPushConstantsStretch { float ndcX0, ndcY0, ndcX1, ndcY1; int useTexAlpha; float strength; float profile; };

struct WindowPushConstantsPostFX  { float ndcX0, ndcY0, ndcX1, ndcY1;
                                    int   effectId;   
                                    float sharpness;
                                    float resW, resH; };

class VulkanRendererContext {
public:
    VulkanRendererContext(ANativeWindow* window, int cWidth, int cHeight, void* adrenotoolsHandle = nullptr);
    ~VulkanRendererContext();

    /**
     * Stops the render worker and proves submitted GPU work is retired before
     * destruction. A false result means the caller must intentionally retain
     * this context; freeing it would turn a driver stall into native UAF.
     */
    bool shutdown(uint32_t timeoutMs);

    void onSurfaceResized(int width, int height);
    void setTransform(float ox, float oy, float sx, float sy);
    void updatePointerPosition(short x, short y);
    bool updateWindowContent(int64_t id, void* pixels, short w, short h, short stride, int x, int y);
    bool updateWindowContentAHB(int64_t id, AHardwareBuffer* ahb, short w, short h, int x, int y);
    void updateCursorImage(void* pixels, short w, short h, short hotX, short hotY);
    void setCursorVisible(bool visible);

    /**
     * What shows where no guest window does.
     *
     * The X root, in other words. It was a hard-coded black, which is correct
     * for a game filling the panel and wrong for everything else: a windowed
     * program sat in a void, and the desktop the user had a moment ago appeared
     * to have been replaced by nothing. The desktop cannot be drawn here — it is
     * a Compose layer in another plane entirely — but its background colour can,
     * and that is the difference between "behind this window is the desktop" and
     * "behind this window is a hole".
     *
     * @param argb packed, alpha ignored; the root is always opaque.
     */
    void setRootColor(int argb);
    void setRenderList(const int64_t* ids, const int* xs, const int* ys, int count);
    void removeWindow(int64_t id);
    void clearBackbuffer() {}
    void beginBatch() {}
    void endBatch() {}
    std::atomic<bool> surfaceDetached{false};

    void detachSurface();
    bool reattachSurface(ANativeWindow* newWindow);

    /*
     * Off unless something asks for it.
     *
     * This defaulted to on and nothing in the app ever turned it off, so every
     * session logged its swapchain, pipeline and buffer-import chatter into
     * logd for no reader. None of it is per-frame -- the buffer imports happen
     * once each as the swapchain is built -- so this is not a frame-rate fix,
     * and claiming it as one would be the sort of thing that makes the next
     * measurement harder to trust. It is off because logging that no one asked
     * for is a cost with no benefit, and because a quiet log is what makes an
     * unusual line worth reading.
     *
     * Turn it back on through VulkanRenderer.setVerboseLog(true), which is
     * wired through to here and is the reason the setter stays.
     */
    bool verboseLog = false;
    void setVerboseLog(bool v) { verboseLog = v; }
    void dumpRendererInfo();

    std::string adrenoDriverPath;
    std::string adrenoDriverName;
    std::string adrenoNativeLibDir;
    void* vulkanHandle = nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    VkTable vk_ = {};
    void loadCustomDriver();
    void loadInstanceDispatch();
    void loadDeviceDispatch();

    void setFilterMode(int mode);
    void setStretchMode(int mode);
    void setPostFXMode(int mode);    
    void setSharpness(float s);      
    void setSwapRB(bool enabled);
    void setPresentMode(VkPresentModeKHR mode);
    std::vector<int> getSupportedPresentModes() const;
    VkExtent2D getSwapchainExtent() const { return swapchainExt; }

    void setCustomScissor(int x, int y, int w, int h);
    void clearCustomScissor();
    inline void setTransformAndScissor(float ox, float oy, float sx, float sy,
                                       bool hasScissor,
                                       int scissorX, int scissorY, int scissorW, int scissorH) {
        setTransform(ox, oy, sx, sy);
        if (hasScissor) setCustomScissor(scissorX, scissorY, scissorW, scissorH);
        else            clearCustomScissor();
    }

private:
    /*
     * Wake the render thread, at most once per pending frame.
     *
     * Several X updates routinely arrive before the renderer consumes the
     * first: one window content upload per visible window, a pointer move, a
     * transform. Each used to call notify_one(), and a notify with a waiter
     * parked on it is a futex wake — a syscall, paid once per update, to
     * schedule a thread that is about to sample all of this state anyway.
     *
     * The flag is set under dirtyMutex rather than beside it. Coalescing
     * removes the accidental safety net the old code had: a notify issued
     * without the mutex can land in the window between the render thread
     * evaluating its wait predicate and actually blocking, and be missed. That
     * was survivable while every update notified, because the next one along
     * woke it regardless. Once a second update declines to notify, a missed
     * wakeup is a render thread asleep with a frame pending and nothing
     * scheduled to rouse it.
     */
    void requestRender();
    void requestCursorRender();
    void forceRender();

    struct WinTex {
        VkImage              img            = VK_NULL_HANDLE;
        VkDeviceMemory       mem            = VK_NULL_HANDLE;
        VkImageView          view           = VK_NULL_HANDLE;
        VkDescriptorSet      ds             = VK_NULL_HANDLE;
        VkBuffer             stg            = VK_NULL_HANDLE;
        VkDeviceMemory       stgMem         = VK_NULL_HANDLE;
        void*                mapped         = nullptr;
        VkDeviceSize         cap            = 0;
        int                  w              = 0;
        int                  h              = 0;
        bool                 dirty          = false;
        bool                 isAHB          = false;
        bool                 needsTransition = false;
        AHardwareBuffer*     ahb            = nullptr;
    };

    struct RenderEntry { int64_t id; int x, y; };
    struct DrawEntry {
        VkImage         img            = VK_NULL_HANDLE;
        VkDescriptorSet ds             = VK_NULL_HANDLE;
        VkBuffer        upload         = VK_NULL_HANDLE;
        int             x=0, y=0, w=0, h=0;
        bool            needsTransition = false;
        bool            isAHB          = false;
    };

    ANativeWindow* window;
    int surfaceWidth, surfaceHeight, containerWidth, containerHeight;
    void* adrenotoolsHandle = nullptr;
    int filterMode  = 0;
    int stretchMode = 0;
    int postFXMode  = 0;  
    float stretchStrength = 0.40f;
    float stretchProfile  = 0.60f;
    float sharpness = 0.5f;
    bool swapRB = false;
    float maxAnisotropy           = 1.0f;
    VkPhysicalDeviceMemoryProperties memProperties{};
    VkPresentModeKHR requestedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t graphicsQueueFamilyIndex = 0;
    std::vector<VkPresentModeKHR> availablePresentModes;

    std::unordered_map<int64_t, WinTex>         texMap;

    std::unordered_map<AHardwareBuffer*, WinTex>              ahbImportCache;
    std::unordered_map<int64_t, std::vector<AHardwareBuffer*>> windowAhbs;

    std::vector<WinTex>    deleteQueue;
    std::vector<RenderEntry> renderList;

    std::vector<DrawEntry>             frameDraws;
    std::vector<VkImageMemoryBarrier>  frameAhbTransitions;
    std::vector<VkImageMemoryBarrier>  framePreUpload;
    std::vector<VkImageMemoryBarrier>  framePostUpload;

    std::atomic<int>  pointerX{0}, pointerY{0};
    float sceneOffsetX=0.f, sceneOffsetY=0.f, sceneScaleX=1.f, sceneScaleY=1.f;

    /** Packed 0x00RRGGBB. Black until the session says otherwise. */
    std::atomic<int> rootColor{0};

    std::atomic<bool> cursorVisible{false};
    short  cursorHotX=0, cursorHotY=0, cursorTexW=0, cursorTexH=0;
    std::vector<uint32_t>  cursorPixels;
    std::atomic<bool> isCursorImageDirty{false};
    std::atomic<bool> cursorMoved{false};

    VkImage         cursorImg   = VK_NULL_HANDLE;
    VkDeviceMemory  cursorMem   = VK_NULL_HANDLE;
    VkImageView     cursorView  = VK_NULL_HANDLE;
    VkDescriptorSet  cursorDS   = VK_NULL_HANDLE;
    VkBuffer         cursorStg  = VK_NULL_HANDLE;
    VkDeviceMemory   cursorStgM = VK_NULL_HANDLE;
    void*            cursorStgP = nullptr;
    VkDeviceSize     cursorStgC = 0;
    VkDeviceSize     cursorUploadSize = 0;

    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice physicalDevice;
    VkDevice         device;
    VkQueue          graphicsQueue;
    VkSwapchainKHR   swapchain   = VK_NULL_HANDLE;
    VkFormat         swapchainFmt;
    VkExtent2D       swapchainExt;

    std::vector<VkImage>       swapchainImages;
    std::vector<VkImageView>   swapchainViews;
    std::vector<VkFramebuffer> swapchainFBs;

    VkRenderPass          renderPass  = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout    = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout  = VK_NULL_HANDLE;

    VkPipeline            pipeline    = VK_NULL_HANDLE;
    VkPipeline            sgsrPipeline   = VK_NULL_HANDLE;
    /** FSR 1.0 EASU. Built on demand, like SGSR — most sessions use neither. */
    VkPipeline            fsrPipeline    = VK_NULL_HANDLE;
    /** NVIDIA Image Scaling. Sharpens as it scales, so it needs no second pass. */
    VkPipeline            nisPipeline    = VK_NULL_HANDLE;
    VkPipeline            stretchPipeline= VK_NULL_HANDLE;
    VkPipeline            postfxPipeline = VK_NULL_HANDLE;

    /*
     * The finishing pass and the target that makes it possible.
     *
     * Some work has to happen after the whole frame is composited rather than
     * per window: RCAS sharpens the *upscaled* image, and a screen effect
     * grades the pixels that will actually be lit. Both need the composited
     * frame as a texture rather than as the thing being presented, so when an
     * upscaler is engaged the windows go into `fsrImg` — one full-screen colour
     * attachment — and a single quad reads it into the swapchain.
     *
     * Which is also what let the two stop being alternatives. `window_finish`
     * runs the sharpener and the effect in that order in one pass; before it
     * existed the pipeline chain in `recordCmdBuf` picked exactly one, and an
     * upscaler always outranked the effects, so choosing one turned the other
     * off in silence.
     *
     * Allocated for any upscaler, not only FSR: SGSR and NIS have no sharpening
     * pass of their own but can still be asked for an effect. Nothing is
     * allocated when no upscaler is set — an effect on its own is cheaper drawn
     * per window, which is the path `postfxPipeline` still serves.
     *
     * One image, not one per frame in flight. `fsrRenderPass` carries an
     * external subpass dependency from FRAGMENT_SHADER reads to
     * COLOR_ATTACHMENT_OUTPUT writes, which orders the next frame's composite
     * after the previous frame's finishing read in submission order — cheaper
     * than eight more megabytes for the same guarantee.
     */
    VkPipeline            finishPipeline = VK_NULL_HANDLE;
    VkRenderPass          fsrRenderPass  = VK_NULL_HANDLE;
    VkImage               fsrImg         = VK_NULL_HANDLE;
    VkDeviceMemory        fsrMem         = VK_NULL_HANDLE;
    VkImageView           fsrView        = VK_NULL_HANDLE;
    VkFramebuffer         fsrFB          = VK_NULL_HANDLE;
    VkDescriptorSet       fsrDS          = VK_NULL_HANDLE;

    VkCommandPool                cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs;

    std::vector<VkSemaphore> imgAvailSems;
    std::vector<VkSemaphore> renderDoneSems;
    std::vector<VkFence>     inFlightFences;
    std::vector<VkFence>     imgInFlight;
    uint32_t                 currentFrame = 0;

    VkSampler        sampler    = VK_NULL_HANDLE;
    VkDescriptorPool winTexPool = VK_NULL_HANDLE;

    std::atomic<bool> needsRender{false};
    std::thread       renderThread;
    std::atomic<bool> isRunning{false};
    std::atomic<bool> fbResized{false};
    std::mutex        renderMutex;
    std::mutex        dirtyMutex;
    std::condition_variable dirtyCV;
    std::shared_timed_mutex frameMutex;
    std::mutex exitMutex;
    std::condition_variable exitCV;
    std::atomic<bool> renderExited{false};
    std::atomic<bool> rendererFailed{false};
    std::atomic<bool> unsafeToDestroy{false};
    std::atomic<bool> detachIncomplete{false};

    VkRect2D          customScissor    = {{0,0},{0,0}};
    bool              hasCustomScissor = false;

    /*
     * Frame generation is not here.
     *
     * It was, twice, and both versions are gone: this device does not own the
     * game's frames, so generating one meant copying into an AHardwareBuffer,
     * crossing to a second Vulkan device and waiting on it. It runs as a
     * Vulkan layer inside the guest now. See `FrameGeneration.java`.
     */

    /** Draws and presents one interpolated frame. Returns false if it could not. */
    bool presentInterpolated();

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createRenderPass();
    void createDSLayout();
    void createPipeline(bool blend, VkPipeline& out);
    void createSgsrPipeline();
    void createFsrPipeline();
    void createNisPipeline();
    void createFinishPipeline();
    void createFsrRenderPass();
    /** Builds the EASU target if FSR is selected and it is not built already. */
    void ensureFsrTarget();
    void destroyFsrTarget();
    void createStretchPipeline();
    void createPostFXPipeline();
    void createFramebuffers();
    void createCmdPool();
    void createSampler();
    void createWinTexPool();
    void createCursorPipeline();
    void createCursorDS();
    void createCmdBufs();
    void createSyncObjects();
    void cleanupSwapchain();

    bool  createWinTexResources(WinTex& wt, int w, int h);
    bool  importAHBToWinTex(WinTex& wt, AHardwareBuffer* ahb);
    void  cleanupAllAHBCache();
    void  flushDeleteQueue();
    void  destroyWinTex(WinTex& wt);
    void  ensureCursorTex(short w, short h);
    void  cleanupCursorTex();
    void  ensureCursorStaging(VkDeviceSize sz);

    void recordCmdBuf(VkCommandBuffer cb, uint32_t imgIdx,
        const std::vector<DrawEntry>& draws,
        std::vector<VkImageMemoryBarrier>& ahbTransitions,
        std::vector<VkImageMemoryBarrier>& preUpload,
        std::vector<VkImageMemoryBarrier>& postUpload,
        VkBuffer cursorUpload, bool hasCursorUpload,
        float ox, float oy, float sx, float sy, float cw, float ch,
        short ptrX, short ptrY, short curHotX, short curHotY,
        short curW, short curH, bool curVis,
        VkRect2D scissorRect);
    void renderLoop();
    void renderFrame();
    bool waitForFence(VkFence fence, const char* operation);
    bool waitForAllFences(const char* operation);
    bool allFrameFencesSignaled();
    void markRendererFailed(const char* operation, VkResult result);

    uint32_t        findMemType(uint32_t filter, VkMemoryPropertyFlags props);
    void            createBuffer(VkDeviceSize sz, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);
    VkCommandBuffer beginOneTime();
    void            endOneTime(VkCommandBuffer cmd);
    void            transition(VkCommandBuffer cmd, VkImage img,
                               VkImageLayout oldL, VkImageLayout newL,
                               VkAccessFlags srcA, VkAccessFlags dstA,
                               VkPipelineStageFlags srcS, VkPipelineStageFlags dstS);
    VkShaderModule  makeShader(const uint32_t* code, size_t sz);
};
