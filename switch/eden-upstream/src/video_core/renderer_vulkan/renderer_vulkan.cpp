// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/ranges.h>

#include "common/logging.h"
#include <ranges>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core_timing.h"
#include "core/frontend/graphics_context.h"
#include "video_core/capture.h"
#include "video_core/gpu.h"
#include "video_core/present.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/textures/decoders.h"
#include "video_core/vulkan_common/vulkan_debug_callback.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#ifdef __ANDROID__
#include <jni.h>
#endif
namespace Vulkan {
namespace {

constexpr VkExtent2D CaptureImageSize{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
};

#ifdef HAS_LSFG
[[nodiscard]] VkExtent2D GuestExtent(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (framebuffers.empty()) {
        return VkExtent2D{};
    }

    const auto& framebuffer = framebuffers.front();
    if (framebuffer.crop_rect.IsEmpty()) {
        return VkExtent2D{.width = framebuffer.width, .height = framebuffer.height};
    }
    return VkExtent2D{
        .width = static_cast<u32>(framebuffer.crop_rect.GetWidth()),
        .height = static_cast<u32>(framebuffer.crop_rect.GetHeight()),
    };
}
#endif

constexpr VkExtent3D CaptureImageExtent{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
    .depth = VideoCore::Capture::LinearDepth,
};

constexpr VkFormat CaptureFormat = VK_FORMAT_A8B8G8R8_UNORM_PACK32;

std::string GetReadableVersion(u32 version) {
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version),
                       VK_VERSION_PATCH(version));
}

std::string GetDriverVersion(const Device& device) {
    // Extracted from
    // https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/5dddea46ea1120b0df14eef8f15ff8e318e35462/functions.php#L308-L314
    const u32 version = device.GetDriverVersion();

    if (device.GetDriverID() == VK_DRIVER_ID_NVIDIA_PROPRIETARY) {
        const u32 major = (version >> 22) & 0x3ff;
        const u32 minor = (version >> 14) & 0x0ff;
        const u32 secondary = (version >> 6) & 0x0ff;
        const u32 tertiary = version & 0x003f;
        return fmt::format("{}.{}.{}.{}", major, minor, secondary, tertiary);
    }
    if (device.GetDriverID() == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS) {
        const u32 major = version >> 14;
        const u32 minor = version & 0x3fff;
        return fmt::format("{}.{}", major, minor);
    }
    return GetReadableVersion(version);
}

std::string BuildCommaSeparatedExtensions(
    const std::set<std::string, std::less<>>& available_extensions) {
    return fmt::format("{}", fmt::join(available_extensions, ","));
}

} // Anonymous namespace

Device CreateDevice(const vk::Instance& instance, const vk::InstanceDispatch& dld, VkSurfaceKHR surface) {
    const std::vector<VkPhysicalDevice> devices = instance.EnumeratePhysicalDevices();
    const u32 device_index = Settings::values.vulkan_device.GetValue();
    if (device_index >= u32(devices.size())) {
        LOG_ERROR(Render_Vulkan, "Invalid device index {}!", device_index);
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const vk::PhysicalDevice physical_device(devices[device_index], dld);
    return Device(*instance, physical_device, surface, dld);
}

class RendererVulkan::CompanionPresentation {
    class Window final : public Core::Frontend::EmuWindow {
    public:
        Window(const VideoCore::CompanionSurface& surface, Core::Frontend::WindowSystemType type) {
            window_info.type = type;
            window_info.render_surface = surface.native_window.get();
            NotifyFramebufferLayoutChanged({surface.width, surface.height,
                                             {0, 0, surface.width, surface.height}, false});
        }
        bool IsShown() const override { return true; }
        std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
            return {};
        }
    };
public:
    CompanionPresentation(const VideoCore::CompanionSurface& source,
                          Core::Frontend::WindowSystemType type,
                          const vk::Instance& instance, const Device& device_,
                          Tegra::MaxwellDeviceMemoryManager& memory,
                          MemoryAllocator& allocator, Scheduler& scheduler_)
        : native_surface{source}, window{source, type}, device{device_}, scheduler{scheduler_},
          surface{CreateSurface(instance, window.GetWindowInfo())},
          swapchain{*surface, device, scheduler, source.width, source.height, true},
          presentation{instance, window, device, allocator, scheduler, swapchain, surface, true},
          blit{memory, device, allocator, presentation, scheduler, PresentFiltersForAppletCapture, true} {}

    ~CompanionPresentation() {
        scheduler.Finish();
        presentation.WaitPresent();
        void(device.GetLogical().WaitIdle());
    }

    bool Matches(const VideoCore::CompanionSurface& next) const {
        return native_surface.native_window == next.native_window &&
               native_surface.width == next.width && native_surface.height == next.height;
    }

    bool Draw(RasterizerVulkan& rasterizer, const Tegra::FramebufferConfig& framebuffer) {
        Frame* frame = presentation.TryGetRenderFrame();
        if (!frame) return false;
        scheduler.RequestOutsideRenderPassOperationContext();
        blit.DrawToFrame(device, rasterizer, frame, {&framebuffer, 1},
                         window.GetFramebufferLayout(), swapchain.GetImageCount(),
                         swapchain.GetImageViewFormat());
        scheduler.Flush(*frame->render_ready);
        presentation.Present(frame);
        return true;
    }

private:
    VideoCore::CompanionSurface native_surface;
    Window window;
    const Device& device;
    Scheduler& scheduler;
    vk::SurfaceKHR surface;
    Swapchain swapchain;
    PresentManager presentation;
    BlitScreen blit;
};

void RendererVulkan::RenderCompanion() {
    const auto state = GetCompanionFrameState();
    if (companion_presentation && !companion_presentation->Matches(state.surface)) {
        companion_presentation.reset();
        SetCompanionPresentedAddress(0);
    }
    if (!state.surface.native_window || !state.surface.width || !state.surface.height) return;
    bool in_progress{};
    auto framebuffer = rasterizer.ResolveCompanionFramebuffer(
        state.texture.program_id, state.texture.gpu_address, state.texture.width, state.texture.height,
        state.texture.flip_vertical, in_progress);
    if (!framebuffer) {
        if (!in_progress || CompanionPresentedAddress() != state.texture.gpu_address) {
            SetCompanionPresentedAddress(0);
        }
        return;
    }
    if (state.texture.flip_vertical) {
        framebuffer->transform_flags = Service::android::BufferTransformFlags::FlipV;
    }
    if (!companion_presentation) {
        companion_presentation = std::make_unique<CompanionPresentation>(
            state.surface, render_window.GetWindowInfo().type, instance, device,
            device_memory, memory_allocator, scheduler);
        LOG_INFO(Render_Vulkan, "[COMPANION-GPU] created presentation surface {}x{}",
                 state.surface.width, state.surface.height);
    }
    if (companion_presentation->Draw(rasterizer, *framebuffer)) {
        if (CompanionPresentedAddress() != state.texture.gpu_address) {
            LOG_INFO(Render_Vulkan, "[COMPANION-GPU] presenting resident map {:#x} {}x{}",
                     state.texture.gpu_address, state.texture.width, state.texture.height);
        }
        SetCompanionPresentedAddress(state.texture.gpu_address);
    }
}

RendererVulkan::RendererVulkan(Core::Frontend::EmuWindow& emu_window,
                               Tegra::MaxwellDeviceMemoryManager& device_memory_,
                               Tegra::GPU& gpu_,
                               std::unique_ptr<Core::Frontend::GraphicsContext> context_)
try
    : RendererBase(emu_window, std::move(context_))
    , device_memory(device_memory_)
    , gpu(gpu_)
    , library(OpenLibrary(context.get()))
    , dld()
    // Create raw Vulkan instance first
    , instance(CreateInstance(*library,
                            dld,
                            VK_API_VERSION_1_1,
                            render_window.GetWindowInfo().type,
                            Settings::values.renderer_debug.GetValue()))
    // Create debug messenger if debug is enabled
    , debug_messenger(Settings::values.renderer_debug ? CreateDebugUtilsCallback(instance)
                                                    : vk::DebugUtilsMessenger{})
    // Create surface
    , surface(CreateSurface(instance, render_window.GetWindowInfo()))
    , device(CreateDevice(instance, dld, *surface))
    , memory_allocator(device)
    , state_tracker()
    , scheduler(device, state_tracker)
    , swapchain(*surface,
                device,
                scheduler,
               render_window.GetFramebufferLayout().width,
               render_window.GetFramebufferLayout().height)
    , present_manager(instance,
                      render_window,
                      device,
                      memory_allocator,
                      scheduler,
                      swapchain,
                      surface)
    , blit_swapchain(device_memory,
                   device,
                   memory_allocator,
                   present_manager,
                   scheduler,
                   PresentFiltersForDisplay)
    , blit_capture(device_memory,
                   device,
                   memory_allocator,
                   present_manager,
                   scheduler,
                   PresentFiltersForDisplay)
    , blit_applet(device_memory,
                  device,
                  memory_allocator,
                  present_manager,
                  scheduler,
                  PresentFiltersForAppletCapture)
    , rasterizer(render_window, gpu, device_memory, device, memory_allocator, state_tracker, scheduler)
#ifdef HAS_LSFG
    , frame_gen(memory_allocator, scheduler)
#endif
{

    if (Settings::values.renderer_force_max_clock.GetValue() && device.ShouldBoostClocks()) {
        turbo_mode.emplace(instance, dld);
        scheduler.RegisterOnSubmit([this] { turbo_mode->QueueSubmitted(); });
    }

    Report();
} catch (const vk::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "Vulkan initialization failed with error: {}", exception.what());
    throw std::runtime_error{fmt::format("Vulkan initialization error {}", exception.what())};
}

RendererVulkan::~RendererVulkan() {
    companion_presentation.reset();
    scheduler.RegisterOnSubmit([] {});
    void(device.GetLogical().WaitIdle());
}

void RendererVulkan::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    SCOPE_EXIT {
        render_window.OnFrameDisplayed();
    };

    if (const auto probe = TakeCompanionTextureProbe()) {
        rasterizer.ProbeCompanionTexture(probe->program_id, probe->gpu_address,
                                         probe->width, probe->height);
    }
    RenderCompanion();

    RenderAppletCaptureLayer(framebuffers);

    if (!render_window.IsShown()) {
        return;
    }

    RenderScreenshot(framebuffers);
    Frame* frame = present_manager.GetRenderFrame();

    scheduler.RequestOutsideRenderPassOperationContext();
    blit_swapchain.DrawToFrame(device, rasterizer, frame, framebuffers,
                               render_window.GetFramebufferLayout(), swapchain.GetImageCount(),
                               swapchain.GetImageViewFormat());

#ifdef HAS_LSFG
    void(frame_gen.WantedGenerations(present_manager.MaxExtraFrames()));

    frame_gen.Process(device, frame, swapchain.GetImageFormat(), GuestExtent(framebuffers));

    const size_t generated_frames = frame_gen.GeneratedFrameCount();
    for (size_t generation = 0; generation < generated_frames; ++generation) {
        Frame* generated = present_manager.GetRenderFrame();
        blit_swapchain.PrepareFrame(device, generated, render_window.GetFramebufferLayout());
        frame_gen.GenerateInto(device, generated, generation);
        scheduler.Flush(*generated->render_ready);
        present_manager.Present(generated);
    }
#endif

    scheduler.Flush(*frame->render_ready);

    present_manager.Present(frame);
#ifdef HAS_LSFG
    scheduler.DispatchWork();
#endif

    gpu.RendererFrameEndNotify();
    rasterizer.TickFrame();
}

void RendererVulkan::Report() const {
    using namespace Common::Literals;
    const std::string vendor_name{device.GetVendorName()};
    const std::string model_name{device.GetModelName()};
    const std::string driver_version = GetDriverVersion(device);
    const std::string driver_name = fmt::format("{} {}", vendor_name, driver_version);

    const std::string api_version = GetReadableVersion(device.ApiVersion());

    const std::string extensions = BuildCommaSeparatedExtensions(device.GetAvailableExtensions());

    const auto available_vram = static_cast<f64>(device.GetDeviceLocalMemory()) / f64{1_GiB};

    LOG_INFO(Render_Vulkan, "Driver: {}", driver_name);
    LOG_INFO(Render_Vulkan, "Device: {}", model_name);
    LOG_INFO(Render_Vulkan, "Vulkan: {}", api_version);
    LOG_INFO(Render_Vulkan, "Available VRAM: {:.2f} GiB", available_vram);
}

vk::Buffer RendererVulkan::RenderToBuffer(std::span<const Tegra::FramebufferConfig> framebuffers,
                                          const Layout::FramebufferLayout& layout, VkFormat format,
                                          VkDeviceSize buffer_size) {
    auto frame = [&]() {
        Frame f{};
        f.image =
            CreateWrappedImage(memory_allocator, VkExtent2D{layout.width, layout.height}, format);
        f.image_view = CreateWrappedImageView(device, f.image, format);
        f.framebuffer = blit_capture.CreateFramebuffer(device, layout, *f.image_view, format);
        return f;
    }();

    auto dst_buffer = CreateWrappedBuffer(memory_allocator, buffer_size, MemoryUsage::Download);
    blit_capture.DrawToFrame(device, rasterizer, &frame, framebuffers, layout, 1, format);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *frame.image, *dst_buffer,
                           VkExtent3D{layout.width, layout.height, 1});
    });

    // Ensure the copy is fully completed before saving the capture
    scheduler.Finish();

    // Copy backing image data to the capture buffer
    dst_buffer.Invalidate();
    return dst_buffer;
}

void RendererVulkan::RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!renderer_settings.screenshot_requested) {
        return;
    }

    const auto& layout{renderer_settings.screenshot_framebuffer_layout};
    const auto dst_buffer = RenderToBuffer(framebuffers, layout, VK_FORMAT_B8G8R8A8_UNORM,
                                           layout.width * layout.height * 4);

    std::memcpy(renderer_settings.screenshot_bits, dst_buffer.Mapped().data(),
                dst_buffer.Mapped().size());
    renderer_settings.screenshot_complete_callback(false);
    renderer_settings.screenshot_requested = false;
}

std::vector<u8> RendererVulkan::GetAppletCaptureBuffer() {
    using namespace VideoCore::Capture;

    std::vector<u8> out(VideoCore::Capture::TiledSize);

    if (!applet_frame.image) {
        return out;
    }

    const auto dst_buffer =
        CreateWrappedBuffer(memory_allocator, VideoCore::Capture::TiledSize, MemoryUsage::Download);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *applet_frame.image, *dst_buffer, CaptureImageExtent);
    });

    // Ensure the copy is fully completed before writing the capture
    scheduler.Finish();

    // Swizzle image data to the capture buffer
    dst_buffer.Invalidate();
    Tegra::Texture::SwizzleTexture(out, dst_buffer.Mapped(), BytesPerPixel, LinearWidth,
                                   LinearHeight, LinearDepth, BlockHeight, BlockDepth);

    return out;
}

void RendererVulkan::RenderAppletCaptureLayer(
    std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!applet_frame.image) {
        applet_frame.image = CreateWrappedImage(memory_allocator, CaptureImageSize, CaptureFormat);
        applet_frame.image_view = CreateWrappedImageView(device, applet_frame.image, CaptureFormat);
        applet_frame.framebuffer = blit_applet.CreateFramebuffer(device,
            VideoCore::Capture::Layout, *applet_frame.image_view, CaptureFormat);
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    blit_applet.DrawToFrame(device, rasterizer, &applet_frame, framebuffers, VideoCore::Capture::Layout, 1,
                            CaptureFormat);
}

} // namespace Vulkan
