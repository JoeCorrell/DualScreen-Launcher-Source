// Exercises the production shader, camera, rasterizer, buffers and readback on
// D3D11 WARP. No Skyrim process, game hooks or visible window is involved.
#include "../src/CustomPlayerPreview.cpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace dis::player_preview;

std::vector<uint64_t> submitted;
void __stdcall Capture(uint32_t, uint32_t, const uint8_t*, uint32_t,
                       uint64_t revision) { submitted.push_back(revision); }

void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

std::vector<uint8_t> ReadFrame(const dis::CharacterPreviewSurface& surface) {
  Check(RenderFrame(surface, GetTickCount64()), "RenderFrame failed");
  auto& slot = g_gpu.readbacks[(g_gpu.next_readback + g_gpu.readbacks.size() - 1) % g_gpu.readbacks.size()];
  g_gpu.immediate->Flush();
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Check(SUCCEEDED(g_gpu.immediate->Map(slot.staging, 0, D3D11_MAP_READ, 0,
                                       &mapped)), "readback failed");
  std::vector<uint8_t> pixels(kRenderWidth * kRenderHeight * 4);
  for (uint32_t y = 0; y < kRenderHeight; ++y)
    std::memcpy(pixels.data() + y * kRenderWidth * 4,
                static_cast<uint8_t*>(mapped.pData) + y * mapped.RowPitch,
                kRenderWidth * 4);
  g_gpu.immediate->Unmap(slot.staging, 0);
  slot.pending = false;
  return pixels;
}

void SaveFrame(const std::filesystem::path& path,
               const std::vector<uint8_t>& pixels) {
  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  file.bfType = 0x4d42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
  info.biSize = sizeof(info);
  info.biWidth = kRenderWidth;
  info.biHeight = -static_cast<LONG>(kRenderHeight);
  info.biPlanes = 1;
  info.biBitCount = 32;
  std::vector<uint8_t> bgra = pixels;
  for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<char*>(&file), sizeof(file));
  out.write(reinterpret_cast<char*>(&info), sizeof(info));
  out.write(reinterpret_cast<char*>(bgra.data()), bgra.size());
}

CpuScene FacingFixture() {
  CpuScene scene;
  scene.world_epoch = scene.revision = 1;
  scene.character_min_x = scene.character_min_z = -1;
  scene.character_max_x = scene.character_max_z = 1;
  scene.character_min_y = -0.2f;
  scene.character_max_y = 0.2f;
  // CCW exterior winding in asset space, normal toward camera (-Y).
  // The rear is a different colour so a culling inversion is unambiguous.
  for (int rear = 0; rear < 2; ++rear) {
    CpuMesh mesh;
    const float y = rear ? 0.1f : -0.1f;
    mesh.vertices = {{-1,y,-1, 0,-1,0, 1,0,0, 0,0,0},
                     { 1,y,-1, 0,-1,0, 1,0,0, 0,0,0},
                     { 0,y, 1, 0,-1,0, 1,0,0, 0,0,0}};
    mesh.indices = rear ? std::vector<uint32_t>{2,1,0}
                        : std::vector<uint32_t>{0,1,2};
    mesh.tint[0] = rear ? 0 : 1;
    mesh.tint[1] = rear ? 1 : 0;
    mesh.tint[2] = 0;
    scene.meshes.push_back(std::move(mesh));
  }
  return scene;
}

// Optional supplied game NIFs are read locally, never checked into the tests.
CpuScene NifFixture(int argc, char** argv) {
  CpuScene scene;
  scene.world_epoch = 1;
  scene.revision = 2;
  bool first = true;
  for (int a = 2; a < argc; ++a) {
    nifly::NifFile nif{std::filesystem::path(argv[a])};
    Check(nif.IsValid(), "invalid supplied NIF");
    for (auto* shape : nif.GetShapes()) {
      const auto* positions = nif.GetVertsForShape(shape);
      const auto* normals = nif.GetNormalsForShape(shape);
      if (!positions || positions->empty()) continue;
      CpuMesh mesh;
      mesh.kind = ComponentKind::kAccessory;
      mesh.tint[0] = mesh.tint[1] = mesh.tint[2] = 0.25f;
      size_t outward = 0, inward = 0;
      std::vector<nifly::Triangle> triangles;
      shape->GetTriangles(triangles);
      std::vector<nifly::Vector3> geometric(positions->size(), nifly::Vector3(0,0,0));
      for (auto& t : triangles) {
        Check(t.p1 < positions->size() && t.p2 < positions->size() &&
              t.p3 < positions->size(), "invalid triangle");
        const auto e1 = (*positions)[t.p2] - (*positions)[t.p1];
        const auto e2 = (*positions)[t.p3] - (*positions)[t.p1];
        const nifly::Vector3 face(e1.y*e2.z-e1.z*e2.y,
                                  e1.z*e2.x-e1.x*e2.z,
                                  e1.x*e2.y-e1.y*e2.x);
        for (auto v : {t.p1,t.p2,t.p3}) geometric[v] += face;
        if (normals && normals->size() == positions->size()) {
          const auto n = (*normals)[t.p1] + (*normals)[t.p2] + (*normals)[t.p3];
          const float d = (e1.y*e2.z-e1.z*e2.y)*n.x +
                          (e1.z*e2.x-e1.x*e2.z)*n.y +
                          (e1.x*e2.y-e1.y*e2.x)*n.z;
          outward += d > 0.00001f;
          inward += d < -0.00001f;
        }
        mesh.indices.insert(mesh.indices.end(), {t.p1,t.p2,t.p3});
      }
      std::printf("NIF %s %s triangles outward=%zu inward=%zu\n", argv[a],
                  shape->name.get().c_str(), outward, inward);
      if (outward || inward)
        Check(outward > inward, "supplied NIF does not have outward CCW winding");
      for (size_t i = 0; i < positions->size(); ++i) {
        const auto p = (*positions)[i];
        geometric[i].Normalize();
        const auto n = normals && i < normals->size() ? (*normals)[i] : geometric[i];
        mesh.vertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,1,0,0,0,0,0});
        if (first) {
          scene.character_min_x = scene.character_max_x = p.x;
          scene.character_min_y = scene.character_max_y = p.y;
          scene.character_min_z = scene.character_max_z = p.z;
          first = false;
        }
        scene.character_min_x = std::min(scene.character_min_x,p.x);
        scene.character_min_y = std::min(scene.character_min_y,p.y);
        scene.character_min_z = std::min(scene.character_min_z,p.z);
        scene.character_max_x = std::max(scene.character_max_x,p.x);
        scene.character_max_y = std::max(scene.character_max_y,p.y);
        scene.character_max_z = std::max(scene.character_max_z,p.z);
      }
      scene.meshes.push_back(std::move(mesh));
    }
  }
  return scene;
}

void SkinningTest() {
  Skeleton skeleton;
  nifly::MatTransform bone;
  bone.Clear();
  bone.rotation = nifly::Matrix3(0,-1,0, 1,0,0, 0,0,1);
  bone.translation = nifly::Vector3(7,9,13);
  skeleton.global_bones["Head"] = bone;
  RawMesh mesh;
  mesh.skinned = true;
  mesh.bones = {"Head"};
  mesh.skin_to_bone = {bone.InverseTransform()};
  RawVertex v;
  v.position = nifly::Vector3(2,3,4);
  v.normal = nifly::Vector3(0,-1,0);
  v.tangent = nifly::Vector3(1,0,0);
  v.influences[0] = {0,0.5f};
  nifly::Vector3 p,n,t;
  SkinVertex(mesh,&skeleton,v,p,n,t);
  Check(std::abs(p.x-2)<0.0001f && std::abs(p.y-3)<0.0001f &&
        std::abs(p.z-4)<0.0001f && std::abs(n.y+1)<0.0001f &&
        std::abs(t.x-1)<0.0001f, "bind inverse/order/weight normalization");
  std::puts("non-identity skinning/bind pose: PASS");
}

void ReadbackTests(const dis::CharacterPreviewSurface& surface) {
  g_submit = &Capture;
  g_requested_reset_epoch.store(g_gpu.world_epoch);
  // Start near the end of the three-slot ring: physical slot order then
  // differs from frame order, as it does in ordinary multi-frame GPU latency.
  g_gpu.next_readback = 2;
  for (int i = 0; i < 3; ++i) Check(RenderFrame(surface,GetTickCount64()), "ring submit");
  const uint64_t latest = g_gpu.frame_revision;
  g_gpu.immediate->Flush();
  const uint64_t start = GetTickCount64();
  while (HasPendingWork() && GetTickCount64()-start < 5000) {
    RetireReadbacks(); Sleep(1);
  }
  Check(!submitted.empty() && submitted.back() == latest, "newest readback lost");
  for (size_t i=1;i<submitted.size();++i)
    Check(submitted[i] > submitted[i-1], "readback moves backwards after ring wrap");
  submitted.clear();
  Check(RenderFrame(surface,GetTickCount64()), "old world frame submit");
  g_requested_reset_epoch.store(g_gpu.world_epoch+1);
  g_gpu.immediate->Flush();
  const uint64_t reset_start = GetTickCount64();
  while (HasPendingWork() && GetTickCount64()-reset_start < 5000) {
    RetireReadbacks(); Sleep(1);
  }
  Check(submitted.empty(), "old world frame published after reset");
  g_submit = nullptr;
  std::puts("readback ordering and old-world rejection: PASS");
}

void TextureTests() {
  TextureBlob blob;
  blob.bytes.resize(4+sizeof(DdsHeader)+4);
  std::memcpy(blob.bytes.data(),"DDS ",4);
  auto* h = reinterpret_cast<DdsHeader*>(blob.bytes.data()+4);
  h->size=124; h->pixel_format.size=32;
  h->width=h->height=1;
  h->pixel_format.rgb_bits=32;
  h->pixel_format.r_mask=0x000000ff;
  h->pixel_format.g_mask=0x0000ff00;
  h->pixel_format.b_mask=0x00ff0000;
  auto* valid = CreateDdsTexture(g_gpu.device,blob,false);
  Check(valid != nullptr,"valid 1x1 DDS rejected"); valid->Release();
  h->mip_count=0xffffffff;
  Check(!CreateDdsTexture(g_gpu.device,blob,false),"unbounded DDS mip count");
  h->mip_count=1; h->width=0xffffffff;
  Check(!CreateDdsTexture(g_gpu.device,blob,false),"unbounded DDS dimensions");
  h->width=1;
  blob.bytes.resize(4+sizeof(DdsHeader)+sizeof(DdsHeaderDx10)+4);
  h = reinterpret_cast<DdsHeader*>(blob.bytes.data()+4);
  h->pixel_format.four_cc=FourCc('D','X','1','0');
  auto* dx = reinterpret_cast<DdsHeaderDx10*>(blob.bytes.data()+4+sizeof(DdsHeader));
  dx->dimension=3; dx->array_size=1;
  dx->format=DXGI_FORMAT_R32G32B32A32_FLOAT;
  Check(!CreateDdsTexture(g_gpu.device,blob,false),"DDS format/stride mismatch");
  auto scene = FacingFixture();
  scene.meshes[0].model_space_normal = true;
  Check(UploadScene(scene),"normal fallback upload");
  Check(g_gpu.meshes[0].flags[0] == 0,"flat fallback treated as model normal");
  std::puts("DDS bounds/layout and missing normal-map fallback: PASS");
}

int main(int argc, char** argv) {
  try {
    Check(argc >= 2, "usage: PreviewRenderTests output-directory [head.nif ...]");
    std::filesystem::create_directories(argv[1]);
    HWND window = CreateWindowExW(0, L"STATIC", L"Skyrim renderer test", WS_POPUP,
                                   0,0,400,640,nullptr,nullptr,nullptr,nullptr);
    Check(window != nullptr, "hidden test window failed");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 400;
    desc.BufferDesc.Height = 640;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,
        nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)),
        "WARP creation failed");
    Check(CreateGpuState(chain), "production GPU initialization failed");
    SkinningTest();
    dis::CharacterPreviewSurface surface{};
    surface.zoom = 1;
    surface.rotation_degrees = -kNeutralFacingYawDegrees;
    Check(UploadScene(FacingFixture()), "fixture upload failed");
    auto pixels = ReadFrame(surface);
    SaveFrame(std::filesystem::path(argv[1]) / "facing.bmp", pixels);
    const size_t center = (kRenderHeight/2*kRenderWidth + kRenderWidth/2)*4;
    const bool correct_front = pixels[center] > 100 && pixels[center+1] < 20;
    std::printf("front-facing triangle: %s (center RGBA %u %u %u %u)\n",
        correct_front ? "PASS" : "FAIL", pixels[center],pixels[center+1],
        pixels[center+2],pixels[center+3]);
    surface.rotation_degrees += 180;
    auto back = ReadFrame(surface);
    const bool correct_back = back[center+1] > 100 && back[center] < 20;
    std::printf("180-degree rear-facing triangle: %s\n", correct_back ? "PASS" : "FAIL");
    surface.rotation_degrees -= 180;
    if (argc > 2) {
      Check(UploadScene(NifFixture(argc,argv)), "NIF upload failed");
      surface.rotation_degrees = 0; // The real panel's default, not a test camera.
      SaveFrame(std::filesystem::path(argv[1]) / "head.bmp", ReadFrame(surface));
    }
    const Matrix4 default_camera = PreviewMvp(0,1);
    const bool correct_default = default_camera.m[6] < 0 && default_camera.m[0] < 0;
    std::printf("default portrait sees +Y asset front without mirroring: %s\n",
                 correct_default ? "PASS" : "FAIL");
    if (correct_front && correct_back) {
      ReadbackTests(surface);
      auto stale = std::make_shared<CpuScene>(FacingFixture());
      stale->revision = 999;
      g_ready_scene = stale;
      surface.visible = 1; surface.width = 400; surface.height = 640;
      RenderPresent(chain,surface);
      Check(g_gpu.scene_revision != 999 && g_gpu.meshes.empty(), "stale scene resurrected after reset");
      g_ready_scene.reset();
      std::puts("stale scene rejection: PASS");
      TextureTests();
    }
    ReleaseGpu();
    context->Release(); device->Release(); chain->Release(); DestroyWindow(window);
    Check(correct_front && correct_back, "exterior culled: rear surface is visible instead");
    Check(correct_default, "default portrait is looking at the back of the head");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
  }
}
