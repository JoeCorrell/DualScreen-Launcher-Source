#include "NativeUiRenderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

#include "Log.h"
#include "Mem.h"
#include "GameTextureArchive.h"

namespace dxs {
namespace {

constexpr uintptr_t kGetDtpDataVa = 0x005A4E60;
// g_resolveSections begins at 0x019E0800. Inventory pickup texture8/texture10
// fields are direct type-5 RenderResource IDs, so they must be resolved through
// g_resolveSections[5]. 0x019E0818 is section 6 (FMOD Wave); an older build
// accidentally queried that audio table and consequently could never find an
// inventory texture.
constexpr uintptr_t kTextureResourceSectionGlobalVa = 0x019E0814;
constexpr uintptr_t kTextureResourceHandlerVtableVa = 0x00A9CC4C;
// Exact PCDX11BaseTexture virtuals in DXHRDC 2.0.66.0. The returned
// RenderResource is a PCDX11Texture whose PCDX11BaseTexture subobject starts at
// +4. Validate the full method tuple before calling into a game object.
constexpr uintptr_t kTextureGetResourceVa = 0x005305C0;
constexpr uintptr_t kTextureCreateViewVa = 0x00530330;
constexpr uintptr_t kTextureCreateAccessViewVa = 0x005303D0;
constexpr size_t kGetBasePointerVtableSlot = 0x30 / sizeof(void*);
constexpr size_t kResolveResourceIdVtableSlot = 0x44 / sizeof(void*);
constexpr size_t kBaseTextureSubobjectOffset = 4;
constexpr size_t kBaseTextureWidthOffset = 0x14;
constexpr size_t kBaseTextureHeightOffset = 0x18;
constexpr size_t kBaseTextureResourceOffset = 0x128;
constexpr size_t kBaseTextureViewOffset = 0x12C;
constexpr size_t kMaximumQueuedQuads = 1024;

using GetDtpDataFn = void*(__cdecl*)(uint32_t);
using GetBasePointerFn = void*(__thiscall*)(void*, uint32_t);
using ResolveResourceIdFn = uint32_t(__thiscall*)(void*, uint32_t);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
                                     const D3D_SHADER_MACRO*, ID3DInclude*,
                                     LPCSTR, LPCSTR, UINT, UINT,
                                     ID3DBlob**, ID3DBlob**);

template <typename T>
void ReleaseAndNull(T*& object) {
  if (object) object->Release();
  object = nullptr;
}

bool LooksLikeComObject(void* object) {
  void** table = nullptr;
  if (!object || !Read(object, &table) || !Readable(table, sizeof(void*) * 3)) {
    return false;
  }
  return Executable(table[0], 1) && Executable(table[1], 1) &&
         Executable(table[2], 1);
}

bool ValidDimension(uint32_t dimension) {
  return dimension > 0 && dimension <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

struct Vertex {
  float position[2];
  float uv[2];
  float color[4];
};

struct PendingQuad {
  ID3D11ShaderResourceView* view = nullptr;
  NativeUiRect destination;
  NativeUiUvRect uv;
  NativeUiColor color;
  float clockwise_degrees = 0.0f;
};

struct CachedTexture {
  ID3D11ShaderResourceView* view = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
};

// Only slots changed by NativeUiRenderer are captured.  Get calls AddRef their
// outputs, so Restore both rebinds and releases the temporary references.
struct SavedD3DState {
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* vertex_buffer = nullptr;
  UINT vertex_stride = 0;
  UINT vertex_offset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

  ID3D11VertexShader* vertex_shader = nullptr;
  ID3D11HullShader* hull_shader = nullptr;
  ID3D11DomainShader* domain_shader = nullptr;
  ID3D11GeometryShader* geometry_shader = nullptr;
  ID3D11PixelShader* pixel_shader = nullptr;
  ID3D11ShaderResourceView* pixel_view = nullptr;
  ID3D11SamplerState* pixel_sampler = nullptr;

  ID3D11RasterizerState* rasterizer = nullptr;
  D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

  ID3D11RenderTargetView* render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* depth_target = nullptr;
  ID3D11BlendState* blend = nullptr;
  FLOAT blend_factor[4] = {};
  UINT sample_mask = 0xffffffffu;
  ID3D11DepthStencilState* depth_state = nullptr;
  UINT stencil_ref = 0;

  void Capture(ID3D11DeviceContext* context) {
    context->IAGetInputLayout(&input_layout);
    context->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
    context->IAGetPrimitiveTopology(&topology);
    context->VSGetShader(&vertex_shader, nullptr, nullptr);
    context->HSGetShader(&hull_shader, nullptr, nullptr);
    context->DSGetShader(&domain_shader, nullptr, nullptr);
    context->GSGetShader(&geometry_shader, nullptr, nullptr);
    context->PSGetShader(&pixel_shader, nullptr, nullptr);
    context->PSGetShaderResources(0, 1, &pixel_view);
    context->PSGetSamplers(0, 1, &pixel_sampler);
    context->RSGetState(&rasterizer);
    context->RSGetViewports(&viewport_count, viewports);
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                render_targets, &depth_target);
    context->OMGetBlendState(&blend, blend_factor, &sample_mask);
    context->OMGetDepthStencilState(&depth_state, &stencil_ref);
  }

  void Restore(ID3D11DeviceContext* context) {
    context->IASetInputLayout(input_layout);
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
    context->IASetPrimitiveTopology(topology);
    context->VSSetShader(vertex_shader, nullptr, 0);
    context->HSSetShader(hull_shader, nullptr, 0);
    context->DSSetShader(domain_shader, nullptr, 0);
    context->GSSetShader(geometry_shader, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetShaderResources(0, 1, &pixel_view);
    context->PSSetSamplers(0, 1, &pixel_sampler);
    context->RSSetState(rasterizer);
    context->RSSetViewports(viewport_count, viewport_count ? viewports : nullptr);
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                render_targets, depth_target);
    context->OMSetBlendState(blend, blend_factor, sample_mask);
    context->OMSetDepthStencilState(depth_state, stencil_ref);

    ReleaseAndNull(input_layout);
    ReleaseAndNull(vertex_buffer);
    ReleaseAndNull(vertex_shader);
    ReleaseAndNull(hull_shader);
    ReleaseAndNull(domain_shader);
    ReleaseAndNull(geometry_shader);
    ReleaseAndNull(pixel_shader);
    ReleaseAndNull(pixel_view);
    ReleaseAndNull(pixel_sampler);
    ReleaseAndNull(rasterizer);
    for (auto*& target : render_targets) ReleaseAndNull(target);
    ReleaseAndNull(depth_target);
    ReleaseAndNull(blend);
    ReleaseAndNull(depth_state);
  }
};

const char kVertexShaderSource[] = R"(
struct VsInput {
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};
struct VsOutput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};
VsOutput main(VsInput input) {
  VsOutput output;
  output.position = float4(input.position, 0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
})";

const char kPixelShaderSource[] = R"(
Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);
struct PsInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};
float4 main(PsInput input) : SV_TARGET {
  float4 result = sourceTexture.Sample(sourceSampler, input.uv) * input.color;
  clip(result.a - (1.0 / 255.0));
  return result;
})";

HMODULE LoadCompiler() {
  constexpr const wchar_t* names[] = {
      L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"};
  for (const wchar_t* name : names) {
    HMODULE module = LoadLibraryW(name);
    if (module && GetProcAddress(module, "D3DCompile")) return module;
    if (module) FreeLibrary(module);
  }
  return nullptr;
}

bool CompileShader(D3DCompileFn compile, const char* source, const char* target,
                   ID3DBlob** bytecode) {
  if (!compile || !source || !target || !bytecode) return false;
  *bytecode = nullptr;
  ID3DBlob* error = nullptr;
  const HRESULT result = compile(source, std::strlen(source), "DXSNativeUi",
                                 nullptr, nullptr, "main", target,
                                 D3DCOMPILE_ENABLE_STRICTNESS |
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                 0, bytecode, &error);
  if (FAILED(result)) {
    const char* message = error ? static_cast<const char*>(error->GetBufferPointer()) :
                                  "no compiler diagnostic";
    DXS_ERR("native UI shader %s failed hr=%08lx: %s", target,
            static_cast<unsigned long>(result), message);
  }
  ReleaseAndNull(error);
  return SUCCEEDED(result) && *bytecode;
}

void WriteQuad(Vertex* vertices, const PendingQuad& quad, float width, float height) {
  const float color[] = {quad.color.red, quad.color.green,
                         quad.color.blue, quad.color.alpha};
  const float center_x = (quad.destination.left + quad.destination.right) * 0.5f;
  const float center_y = (quad.destination.top + quad.destination.bottom) * 0.5f;
  constexpr float kDegreesToRadians = 0.01745329251994329577f;
  float cosine = 1.0f;
  float sine = 0.0f;
  if (std::fabs(quad.clockwise_degrees) > 0.0001f) {
    const float radians = quad.clockwise_degrees * kDegreesToRadians;
    cosine = std::cos(radians);
    sine = std::sin(radians);
  }

  auto set = [&](int index, float pixel_x, float pixel_y, float u, float v) {
    const float dx = pixel_x - center_x;
    const float dy = pixel_y - center_y;
    const float rotated_x = center_x + dx * cosine - dy * sine;
    const float rotated_y = center_y + dx * sine + dy * cosine;
    vertices[index].position[0] = rotated_x / width * 2.0f - 1.0f;
    vertices[index].position[1] = 1.0f - rotated_y / height * 2.0f;
    vertices[index].uv[0] = u;
    vertices[index].uv[1] = v;
    std::memcpy(vertices[index].color, color, sizeof(color));
  };
  set(0, quad.destination.left,  quad.destination.top,
      quad.uv.left,  quad.uv.top);
  set(1, quad.destination.right, quad.destination.top,
      quad.uv.right, quad.uv.top);
  set(2, quad.destination.right, quad.destination.bottom,
      quad.uv.right, quad.uv.bottom);
  set(3, quad.destination.left,  quad.destination.top,
      quad.uv.left,  quad.uv.top);
  set(4, quad.destination.right, quad.destination.bottom,
      quad.uv.right, quad.uv.bottom);
  set(5, quad.destination.left,  quad.destination.bottom,
      quad.uv.left,  quad.uv.bottom);
}

}  // namespace

struct NativeUiRenderer::Impl {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11RenderTargetView* target = nullptr;
  ID3D11VertexShader* vertex_shader = nullptr;
  ID3D11PixelShader* pixel_shader = nullptr;
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* vertex_buffer = nullptr;
  ID3D11SamplerState* sampler = nullptr;
  ID3D11BlendState* blend = nullptr;
  ID3D11RasterizerState* rasterizer = nullptr;
  ID3D11DepthStencilState* depth = nullptr;
  ID3D11ShaderResourceView* circle_mask = nullptr;
  ID3D11ShaderResourceView* diamond_mask = nullptr;
  ID3D11ShaderResourceView* player_arrow_mask = nullptr;
  ID3D11ShaderResourceView* lock_mask = nullptr;
  ID3D11ShaderResourceView* reticle_mask = nullptr;
  HMODULE compiler_module = nullptr;
  size_t vertex_capacity = 0;
  float width = 0.0f;
  float height = 0.0f;
  bool frame_active = false;
  uint32_t rendered_last_frame = 0;
  std::vector<PendingQuad> pending;
  std::unordered_map<uint32_t, CachedTexture> textures;
  std::unordered_map<uint32_t, uint64_t> texture_retry_after;
  std::unordered_map<uint32_t, int> texture_failure_reason;

  void ReleasePending() {
    for (auto& quad : pending) ReleaseAndNull(quad.view);
    pending.clear();
    ReleaseAndNull(target);
    frame_active = false;
  }

  void Shutdown() {
    ReleasePending();
    for (auto& entry : textures) ReleaseAndNull(entry.second.view);
    textures.clear();
    texture_retry_after.clear();
    texture_failure_reason.clear();
    ReleaseAndNull(vertex_buffer);
    ReleaseAndNull(circle_mask);
    ReleaseAndNull(diamond_mask);
    ReleaseAndNull(player_arrow_mask);
    ReleaseAndNull(lock_mask);
    ReleaseAndNull(reticle_mask);
    ReleaseAndNull(depth);
    ReleaseAndNull(rasterizer);
    ReleaseAndNull(blend);
    ReleaseAndNull(sampler);
    ReleaseAndNull(input_layout);
    ReleaseAndNull(pixel_shader);
    ReleaseAndNull(vertex_shader);
    ReleaseAndNull(context);
    ReleaseAndNull(device);
    if (compiler_module) FreeLibrary(compiler_module);
    compiler_module = nullptr;
    vertex_capacity = 0;
    rendered_last_frame = 0;
  }

  bool CreateMarkerMask(bool diamond,
                        ID3D11ShaderResourceView** destination) {
    if (!device || !destination) return false;
    if (*destination) return true;
    constexpr uint32_t kSize = 32;
    std::vector<uint32_t> pixels(kSize * kSize, 0u);
    for (uint32_t y = 0; y < kSize; ++y) {
      for (uint32_t x = 0; x < kSize; ++x) {
        const float nx = (static_cast<float>(x) + 0.5f - kSize * 0.5f) /
                         (kSize * 0.5f - 1.0f);
        const float ny = (static_cast<float>(y) + 0.5f - kSize * 0.5f) /
                         (kSize * 0.5f - 1.0f);
        const float distance = diamond ? std::fabs(nx) + std::fabs(ny)
                                       : std::sqrt(nx * nx + ny * ny);
        const float coverage = (std::max)(0.0f, (std::min)(
            1.0f, (1.035f - distance) * 18.0f));
        const uint32_t alpha = static_cast<uint32_t>(
            std::lround(coverage * 255.0f));
        pixels[static_cast<size_t>(y) * kSize + x] =
            (alpha << 24u) | 0x00ffffffu;
      }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = kSize * sizeof(uint32_t);
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(result) || !texture) {
      ReleaseAndNull(texture);
      return false;
    }
    result = device->CreateShaderResourceView(texture, nullptr, destination);
    ReleaseAndNull(texture);
    return SUCCEEDED(result) && *destination;
  }

  bool CreatePlayerArrowMask() {
    if (!device) return false;
    if (player_arrow_mask) return true;
    constexpr uint32_t kSize = 64;
    constexpr int kSamples = 4;
    // The retail orientation value is measured from the authored map X axis,
    // so the unrotated procedural arrow points right rather than north.
    constexpr float polygon[][2] = {
        {0.965f, 0.50f}, {0.51f, 0.93f}, {0.51f, 0.66f},
        {0.06f, 0.66f},  {0.06f, 0.34f}, {0.51f, 0.34f},
        {0.51f, 0.07f},
    };
    constexpr int kPoints =
        static_cast<int>(sizeof(polygon) / sizeof(polygon[0]));
    std::vector<uint32_t> pixels(kSize * kSize, 0u);
    for (uint32_t y = 0; y < kSize; ++y) {
      for (uint32_t x = 0; x < kSize; ++x) {
        int covered = 0;
        for (int sy = 0; sy < kSamples; ++sy) {
          for (int sx = 0; sx < kSamples; ++sx) {
            const float px = (static_cast<float>(x) +
                              (sx + 0.5f) / kSamples) / kSize;
            const float py = (static_cast<float>(y) +
                              (sy + 0.5f) / kSamples) / kSize;
            bool inside = false;
            for (int i = 0, j = kPoints - 1; i < kPoints; j = i++) {
              const float xi = polygon[i][0];
              const float yi = polygon[i][1];
              const float xj = polygon[j][0];
              const float yj = polygon[j][1];
              if ((yi > py) != (yj > py) &&
                  px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
                inside = !inside;
              }
            }
            if (inside) ++covered;
          }
        }
        const uint32_t alpha = static_cast<uint32_t>(
            std::lround(255.0f * covered / (kSamples * kSamples)));
        pixels[static_cast<size_t>(y) * kSize + x] =
            (alpha << 24u) | 0x00ffffffu;
      }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = kSize * sizeof(uint32_t);
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(result) || !texture) {
      ReleaseAndNull(texture);
      return false;
    }
    result = device->CreateShaderResourceView(texture, nullptr,
                                               &player_arrow_mask);
    ReleaseAndNull(texture);
    return SUCCEEDED(result) && player_arrow_mask;
  }

  bool CreateLockMask() {
    if (!device) return false;
    if (lock_mask) return true;
    constexpr uint32_t kSize = 64;
    constexpr int kSamples = 4;
    std::vector<uint32_t> pixels(kSize * kSize, 0u);
    for (uint32_t y = 0; y < kSize; ++y) {
      for (uint32_t x = 0; x < kSize; ++x) {
        int covered = 0;
        for (int sy = 0; sy < kSamples; ++sy) {
          for (int sx = 0; sx < kSamples; ++sx) {
            const float px = (static_cast<float>(x) +
                              (sx + 0.5f) / kSamples) / kSize;
            const float py = (static_cast<float>(y) +
                              (sy + 0.5f) / kSamples) / kSize;
            const float dx = px - 0.5f;
            const float outer = (dx * dx) / (0.27f * 0.27f) +
                ((py - 0.38f) * (py - 0.38f)) / (0.27f * 0.27f);
            const float inner = (dx * dx) / (0.145f * 0.145f) +
                ((py - 0.39f) * (py - 0.39f)) / (0.16f * 0.16f);
            const bool shackle = py >= 0.10f && py <= 0.51f &&
                                 outer <= 1.0f && inner >= 1.0f;
            const float corner_x = (std::max)(0.0f, std::fabs(dx) - 0.285f);
            const float corner_y = (std::max)(0.0f, std::fabs(py - 0.68f) -
                                                     0.205f);
            const bool body = py >= 0.43f && py <= 0.91f &&
                              corner_x * corner_x + corner_y * corner_y <=
                                  0.055f * 0.055f;
            const bool key_head = dx * dx + (py - 0.64f) * (py - 0.64f) <=
                                  0.052f * 0.052f;
            const bool key_stem = std::fabs(dx) <= 0.025f &&
                                  py >= 0.64f && py <= 0.78f;
            if ((shackle || body) && !key_head && !key_stem) ++covered;
          }
        }
        const uint32_t alpha = static_cast<uint32_t>(
            std::lround(255.0f * covered / (kSamples * kSamples)));
        pixels[static_cast<size_t>(y) * kSize + x] =
            (alpha << 24u) | 0x00ffffffu;
      }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = kSize * sizeof(uint32_t);
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(result) || !texture) {
      ReleaseAndNull(texture);
      return false;
    }
    result = device->CreateShaderResourceView(texture, nullptr, &lock_mask);
    ReleaseAndNull(texture);
    return SUCCEEDED(result) && lock_mask;
  }

  bool CreateReticleMask() {
    if (!device) return false;
    if (reticle_mask) return true;
    constexpr uint32_t kSize = 64;
    constexpr int kSamples = 4;
    std::vector<uint32_t> pixels(kSize * kSize, 0u);
    for (uint32_t y = 0; y < kSize; ++y) {
      for (uint32_t x = 0; x < kSize; ++x) {
        int covered = 0;
        for (int sy = 0; sy < kSamples; ++sy) {
          for (int sx = 0; sx < kSamples; ++sx) {
            const float px = (static_cast<float>(x) +
                              (sx + 0.5f) / kSamples) / kSize;
            const float py = (static_cast<float>(y) +
                              (sy + 0.5f) / kSamples) / kSize;
            const float dx = std::fabs(px - 0.5f);
            const float dy = std::fabs(py - 0.5f);
            // Four short, square-ended rails with a clear aiming gap and a
            // subtle centre point. This matches DXHR's restrained HUD weight
            // without reintroducing any of the hidden status furniture.
            const bool horizontal = dy <= 0.020f &&
                dx >= 0.125f && dx <= 0.385f;
            const bool vertical = dx <= 0.020f &&
                dy >= 0.125f && dy <= 0.385f;
            const bool centre = dx * dx + dy * dy <= 0.018f * 0.018f;
            if (horizontal || vertical || centre) ++covered;
          }
        }
        const uint32_t alpha = static_cast<uint32_t>(
            std::lround(255.0f * covered / (kSamples * kSamples)));
        pixels[static_cast<size_t>(y) * kSize + x] =
            (alpha << 24u) | 0x00ffffffu;
      }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = kSize * sizeof(uint32_t);
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(result) || !texture) {
      ReleaseAndNull(texture);
      return false;
    }
    result = device->CreateShaderResourceView(texture, nullptr, &reticle_mask);
    ReleaseAndNull(texture);
    return SUCCEEDED(result) && reticle_mask;
  }

  bool CreatePipeline() {
    compiler_module = LoadCompiler();
    if (!compiler_module) {
      DXS_ERR("native UI renderer: no supported d3dcompiler DLL");
      return false;
    }
    auto compile = reinterpret_cast<D3DCompileFn>(
        GetProcAddress(compiler_module, "D3DCompile"));
    ID3DBlob* vertex_code = nullptr;
    ID3DBlob* pixel_code = nullptr;
    if (!CompileShader(compile, kVertexShaderSource, "vs_4_0", &vertex_code) ||
        !CompileShader(compile, kPixelShaderSource, "ps_4_0", &pixel_code)) {
      ReleaseAndNull(vertex_code);
      ReleaseAndNull(pixel_code);
      return false;
    }

    HRESULT result = device->CreateVertexShader(vertex_code->GetBufferPointer(),
                                                 vertex_code->GetBufferSize(),
                                                 nullptr, &vertex_shader);
    if (SUCCEEDED(result)) {
      result = device->CreatePixelShader(pixel_code->GetBufferPointer(),
                                         pixel_code->GetBufferSize(),
                                         nullptr, &pixel_shader);
    }
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (SUCCEEDED(result)) {
      result = device->CreateInputLayout(elements, 3,
                                         vertex_code->GetBufferPointer(),
                                         vertex_code->GetBufferSize(),
                                         &input_layout);
    }
    ReleaseAndNull(vertex_code);
    ReleaseAndNull(pixel_code);
    if (FAILED(result)) {
      DXS_ERR("native UI renderer: shader pipeline creation failed hr=%08lx",
              static_cast<unsigned long>(result));
      return false;
    }

    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    result = device->CreateSamplerState(&sampler_desc, &sampler);

    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result)) result = device->CreateBlendState(&blend_desc, &blend);

    D3D11_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    if (SUCCEEDED(result)) {
      result = device->CreateRasterizerState(&raster_desc, &rasterizer);
    }

    D3D11_DEPTH_STENCIL_DESC depth_desc = {};
    depth_desc.DepthEnable = FALSE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth_desc.StencilEnable = FALSE;
    if (SUCCEEDED(result)) {
      result = device->CreateDepthStencilState(&depth_desc, &depth);
    }
    if (FAILED(result)) {
      DXS_ERR("native UI renderer: fixed state creation failed hr=%08lx",
              static_cast<unsigned long>(result));
      return false;
    }
    return true;
  }

  bool EnsureVertexBuffer(size_t vertices) {
    if (vertex_buffer && vertex_capacity >= vertices) return true;
    ReleaseAndNull(vertex_buffer);
    size_t capacity = 96;
    while (capacity < vertices) capacity *= 2;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(capacity * sizeof(Vertex));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT result = device->CreateBuffer(&desc, nullptr, &vertex_buffer);
    if (FAILED(result)) {
      DXS_WARN("native UI renderer: vertex buffer creation failed hr=%08lx",
               static_cast<unsigned long>(result));
      return false;
    }
    vertex_capacity = capacity;
    return true;
  }

  CachedTexture* ResolveArchivedTexture(uint32_t texture_id) {
    GameArchiveTexture archived;
    if (!TheGameTextureArchive().AcquireTexture(device, texture_id,
                                                 &archived) ||
        !archived.view || !ValidDimension(archived.width) ||
        !ValidDimension(archived.height)) {
      ReleaseAndNull(archived.view);
      return nullptr;
    }
    CachedTexture entry;
    // AcquireTexture returns one caller-owned reference. Transfer it directly
    // into the renderer cache; no extra AddRef is needed here.
    entry.view = archived.view;
    entry.width = archived.width;
    entry.height = archived.height;
    const auto inserted = textures.emplace(texture_id, entry);
    if (!inserted.second) ReleaseAndNull(archived.view);
    texture_retry_after.erase(texture_id);
    texture_failure_reason.erase(texture_id);
    DXS_INFO("native UI texture %08x ready from game archive (%ux%u)",
             texture_id, inserted.first->second.width,
             inserted.first->second.height);
    return &inserted.first->second;
  }

  CachedTexture* ResolveTexture(uint32_t texture_id) {
    if (!texture_id) return nullptr;
    auto cached = textures.find(texture_id);
    if (cached != textures.end()) return &cached->second;
    const uint64_t now = GetTickCount64();
    const auto delayed = texture_retry_after.find(texture_id);
    if (delayed != texture_retry_after.end() && now < delayed->second) {
      return nullptr;
    }

    // Most pickup/augmentation art is streamed only while the corresponding
    // vanilla menu is open. Resolve it from DXHR's own BIGFILE data first so
    // the lower UI neither opens that menu nor depends on a transient runtime
    // hash-table membership. Live-only resources continue into the engine path
    // below.
    if (CachedTexture* archived = ResolveArchivedTexture(texture_id)) {
      return archived;
    }
    auto miss = [&](int reason, const char* stage) -> CachedTexture* {
      // Textures can become resident after streaming or a level transition,
      // so misses are temporary. Retrying every rendered frame caused a hash
      // walk per empty card and collapsed frame rate; one retry per second is
      // responsive without turning absence into a hot loop.
      texture_retry_after[texture_id] = now + 1000;
      const auto old_reason = texture_failure_reason.find(texture_id);
      if (old_reason == texture_failure_reason.end() ||
          old_reason->second != reason) {
        texture_failure_reason[texture_id] = reason;
        DXS_WARN("native UI texture %08x unavailable at %s",
                 texture_id, stage ? stage : "unknown stage");
      }
      return nullptr;
    };

    // Mirror the retail renderer exactly: membership is checked through +0x44
    // before +0x30, whose implementation assumes the hash entry exists.
    void* section = nullptr;
    void* const* section_global = reinterpret_cast<void* const*>(
        Rebase(kTextureResourceSectionGlobalVa));
    if (!Read(section_global, &section) || !section ||
        !Readable(section, sizeof(void*))) {
      return miss(1, "RenderResource section");
    }
    void** section_vtable = nullptr;
    if (!Read(section, &section_vtable) ||
        section_vtable != reinterpret_cast<void**>(
            Rebase(kTextureResourceHandlerVtableVa)) ||
        !Readable(section_vtable + kResolveResourceIdVtableSlot,
                  sizeof(void*))) {
      return miss(2, "RenderResource section vtable");
    }
    auto get_base = reinterpret_cast<GetBasePointerFn>(
        section_vtable[kGetBasePointerVtableSlot]);
    auto find_resource = reinterpret_cast<ResolveResourceIdFn>(
        section_vtable[kResolveResourceIdVtableSlot]);
    if (!Executable(reinterpret_cast<void*>(get_base), 1) ||
        !Executable(reinterpret_cast<void*>(find_resource), 1)) {
      return miss(3, "RenderResource section methods");
    }
    const uint32_t resource_id = find_resource(section, texture_id);
    if (resource_id == 0xFFFFFFFFu) {
      return miss(4, "RenderResource ID lookup");
    }
    void* resource = get_base(section, resource_id);
    if (!resource) return miss(5, "RenderResource object lookup");

    uint8_t* base_texture = static_cast<uint8_t*>(resource) +
                            kBaseTextureSubobjectOffset;
    if (!Readable(base_texture, kBaseTextureHeightOffset + sizeof(uint32_t))) {
      return miss(6, "PCDX11Texture object range");
    }
    void** texture_vtable = nullptr;
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    if (!Read(base_texture, &texture_vtable) ||
        !Readable(texture_vtable, sizeof(void*) * 3) ||
        texture_vtable[0] !=
            reinterpret_cast<void*>(Rebase(kTextureGetResourceVa)) ||
        texture_vtable[1] != reinterpret_cast<void*>(Rebase(kTextureCreateViewVa)) ||
        texture_vtable[2] !=
            reinterpret_cast<void*>(Rebase(kTextureCreateAccessViewVa)) ||
        !Read(base_texture + kBaseTextureWidthOffset, &texture_width) ||
        !Read(base_texture + kBaseTextureHeightOffset, &texture_height) ||
        !ValidDimension(texture_width) || !ValidDimension(texture_height)) {
      return miss(7, "PCDX11Texture validation");
    }

    // Never make the game's texture object stream/finalize work from the
    // auxiliary Present path. The archive path above owns normal inventory and
    // augmentation art; this live fallback is intentionally passive and uses
    // only a view/resource that the retail renderer has already made resident.
    // Calling the +0/+4 virtuals here can wait on the streamer and mutate its
    // state during a save transition, which is both a frame spike and a crash
    // surface under Wine/DXVK.
    ID3D11ShaderResourceView* view = nullptr;
    bool owns_view = false;
    Read(base_texture + kBaseTextureViewOffset, &view);
    if (view && LooksLikeComObject(view)) {
      // Stabilize the borrowed pointer before using any further COM methods.
      view->AddRef();
      owns_view = true;
    } else {
      view = nullptr;
      ID3D11Resource* resident_resource = nullptr;
      if (!Read(base_texture + kBaseTextureResourceOffset,
                &resident_resource) ||
          !resident_resource || !LooksLikeComObject(resident_resource)) {
        return miss(8, "resident texture resource");
      }
      // The PCDX11Texture field is borrowed. Hold the resource across GetDevice
      // and CreateShaderResourceView so an asynchronous eviction cannot free it
      // between validation and use.
      resident_resource->AddRef();
      ID3D11Device* resident_device = nullptr;
      resident_resource->GetDevice(&resident_device);
      const bool resident_on_panel_device = resident_device == device;
      ReleaseAndNull(resident_device);
      const HRESULT create_result = resident_on_panel_device
          ? device->CreateShaderResourceView(resident_resource, nullptr, &view)
          : E_FAIL;
      resident_resource->Release();
      if (!resident_on_panel_device || FAILED(create_result) || !view ||
          !LooksLikeComObject(view)) {
        ReleaseAndNull(view);
        return miss(9, "D3D texture view");
      }
      owns_view = true;
    }
    auto view_miss = [&](int reason, const char* stage) -> CachedTexture* {
      if (owns_view) ReleaseAndNull(view);
      return miss(reason, stage);
    };

    // Validate the finalized resource through the standard D3D view API. This
    // also avoids depending on the engine's borrowed pre-finalization pointer.
    ID3D11Resource* d3d_resource = nullptr;
    view->GetResource(&d3d_resource);
    if (!d3d_resource) return view_miss(10, "view resource validation");
    D3D11_RESOURCE_DIMENSION resource_dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    d3d_resource->GetType(&resource_dimension);
    ReleaseAndNull(d3d_resource);
    if (resource_dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
      return view_miss(11, "view resource dimension");
    }

    ID3D11Device* resource_device = nullptr;
    view->GetDevice(&resource_device);
    const bool same_device = resource_device == device;
    ReleaseAndNull(resource_device);
    if (!same_device) return view_miss(12, "view device validation");

    CachedTexture entry;
    entry.view = view;
    entry.width = texture_width;
    entry.height = texture_height;
    auto inserted = textures.emplace(texture_id, entry);
    texture_retry_after.erase(texture_id);
    texture_failure_reason.erase(texture_id);
    DXS_INFO("native UI texture %08x ready (%ux%u)", texture_id,
             texture_width, texture_height);
    return &inserted.first->second;
  }
};

namespace {
NativeUiRenderer g_native_ui_renderer;
}

NativeUiRenderer::NativeUiRenderer() = default;

NativeUiRenderer::~NativeUiRenderer() {
  // The proxy is pinned for the process lifetime. During normal explicit
  // unload ProbeShutdown calls Shutdown while D3D is alive; during process
  // termination, however, global destructors can run after d3d11/dxgi have
  // already torn down. Calling COM Release there caused an access violation in
  // the depth-state vtable. The address space is disappearing, so retaining
  // these references at that point is both safe and intentional.
}

bool NativeUiRenderer::Initialize(ID3D11Device* device,
                                  ID3D11DeviceContext* context) {
  if (!device || !context) return false;
  if (impl_ && impl_->device == device && impl_->context == context &&
      impl_->vertex_shader && impl_->pixel_shader) {
    return true;
  }
  Shutdown();
  impl_ = new (std::nothrow) Impl();
  if (!impl_) return false;
  impl_->device = device;
  impl_->context = context;
  device->AddRef();
  context->AddRef();
  if (!impl_->CreatePipeline()) {
    Shutdown();
    return false;
  }
  const bool archive_ready = TheGameTextureArchive().Initialize();
  DXS_INFO("native UI renderer ready; game RenderResource textures enabled; "
           "archive fallback=%s (%u indexed)",
           archive_ready ? "ready" : "unavailable",
           TheGameTextureArchive().indexed_texture_count());
  return true;
}

void NativeUiRenderer::Shutdown() {
  if (!impl_) return;
  impl_->Shutdown();
  delete impl_;
  impl_ = nullptr;
}

bool NativeUiRenderer::Begin(ID3D11RenderTargetView* target,
                             float width, float height) {
  if (!impl_ || !target || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0.0f || height <= 0.0f) {
    return false;
  }
  impl_->ReleasePending();
  impl_->target = target;
  target->AddRef();
  impl_->width = width;
  impl_->height = height;
  impl_->rendered_last_frame = 0;
  impl_->frame_active = true;
  return true;
}

bool NativeUiRenderer::DrawPickup(uint32_t pickup_dtp_id, bool transposed,
                                  const NativeUiRect& destination,
                                  const NativeUiColor& color,
                                  NativeTextureInfo* info) {
  if (!impl_ || !impl_->frame_active || !pickup_dtp_id) return false;
  auto get_dtp = reinterpret_cast<GetDtpDataFn>(Rebase(kGetDtpDataVa));
  static const uint8_t expected[] = {0x8B, 0x44, 0x24, 0x04,
                                     0x3D, 0x00, 0x80, 0x01, 0x00};
  if (!Readable(reinterpret_cast<void*>(get_dtp), sizeof(expected)) ||
      !Executable(reinterpret_cast<void*>(get_dtp), sizeof(expected)) ||
      std::memcmp(reinterpret_cast<void*>(get_dtp), expected, sizeof(expected)) != 0) {
    return false;
  }
  const uint8_t* pickup = static_cast<const uint8_t*>(get_dtp(pickup_dtp_id));
  if (!pickup || !Readable(pickup, 0x34)) return false;
  uint32_t texture_id = 0;
  // This mirrors InventorySlot in the original engine: the transposed grid
  // orientation uses Pickup::texture8, normal orientation uses texture10.
  if (!Read(pickup + (transposed ? 0x08 : 0x10), &texture_id) || !texture_id) {
    return false;
  }
  CachedTexture* texture = impl_->ResolveTexture(texture_id);
  if (!texture || !texture->view) return false;
  if (info) {
    info->width = texture->width;
    info->height = texture->height;
    info->texture_id = texture_id;
  }

  // Rifles, ammo boxes and consumables have very different aspect ratios.
  // Scaleform aspect-fits pickup art; stretching it to every card does not.
  NativeUiRect fitted = destination;
  const float available_width = destination.right - destination.left;
  const float available_height = destination.bottom - destination.top;
  if (available_width > 0.0f && available_height > 0.0f &&
      texture->width > 0 && texture->height > 0) {
    const float scale = (std::min)(available_width / texture->width,
                                   available_height / texture->height);
    const float fitted_width = texture->width * scale;
    const float fitted_height = texture->height * scale;
    fitted.left += (available_width - fitted_width) * 0.5f;
    fitted.right = fitted.left + fitted_width;
    fitted.top += (available_height - fitted_height) * 0.5f;
    fitted.bottom = fitted.top + fitted_height;
  }
  return DrawView(texture->view, fitted, color, NativeUiUvRect());
}

bool NativeUiRenderer::DrawTexture(uint32_t texture_id,
                                   const NativeUiRect& destination,
                                   const NativeUiColor& color,
                                   const NativeUiUvRect& uv,
                                   NativeTextureInfo* info) {
  if (!impl_ || !impl_->frame_active) return false;
  CachedTexture* texture = impl_->ResolveTexture(texture_id);
  if (!texture || !texture->view) return false;
  if (info) {
    info->width = texture->width;
    info->height = texture->height;
    info->texture_id = texture_id;
  }
  return DrawView(texture->view, destination, color, uv);
}

bool NativeUiRenderer::DrawView(ID3D11ShaderResourceView* view,
                                const NativeUiRect& destination,
                                const NativeUiColor& color,
                                const NativeUiUvRect& uv,
                                float clockwise_degrees) {
  if (!impl_ || !impl_->frame_active || !view ||
      impl_->pending.size() >= kMaximumQueuedQuads ||
      !std::isfinite(destination.left) || !std::isfinite(destination.top) ||
      !std::isfinite(destination.right) || !std::isfinite(destination.bottom) ||
      !std::isfinite(uv.left) || !std::isfinite(uv.top) ||
      !std::isfinite(uv.right) || !std::isfinite(uv.bottom) ||
      !std::isfinite(color.red) || !std::isfinite(color.green) ||
      !std::isfinite(color.blue) || !std::isfinite(color.alpha) ||
      !std::isfinite(clockwise_degrees) ||
      destination.right <= destination.left || destination.bottom <= destination.top ||
      destination.right <= 0.0f || destination.bottom <= 0.0f ||
      destination.left >= impl_->width || destination.top >= impl_->height) {
    return false;
  }
  PendingQuad quad;
  quad.view = view;
  quad.destination = destination;
  quad.uv = uv;
  quad.color = color;
  quad.clockwise_degrees = clockwise_degrees;
  view->AddRef();
  impl_->pending.push_back(quad);
  return true;
}

bool NativeUiRenderer::DrawCircle(const NativeUiRect& destination,
                                  const NativeUiColor& color) {
  if (!impl_ || !impl_->frame_active ||
      !impl_->CreateMarkerMask(false, &impl_->circle_mask)) {
    return false;
  }
  return DrawView(impl_->circle_mask, destination, color);
}

bool NativeUiRenderer::DrawDiamond(const NativeUiRect& destination,
                                   const NativeUiColor& color) {
  if (!impl_ || !impl_->frame_active ||
      !impl_->CreateMarkerMask(true, &impl_->diamond_mask)) {
    return false;
  }
  return DrawView(impl_->diamond_mask, destination, color);
}

bool NativeUiRenderer::DrawPlayerArrow(const NativeUiRect& destination,
                                       float clockwise_degrees,
                                       const NativeUiColor& color) {
  if (!impl_ || !impl_->frame_active || !impl_->CreatePlayerArrowMask()) {
    return false;
  }
  return DrawView(impl_->player_arrow_mask, destination, color,
                  NativeUiUvRect(), clockwise_degrees);
}

bool NativeUiRenderer::DrawLock(const NativeUiRect& destination,
                                const NativeUiColor& color) {
  if (!impl_ || !impl_->frame_active || !impl_->CreateLockMask()) return false;
  return DrawView(impl_->lock_mask, destination, color);
}

bool NativeUiRenderer::DrawReticle(const NativeUiRect& destination,
                                   const NativeUiColor& color) {
  if (!impl_ || !impl_->frame_active || !impl_->CreateReticleMask()) return false;
  return DrawView(impl_->reticle_mask, destination, color);
}

bool NativeUiRenderer::End() {
  if (!impl_ || !impl_->frame_active) return false;
  if (impl_->pending.empty()) {
    impl_->ReleasePending();
    return true;
  }
  const size_t vertex_count = impl_->pending.size() * 6;
  if (!impl_->EnsureVertexBuffer(vertex_count)) {
    impl_->ReleasePending();
    return false;
  }

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  HRESULT result = impl_->context->Map(impl_->vertex_buffer, 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(result) || !mapped.pData) {
    impl_->ReleasePending();
    return false;
  }
  auto* vertices = static_cast<Vertex*>(mapped.pData);
  for (size_t i = 0; i < impl_->pending.size(); ++i) {
    WriteQuad(vertices + i * 6, impl_->pending[i], impl_->width, impl_->height);
  }
  impl_->context->Unmap(impl_->vertex_buffer, 0);

  SavedD3DState saved;
  saved.Capture(impl_->context);

  const UINT stride = sizeof(Vertex);
  const UINT offset = 0;
  impl_->context->IASetInputLayout(impl_->input_layout);
  impl_->context->IASetVertexBuffers(0, 1, &impl_->vertex_buffer, &stride, &offset);
  impl_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  impl_->context->VSSetShader(impl_->vertex_shader, nullptr, 0);
  impl_->context->HSSetShader(nullptr, nullptr, 0);
  impl_->context->DSSetShader(nullptr, nullptr, 0);
  impl_->context->GSSetShader(nullptr, nullptr, 0);
  impl_->context->PSSetShader(impl_->pixel_shader, nullptr, 0);
  impl_->context->PSSetSamplers(0, 1, &impl_->sampler);
  impl_->context->RSSetState(impl_->rasterizer);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = impl_->width;
  viewport.Height = impl_->height;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  impl_->context->RSSetViewports(1, &viewport);
  impl_->context->OMSetRenderTargets(1, &impl_->target, nullptr);
  const FLOAT blend_factor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  impl_->context->OMSetBlendState(impl_->blend, blend_factor, 0xffffffffu);
  impl_->context->OMSetDepthStencilState(impl_->depth, 0);

  for (size_t i = 0; i < impl_->pending.size(); ++i) {
    ID3D11ShaderResourceView* view = impl_->pending[i].view;
    impl_->context->PSSetShaderResources(0, 1, &view);
    impl_->context->Draw(6, static_cast<UINT>(i * 6));
  }
  ID3D11ShaderResourceView* no_view = nullptr;
  impl_->context->PSSetShaderResources(0, 1, &no_view);
  saved.Restore(impl_->context);

  impl_->rendered_last_frame = static_cast<uint32_t>(impl_->pending.size());
  impl_->ReleasePending();
  return true;
}

uint32_t NativeUiRenderer::queued() const {
  return impl_ ? static_cast<uint32_t>(impl_->pending.size()) : 0;
}

uint32_t NativeUiRenderer::rendered_last_frame() const {
  return impl_ ? impl_->rendered_last_frame : 0;
}

NativeUiRenderer& TheNativeUiRenderer() {
  return g_native_ui_renderer;
}

}  // namespace dxs
