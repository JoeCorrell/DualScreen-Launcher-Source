#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "VulkanRendererContext.h"

#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <inttypes.h>
#include <dlfcn.h>
#include "window_vert.h"
#include "window_frag.h"
#include "window_sgsr_frag.h"
#include "window_fsr_frag.h"
#include "window_finish_frag.h"
#include "window_nis_frag.h"
#include "window_stretch_frag.h"
#include "window_postfx_frag.h"

static constexpr uint64_t kGpuWaitTimeoutNs = 500'000'000ULL;
static constexpr auto kFrameLockTimeout = std::chrono::milliseconds(500);

VulkanRendererContext::VulkanRendererContext(ANativeWindow* win, int cW, int cH, void* aHandle)
    : window(win), surfaceWidth(cW), surfaceHeight(cH), containerWidth(cW), containerHeight(cH),
      adrenotoolsHandle(aHandle)
{
    createInstance(); createSurface(); pickPhysicalDevice(); createLogicalDevice();
    createSwapchain(); createRenderPass(); createDSLayout();
    createPipeline(true, pipeline);
    createFramebuffers(); createCmdPool(); createSampler();
    createWinTexPool(); createCursorDS(); createCmdBufs(); createSyncObjects();
    isRunning = true;
    renderThread = std::thread(&VulkanRendererContext::renderLoop, this);
}

VulkanRendererContext::~VulkanRendererContext() {
    // nativeDestroy calls shutdown first and deliberately leaks this context if
    // a vendor call cannot be retired. Keep this idempotent for construction
    // and future native callers.
    shutdown(2000);
    std::lock_guard<std::mutex> lk(renderMutex);
    for (auto& [id, wt] : texMap) destroyWinTex(wt);
    texMap.clear();

    cleanupAllAHBCache();

    for (auto& wt : deleteQueue) {
        if (wt.ds   != VK_NULL_HANDLE) vk_.FreeDescriptorSets(device, winTexPool, 1, &wt.ds);
        if (wt.view != VK_NULL_HANDLE) vk_.DestroyImageView(device, wt.view, nullptr);
        if (wt.img  != VK_NULL_HANDLE) vk_.DestroyImage(device, wt.img, nullptr);
        if (wt.mem  != VK_NULL_HANDLE) vk_.FreeMemory(device, wt.mem, nullptr);
        if (wt.stg  != VK_NULL_HANDLE) { vk_.DestroyBuffer(device, wt.stg, nullptr); vk_.FreeMemory(device, wt.stgMem, nullptr); }
    }
    deleteQueue.clear();
    cleanupSwapchain(); cleanupCursorTex();

    vk_.DestroySampler(device, sampler, nullptr);
    vk_.DestroyDescriptorPool(device, winTexPool, nullptr);
    if (sgsrPipeline != VK_NULL_HANDLE) vk_.DestroyPipeline(device, sgsrPipeline, nullptr);
    if (fsrPipeline  != VK_NULL_HANDLE) vk_.DestroyPipeline(device, fsrPipeline,  nullptr);
    if (nisPipeline  != VK_NULL_HANDLE) vk_.DestroyPipeline(device, nisPipeline,  nullptr);
    if (finishPipeline != VK_NULL_HANDLE) vk_.DestroyPipeline(device, finishPipeline, nullptr);
    if (stretchPipeline != VK_NULL_HANDLE) vk_.DestroyPipeline(device, stretchPipeline, nullptr);
    if (postfxPipeline != VK_NULL_HANDLE) vk_.DestroyPipeline(device, postfxPipeline, nullptr);
    vk_.DestroyPipeline(device, pipeline, nullptr);
    vk_.DestroyPipelineLayout(device, pipeLayout, nullptr);
    vk_.DestroyDescriptorSetLayout(device, dsLayout, nullptr);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk_.DestroySemaphore(device, renderDoneSems[i], nullptr);
        vk_.DestroySemaphore(device, imgAvailSems[i], nullptr);
        vk_.DestroyFence(device, inFlightFences[i], nullptr);
    }
    vk_.DestroyCommandPool(device, cmdPool, nullptr);
    if (fsrRenderPass != VK_NULL_HANDLE) vk_.DestroyRenderPass(device, fsrRenderPass, nullptr);
    vk_.DestroyRenderPass(device, renderPass, nullptr);
    vk_.DestroyDevice(device, nullptr);
    vk_.DestroySurfaceKHR(instance, surface, nullptr);
    vk_.DestroyInstance(instance, nullptr);
    if (window) {
        ANativeWindow_release(window);
        window = nullptr;
    }
    if (adrenotoolsHandle) { dlclose(adrenotoolsHandle); adrenotoolsHandle = nullptr; }
}

bool VulkanRendererContext::shutdown(uint32_t timeoutMs) {
    isRunning.store(false, std::memory_order_release);
    dirtyCV.notify_all();
    if (renderThread.joinable()) {
        std::unique_lock<std::mutex> lock(exitMutex);
        if (!exitCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return renderExited.load(std::memory_order_acquire); })) {
            RLOG_E("shutdown: render worker did not exit within %u ms; retaining context",
                   timeoutMs);
            return false;
        }
        lock.unlock();
        renderThread.join();
    }
    if (unsafeToDestroy.load(std::memory_order_acquire)) return false;
    return waitForAllFences("shutdown");
}

void VulkanRendererContext::markRendererFailed(const char* operation, VkResult result) {
    if (!rendererFailed.exchange(true, std::memory_order_acq_rel)) {
        RLOG_E("%s failed or timed out (VkResult=%d); renderer stopped safely",
               operation, static_cast<int>(result));
    }
    isRunning.store(false, std::memory_order_release);
    dirtyCV.notify_all();
}

bool VulkanRendererContext::waitForFence(VkFence fence, const char* operation) {
    if (fence == VK_NULL_HANDLE) return true;
    const VkResult result = vk_.WaitForFences(
            device, 1, &fence, VK_TRUE, kGpuWaitTimeoutNs);
    if (result == VK_SUCCESS) return true;
    unsafeToDestroy.store(true, std::memory_order_release);
    markRendererFailed(operation, result);
    return false;
}

bool VulkanRendererContext::waitForAllFences(const char* operation) {
    for (VkFence fence : inFlightFences) {
        if (!waitForFence(fence, operation)) return false;
    }
    return true;
}

bool VulkanRendererContext::allFrameFencesSignaled() {
    for (VkFence fence : inFlightFences) {
        if (fence == VK_NULL_HANDLE) continue;
        const VkResult result = vk_.GetFenceStatus(device, fence);
        if (result == VK_NOT_READY) return false;
        if (result != VK_SUCCESS) {
            markRendererFailed("vkGetFenceStatus", result);
            return false;
        }
    }
    return true;
}

void VulkanRendererContext::loadInstanceDispatch() {
    auto i = [&](const char* name) { return gipa ? gipa(instance, name) : nullptr; };
#define LOAD_I2(fn) vk_.fn = (PFN_vk##fn)i("vk"#fn)
    LOAD_I2(DestroyInstance);
    LOAD_I2(EnumeratePhysicalDevices);
    LOAD_I2(GetPhysicalDeviceProperties);
    LOAD_I2(GetPhysicalDeviceMemoryProperties);
    LOAD_I2(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_I2(GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_I2(GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD_I2(GetPhysicalDeviceQueueFamilyProperties);
    LOAD_I2(GetPhysicalDeviceSurfaceSupportKHR);
    LOAD_I2(CreateDevice);
    LOAD_I2(DestroySurfaceKHR);
    LOAD_I2(CreateAndroidSurfaceKHR);
    LOAD_I2(GetDeviceProcAddr);
}

void VulkanRendererContext::loadDeviceDispatch() {
    auto d = [&](const char* name) -> PFN_vkVoidFunction {
        return vk_.GetDeviceProcAddr ? vk_.GetDeviceProcAddr(device, name) : nullptr;
    };
#define LOAD_D2(fn) vk_.fn = (PFN_vk##fn)d("vk"#fn)
    LOAD_D2(DestroyDevice);
    LOAD_D2(GetDeviceQueue);
    LOAD_D2(DeviceWaitIdle);
    LOAD_D2(CreateSwapchainKHR);
    LOAD_D2(DestroySwapchainKHR);
    LOAD_D2(GetSwapchainImagesKHR);
    LOAD_D2(AcquireNextImageKHR);
    LOAD_D2(QueuePresentKHR);
    LOAD_D2(QueueSubmit);
    LOAD_D2(CreateRenderPass);
    LOAD_D2(DestroyRenderPass);
    LOAD_D2(CreateFramebuffer);
    LOAD_D2(DestroyFramebuffer);
    LOAD_D2(CreateImageView);
    LOAD_D2(DestroyImageView);
    LOAD_D2(CreateImage);
    LOAD_D2(DestroyImage);
    LOAD_D2(CreateBuffer);
    LOAD_D2(DestroyBuffer);
    LOAD_D2(AllocateMemory);
    LOAD_D2(FreeMemory);
    LOAD_D2(MapMemory);
    LOAD_D2(FlushMappedMemoryRanges);
    LOAD_D2(BindBufferMemory);
    LOAD_D2(BindImageMemory);
    LOAD_D2(GetBufferMemoryRequirements);
    LOAD_D2(GetImageMemoryRequirements);
    LOAD_D2(CreateDescriptorSetLayout);
    LOAD_D2(DestroyDescriptorSetLayout);
    LOAD_D2(CreateDescriptorPool);
    LOAD_D2(DestroyDescriptorPool);
    LOAD_D2(AllocateDescriptorSets);
    LOAD_D2(FreeDescriptorSets);
    LOAD_D2(UpdateDescriptorSets);
    LOAD_D2(CreatePipelineLayout);
    LOAD_D2(DestroyPipelineLayout);
    LOAD_D2(CreateShaderModule);
    LOAD_D2(DestroyShaderModule);
    LOAD_D2(CreateGraphicsPipelines);
    LOAD_D2(DestroyPipeline);
    LOAD_D2(CreateCommandPool);
    LOAD_D2(DestroyCommandPool);
    LOAD_D2(AllocateCommandBuffers);
    LOAD_D2(FreeCommandBuffers);
    LOAD_D2(BeginCommandBuffer);
    LOAD_D2(EndCommandBuffer);
    LOAD_D2(ResetCommandBuffer);
    LOAD_D2(CmdBeginRenderPass);
    LOAD_D2(CmdEndRenderPass);
    LOAD_D2(CmdBindPipeline);
    LOAD_D2(CmdBindDescriptorSets);
    LOAD_D2(CmdDraw);
    LOAD_D2(CmdPushConstants);
    LOAD_D2(CmdSetViewport);
    LOAD_D2(CmdSetScissor);
    LOAD_D2(CmdPipelineBarrier);
    LOAD_D2(CmdCopyImage);
    LOAD_D2(CmdCopyBufferToImage);
    LOAD_D2(CreateSampler);
    LOAD_D2(DestroySampler);
    LOAD_D2(CreateSemaphore);
    LOAD_D2(DestroySemaphore);
    LOAD_D2(CreateFence);
    LOAD_D2(DestroyFence);
    LOAD_D2(WaitForFences);
    LOAD_D2(ResetFences);
    LOAD_D2(GetFenceStatus);

    vk_.GetAndroidHardwareBufferPropertiesANDROID =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)d("vkGetAndroidHardwareBufferPropertiesANDROID");
}

void VulkanRendererContext::createInstance() {
    RLOG("createInstance: adrenotoolsHandle=%p (custom driver %s)",
        adrenotoolsHandle, adrenotoolsHandle?"ACTIVE":"NOT SET - using stock driver");

    if (adrenotoolsHandle) {
        gipa = (PFN_vkGetInstanceProcAddr)dlsym(adrenotoolsHandle, "vkGetInstanceProcAddr");
    }
    if (!gipa) {
        void* loaderLib = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
        if (loaderLib)
            gipa = (PFN_vkGetInstanceProcAddr)dlsym(loaderLib, "vkGetInstanceProcAddr");
    }

    vk_.CreateInstance = (PFN_vkCreateInstance)gipa(nullptr, "vkCreateInstance");
    VkApplicationInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName="Wemu"; ai.apiVersion=VK_API_VERSION_1_3;
    const char* ext[]={"VK_KHR_surface","VK_KHR_android_surface"};
    VkInstanceCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo=&ai; ci.enabledExtensionCount=2; ci.ppEnabledExtensionNames=ext;
    if (vk_.CreateInstance(&ci,nullptr,&instance)!=VK_SUCCESS) throw std::runtime_error("instance");

    loadInstanceDispatch();
}

void VulkanRendererContext::createSurface() {
    VkAndroidSurfaceCreateInfoKHR ci{}; ci.sType=VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window=window;
    if (vk_.CreateAndroidSurfaceKHR(instance,&ci,nullptr,&surface)!=VK_SUCCESS) throw std::runtime_error("surface");
}

void VulkanRendererContext::pickPhysicalDevice() {
    uint32_t n=0; vk_.EnumeratePhysicalDevices(instance,&n,nullptr);
    std::vector<VkPhysicalDevice> devs(n); vk_.EnumeratePhysicalDevices(instance,&n,devs.data());
    physicalDevice = VK_NULL_HANDLE;
    graphicsQueueFamilyIndex = 0;
    for (auto d : devs) {
        uint32_t qCount = 0;
        vk_.GetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vk_.GetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());
        for (uint32_t i = 0; i < qCount; i++) {
            VkBool32 present = VK_FALSE;
            vk_.GetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                physicalDevice = d;
                graphicsQueueFamilyIndex = i;
                return;
            }
        }
    }
    if (n > 0) physicalDevice = devs[0];
}

void VulkanRendererContext::createLogicalDevice() {
    float p=1.f;
    VkDeviceQueueCreateInfo qi{}; qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex=graphicsQueueFamilyIndex; qi.queueCount=1; qi.pQueuePriorities=&p;

    PFN_vkEnumerateDeviceExtensionProperties enumDevExts =
        (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");
    { uint32_t n=0; if(enumDevExts) enumDevExts(physicalDevice,nullptr,&n,nullptr);
      std::vector<VkExtensionProperties> av(n);
      if(enumDevExts) enumDevExts(physicalDevice,nullptr,&n,av.data());
      for (auto& e:av) {
          (void)e;
      } }
    std::vector<const char*> extList = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME
    };
    VkDeviceCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pQueueCreateInfos=&qi; ci.queueCreateInfoCount=1;
    ci.enabledExtensionCount=(uint32_t)extList.size(); ci.ppEnabledExtensionNames=extList.data();
    if (vk_.CreateDevice(physicalDevice,&ci,nullptr,&device)!=VK_SUCCESS) throw std::runtime_error("device");
    vk_.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
    loadDeviceDispatch();
    vk_.GetDeviceQueue(device,graphicsQueueFamilyIndex,0,&graphicsQueue);

    vk_.GetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    VkPhysicalDeviceProperties props{};
    vk_.GetPhysicalDeviceProperties(physicalDevice, &props);
    maxAnisotropy = props.limits.maxSamplerAnisotropy;
}

void VulkanRendererContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vk_.GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,surface,&caps);

    if (caps.currentExtent.width != 0xFFFFFFFF) {
        swapchainExt = caps.currentExtent;
    } else if (window) {
        int winW = ANativeWindow_getWidth(window);
        int winH = ANativeWindow_getHeight(window);
        swapchainExt = {
            (uint32_t)std::clamp(winW, (int)caps.minImageExtent.width, (int)caps.maxImageExtent.width),
            (uint32_t)std::clamp(winH, (int)caps.minImageExtent.height, (int)caps.maxImageExtent.height)
        };
    } else {
        swapchainExt = {(uint32_t)surfaceWidth, (uint32_t)surfaceHeight};
    }
    uint32_t fmtN=0; vk_.GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,surface,&fmtN,nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtN); vk_.GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,surface,&fmtN,fmts.data());
    swapchainFmt = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t imgCount=caps.minImageCount+1;
    if (caps.maxImageCount>0&&imgCount>caps.maxImageCount) imgCount=caps.maxImageCount;

    uint32_t pmCount=0;
    vk_.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,surface,&pmCount,nullptr);
    availablePresentModes.resize(pmCount);
    vk_.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,surface,&pmCount,availablePresentModes.data());
    VkPresentModeKHR presentMode=VK_PRESENT_MODE_FIFO_KHR;
    for (auto pm:availablePresentModes) if(pm==requestedPresentMode){presentMode=pm;break;}
    if(verboseLog){
        std::string pmList;
        for(auto pm:availablePresentModes) pmList+=std::to_string((int)pm)+" ";
        RLOG("createSwapchain: %dx%d fmt=%d supportedPresentModes=[%s] chosen=%d req=%d",
            swapchainExt.width,swapchainExt.height,(int)swapchainFmt,pmList.c_str(),(int)presentMode,(int)requestedPresentMode);
    }

    VkSurfaceTransformFlagBitsKHR pre=
        (caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)?
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:caps.currentTransform;

    VkCompositeAlphaFlagBitsKHR compositeAlpha=
        (caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)?
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR:VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainKHR oldSwapchain=swapchain;
    VkSwapchainCreateInfoKHR ci{}; ci.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface=surface; ci.minImageCount=imgCount; ci.imageFormat=swapchainFmt;
    ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; ci.imageExtent=swapchainExt;
    ci.imageArrayLayers=1;
    /*
     * TRANSFER_SRC only when something is going to read these images.
     *
     * Frame generation copies each finished frame out of the swapchain, which
     * needs the bit — but asking for it unconditionally is not free. On this
     * GPU a swapchain image that can be a transfer source loses framebuffer
     * compression, and that is bandwidth spent on every frame of every
     * session, including the overwhelming majority that never generate a
     * frame. It was requested unconditionally for one build and made the
     * compositor slower across the board with the feature switched off, which
     * is the kind of cost that is easy to add and hard to notice.
     *
     * `setFrameGeneration` forces a swapchain rebuild when this changes, so
     * turning the feature on mid-session still gets a swapchain it can copy.
     */
    ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; ci.preTransform=pre;
    ci.compositeAlpha=compositeAlpha; ci.presentMode=presentMode; ci.clipped=VK_TRUE;
    ci.oldSwapchain=oldSwapchain;
    if (vk_.CreateSwapchainKHR(device,&ci,nullptr,&swapchain)!=VK_SUCCESS) throw std::runtime_error("swapchain");
    RLOG("swapchain created: %dx%d format=%d presentMode=%d compositeAlpha=%d imgCount=%u",
        swapchainExt.width,swapchainExt.height,(int)swapchainFmt,(int)presentMode,(int)compositeAlpha,imgCount);
    if (oldSwapchain!=VK_NULL_HANDLE) vk_.DestroySwapchainKHR(device,oldSwapchain,nullptr);
    vk_.GetSwapchainImagesKHR(device,swapchain,&imgCount,nullptr);
    swapchainImages.resize(imgCount); vk_.GetSwapchainImagesKHR(device,swapchain,&imgCount,swapchainImages.data());
    swapchainViews.resize(imgCount);
    for (size_t i=0;i<imgCount;i++) {
        VkImageViewCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image=swapchainImages[i]; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=swapchainFmt;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkComponentMapping mapping{};
        mapping.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        mapping.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        mapping.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        mapping.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components = mapping;
        if (vk_.CreateImageView(device,&vi,nullptr,&swapchainViews[i])!=VK_SUCCESS) throw std::runtime_error("imgview");
    }
}

void VulkanRendererContext::createRenderPass() {
    VkAttachmentDescription att{}; att.format=swapchainFmt; att.samples=VK_SAMPLE_COUNT_1_BIT;
    att.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1; sub.pColorAttachments=&ref;
    VkSubpassDependency dep{}; dep.srcSubpass=VK_SUBPASS_EXTERNAL; dep.dstSubpass=0;
    dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.srcAccessMask=0;
    dep.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount=1; ci.pAttachments=&att; ci.subpassCount=1; ci.pSubpasses=&sub;
    ci.dependencyCount=1; ci.pDependencies=&dep;
    if (vk_.CreateRenderPass(device,&ci,nullptr,&renderPass)!=VK_SUCCESS) throw std::runtime_error("renderpass");
}

void VulkanRendererContext::createDSLayout() {
    VkDescriptorSetLayoutBinding b{}; b.binding=0; b.descriptorCount=1;
    b.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount=1; ci.pBindings=&b;
    if (vk_.CreateDescriptorSetLayout(device,&ci,nullptr,&dsLayout)!=VK_SUCCESS) throw std::runtime_error("dslayout");
}

VkShaderModule VulkanRendererContext::makeShader(const uint32_t* code, size_t sz) {
    VkShaderModuleCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize=sz; ci.pCode=code; VkShaderModule m;
    if (vk_.CreateShaderModule(device,&ci,nullptr,&m)!=VK_SUCCESS) throw std::runtime_error("shader");
    return m;
}

void VulkanRendererContext::createPipeline(bool blend, VkPipeline& out) {
    if (pipeLayout==VK_NULL_HANDLE) {
        VkPushConstantRange pc{}; pc.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
        pc.size=sizeof(WindowPushConstantsSGSR);
        VkPipelineLayoutCreateInfo li{}; li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount=1; li.pSetLayouts=&dsLayout; li.pushConstantRangeCount=1; li.pPushConstantRanges=&pc;
        if (vk_.CreatePipelineLayout(device,&li,nullptr,&pipeLayout)!=VK_SUCCESS) throw std::runtime_error("pipelayout");
    }
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_frag_code,sizeof(window_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=blend?VK_TRUE:VK_FALSE;
    if (blend){ba.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;ba.colorBlendOp=VK_BLEND_OP_ADD;ba.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;ba.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;ba.alphaBlendOp=VK_BLEND_OP_ADD;}
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    if (vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&out)!=VK_SUCCESS) throw std::runtime_error("pipeline");
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
}

void VulkanRendererContext::createCursorPipeline() {  }

/*
 * FSR's pipeline is SGSR's with a different fragment shader.
 *
 * Same vertex stage, same layout, same push constant block — which is the
 * point: both are spatial upscalers taking a source size and an output
 * rectangle, so sharing `WindowPushConstantsSGSR` means the draw path does not
 * have to care which of them is bound.
 */
void VulkanRendererContext::createFsrPipeline() {
    if (fsrPipeline != VK_NULL_HANDLE) return;
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_fsr_frag_code,sizeof(window_fsr_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    VkResult r = vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&fsrPipeline);
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    if (r!=VK_SUCCESS) throw std::runtime_error("fsr_pipeline");
    RLOG("createFsrPipeline: done");
}

/*
 * NIS's pipeline. Structurally SGSR's, like every upscaler here.
 *
 * Unlike FSR it needs no second pass: NVScaler's sharpening is part of the
 * scaling filter rather than a stage after it, so the sharpness dial reaches it
 * through the same push constant and the result goes straight to the swapchain.
 */
void VulkanRendererContext::createNisPipeline() {
    if (nisPipeline != VK_NULL_HANDLE) return;
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_nis_frag_code,sizeof(window_nis_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    VkResult r = vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&nisPipeline);
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    if (r!=VK_SUCCESS) { nisPipeline=VK_NULL_HANDLE; RLOG_E("createNisPipeline: failed with status=%d",(int)r); return; }
    RLOG("createNisPipeline: done");
}

/*
 * The finishing pipeline, which draws into the swapchain and so belongs to the
 * normal render pass. It is the upscalers' opposite number: those are built
 * against `renderPass` but *used* inside `fsrRenderPass`, which is legal
 * because the two passes are compatible — same format, same sample count, one
 * colour attachment each — and render pass compatibility deliberately ignores
 * load/store ops and layouts, which is all they differ by.
 */
void VulkanRendererContext::createFinishPipeline() {
    if (finishPipeline != VK_NULL_HANDLE) return;
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_finish_frag_code,sizeof(window_finish_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    VkResult r = vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&finishPipeline);
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    if (r!=VK_SUCCESS) { finishPipeline=VK_NULL_HANDLE; RLOG_E("createFinishPipeline: failed with status=%d",(int)r); return; }
    RLOG("createFinishPipeline: done");
}

/*
 * The pass EASU renders into. Two differences from the swapchain's pass carry
 * all the meaning: it ends in SHADER_READ_ONLY_OPTIMAL instead of
 * PRESENT_SRC_KHR, and it declares both of its external dependencies.
 *
 * The outgoing one makes EASU's writes visible to RCAS's sample later in the
 * same command buffer; the implicit dependency Vulkan would supply orders the
 * layout transition but makes nothing available, so without this the sharpened
 * frame would read whatever the tiler had flushed by chance.
 *
 * The incoming one is what lets a single image serve two frames in flight: it
 * holds the next frame's EASU writes until the previous frame's RCAS read has
 * finished, and an external dependency's first scope spans earlier submissions
 * on the same queue, not just earlier commands in this buffer.
 */
void VulkanRendererContext::createFsrRenderPass() {
    if (fsrRenderPass != VK_NULL_HANDLE) return;
    VkAttachmentDescription att{}; att.format=swapchainFmt; att.samples=VK_SAMPLE_COUNT_1_BIT;
    att.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1; sub.pColorAttachments=&ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass=VK_SUBPASS_EXTERNAL; deps[0].dstSubpass=0;
    deps[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0; deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount=1; ci.pAttachments=&att; ci.subpassCount=1; ci.pSubpasses=&sub;
    ci.dependencyCount=2; ci.pDependencies=deps;
    if (vk_.CreateRenderPass(device,&ci,nullptr,&fsrRenderPass)!=VK_SUCCESS) {
        fsrRenderPass=VK_NULL_HANDLE;
        RLOG_E("createFsrRenderPass: failed");
    }
}

/*
 * Everything the finishing pass needs that is sized to the swapchain, built in
 * one go.
 *
 * Allocated for any upscaler rather than only for FSR. It used to be FSR's
 * alone because RCAS was the only thing that ran here, and RCAS is FSR's second
 * half; now the same target also carries the screen effects, which SGSR and NIS
 * can be asked for just as easily. Deciding at allocation time on the upscaler
 * alone — rather than on whether an effect or a sharpness happens to be set
 * right now — is what lets both of those be changed mid-session without
 * rebuilding an image the render thread may be reading.
 *
 * Failure here is not fatal and deliberately does not throw: without it FSR is
 * EASU alone, which is a slightly softer picture, the effects are skipped, and
 * `recordCmdBuf` falls back to the single-pass route whenever `fsrFB` is null.
 * Losing the upscaler entirely — or the session — because an eight megabyte
 * allocation failed would be the worse trade.
 */
void VulkanRendererContext::ensureFsrTarget() {
    if (filterMode < 2 || filterMode > 4) return;
    if (fsrFB != VK_NULL_HANDLE) return;
    if (swapchainExt.width == 0 || swapchainExt.height == 0) return;
    // At construction the framebuffers exist before the descriptor pool does.
    if (winTexPool == VK_NULL_HANDLE || dsLayout == VK_NULL_HANDLE) return;

    createFsrRenderPass();
    createFinishPipeline();
    if (fsrRenderPass == VK_NULL_HANDLE || finishPipeline == VK_NULL_HANDLE) { destroyFsrTarget(); return; }

    bool ok = [&]() -> bool {
        VkImageCreateInfo ii{}; ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType=VK_IMAGE_TYPE_2D; ii.format=swapchainFmt;
        ii.extent={swapchainExt.width,swapchainExt.height,1};
        ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT;
        ii.tiling=VK_IMAGE_TILING_OPTIMAL;
        ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        if (vk_.CreateImage(device,&ii,nullptr,&fsrImg)!=VK_SUCCESS) { fsrImg=VK_NULL_HANDLE; return false; }

        VkMemoryRequirements req; vk_.GetImageMemoryRequirements(device,fsrImg,&req);
        uint32_t typeIdx;
        try { typeIdx = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); }
        catch (...) { return false; }
        VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize=req.size; ai.memoryTypeIndex=typeIdx;
        if (vk_.AllocateMemory(device,&ai,nullptr,&fsrMem)!=VK_SUCCESS) { fsrMem=VK_NULL_HANDLE; return false; }
        if (vk_.BindImageMemory(device,fsrImg,fsrMem,0)!=VK_SUCCESS) return false;

        VkImageViewCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image=fsrImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=swapchainFmt;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        if (vk_.CreateImageView(device,&vi,nullptr,&fsrView)!=VK_SUCCESS) { fsrView=VK_NULL_HANDLE; return false; }

        VkDescriptorSetAllocateInfo dai{}; dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool=winTexPool; dai.descriptorSetCount=1; dai.pSetLayouts=&dsLayout;
        if (vk_.AllocateDescriptorSets(device,&dai,&fsrDS)!=VK_SUCCESS) { fsrDS=VK_NULL_HANDLE; return false; }

        VkDescriptorImageInfo dii{}; dii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii.imageView=fsrView; dii.sampler=sampler;
        VkWriteDescriptorSet wr{}; wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet=fsrDS; wr.dstBinding=0; wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount=1; wr.pImageInfo=&dii;
        vk_.UpdateDescriptorSets(device,1,&wr,0,nullptr);

        VkImageView att[]={fsrView};
        VkFramebufferCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass=fsrRenderPass; fi.attachmentCount=1; fi.pAttachments=att;
        fi.width=swapchainExt.width; fi.height=swapchainExt.height; fi.layers=1;
        if (vk_.CreateFramebuffer(device,&fi,nullptr,&fsrFB)!=VK_SUCCESS) { fsrFB=VK_NULL_HANDLE; return false; }
        return true;
    }();

    if (!ok) {
        RLOG_E("ensureFsrTarget: could not build the finishing target; sharpening and screen effects are skipped");
        destroyFsrTarget();
        return;
    }
    RLOG("ensureFsrTarget: finishing target ready at %ux%u", swapchainExt.width, swapchainExt.height);
}

void VulkanRendererContext::destroyFsrTarget() {
    if (fsrFB   != VK_NULL_HANDLE) { vk_.DestroyFramebuffer(device,fsrFB,nullptr);   fsrFB   = VK_NULL_HANDLE; }
    if (fsrDS   != VK_NULL_HANDLE) { vk_.FreeDescriptorSets(device,winTexPool,1,&fsrDS); fsrDS = VK_NULL_HANDLE; }
    if (fsrView != VK_NULL_HANDLE) { vk_.DestroyImageView(device,fsrView,nullptr);   fsrView = VK_NULL_HANDLE; }
    if (fsrImg  != VK_NULL_HANDLE) { vk_.DestroyImage(device,fsrImg,nullptr);        fsrImg  = VK_NULL_HANDLE; }
    if (fsrMem  != VK_NULL_HANDLE) { vk_.FreeMemory(device,fsrMem,nullptr);          fsrMem  = VK_NULL_HANDLE; }
}

void VulkanRendererContext::createSgsrPipeline() {
    if (sgsrPipeline != VK_NULL_HANDLE) return;
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_sgsr_frag_code,sizeof(window_sgsr_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    if (vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&sgsrPipeline)!=VK_SUCCESS) throw std::runtime_error("sgsr_pipeline");
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    RLOG("createSgsrPipeline: done");
}
void VulkanRendererContext::createStretchPipeline() {
    if (stretchPipeline != VK_NULL_HANDLE) return;
    auto vert=makeShader(window_vert_code,sizeof(window_vert_code));
    auto frag=makeShader(window_stretch_frag_code,sizeof(window_stretch_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    if (vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&stretchPipeline)!=VK_SUCCESS) throw std::runtime_error("stretch_pipeline");
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    RLOG("createStretchPipeline: done");
}

void VulkanRendererContext::createPostFXPipeline() {
    if (postfxPipeline != VK_NULL_HANDLE) return;
    auto vert = makeShader(window_vert_code,        sizeof(window_vert_code));
    auto frag = makeShader(window_postfx_frag_code, sizeof(window_postfx_frag_code));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module=vert; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo   vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; ds.dynamicStateCount=2; ds.pDynamicStates=dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.f; rast.cullMode=VK_CULL_MODE_NONE; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF; ba.blendEnable=VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount=1; cb.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
    pi.pViewportState=&vp; pi.pRasterizationState=&rast; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.pDynamicState=&ds; pi.layout=pipeLayout; pi.renderPass=renderPass; pi.subpass=0;
    if (vk_.CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&postfxPipeline)!=VK_SUCCESS) throw std::runtime_error("postfx_pipeline");
    vk_.DestroyShaderModule(device,frag,nullptr); vk_.DestroyShaderModule(device,vert,nullptr);
    RLOG("createPostFXPipeline: done");
}

/*
 * Choosing a screen effect, which used to depend on what the upscaler was.
 *
 * The pipeline was built only `if (filterMode != 2)` — that is, not while SGSR
 * was selected, because with SGSR bound the effect shader could never be
 * reached and building it would have been waste. True at the time and wrong the
 * moment the upscaler changed: pick an effect under SGSR, then turn the
 * upscaler off, and the effect the console still showed as on had no pipeline
 * to run and quietly did nothing until it was reselected. Filter mode is not
 * this function's business, so it no longer asks.
 *
 * The lock is the one `setFilterMode` already takes and for the same reason:
 * `DeviceWaitIdle` settles the GPU and says nothing about the render thread,
 * which holds `frameMutex` shared for the length of `renderFrame` and may be
 * partway through a command buffer that names the pipeline being destroyed.
 */
void VulkanRendererContext::setPostFXMode(int mode) {
    RLOG("setPostFXMode: %d -> %d", postFXMode, mode);
    if (postFXMode == mode) return;

    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout)) {
        RLOG_E("setPostFXMode: renderer did not quiesce; keeping the existing mode");
        return;
    }
    postFXMode = mode;

    if (mode > 0) {
        if (postfxPipeline == VK_NULL_HANDLE) createPostFXPipeline();
    } else if (postfxPipeline != VK_NULL_HANDLE) {
        if (!waitForAllFences("setPostFXMode")) return;
        vk_.DestroyPipeline(device, postfxPipeline, nullptr);
        postfxPipeline = VK_NULL_HANDLE;
        RLOG("setPostFXMode: post-effect pipeline released");
    }
    requestRender();
}

void VulkanRendererContext::setSharpness(float s) {
    sharpness = std::clamp(s, 0.0f, 1.0f);
    requestRender();
}

void VulkanRendererContext::createFramebuffers() {
    swapchainFBs.resize(swapchainViews.size());
    for (size_t i=0;i<swapchainViews.size();i++) {
        VkImageView att[]={swapchainViews[i]};
        VkFramebufferCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass=renderPass; fi.attachmentCount=1; fi.pAttachments=att;
        fi.width=swapchainExt.width; fi.height=swapchainExt.height; fi.layers=1;
        if (vk_.CreateFramebuffer(device,&fi,nullptr,&swapchainFBs[i])!=VK_SUCCESS) throw std::runtime_error("fb");
    }
    // FSR's offscreen target is the same size as these and is rebuilt with
    // them, so every path that resizes the swapchain gets it for free.
    ensureFsrTarget();
}

void VulkanRendererContext::createCmdPool() {
    VkCommandPoolCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; ci.queueFamilyIndex=graphicsQueueFamilyIndex;
    if (vk_.CreateCommandPool(device,&ci,nullptr,&cmdPool)!=VK_SUCCESS) throw std::runtime_error("cmdpool");
}

void VulkanRendererContext::createSampler() {
    VkFilter filter = (filterMode == 1) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    RLOG("createSampler: filter=%s (filterMode=%d)",
        filterMode==1?"NEAREST":"LINEAR", filterMode);
    VkSamplerCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter=filter; ci.minFilter=filter;
    ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.minLod=0.f; ci.maxLod=0.f;
    if (vk_.CreateSampler(device,&ci,nullptr,&sampler)!=VK_SUCCESS) throw std::runtime_error("sampler");
}

void VulkanRendererContext::createWinTexPool() {

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 129};
    VkDescriptorPoolCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.poolSizeCount=1; ci.pPoolSizes=&ps; ci.maxSets=129;
    if (vk_.CreateDescriptorPool(device,&ci,nullptr,&winTexPool)!=VK_SUCCESS) throw std::runtime_error("wintexpool");
}

void VulkanRendererContext::createCursorDS() {
    VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool=winTexPool; ai.descriptorSetCount=1; ai.pSetLayouts=&dsLayout;
    vk_.AllocateDescriptorSets(device,&ai,&cursorDS);
}

void VulkanRendererContext::createCmdBufs() {
    cmdBufs.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool=cmdPool; ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount=MAX_FRAMES_IN_FLIGHT;
    if (vk_.AllocateCommandBuffers(device,&ai,cmdBufs.data())!=VK_SUCCESS) throw std::runtime_error("cmdbuf");
}

void VulkanRendererContext::createSyncObjects() {
    imgAvailSems.resize(MAX_FRAMES_IN_FLIGHT); renderDoneSems.resize(MAX_FRAMES_IN_FLIGHT); inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo si{}; si.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i=0;i<MAX_FRAMES_IN_FLIGHT;i++) {
        if (vk_.CreateSemaphore(device,&si,nullptr,&imgAvailSems[i])!=VK_SUCCESS||
            vk_.CreateSemaphore(device,&si,nullptr,&renderDoneSems[i])!=VK_SUCCESS||
            vk_.CreateFence(device,&fi,nullptr,&inFlightFences[i])!=VK_SUCCESS) throw std::runtime_error("sync");
    }
}

void VulkanRendererContext::cleanupSwapchain() {
    destroyFsrTarget();
    for (auto fb:swapchainFBs) vk_.DestroyFramebuffer(device,fb,nullptr); swapchainFBs.clear();
    for (auto iv:swapchainViews) vk_.DestroyImageView(device,iv,nullptr); swapchainViews.clear();
    if (!cmdBufs.empty()){vk_.FreeCommandBuffers(device,cmdPool,(uint32_t)cmdBufs.size(),cmdBufs.data());cmdBufs.clear();}
    if (swapchain!=VK_NULL_HANDLE) { vk_.DestroySwapchainKHR(device,swapchain,nullptr); swapchain=VK_NULL_HANDLE; }
    // Sized to the swapchain extent, so it cannot outlive it.
}

uint32_t VulkanRendererContext::findMemType(uint32_t filter, VkMemoryPropertyFlags props) {
    for (uint32_t i=0;i<memProperties.memoryTypeCount;i++)
        if ((filter&(1u<<i))&&(memProperties.memoryTypes[i].propertyFlags&props)==props) return i;
    throw std::runtime_error("memtype");
}

void VulkanRendererContext::createBuffer(VkDeviceSize sz, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size=sz; bi.usage=usage; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if (vk_.CreateBuffer(device,&bi,nullptr,&buf)!=VK_SUCCESS) throw std::runtime_error("buffer");
    VkMemoryRequirements req; vk_.GetBufferMemoryRequirements(device,buf,&req);
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=req.size; ai.memoryTypeIndex=findMemType(req.memoryTypeBits,props);
    if (vk_.AllocateMemory(device,&ai,nullptr,&mem)!=VK_SUCCESS) throw std::runtime_error("bufmem");
    vk_.BindBufferMemory(device,buf,mem,0);
}

VkCommandBuffer VulkanRendererContext::beginOneTime() {
    VkCommandBufferAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandPool=cmdPool; ai.commandBufferCount=1;
    VkCommandBuffer cb; vk_.AllocateCommandBuffers(device,&ai,&cb);
    VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_.BeginCommandBuffer(cb,&bi); return cb;
}

void VulkanRendererContext::endOneTime(VkCommandBuffer cb) {
    vk_.EndCommandBuffer(cb);
    VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount=1; si.pCommandBuffers=&cb;
    VkFenceCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; VkFence fence;
    if (vk_.CreateFence(device,&fi,nullptr,&fence) != VK_SUCCESS) {
        markRendererFailed("endOneTime/createFence", VK_ERROR_INITIALIZATION_FAILED);
        vk_.FreeCommandBuffers(device,cmdPool,1,&cb);
        return;
    }
    const VkResult submit = vk_.QueueSubmit(graphicsQueue,1,&si,fence);
    if (submit != VK_SUCCESS) {
        markRendererFailed("endOneTime/queueSubmit", submit);
        vk_.DestroyFence(device,fence,nullptr);
        vk_.FreeCommandBuffers(device,cmdPool,1,&cb);
        return;
    }
    if (!waitForFence(fence, "endOneTime/wait")) {
        // The submitted command still owns both objects. Retain them with the
        // failed context instead of freeing them underneath the driver.
        return;
    }
    vk_.DestroyFence(device,fence,nullptr);
    vk_.FreeCommandBuffers(device,cmdPool,1,&cb);
}

void VulkanRendererContext::transition(VkCommandBuffer cb, VkImage img,
    VkImageLayout ol, VkImageLayout nl, VkAccessFlags sa, VkAccessFlags da,
    VkPipelineStageFlags ss, VkPipelineStageFlags ds)
{
    VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout=ol; b.newLayout=nl; b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; b.srcAccessMask=sa; b.dstAccessMask=da;
    vk_.CmdPipelineBarrier(cb,ss,ds,0,0,nullptr,0,nullptr,1,&b);
}

bool VulkanRendererContext::createWinTexResources(WinTex& wt, int w, int h) {

    VkImageCreateInfo ii{}; ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ii.imageType=VK_IMAGE_TYPE_2D;
    ii.extent={(uint32_t)w,(uint32_t)h,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.format=VK_FORMAT_B8G8R8A8_UNORM;
    ii.tiling=VK_IMAGE_TILING_OPTIMAL; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if (vk_.CreateImage(device,&ii,nullptr,&wt.img)!=VK_SUCCESS) return false;
    VkMemoryRequirements req; vk_.GetImageMemoryRequirements(device,wt.img,&req);
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=req.size; ai.memoryTypeIndex=findMemType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vk_.AllocateMemory(device,&ai,nullptr,&wt.mem)!=VK_SUCCESS){vk_.DestroyImage(device,wt.img,nullptr);wt.img=VK_NULL_HANDLE;return false;}
    vk_.BindImageMemory(device,wt.img,wt.mem,0);
    VkImageViewCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image=wt.img; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_B8G8R8A8_UNORM; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vi.components={swapRB?VK_COMPONENT_SWIZZLE_B:VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,swapRB?VK_COMPONENT_SWIZZLE_R:VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
    if (vk_.CreateImageView(device,&vi,nullptr,&wt.view)!=VK_SUCCESS){destroyWinTex(wt);return false;}
    VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; dsai.descriptorPool=winTexPool; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsLayout;
    if (vk_.AllocateDescriptorSets(device,&dsai,&wt.ds)!=VK_SUCCESS){destroyWinTex(wt);return false;}
    VkDescriptorImageInfo dii{}; dii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; dii.imageView=wt.view; dii.sampler=sampler;
    VkWriteDescriptorSet wr{}; wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr.dstSet=wt.ds; wr.dstBinding=0; wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.descriptorCount=1; wr.pImageInfo=&dii;
    vk_.UpdateDescriptorSets(device,1,&wr,0,nullptr);
    VkDeviceSize stgSz=(VkDeviceSize)w*h*4;
    createBuffer(stgSz,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,wt.stg,wt.stgMem);
    vk_.MapMemory(device,wt.stgMem,0,stgSz,0,&wt.mapped);
    wt.cap=stgSz; wt.w=w; wt.h=h; wt.needsTransition=true;
    return true;
}

bool VulkanRendererContext::importAHBToWinTex(WinTex& wt, AHardwareBuffer* ahb) {
    if (!vk_.GetAndroidHardwareBufferPropertiesANDROID) {
        RLOG_E("importAHBToWinTex: VK_ANDROID_external_memory_android_hardware_buffer is unavailable");
        return false;
    }

    VkAndroidHardwareBufferFormatPropertiesANDROID fmtP{};
    fmtP.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID props{};
    props.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    props.pNext=&fmtP;
    const VkResult propertyResult =
        vk_.GetAndroidHardwareBufferPropertiesANDROID(device,ahb,&props);
    if (propertyResult!=VK_SUCCESS) {
        RLOG_E("importAHBToWinTex: hardware-buffer properties failed status=%d",
               (int)propertyResult);
        return false;
    }

    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(ahb,&desc);

    /*
     * AHardwareBuffer's descriptor is the source of truth for channel order.
     * Steam CEF uses both RGBA and BGRA DRI3 buffers as its login, popup and
     * overlay windows are replaced.  Treating every import as the global
     * swapRB choice made one class render correctly while the other produced
     * blue rectangles or black/stale browser surfaces.
     *
     * Prefer the driver's queried VkFormat. VkExternalFormatANDROID cannot be
     * substituted here: its value is an implementation-defined external-format
     * identifier, not a VkFormat enum, and sampling one also requires the exact
     * sampler/Y'CbCr conversion returned by the driver.
     */
    const VkFormat importedFormat = fmtP.format;
    if (importedFormat == VK_FORMAT_UNDEFINED) {
        /*
         * External-format images require a matching sampler-YCbCr conversion,
         * not a made-up VkFormat in VkExternalFormatANDROID. Window buffers
         * are expected to expose an RGBA/BGRA equivalent format. Reject an
         * external-only buffer instead of sampling it incorrectly. Callers
         * with CPU backing may fall back; a socket-only DRI3 buffer fails
         * visibly and leaves this diagnostic rather than becoming corrupted.
         */
        RLOG_E("importAHBToWinTex: external-only AHB format=%u external=%llu",
               desc.format, (unsigned long long)fmtP.externalFormat);
        return false;
    }
    if (importedFormat != VK_FORMAT_R8G8B8A8_UNORM &&
        importedFormat != VK_FORMAT_B8G8R8A8_UNORM) {
        RLOG_E("importAHBToWinTex: unsupported AHB format=%u vkFormat=%d external=%llu",
               desc.format, (int)importedFormat,
               (unsigned long long)fmtP.externalFormat);
        return false;
    }
    const bool sourceNeedsSwap = importedFormat == VK_FORMAT_R8G8B8A8_UNORM;
    const bool effectiveSwap = sourceNeedsSwap != swapRB;

    RLOG("importAHBToWinTex: AHB format=%u vkFormat=%d external=%llu swap=%d",
         desc.format, (int)importedFormat,
         (unsigned long long)fmtP.externalFormat, (int)effectiveSwap);

    VkExternalMemoryImageCreateInfo emi{};
    emi.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    emi.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo ii{};
    ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.pNext=&emi; ii.imageType=VK_IMAGE_TYPE_2D;
    ii.format=importedFormat;
    ii.extent={desc.width,desc.height,1};
    ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT;
    ii.tiling=VK_IMAGE_TILING_OPTIMAL; ii.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    const VkResult imageResult = vk_.CreateImage(device,&ii,nullptr,&wt.img);
    if (imageResult!=VK_SUCCESS) {
        RLOG_E("importAHBToWinTex: CreateImage failed status=%d format=%d size=%ux%u",
               (int)imageResult, (int)importedFormat, desc.width, desc.height);
        return false;
    }

    VkImportAndroidHardwareBufferInfoANDROID imp{};
    imp.sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    imp.buffer=ahb;

    VkMemoryDedicatedAllocateInfo ded{};
    ded.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    ded.pNext=&imp; ded.image=wt.img;

    VkMemoryAllocateInfo mai{};
    mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext=&ded; mai.allocationSize=props.allocationSize;
    mai.memoryTypeIndex=findMemType(props.memoryTypeBits,0);
    const VkResult allocationResult = vk_.AllocateMemory(device,&mai,nullptr,&wt.mem);
    if (allocationResult!=VK_SUCCESS){
        RLOG_E("importAHBToWinTex: AllocateMemory failed status=%d bytes=%llu types=0x%x",
               (int)allocationResult, (unsigned long long)props.allocationSize,
               props.memoryTypeBits);
        vk_.DestroyImage(device,wt.img,nullptr);
        wt.img=VK_NULL_HANDLE;
        return false;
    }
    const VkResult bindResult = vk_.BindImageMemory(device,wt.img,wt.mem,0);
    if (bindResult!=VK_SUCCESS) {
        RLOG_E("importAHBToWinTex: BindImageMemory failed status=%d", (int)bindResult);
        vk_.FreeMemory(device,wt.mem,nullptr);
        vk_.DestroyImage(device,wt.img,nullptr);
        wt.mem=VK_NULL_HANDLE;
        wt.img=VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo vi{};
    vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image=wt.img; vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
    vi.format=importedFormat;
    vi.components={effectiveSwap ? VK_COMPONENT_SWIZZLE_B : VK_COMPONENT_SWIZZLE_IDENTITY,
                   VK_COMPONENT_SWIZZLE_IDENTITY,
                   effectiveSwap ? VK_COMPONENT_SWIZZLE_R : VK_COMPONENT_SWIZZLE_IDENTITY,
                   VK_COMPONENT_SWIZZLE_IDENTITY};
    vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    const VkResult viewResult = vk_.CreateImageView(device,&vi,nullptr,&wt.view);
    if (viewResult!=VK_SUCCESS){
        RLOG_E("importAHBToWinTex: CreateImageView failed status=%d format=%d",
               (int)viewResult, (int)importedFormat);
        destroyWinTex(wt);
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool=winTexPool; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsLayout;
    VkResult dsRes=vk_.AllocateDescriptorSets(device,&dsai,&wt.ds);
    if (dsRes==VK_ERROR_OUT_OF_POOL_MEMORY){
        RLOG_E("importAHBToWinTex: descriptor pool exhausted for AHB texture");
        destroyWinTex(wt);
        return false;
    }
    if (dsRes!=VK_SUCCESS){
        RLOG_E("importAHBToWinTex: descriptor allocation failed status=%d", (int)dsRes);
        destroyWinTex(wt);
        return false;
    }

    VkDescriptorImageInfo dii{};
    dii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView=wt.view; dii.sampler=sampler;

    VkWriteDescriptorSet wr{};
    wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet=wt.ds; wr.dstBinding=0;
    wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.descriptorCount=1; wr.pImageInfo=&dii;
    vk_.UpdateDescriptorSets(device,1,&wr,0,nullptr);

    wt.needsTransition=true;
    wt.isAHB=true;
    wt.w=(int)desc.width;
    wt.h=(int)desc.height;
    return true;
}

void VulkanRendererContext::destroyWinTex(WinTex& wt) {
    if (wt.isAHB) {

        wt = {};
        return;
    }
    if (wt.img!=VK_NULL_HANDLE || wt.stg!=VK_NULL_HANDLE) {

        WinTex deferred = wt;
        deferred.isAHB = false;
        deleteQueue.push_back(deferred);
    }
    wt={};
}

void VulkanRendererContext::ensureCursorTex(short w, short h) {
    if (cursorImg!=VK_NULL_HANDLE && cursorTexW==w && cursorTexH==h) return;
    cleanupCursorTex();
    VkImageCreateInfo ii{}; ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ii.imageType=VK_IMAGE_TYPE_2D;
    ii.extent={(uint32_t)w,(uint32_t)h,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.format=VK_FORMAT_B8G8R8A8_UNORM;
    ii.tiling=VK_IMAGE_TILING_OPTIMAL; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    vk_.CreateImage(device,&ii,nullptr,&cursorImg);
    VkMemoryRequirements req; vk_.GetImageMemoryRequirements(device,cursorImg,&req);
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=req.size; ai.memoryTypeIndex=findMemType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vk_.AllocateMemory(device,&ai,nullptr,&cursorMem); vk_.BindImageMemory(device,cursorImg,cursorMem,0);
    VkImageViewCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image=cursorImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_B8G8R8A8_UNORM; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vk_.CreateImageView(device,&vi,nullptr,&cursorView);
    VkDescriptorImageInfo dii{}; dii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; dii.imageView=cursorView; dii.sampler=sampler;
    VkWriteDescriptorSet wr{}; wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr.dstSet=cursorDS; wr.dstBinding=0; wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.descriptorCount=1; wr.pImageInfo=&dii;
    vk_.UpdateDescriptorSets(device,1,&wr,0,nullptr);

    cursorTexW=w; cursorTexH=h;
}

void VulkanRendererContext::cleanupCursorTex() {
    if (cursorView!=VK_NULL_HANDLE){vk_.DestroyImageView(device,cursorView,nullptr);cursorView=VK_NULL_HANDLE;}
    if (cursorImg!=VK_NULL_HANDLE){vk_.DestroyImage(device,cursorImg,nullptr);cursorImg=VK_NULL_HANDLE;}
    if (cursorMem!=VK_NULL_HANDLE){vk_.FreeMemory(device,cursorMem,nullptr);cursorMem=VK_NULL_HANDLE;}
    if (cursorStg!=VK_NULL_HANDLE){vk_.DestroyBuffer(device,cursorStg,nullptr);vk_.FreeMemory(device,cursorStgM,nullptr);cursorStg=VK_NULL_HANDLE;cursorStgP=nullptr;cursorStgC=0;}
    cursorTexW=0; cursorTexH=0;
}

void VulkanRendererContext::ensureCursorStaging(VkDeviceSize sz) {
    if (cursorStgC>=sz) return;
    if (cursorStg!=VK_NULL_HANDLE){vk_.DestroyBuffer(device,cursorStg,nullptr);vk_.FreeMemory(device,cursorStgM,nullptr);}
    createBuffer(sz,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,cursorStg,cursorStgM);
    vk_.MapMemory(device,cursorStgM,0,sz,0,&cursorStgP); cursorStgC=sz;
}

void VulkanRendererContext::recordCmdBuf(VkCommandBuffer cb, uint32_t imgIdx,
    const std::vector<DrawEntry>& draws,
    std::vector<VkImageMemoryBarrier>& ahbTransitions,
    std::vector<VkImageMemoryBarrier>& preUpload,
    std::vector<VkImageMemoryBarrier>& postUpload,
    VkBuffer cursorUpload, bool hasCursorUpload,
    float ox, float oy, float sx, float sy, float cw, float ch,
    short ptrX, short ptrY, short curHotX, short curHotY,
    short curW, short curH, bool curVis,
    VkRect2D scissorRect)
{
    VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vk_.BeginCommandBuffer(cb,&bi)!=VK_SUCCESS) throw std::runtime_error("begin cb");

    ahbTransitions.clear(); preUpload.clear(); postUpload.clear();

    for (auto& d : draws) {
        if (d.img==VK_NULL_HANDLE) continue;
        if (d.isAHB && d.needsTransition) {
            VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=d.img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.srcAccessMask=0; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            ahbTransitions.push_back(b);
        } else if (!d.isAHB && (d.needsTransition || d.upload!=VK_NULL_HANDLE)) {
            VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=d.img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.srcAccessMask=0; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            preUpload.push_back(b);
            b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            postUpload.push_back(b);
        }
    }

    if (!ahbTransitions.empty())
        vk_.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, (uint32_t)ahbTransitions.size(), ahbTransitions.data());
    if (!preUpload.empty())
        vk_.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, (uint32_t)preUpload.size(), preUpload.data());

    for (auto& d : draws) {
        if (d.isAHB || d.upload==VK_NULL_HANDLE || d.img==VK_NULL_HANDLE) continue;
        VkBufferImageCopy r{}; r.bufferOffset=0; r.bufferRowLength=0; r.bufferImageHeight=0;
        r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        r.imageExtent={(uint32_t)d.w,(uint32_t)d.h,1};
        vk_.CmdCopyBufferToImage(cb, d.upload, d.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    }

    bool cursorDrawn = curVis && cursorImg!=VK_NULL_HANDLE && cursorDS!=VK_NULL_HANDLE;
    bool hasCursorCopy = hasCursorUpload && cursorImg!=VK_NULL_HANDLE && cursorUpload!=VK_NULL_HANDLE;
    if (hasCursorCopy) {
        VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=cursorImg; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vk_.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy r{}; r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        r.imageExtent={(uint32_t)curW,(uint32_t)curH,1};
        vk_.CmdCopyBufferToImage(cb, cursorUpload, cursorImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        postUpload.push_back(b);
    }

    if (!postUpload.empty())
        vk_.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, (uint32_t)postUpload.size(), postUpload.data());

    bool useFsr     = (filterMode == 3) && (fsrPipeline     != VK_NULL_HANDLE);
    bool useNis     = (filterMode == 4) && (nisPipeline     != VK_NULL_HANDLE);
    // One branch for every upscaler: they take the same push constants, and
    // only the bound pipeline differs.
    bool useSgsr    = ((filterMode == 2) && (sgsrPipeline != VK_NULL_HANDLE)) || useFsr || useNis;
    bool useStretch = (stretchMode == 1) && (stretchPipeline != VK_NULL_HANDLE);

    /*
     * The cases that need two passes.
     *
     * Both things that run after the composite — FSR's sharpener and the screen
     * effects — read the finished frame as a texture, so when either is going
     * to run the windows are composited into `fsrFB` first and the swapchain
     * receives a single finishing quad instead.
     *
     * Neither is worth a second pass on its own account when it would do
     * nothing: sharpness at zero makes RCAS a copy, and effect zero makes the
     * grade a copy, so with both at rest the ordinary one-pass route lands the
     * upscaled frame directly on the screen. So does the case where the target
     * could not be allocated.
     */
    bool wantSharpen = useFsr && sharpness > 0.0f;
    bool wantEffect  = postFXMode > 0;
    bool useFinish   = useSgsr && (wantSharpen || wantEffect)
                    && fsrFB != VK_NULL_HANDLE && fsrDS != VK_NULL_HANDLE
                    && finishPipeline != VK_NULL_HANDLE;

    /*
     * The per-window effect shader is now only for the case with no upscaler.
     *
     * It draws the effect into each window's own quad, which is cheaper than a
     * fullscreen pass and was the only way to do it before `fsrFB` existed. But
     * an upscaler wants the same slot, and the chain below can only bind one
     * pipeline — which is exactly how choosing an upscaler used to turn the
     * effects off without saying so. When one is set the effect moves to the
     * finishing pass, where it also belongs on ordering grounds: it grades the
     * pixels that will be lit, after the reconstruction, not before it.
     */
    bool usePostFX  = wantEffect && !useSgsr && (postfxPipeline != VK_NULL_HANDLE);

    VkViewport vp{0,0,(float)swapchainExt.width,(float)swapchainExt.height,0,1};
    VkRect2D   sc{};
    {
        int32_t  ox2 = std::max(0, scissorRect.offset.x);
        int32_t  oy2 = std::max(0, scissorRect.offset.y);
        uint32_t maxW = (swapchainExt.width  > (uint32_t)ox2) ? swapchainExt.width  - (uint32_t)ox2 : 0u;
        uint32_t maxH = (swapchainExt.height > (uint32_t)oy2) ? swapchainExt.height - (uint32_t)oy2 : 0u;
        sc = VkRect2D{{ox2, oy2},
                      {std::min(scissorRect.extent.width,  maxW),
                       std::min(scissorRect.extent.height, maxH)}};
    }

    /*
     * The root, in the session's own colour rather than black.
     *
     * Read per frame from an atomic rather than cached into the clear value,
     * because the theme can change while a container is open and a clear colour
     * captured at swapchain creation would not follow it.
     *
     * Straight sRGB values into a UNORM target: the swapchain format here is
     * R8G8B8A8_UNORM, so what is written is what is scanned out, and converting
     * to linear would wash the colour out against the Compose desktop that has
     * to match it.
     */
    const int root = rootColor.load(std::memory_order_relaxed);
    VkClearValue clr={{{((root >> 16) & 0xFF) / 255.f,
                        ((root >>  8) & 0xFF) / 255.f,
                        ( root        & 0xFF) / 255.f, 1.f}}};
    VkRenderPassBeginInfo rpi{}; rpi.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass  = useFinish ? fsrRenderPass : renderPass;
    rpi.framebuffer = useFinish ? fsrFB         : swapchainFBs[imgIdx];
    rpi.renderArea={{0,0},swapchainExt};
    rpi.clearValueCount=1; rpi.pClearValues=&clr;

    vk_.CmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vk_.CmdSetViewport(cb, 0, 1, &vp);
    vk_.CmdSetScissor(cb, 0, 1, &sc);

    VkPipeline activePipeline = useFsr    ? fsrPipeline
                              : useNis    ? nisPipeline
                              : useSgsr   ? sgsrPipeline
                              : usePostFX ? postfxPipeline
                              : useStretch? stretchPipeline
                              : pipeline;
    vk_.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);
    for (auto& d : draws) {
        if (d.ds==VK_NULL_HANDLE) continue;
        vk_.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &d.ds, 0, nullptr);
        if (useSgsr) {
            WindowPushConstantsSGSR pc{};

            if (useStretch) {
                pc.ndcX0 = -1.f;
                pc.ndcX1 =  1.f;
            } else {
                pc.ndcX0=(ox+(float)d.x*sx)/cw*2.f-1.f;
                pc.ndcX1=(ox+(float)(d.x+d.w)*sx)/cw*2.f-1.f;
            }
            pc.ndcY0=(oy+(float)d.y*sy)/ch*2.f-1.f;
            pc.ndcY1=(oy+(float)(d.y+d.h)*sy)/ch*2.f-1.f;
            pc.useTexAlpha = 0;
            float sw = (float)std::max(d.w, 1); float sh = (float)std::max(d.h, 1);
            pc.invSrcW = 1.0f / sw; pc.invSrcH = 1.0f / sh;
            pc.srcW = sw; pc.srcH = sh;
            // The upscaler shaders read neither of these. The effect and the
            // sharpener both run in the finishing pass now, so leaving them
            // at rest here is what keeps them from being applied twice.
            pc.effectId     = 0;
            pc.effectAmount = 0.0f;
            pc.sharpness    = sharpness;
            vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        } else if (usePostFX) {
            WindowPushConstantsPostFX pc{};
            pc.ndcX0=(ox+(float)d.x*sx)/cw*2.f-1.f;
            pc.ndcY0=(oy+(float)d.y*sy)/ch*2.f-1.f;
            pc.ndcX1=(ox+(float)(d.x+d.w)*sx)/cw*2.f-1.f;
            pc.ndcY1=(oy+(float)(d.y+d.h)*sy)/ch*2.f-1.f;
            pc.effectId  = postFXMode;
            pc.sharpness = sharpness;
            pc.resW      = (float)std::max(d.w, 1);
            pc.resH      = (float)std::max(d.h, 1);
            vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        } else if (useStretch) {
            WindowPushConstantsStretch pc{};
            pc.ndcX0 = -1.f;
            pc.ndcX1 =  1.f;
            pc.ndcY0=(oy+(float)d.y*sy)/ch*2.f-1.f;
            pc.ndcY1=(oy+(float)(d.y+d.h)*sy)/ch*2.f-1.f;
            pc.useTexAlpha = 0;
            pc.strength = stretchStrength;
            pc.profile  = stretchProfile;
            vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        } else {
            WindowPushConstants pc{};
            pc.ndcX0=(ox+(float)d.x*sx)/cw*2.f-1.f;
            pc.ndcY0=(oy+(float)d.y*sy)/ch*2.f-1.f;
            pc.ndcX1=(ox+(float)(d.x+d.w)*sx)/cw*2.f-1.f;
            pc.ndcY1=(oy+(float)(d.y+d.h)*sy)/ch*2.f-1.f;
            pc.useTexAlpha = 0;
            vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        }
        vk_.CmdDraw(cb, 4, 1, 0, 0);
    }

    /*
     * Second pass: sharpen and grade what the upscaler just produced, into the
     * screen.
     *
     * The cursor is deliberately left for after this. It is not part of the
     * game's image, it arrives at native resolution already, and sharpening a
     * hard-edged alpha-blended sprite gives it a dark outline — so it goes on
     * top of the finished frame rather than through it, which is also what
     * happens on the SGSR path today. A screen effect is left off it for the
     * same reason: a scanline grid or a bloom over the pointer would be
     * grading the interface rather than the game.
     */
    if (useFinish) {
        vk_.CmdEndRenderPass(cb);

        VkRenderPassBeginInfo rpi2{}; rpi2.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi2.renderPass=renderPass; rpi2.framebuffer=swapchainFBs[imgIdx];
        rpi2.renderArea={{0,0},swapchainExt};
        rpi2.clearValueCount=1; rpi2.pClearValues=&clr;
        vk_.CmdBeginRenderPass(cb, &rpi2, VK_SUBPASS_CONTENTS_INLINE);
        vk_.CmdSetViewport(cb, 0, 1, &vp);
        vk_.CmdSetScissor(cb, 0, 1, &sc);

        vk_.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, finishPipeline);
        vk_.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &fsrDS, 0, nullptr);
        WindowPushConstantsSGSR rpc{};
        // Full screen. The source is the whole offscreen image, so the sizes
        // this pass needs for its texel step are the swapchain's, not a
        // window's.
        rpc.ndcX0=-1.f; rpc.ndcY0=-1.f; rpc.ndcX1=1.f; rpc.ndcY1=1.f;
        rpc.useTexAlpha=0;
        rpc.invSrcW=1.f/(float)std::max(swapchainExt.width, 1u);
        rpc.invSrcH=1.f/(float)std::max(swapchainExt.height,1u);
        rpc.srcW=(float)swapchainExt.width; rpc.srcH=(float)swapchainExt.height;
        /*
         * Sharpening is FSR's and only FSR's — RCAS is the second half of that
         * algorithm, not a filter that happens to follow one — so on the SGSR
         * and NIS paths it is sent at rest and the pass runs the effect alone.
         * The effect keeps the dial either way.
         */
        rpc.effectId     = wantEffect ? postFXMode : 0;
        rpc.effectAmount = sharpness;
        rpc.sharpness    = wantSharpen ? sharpness : 0.0f;
        vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rpc), &rpc);
        vk_.CmdDraw(cb, 4, 1, 0, 0);
    }

    if (cursorDrawn) {

        if (useSgsr || useStretch || usePostFX) vk_.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk_.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &cursorDS, 0, nullptr);
        float cx=(float)std::max(0,(int)ptrX-curHotX), cy=(float)std::max(0,(int)ptrY-curHotY);
        WindowPushConstants cpc{};
        cpc.ndcX0=(ox+cx*sx)/cw*2.f-1.f; cpc.ndcY0=(oy+cy*sy)/ch*2.f-1.f;
        cpc.ndcX1=(ox+(cx+curW)*sx)/cw*2.f-1.f; cpc.ndcY1=(oy+(cy+curH)*sy)/ch*2.f-1.f;
        cpc.useTexAlpha = 1;
        vk_.CmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cpc), &cpc);
        vk_.CmdDraw(cb, 4, 1, 0, 0);
    }
    vk_.CmdEndRenderPass(cb);

    // After the render pass, so the copy takes the finished frame: windows,
    // cursor and any post effect all already in it.

    VkResult endStatus = vk_.EndCommandBuffer(cb);
    if (endStatus!=VK_SUCCESS) {
        RLOG_E("recordCmdBuf: EndCommandBuffer failed with status=%d (swapRB=%d draws=%zu imgIdx=%u)",
            (int)endStatus, (int)swapRB, draws.size(), imgIdx);
        throw std::runtime_error("end cb");
    }
}

// Frame generation used to live here, and does not any more.
//
// Two versions were built in this file: a motion search of our own, and then
// Lossless Scaling's LSFG driven over AHardwareBuffers. Both worked and both
// were the wrong place for it. The compositor is a second Vulkan device that
// does not own the frames, so every generated frame cost two full-screen
// copies and a `vkDeviceWaitIdle` with no semaphore able to cross between the
// devices — 45 ms a frame at 1080p, measured on an AYN Thor.
//
// It is a Vulkan layer inside the guest now, in the game's own device, where
// the frames already are and nothing has to be copied. See
// `FrameGeneration.java` for the arrangement and commit 99ad959 for the full
// measurement, kept because both routes look obvious from here.

/*
 * If a stutter or a freeze is traced back to here, the shapes are told apart by
 * what the picture does, not by the log.
 *
 * 1. Low GPU utilisation with stutter and a low frame rate. Frames are not
 *    being submitted often enough, so look at what decides that a frame is
 *    wanted -- the wakeups above and this predicate -- before looking at the
 *    renderer. Coalescing those wakeups produced exactly this and was reverted;
 *    see the comment on requestRender().
 *
 * 2. The image stops dead while audio continues and the app still responds, or
 *    a container opens onto nothing at all. That is this thread asleep with a
 *    frame pending. The predicate gates needsRender behind !surfaceDetached, so
 *    a raised flag is not by itself a frame that will be drawn: whatever makes
 *    the predicate satisfiable again has to notify. Surface reattach is the only
 *    such transition today and uses forceRender(); a new term in the predicate
 *    would add more. `cursorMoved` is deliberately outside that gate and is the
 *    only term that is -- it is the escape hatch, and tightening it is how this
 *    symptom was last produced.
 *
 * 3. A brief hitch whenever the surface or the resolution changes. That was the
 *    `continue` below returning to a predicate that was still true, and it is
 *    fixed; what it used to do is described where the fix is.
 */
void VulkanRendererContext::renderLoop() {

    while (isRunning) {
        { std::unique_lock<std::mutex> lk(dirtyMutex);
          dirtyCV.wait(lk,[this]{
              /*
               * `cursorMoved` stays outside the surface gate. It was moved
               * inside it once, for one build, and that build could not open a
               * container.
               *
               * The reasoning for moving it was sound as far as it went: a
               * cursor move with no surface to draw on is not a frame, and it
               * was the term that let the loop wake with the swapchain already
               * destroyed. What it missed is that being the only ungated term
               * made it this loop's escape hatch. Every other way back in
               * depends on someone clearing surfaceDetached and then notifying,
               * and reattachSurface is the only code that does both -- so any
               * path that leaves the flag set without going through it leaves a
               * render thread that never wakes again. Nothing draws: a container
               * that opens onto nothing, a game that runs with no picture.
               *
               * The spin it was meant to stop is fixed below instead, by not
               * spinning. That fix does not depend on which flag woke us, which
               * is what makes it the safe half of the pair.
               */
              return !isRunning
                  || (!surfaceDetached.load()&&(needsRender.load()||fbResized.load()))
                  || cursorMoved.load(); }); }
        if (!isRunning) break;

        if (swapchain == VK_NULL_HANDLE || cmdBufs.empty()) {
            /*
             * Nothing to draw into, so wait rather than spin.
             *
             * This was a bare `continue`, and a bare `continue` here is a busy
             * loop: the flags that satisfied the predicate are only cleared by
             * renderFrame(), which this skips, so the wait above returned
             * immediately every time. The one path that reaches here and
             * recovers on its own is a swapchain that failed to build in the
             * resize branch below -- it leaves fbResized raised to try again,
             * and without this it retried as fast as the CPU allowed, forever.
             *
             * The flags are deliberately left raised. Clearing them here would
             * turn a frame that could not be drawn yet into a frame that is
             * never drawn, which is the failure mode this renderer has already
             * shipped once. A timed wait keeps every wakeup: a real notify still
             * returns at once, and the timeout only bounds how often we ask
             * again while there is genuinely nothing to ask about.
             */
            std::unique_lock<std::mutex> lk(dirtyMutex);
            dirtyCV.wait_for(lk, std::chrono::milliseconds(100));
            continue;
        }
        try {
            renderFrame();
        } catch (const std::exception& failure) {
            RLOG_E("renderLoop: %s", failure.what());
            markRendererFailed("renderFrame exception", VK_ERROR_UNKNOWN);
        } catch (...) {
            RLOG_E("renderLoop: unknown render exception");
            markRendererFailed("renderFrame exception", VK_ERROR_UNKNOWN);
        }
    }
    renderExited.store(true, std::memory_order_release);
    exitCV.notify_all();
}

void VulkanRendererContext::flushDeleteQueue() {

    std::lock_guard<std::mutex> lk(renderMutex);
    if (deleteQueue.empty()) return;
    if (!allFrameFencesSignaled()) return;
    for (auto& wt:deleteQueue) {
        if (wt.ds  !=VK_NULL_HANDLE) vk_.FreeDescriptorSets(device,winTexPool,1,&wt.ds);
        if (wt.view!=VK_NULL_HANDLE) vk_.DestroyImageView(device,wt.view,nullptr);
        if (wt.img !=VK_NULL_HANDLE) vk_.DestroyImage(device,wt.img,nullptr);
        if (wt.mem !=VK_NULL_HANDLE) vk_.FreeMemory(device,wt.mem,nullptr);
        if (wt.stg !=VK_NULL_HANDLE){vk_.DestroyBuffer(device,wt.stg,nullptr);vk_.FreeMemory(device,wt.stgMem,nullptr);}
    }
    deleteQueue.clear();
}

void VulkanRendererContext::renderFrame() {
    std::shared_lock<std::shared_timed_mutex> frameLock(frameMutex);

    needsRender.store(false,std::memory_order_relaxed);
    cursorMoved.store(false,std::memory_order_relaxed);

    if (surfaceDetached.load(std::memory_order_acquire)) return;
    if (surfaceWidth==0||surfaceHeight==0) return;

    if (fbResized.load()) {
        if (!waitForAllFences("swapchain resize")) return;
        cleanupSwapchain();
        bool ok=false;
        try{createSwapchain();createFramebuffers();createCmdBufs();imgInFlight.assign(swapchainImages.size(),VK_NULL_HANDLE);
ok=true;}catch(...){}
        // Said out loud, because a renderer that cannot build a swapchain shows
        // a frozen picture and used to say nothing at all about why. fbResized
        // stays raised so the next wakeup tries again.
        if (!ok) RLOG_E("renderFrame: swapchain recreate failed at %dx%d; retrying",
                        surfaceWidth, surfaceHeight);
        if (ok) {
            fbResized.store(false);
            needsRender.store(true, std::memory_order_release);
        }
        return;
    }

    if (currentFrame >= cmdBufs.size() || cmdBufs[currentFrame] == VK_NULL_HANDLE) return;
    bool currentFenceWaited = false;
    VkResult currentStatus = vk_.GetFenceStatus
            ? vk_.GetFenceStatus(device, inFlightFences[currentFrame]) : VK_NOT_READY;
    if (currentStatus == VK_NOT_READY) {
        if (!waitForFence(inFlightFences[currentFrame], "current frame fence")) return;
        currentFenceWaited = true;
    } else if (currentStatus != VK_SUCCESS) {
        markRendererFailed("current frame fence status", currentStatus);
        return;
    }

    uint32_t imgIdx;
    VkResult res=vk_.AcquireNextImageKHR(device,swapchain,kGpuWaitTimeoutNs,imgAvailSems[currentFrame],VK_NULL_HANDLE,&imgIdx);
    if (res==VK_TIMEOUT || res==VK_NOT_READY) {
        // No image became available within the bounded wait. This is normal
        // while SurfaceFlinger is throttling/backgrounding the surface and is
        // not evidence of a lost device. A later damage/resize wake retries.
        return;
    }
    if (res==VK_ERROR_OUT_OF_DATE_KHR){fbResized.store(true);return;}
    if (res==VK_ERROR_SURFACE_LOST_KHR){
        RLOG_E("renderFrame: surface lost; waiting for Android to attach a new surface");
        surfaceDetached.store(true, std::memory_order_release);
        return;
    }
    if (res!=VK_SUCCESS&&res!=VK_SUBOPTIMAL_KHR) {
        markRendererFailed("vkAcquireNextImageKHR", res);
        return;
    }
    if (imgIdx >= swapchainFBs.size() || imgIdx >= swapchainImages.size()) {
        RLOG_E("renderFrame: invalid acquired image index=%u (fb=%zu images=%zu)",
            imgIdx, swapchainFBs.size(), swapchainImages.size());
        return;
    }

    if (imgInFlight.size()!=swapchainImages.size()) imgInFlight.assign(swapchainImages.size(),VK_NULL_HANDLE);
    if (imgInFlight[imgIdx]!=VK_NULL_HANDLE &&
        (!currentFenceWaited || imgInFlight[imgIdx] != inFlightFences[currentFrame])) {
        VkResult ownerStatus = vk_.GetFenceStatus
                ? vk_.GetFenceStatus(device, imgInFlight[imgIdx]) : VK_NOT_READY;
        if (ownerStatus == VK_NOT_READY) {
            if (!waitForFence(imgInFlight[imgIdx], "swapchain image fence")) return;
        } else if (ownerStatus != VK_SUCCESS) {
            markRendererFailed("swapchain image fence status", ownerStatus);
            return;
        }
    }
    imgInFlight[imgIdx]=inFlightFences[currentFrame];

    vk_.ResetCommandBuffer(cmdBufs[currentFrame],0);

    float ox,oy,sx,sy,cw,ch;
    short ptrX,ptrY,curHotX,curHotY,curW,curH; bool curVis;
    VkBuffer curUpload=VK_NULL_HANDLE; bool hasCurUpload=false;

    VkRect2D effectiveScissor{{0,0},swapchainExt};

    {
        std::lock_guard<std::mutex> lk(renderMutex);

        if (!deleteQueue.empty() && allFrameFencesSignaled()) {
            for (auto& wt:deleteQueue) {
                if (wt.ds  !=VK_NULL_HANDLE) vk_.FreeDescriptorSets(device,winTexPool,1,&wt.ds);
                if (wt.view!=VK_NULL_HANDLE) vk_.DestroyImageView(device,wt.view,nullptr);
                if (wt.img !=VK_NULL_HANDLE) vk_.DestroyImage(device,wt.img,nullptr);
                if (wt.mem !=VK_NULL_HANDLE) vk_.FreeMemory(device,wt.mem,nullptr);
                if (wt.stg !=VK_NULL_HANDLE){vk_.DestroyBuffer(device,wt.stg,nullptr);vk_.FreeMemory(device,wt.stgMem,nullptr);}
            }
            deleteQueue.clear();
        }

        ox=sceneOffsetX; oy=sceneOffsetY; sx=sceneScaleX; sy=sceneScaleY;
        cw=(float)containerWidth; ch=(float)containerHeight;
        ptrX=(short)pointerX.load(); ptrY=(short)pointerY.load();
        curHotX=cursorHotX; curHotY=cursorHotY; curW=cursorTexW; curH=cursorTexH;
        curVis=cursorVisible.load();
        if (hasCustomScissor) effectiveScissor = customScissor;

        frameDraws.clear();
        for (auto& re:renderList) {
            auto it=texMap.find(re.id);
            if (it==texMap.end()) continue;
            WinTex& wt=it->second;
            if (wt.ds==VK_NULL_HANDLE) continue;
            DrawEntry de{wt.img,wt.ds,VK_NULL_HANDLE,re.x,re.y,wt.w,wt.h};
            de.isAHB=wt.isAHB;
            if (wt.needsTransition) { de.needsTransition=true; wt.needsTransition=false; }
            if (wt.dirty && !wt.isAHB && wt.stg!=VK_NULL_HANDLE) {
                de.upload=wt.stg;
                wt.dirty=false;
            } else if (wt.isAHB) {
                wt.dirty=false;
            }
            frameDraws.push_back(de);
        }

        if (isCursorImageDirty.load() && cursorImg!=VK_NULL_HANDLE && !cursorPixels.empty()) {
            VkDeviceSize csz=(VkDeviceSize)cursorTexW*cursorTexH*4;
            ensureCursorStaging(csz);
            isCursorImageDirty.store(false); hasCurUpload=true; curUpload=cursorStg;

            cursorUploadSize = csz;
        }
    }

    if (hasCurUpload && cursorStgP && !cursorPixels.empty())
        memcpy(cursorStgP, cursorPixels.data(), cursorUploadSize);

    bool effectiveCurVis = curVis;
    recordCmdBuf(cmdBufs[currentFrame],imgIdx,frameDraws,
        frameAhbTransitions,framePreUpload,framePostUpload,
        curUpload,hasCurUpload,
        ox,oy,sx,sy,cw,ch,ptrX,ptrY,curHotX,curHotY,curW,curH,effectiveCurVis,
        effectiveScissor);

    VkSemaphore wSem[]={imgAvailSems[currentFrame]}, sSem[]={renderDoneSems[currentFrame]};
    VkPipelineStageFlags wStage[]={VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount=1; si.pWaitSemaphores=wSem; si.pWaitDstStageMask=wStage;
    si.commandBufferCount=1; si.pCommandBuffers=&cmdBufs[currentFrame];
    si.signalSemaphoreCount=1; si.pSignalSemaphores=sSem;

    vk_.ResetFences(device,1,&inFlightFences[currentFrame]);
    VkResult submitResult = vk_.QueueSubmit(graphicsQueue,1,&si,inFlightFences[currentFrame]);
    if (submitResult!=VK_SUCCESS) {
        VkFence failedFence = inFlightFences[currentFrame];
        for (VkFence& owner : imgInFlight) {
            if (owner == failedFence) owner = VK_NULL_HANDLE;
        }
        vk_.DestroyFence(device,failedFence,nullptr);
        VkFenceCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        vk_.CreateFence(device,&fi,nullptr,&inFlightFences[currentFrame]);
        markRendererFailed("vkQueueSubmit", submitResult);
        return;
    }

    VkSwapchainKHR scs[]={swapchain};
    VkPresentInfoKHR pi{}; pi.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount=1; pi.pWaitSemaphores=sSem; pi.swapchainCount=1; pi.pSwapchains=scs; pi.pImageIndices=&imgIdx;
    res=vk_.QueuePresentKHR(graphicsQueue,&pi);
    if (res==VK_ERROR_OUT_OF_DATE_KHR) fbResized.store(true);
    else if (res==VK_ERROR_SURFACE_LOST_KHR) surfaceDetached.store(true, std::memory_order_release);
    else if (res!=VK_SUCCESS&&res!=VK_SUBOPTIMAL_KHR) markRendererFailed("vkQueuePresentKHR",res);
    currentFrame=(currentFrame+1)%MAX_FRAMES_IN_FLIGHT;
}

void VulkanRendererContext::onSurfaceResized(int w, int h) {
    std::lock_guard<std::mutex> lk(renderMutex);
    if (w==0||h==0) return;
    surfaceWidth=w; surfaceHeight=h; fbResized.store(true); dirtyCV.notify_one();
}

void VulkanRendererContext::detachSurface() {
    surfaceDetached.store(true, std::memory_order_release);
    dirtyCV.notify_all();

    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout)) {
        detachIncomplete.store(true, std::memory_order_release);
        RLOG_E("detachSurface: render worker did not release the surface in time");
        return;
    }
    if (!waitForAllFences("detachSurface")) {
        detachIncomplete.store(true, std::memory_order_release);
        return;
    }

    cleanupSwapchain();
    if (surface != VK_NULL_HANDLE) {
        vk_.DestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (window) {
        ANativeWindow_release(window);
        window = nullptr;
    }
    detachIncomplete.store(false, std::memory_order_release);
}

bool VulkanRendererContext::reattachSurface(ANativeWindow* newWindow) {
    if (!newWindow) return false;
    if (detachIncomplete.load(std::memory_order_acquire) || rendererFailed.load()) {
        RLOG_E("reattachSurface: prior surface did not detach safely");
        ANativeWindow_release(newWindow);
        return false;
    }

    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout)) {
        RLOG_E("reattachSurface: render worker did not quiesce");
        ANativeWindow_release(newWindow);
        return false;
    }
    if (window) { ANativeWindow_release(window); window = nullptr; }
    window = newWindow;
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;
    if (vk_.CreateAndroidSurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "Wemu_Renderer", "reattachSurface: CreateAndroidSurface failed");
        ANativeWindow_release(window); window = nullptr;
        return false;
    }
    try {
        createSwapchain();
        createFramebuffers();
        createCmdBufs();
        imgInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, "Wemu_Renderer", "reattachSurface: swapchain recreate failed");
        cleanupSwapchain();
        if (surface != VK_NULL_HANDLE) {
            vk_.DestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        ANativeWindow_release(window);
        window = nullptr;
        return false;
    }

    surfaceWidth  = (int)swapchainExt.width;
    surfaceHeight = (int)swapchainExt.height;

    for (auto& [id, wt] : texMap)
        wt.needsTransition = true;
    for (auto& [ahb, wt] : ahbImportCache)
        wt.needsTransition = true;

    detachIncomplete.store(false, std::memory_order_release);
    surfaceDetached.store(false, std::memory_order_release);
    forceRender();
    __android_log_print(ANDROID_LOG_DEBUG, "Wemu_Renderer", "reattachSurface: OK");
    return true;
}

/*
 * Wake the render thread. One named place, deliberately doing the simple thing.
 *
 * These were briefly coalesced: exchange(true) on the flag and notify only on
 * the false->true edge, on the theory that several X updates arrive per frame
 * and each notify_one() with a thread parked on it is a futex wake. It cost
 * frame rate on the Thor -- Valheim lost GPU utilisation and gained stutter,
 * measured 2026-08-11 -- so it is not here any more, and this comment is the
 * reason not to reinvent it without a measurement in hand.
 *
 * The likely account, kept because it also explains what would have to change
 * for the idea to work: the notify is not the expensive part. The renderer
 * blocks on a predicate that gates needsRender behind !surfaceDetached, so a
 * raised flag is not the same as a frame that will be drawn, and dropping
 * notifies against a flag that is already set removes wakeups the renderer was
 * relying on to re-evaluate. Anything attempting this again needs the predicate
 * and the flag to mean the same thing first.
 */
void VulkanRendererContext::requestRender() {
    needsRender.store(true, std::memory_order_release);
    dirtyCV.notify_one();
}

/** Reattach and other transitions that make a frame drawable again. */
void VulkanRendererContext::forceRender() {
    needsRender.store(true, std::memory_order_release);
    dirtyCV.notify_all();
}

void VulkanRendererContext::requestCursorRender() {
    cursorMoved.store(true, std::memory_order_release);
    dirtyCV.notify_one();
}

void VulkanRendererContext::setTransform(float ox, float oy, float sx, float sy) {
    { std::lock_guard<std::mutex> lk(renderMutex); sceneOffsetX=ox;sceneOffsetY=oy;sceneScaleX=sx;sceneScaleY=sy; }
    requestRender();
}

void VulkanRendererContext::updatePointerPosition(short x, short y) {
    pointerX.store(x); pointerY.store(y);
    if (cursorVisible.load()) requestCursorRender();
}

void VulkanRendererContext::setRootColor(int argb) {
    if (rootColor.exchange(argb & 0x00FFFFFF) == (argb & 0x00FFFFFF)) return;
    // The root is only redrawn when something asks for a frame, and a theme
    // change on an idle desktop asks for nothing.
    needsRender.store(true, std::memory_order_release);
    dirtyCV.notify_one();
}

void VulkanRendererContext::setCursorVisible(bool v) {
    cursorVisible.store(v);
    requestCursorRender();
}

void VulkanRendererContext::updateCursorImage(void* px, short w, short h, short hotX, short hotY) {
    if (!px||w<=0||h<=0) return;
    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout) || !waitForAllFences("cursor update")) {
        RLOG_E("updateCursorImage: renderer did not quiesce; dropping cursor image");
        return;
    }
    std::lock_guard<std::mutex> lk(renderMutex);
    ensureCursorTex(w,h);
    cursorPixels.resize((size_t)w*h); memcpy(cursorPixels.data(),px,(size_t)w*h*4);
    cursorHotX=hotX; cursorHotY=hotY;
    isCursorImageDirty.store(true);
    requestRender();
}

bool VulkanRendererContext::updateWindowContent(int64_t id, void* px, short w, short h, short stride, int, int) {
    if (!px||w<=0||h<=0 || rendererFailed.load(std::memory_order_acquire)) return false;
    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout) || !waitForAllFences("CPU window upload")) {
        RLOG_E("updateWindowContent: renderer did not quiesce; dropping window upload");
        return false;
    }
    std::lock_guard<std::mutex> lk(renderMutex);
    WinTex& wt=texMap[id];
    if (wt.isAHB) {
        // The AHB entry aliases an object owned by ahbImportCache and has no
        // CPU staging map. Switching the same-sized window back to drawable
        // pixels must detach that alias before the ordinary size check, or the
        // function reaches wt.mapped == nullptr and rejects every CPU frame.
        wt = {};
    }
    if (wt.img==VK_NULL_HANDLE || wt.w!=w || wt.h!=h) {
        if (wt.img!=VK_NULL_HANDLE) destroyWinTex(wt);
        if (!createWinTexResources(wt,w,h)) { texMap.erase(id); return false; }
    }
    void* mapped=wt.mapped;
    if (!mapped) return false;
    const size_t dstPitch=(size_t)w*4;
    const int32_t srcStride=stride>0?stride:w;
    uint32_t* src2=static_cast<uint32_t*>(px);
    uint8_t*  dst2=static_cast<uint8_t*>(mapped);
    if (srcStride==w) {
        /*
         * The common case, and worth separating. The destination pitch is
         * exactly w*4, so a source with no row padding makes the whole surface
         * one contiguous run: a single libc copy that can use the widest loads
         * the CPU has, instead of one call per scanline — 1080 of them for a
         * 1080p window, every frame it changes.
         */
        memcpy(dst2,src2,dstPitch*(size_t)h);
    }
    else {
        for (int row=0;row<h;++row)
            memcpy(dst2+(size_t)row*dstPitch,
                   &src2[(size_t)row*srcStride],dstPitch);
    }
    wt.dirty=true;
    requestRender();
    return true;
}

bool VulkanRendererContext::updateWindowContentAHB(int64_t id, AHardwareBuffer* ahb, short, short, int, int) {
    if (!ahb || rendererFailed.load(std::memory_order_acquire)) return false;
    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout)) {
        RLOG_E("updateWindowContentAHB: renderer did not release the frame lock; dropping upload");
        return false;
    }
    std::lock_guard<std::mutex> lk(renderMutex);

    auto cit = ahbImportCache.find(ahb);
    if (cit == ahbImportCache.end()) {
        WinTex tmp{};
        if (!importAHBToWinTex(tmp, ahb)) {
            RLOG_E("updateWindowContentAHB: import failed for id=%" PRId64, id);
            return false;
        }
        AHardwareBuffer_acquire(ahb);
        ahbImportCache[ahb] = tmp;
        cit = ahbImportCache.find(ahb);
        RLOG("updateWindowContentAHB: imported new AHB %p for id=%" PRId64 " (%dx%d)",
            (void*)ahb, id, tmp.w, tmp.h);
    }

    auto& ownedBuffers = windowAhbs[id];
    if (std::find(ownedBuffers.begin(), ownedBuffers.end(), ahb) == ownedBuffers.end()) {
        ownedBuffers.push_back(ahb);
    }

    WinTex& src = cit->second;
    WinTex& wt  = texMap[id];
    if (!wt.isAHB && (wt.img != VK_NULL_HANDLE || wt.stg != VK_NULL_HANDLE)) {
        // Retired after every frame using the CPU texture has completed.
        if (!waitForAllFences("retire CPU window texture")) return false;
        destroyWinTex(wt);
    }
    wt.img  = src.img;
    wt.mem  = src.mem;
    wt.view = src.view;
    wt.ds   = src.ds;
    wt.isAHB = true;
    wt.ahb  = ahb;
    wt.w    = src.w;
    wt.h    = src.h;

    if (src.needsTransition) {
        wt.needsTransition  = true;
        src.needsTransition = false;
    }
    requestRender();
    return true;
}

void VulkanRendererContext::setRenderList(const int64_t* ids, const int* xs, const int* ys, int count) {
    std::lock_guard<std::mutex> lk(renderMutex);
    renderList.resize(count);
    for (int i=0;i<count;i++) renderList[i]={ids[i],xs[i],ys[i]};
    requestRender();
}

void VulkanRendererContext::removeWindow(int64_t id) {
    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout) || !waitForAllFences("removeWindow")) {
        RLOG_E("removeWindow: renderer did not quiesce; retaining window resources");
        return;
    }
    std::lock_guard<std::mutex> lk(renderMutex);

    auto it = texMap.find(id);
    if (it != texMap.end()) {
        if (!it->second.isAHB) destroyWinTex(it->second);
        else it->second = {};
        texMap.erase(it);
    }

    auto wit = windowAhbs.find(id);
    if (wit != windowAhbs.end()) {
        for (AHardwareBuffer* ahb : wit->second) {
            bool usedElsewhere = false;
            for (const auto& owner : windowAhbs) {
                if (owner.first == id) continue;
                if (std::find(owner.second.begin(), owner.second.end(), ahb) != owner.second.end()) {
                    usedElsewhere = true;
                    break;
                }
            }
            if (usedElsewhere) continue;
            auto cit = ahbImportCache.find(ahb);
            if (cit != ahbImportCache.end()) {
                WinTex deferred = cit->second;
                deferred.isAHB  = false;
                deleteQueue.push_back(deferred);
                AHardwareBuffer_release(ahb);
                ahbImportCache.erase(cit);
            }
        }
        windowAhbs.erase(wit);
    }

    renderList.erase(std::remove_if(renderList.begin(),renderList.end(),
        [id](const RenderEntry& e){return e.id==id;}),renderList.end());
    requestRender();
}

void VulkanRendererContext::cleanupAllAHBCache() {
    for (auto& [ahb, wt] : ahbImportCache) {
        if (wt.ds   != VK_NULL_HANDLE) vk_.FreeDescriptorSets(device, winTexPool, 1, &wt.ds);
        if (wt.view != VK_NULL_HANDLE) vk_.DestroyImageView(device, wt.view, nullptr);
        if (wt.img  != VK_NULL_HANDLE) vk_.DestroyImage(device, wt.img, nullptr);
        if (wt.mem  != VK_NULL_HANDLE) vk_.FreeMemory(device, wt.mem, nullptr);
        AHardwareBuffer_release(ahb);
    }
    ahbImportCache.clear();
    windowAhbs.clear();
}

void VulkanRendererContext::dumpRendererInfo() {
    VkPhysicalDeviceProperties props{};
    vk_.GetPhysicalDeviceProperties(physicalDevice,&props);
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "=== RENDERER INFO ===");
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "GPU: %s vendorID=0x%x driverVersion=0x%x apiVersion=%d.%d.%d",
        props.deviceName,props.vendorID,props.driverVersion,
        VK_VERSION_MAJOR(props.apiVersion),VK_VERSION_MINOR(props.apiVersion),VK_VERSION_PATCH(props.apiVersion));
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "Swapchain: %dx%d fmt=%d",swapchainExt.width,swapchainExt.height,(int)swapchainFmt);
    std::string pmList;
    for(auto pm:availablePresentModes) pmList+=std::to_string((int)pm)+" ";
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "SupportedPresentModes: [%s] current=%d",pmList.c_str(),(int)requestedPresentMode);
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "Filter: mode=%d (%s)", filterMode, filterMode==3?"FSR":filterMode==2?"SGSR":filterMode==1?"NEAREST":"LINEAR");
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,
        "Surface: %dx%d container: %dx%d",
        surfaceWidth,surfaceHeight,containerWidth,containerHeight);
    __android_log_print(ANDROID_LOG_DEBUG,WLOG_TAG,"=== END RENDERER INFO ===");
}

void VulkanRendererContext::setFilterMode(int mode) {
    auto modeName=[](int m){ return m==1?"NEAREST":m==2?"SGSR":m==3?"FSR":m==4?"NIS":"LINEAR"; };
    RLOG("setFilterMode: %d -> %d (%s->%s)", filterMode, mode, modeName(filterMode), modeName(mode));
    if (filterMode==mode) { RLOG("setFilterMode: already set, skipping"); return; }

    /*
     * Held for the whole of this function, which it was not before.
     *
     * `DeviceWaitIdle` below settles the GPU but says nothing about the render
     * thread, which holds this same mutex in shared mode for the length of
     * `renderFrame` and could be halfway through recording a command buffer
     * right now. Destroying the sampler under it was already a race; this
     * function now also frees a framebuffer, an image and a descriptor set
     * that a half-recorded command buffer may name, which is the difference
     * between a race that usually gets away with it and one that does not.
     */
    std::unique_lock<std::shared_timed_mutex> frameLock(frameMutex, std::defer_lock);
    if (!frameLock.try_lock_for(kFrameLockTimeout)) {
        RLOG_E("setFilterMode: renderer did not quiesce; keeping the existing filter");
        return;
    }

    filterMode=mode;
    if (mode == 2 && sgsrPipeline == VK_NULL_HANDLE) createSgsrPipeline();
    if (mode == 3 && fsrPipeline  == VK_NULL_HANDLE) createFsrPipeline();
    if (mode == 4 && nisPipeline  == VK_NULL_HANDLE) createNisPipeline();
    if (!waitForAllFences("setFilterMode")) return;
    if (sampler!=VK_NULL_HANDLE){vk_.DestroySampler(device,sampler,nullptr);sampler=VK_NULL_HANDLE;}
    createSampler();
    auto updateDS=[&](VkDescriptorSet ds, VkImageView view){
        if(ds==VK_NULL_HANDLE||view==VK_NULL_HANDLE) return;
        VkDescriptorImageInfo dii{}; dii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii.imageView=view; dii.sampler=sampler;
        VkWriteDescriptorSet wr{}; wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet=ds; wr.dstBinding=0; wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount=1; wr.pImageInfo=&dii;
        vk_.UpdateDescriptorSets(device,1,&wr,0,nullptr);
    };

    for (auto& [id,wt]:texMap) updateDS(wt.ds, wt.view);

    for (auto& [ahb,wt]:ahbImportCache) updateDS(wt.ds, wt.view);
    if (cursorDS!=VK_NULL_HANDLE&&cursorView!=VK_NULL_HANDLE) updateDS(cursorDS, cursorView);

    // The finishing target is worth eight megabytes only while an upscaler is
    // set. Its descriptor also holds the sampler that was just destroyed above,
    // so rebuilding rather than rewriting it is the simpler correctness argument.
    destroyFsrTarget();
    ensureFsrTarget();

    requestRender();
}

void VulkanRendererContext::setStretchMode(int mode) {
    RLOG("setStretchMode: %d -> %d", stretchMode, mode);
    if (stretchMode == mode) return;
    stretchMode = mode;
    if (mode == 1 && stretchPipeline == VK_NULL_HANDLE) createStretchPipeline();
    requestRender();
}

void VulkanRendererContext::setSwapRB(bool enabled) {
    if (swapRB == enabled) return;
    swapRB = enabled;
    RLOG("setSwapRB: %d", (int)swapRB);

}

void VulkanRendererContext::setPresentMode(VkPresentModeKHR mode) {
    bool supported = false;
    for (auto pm : availablePresentModes) if (pm == mode) { supported = true; break; }
    VkPresentModeKHR target = supported ? mode : VK_PRESENT_MODE_FIFO_KHR;
    RLOG("setPresentMode: requested=%d supported=%d -> applying=%d",
        (int)mode, (int)supported, (int)target);
    if (requestedPresentMode==target) { RLOG("setPresentMode: already set, skipping"); return; }
    requestedPresentMode=target;
    fbResized.store(true); dirtyCV.notify_one();
}

std::vector<int> VulkanRendererContext::getSupportedPresentModes() const {
    std::vector<int> out;
    for (auto pm:availablePresentModes) out.push_back((int)pm);
    return out;
}

void VulkanRendererContext::setCustomScissor(int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lk(renderMutex);
    customScissor  = {{x, y}, {(uint32_t)std::max(0, w), (uint32_t)std::max(0, h)}};
    hasCustomScissor = true;
    requestRender();
}

void VulkanRendererContext::clearCustomScissor() {
    std::lock_guard<std::mutex> lk(renderMutex);
    hasCustomScissor = false;
    requestRender();
}

#pragma GCC diagnostic pop
