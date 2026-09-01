#include "CustomPlayerPreview.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "NifFile.hpp"

namespace dis::player_preview {
namespace {

constexpr uintptr_t kResourceStreamCtor = 0x00C76060;
constexpr uintptr_t kResourceStreamDtor = 0x00C76340;
constexpr uint32_t kRenderWidth = 400;
constexpr uint32_t kRenderHeight = 640;
constexpr uint64_t kStaticPerfLogIntervalMs = 10000;
constexpr uint64_t kAnimationFrameMs = 33;
// The assembled Skyrim meshes already face this private camera at zero yaw.
// Retain only the measured asset-space correction. Applying an additional
// 180-degree turn presents the rear/inside of FaceGen head parts: the actual
// face is then back-face culled while separately rendered mouth geometry can
// remain visible through the resulting hole.
constexpr float kNeutralFacingYawDegrees = -7.0f;
constexpr size_t kMaxAssetBytes = 256u * 1024u * 1024u;
constexpr size_t kMaxNifBytes = 64u * 1024u * 1024u;

uintptr_t g_skyrim_base = 0;
SubmitCharacterPreviewFn g_submit = nullptr;
LogLineFn g_log_line = nullptr;
HANDLE g_work_event = nullptr;
HANDLE g_worker = nullptr;
std::atomic<bool> g_running{false};
std::atomic<bool> g_build_pending{false};
std::atomic<uint64_t> g_requested_reset_epoch{0};
SRWLOCK g_manifest_lock = SRWLOCK_INIT;
PlayerManifest g_pending_manifest = {};
uint64_t g_last_published_revision = 0;

void Log(const char* tag, const char* format, ...) {
  if (!g_log_line || !format) return;
  char body[1536] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(body, sizeof(body), format, args);
  va_end(args);
  char line[1700] = {};
  std::snprintf(line, sizeof(line), "%s %s", tag ? tag : "[SKYRIM-PREVIEW]",
                body);
  g_log_line(line);
}

std::string NormalizePath(const char* raw, const char* prefix) {
  if (!raw || !*raw) return {};
  std::string path(raw);
  std::replace(path.begin(), path.end(), '/', '\\');
  while (!path.empty() && (path.front() == '\\' || path.front() == ' '))
    path.erase(path.begin());
  if (path.size() >= 5 && _strnicmp(path.c_str(), "data\\", 5) == 0)
    path.erase(0, 5);
  if (prefix && *prefix &&
      _strnicmp(path.c_str(), prefix, std::strlen(prefix)) != 0)
    path.insert(0, prefix);
  std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return path;
}

bool ReadGameAsset(const std::string& path, std::vector<uint8_t>& output) {
  output.clear();
  if (!g_skyrim_base || path.empty()) return false;
  const uint64_t start = GetTickCount64();
  alignas(void*) uint8_t object[0x38] = {};
  using CtorFn = void*(*)(void*, const char*, bool, void*);
  using DtorFn = void(*)(void*);
  using ReadFn = uint32_t(*)(void*, void*, uint32_t, uint32_t*, uint32_t);
  auto ctor = reinterpret_cast<CtorFn>(g_skyrim_base + kResourceStreamCtor);
  auto dtor = reinterpret_cast<DtorFn>(g_skyrim_base + kResourceStreamDtor);
  ctor(object, path.c_str(), false, nullptr);
  void* stream = *reinterpret_cast<void**>(object + 0x20);
  const uint32_t total = stream
      ? *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(stream) + 8)
      : 0;
  auto read = *reinterpret_cast<ReadFn*>(object + 0x10);
  bool success = false;
  if (stream && read && total && total <= kMaxAssetBytes &&
      (path.size() < 4 || path.substr(path.size() - 4) != ".nif" ||
       total <= kMaxNifBytes)) {
    output.resize(total);
    uint32_t component = 1;
    const uint32_t received = read(object, output.data(), total, &component, 1);
    success = received == total;
    if (!success) output.clear();
  }
  dtor(object);
  Log(success ? "[SKYRIM-PREVIEW-ASSET]" : "[SKYRIM-PREVIEW-ASSET]",
      "%s path=\"%s\" bytes=%zu elapsed_ms=%llu", success ? "resolved" : "failed",
      path.c_str(), output.size(),
      static_cast<unsigned long long>(GetTickCount64() - start));
  return success;
}

struct Influence {
  uint16_t bone = 0;
  float weight = 0.0f;
};

struct RawVertex {
  nifly::Vector3 position;
  nifly::Vector3 normal;
  nifly::Vector3 tangent;
  nifly::Vector2 uv;
  std::array<Influence, 4> influences{};
};

struct RawMesh {
  std::string name;
  std::vector<RawVertex> vertices;
  std::vector<nifly::Triangle> triangles;
  std::vector<std::string> bones;
  std::vector<nifly::MatTransform> skin_to_bone;
  std::vector<int> triangle_partitions;
  std::vector<uint16_t> partition_ids;
  std::array<std::string, 9> textures{};
  nifly::MatTransform shape_to_global;
  bool skinned = false;
  bool skin_tinted = false;
  bool double_sided = false;
  bool emissive = false;
  bool glow_map = false;
  bool specular = false;
  float specular_strength = 0.0f;
  float glossiness = 0.0f;
  float emissive_multiple = 0.0f;
  float environment_scale = 0.0f;
};

struct ParsedModel {
  std::string path;
  // Skyrim weapon NIFs declare their authoritative skeleton attachment point
  // through NiStringExtraData("Prn").  Keeping it with the cached model avoids
  // a per-weapon transform table and preserves replacement/modded NIF data.
  std::string attachment_node;
  std::vector<RawMesh> meshes;
  bool unknown_blocks = false;
};

struct Skeleton {
  std::string path;
  std::unordered_map<std::string, nifly::MatTransform> global_bones;
  // Parent links let preview pose changes propagate to Bethesda attachment
  // nodes (WeaponSword, WeaponBow, Shield, and modded descendants), not just
  // to vertices directly weighted to NPC arm bones.
  std::unordered_map<std::string, std::string> parent_bones;
};

struct TextureBlob {
  std::string path;
  std::vector<uint8_t> bytes;
};

struct CpuVertex {
  float px, py, pz;
  float nx, ny, nz;
  float tx, ty, tz;
  float u, v;
  uint32_t animation_group;
};

struct CpuMesh {
  std::vector<CpuVertex> vertices;
  std::vector<uint32_t> indices;
  std::shared_ptr<TextureBlob> diffuse;
  std::shared_ptr<TextureBlob> normal;
  std::shared_ptr<TextureBlob> glow;
  std::shared_ptr<TextureBlob> environment_mask;
  float tint[4] = {1, 1, 1, 1};
  float material[4] = {0, 0, 0, 0};
  bool double_sided = false;
  bool model_space_normal = false;
  bool skin_material = false;
  uint32_t form_id = 0;
  ComponentKind kind = ComponentKind::kAccessory;
};

struct CpuScene {
  uint64_t world_epoch = 0;
  uint64_t revision = 0;
  uint32_t right_weapon = 0;
  uint32_t left_weapon = 0;
  std::vector<CpuMesh> meshes;
  float min_x = 0, min_y = 0, min_z = 0;
  float max_x = 0, max_y = 0, max_z = 0;
  // Framing is anchored to the character, not a long sword/bow.  Equipment is
  // still drawn and contributes to the scale below, but must not drag the
  // player's body away from the center of the panel.
  float character_min_x = 0, character_min_y = 0, character_min_z = 0;
  float character_max_x = 0, character_max_y = 0, character_max_z = 0;
  // Exact Skyrim shoulder pivots used by the brief weapon-change animation.
  // Both the skinned arm vertices and attached weapon use the same pivot, so
  // the weapon cannot slide independently through the hand during a change.
  float right_arm_pivot[3] = {};
  float left_arm_pivot[3] = {};
  uint32_t nif_cache_hits = 0;
  uint32_t nif_cache_misses = 0;
  uint32_t texture_cache_hits = 0;
  uint32_t texture_cache_misses = 0;
};

std::mutex g_asset_mutex;
std::unordered_map<std::string, std::shared_ptr<ParsedModel>> g_model_cache;
std::unordered_map<std::string, std::shared_ptr<TextureBlob>> g_texture_cache;
std::unordered_map<std::string, std::shared_ptr<Skeleton>> g_skeleton_cache;
SRWLOCK g_scene_lock = SRWLOCK_INIT;
std::shared_ptr<CpuScene> g_ready_scene;

nifly::MatTransform ShapeToGlobal(nifly::NifFile& nif, nifly::NiShape* shape) {
  nifly::MatTransform transform = shape->GetTransformToParent();
  nifly::NiNode* parent = nif.GetParentNode(shape);
  while (parent) {
    transform = parent->GetTransformToParent().ComposeTransforms(transform);
    parent = nif.GetParentNode(parent);
  }
  return transform;
}

std::shared_ptr<ParsedModel> LoadModel(const std::string& input_path,
                                       CpuScene& stats) {
  const std::string path = NormalizePath(input_path.c_str(), "meshes\\");
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    auto found = g_model_cache.find(path);
    if (found != g_model_cache.end()) {
      ++stats.nif_cache_hits;
      return found->second;
    }
  }
  ++stats.nif_cache_misses;
  const uint64_t load_start = GetTickCount64();
  std::vector<uint8_t> bytes;
  if (!ReadGameAsset(path, bytes)) return {};
  const uint64_t parse_start = GetTickCount64();
  std::string storage(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::istringstream stream(storage, std::ios::binary);
  nifly::NifFile nif;
  const int status = nif.Load(stream);
  if (status != 0 || !nif.IsValid()) {
    Log("[SKYRIM-PREVIEW-NIF]", "parse failed path=\"%s\" status=%d",
        path.c_str(), status);
    return {};
  }
  auto model = std::make_shared<ParsedModel>();
  model->path = path;
  model->unknown_blocks = nif.HasUnknown();
  if (nifly::NiNode* root = nif.GetRootNode()) {
    for (const auto& extra_ref : root->extraDataRefs) {
      auto* string_data =
          nif.GetHeader().GetBlock<nifly::NiStringExtraData>(extra_ref);
      if (!string_data || _stricmp(string_data->name.get().c_str(), "Prn") != 0)
        continue;
      model->attachment_node = string_data->stringData.get();
      break;
    }
  }
  const std::vector<nifly::NiShape*> nif_shapes = nif.GetShapes();
  uint32_t partition_backed_shapes = 0;
  for (nifly::NiShape* shape : nif_shapes) {
    if (!shape) continue;
    const std::vector<nifly::Vector3>* positions = nif.GetVertsForShape(shape);
    const std::vector<nifly::Vector3>* normals = nif.GetNormalsForShape(shape);
    const std::vector<nifly::Vector3>* tangents = nif.GetTangentsForShape(shape);
    const std::vector<nifly::Vector2>* uvs = nif.GetUvsForShape(shape);
    if (!positions || positions->empty()) continue;
    // Skyrim FaceGen heads, mouths, eyes and brows can carry their complete
    // vertex payload in NiSkinPartition while the shape-level VF_VERTEX flag
    // is unset. NifFile::PrepareData has already reconstructed vertData from
    // that partition, so HasVertices() is not a valid rejection test here.
    if (!shape->HasVertices()) ++partition_backed_shapes;
    RawMesh mesh;
    mesh.name = shape->name.get();
    mesh.shape_to_global = ShapeToGlobal(nif, shape);
    mesh.skinned = shape->IsSkinned();
    mesh.vertices.resize(positions->size());
    for (size_t i = 0; i < positions->size(); ++i) {
      RawVertex& out = mesh.vertices[i];
      out.position = (*positions)[i];
      out.normal = normals && i < normals->size()
                       ? (*normals)[i]
                       : nifly::Vector3(0.0f, -1.0f, 0.0f);
      out.tangent = tangents && i < tangents->size()
                        ? (*tangents)[i]
                        : nifly::Vector3(1.0f, 0.0f, 0.0f);
      out.uv = uvs && i < uvs->size() ? (*uvs)[i]
                                      : nifly::Vector2(0.0f, 0.0f);
    }
    shape->GetTriangles(mesh.triangles);
    nif.GetShapeBoneList(shape, mesh.bones);
    mesh.skin_to_bone.resize(mesh.bones.size());
    for (uint32_t bone = 0; bone < mesh.bones.size(); ++bone) {
      nif.GetShapeTransformSkinToBone(shape, bone, mesh.skin_to_bone[bone]);
      std::unordered_map<uint16_t, float> weights;
      nif.GetShapeBoneWeights(shape, bone, weights);
      for (const auto& [vertex, weight] : weights) {
        if (vertex >= mesh.vertices.size() || weight <= 0.0f) continue;
        auto& influences = mesh.vertices[vertex].influences;
        auto weakest = std::min_element(
            influences.begin(), influences.end(),
            [](const Influence& a, const Influence& b) {
              return a.weight < b.weight;
            });
        if (weakest != influences.end() && weight > weakest->weight)
          *weakest = {static_cast<uint16_t>(bone), weight};
      }
    }
    nifly::NiVector<nifly::BSDismemberSkinInstance::PartitionInfo> partitions;
    if (nif.GetShapePartitions(shape, partitions, mesh.triangle_partitions)) {
      for (const auto& partition : partitions)
        mesh.partition_ids.push_back(partition.partID);
    }
    const auto texture_refs = nif.GetTexturePathRefs(shape);
    for (size_t i = 0; i < texture_refs.size() && i < mesh.textures.size(); ++i)
      mesh.textures[i] = NormalizePath(texture_refs[i].get().c_str(),
                                       "textures\\");
    if (nifly::NiShader* shader = nif.GetShader(shape)) {
      mesh.skin_tinted = shader->IsSkinTinted() || shader->IsFaceTinted();
      mesh.double_sided = shader->IsDoubleSided();
      mesh.emissive = shader->IsEmissive();
      mesh.glow_map = shader->HasGlowmap();
      mesh.specular = shader->HasSpecular();
      mesh.specular_strength = shader->GetSpecularStrength();
      mesh.glossiness = shader->GetGlossiness();
      mesh.emissive_multiple = shader->GetEmissiveMultiple();
      mesh.environment_scale = shader->GetEnvironmentMapScale();
    }
    model->meshes.push_back(std::move(mesh));
  }
  Log("[SKYRIM-PREVIEW-NIF]",
      "loaded path=\"%s\" bytes=%zu nif_shapes=%zu rendered_shapes=%zu "
      "partition_backed=%u prn=\"%s\" unknown=%u load_ms=%llu parse_ms=%llu",
      path.c_str(), bytes.size(), nif_shapes.size(), model->meshes.size(),
      partition_backed_shapes,
      model->attachment_node.c_str(),
      model->unknown_blocks ? 1u : 0u,
      static_cast<unsigned long long>(parse_start - load_start),
      static_cast<unsigned long long>(GetTickCount64() - parse_start));
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    g_model_cache[path] = model;
  }
  return model;
}

std::shared_ptr<Skeleton> LoadSkeleton(const std::string& input_path,
                                       CpuScene& stats) {
  const std::string path = NormalizePath(input_path.c_str(), "meshes\\");
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    auto found = g_skeleton_cache.find(path);
    if (found != g_skeleton_cache.end()) return found->second;
  }
  std::vector<uint8_t> bytes;
  if (!ReadGameAsset(path, bytes)) return {};
  std::string storage(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::istringstream stream(storage, std::ios::binary);
  nifly::NifFile nif;
  if (nif.Load(stream) != 0 || !nif.IsValid()) return {};
  auto skeleton = std::make_shared<Skeleton>();
  skeleton->path = path;
  std::vector<nifly::NiObject*> tree;
  nif.GetTree(tree);
  for (nifly::NiObject* object : tree) {
    auto* node = dynamic_cast<nifly::NiNode*>(object);
    if (!node || node->name.get().empty()) continue;
    const std::string name = node->name.get();
    nifly::MatTransform global;
    if (nif.GetNodeTransformToGlobal(name, global))
      skeleton->global_bones[name] = global;
    if (nifly::NiNode* parent = nif.GetParentNode(node);
        parent && !parent->name.get().empty())
      skeleton->parent_bones[name] = parent->name.get();
  }
  Log("[SKYRIM-PREVIEW-SKELETON]",
      "loaded path=\"%s\" bones=%zu bytes=%zu", path.c_str(),
      skeleton->global_bones.size(), bytes.size());
  for (const auto& [name, transform] : skeleton->global_bones) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (lower.find("weapon") == std::string::npos &&
        lower.find("shield") == std::string::npos &&
        lower.find("bow") == std::string::npos &&
        lower.find(" hand ") == std::string::npos)
      continue;
    const auto parent = skeleton->parent_bones.find(name);
    Log("[SKYRIM-PREVIEW-SKELETON]",
        "socket=\"%s\" parent=\"%s\" position=(%.2f %.2f %.2f)",
        name.c_str(),
        parent == skeleton->parent_bones.end() ? "" : parent->second.c_str(),
        transform.translation.x, transform.translation.y,
        transform.translation.z);
  }
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    g_skeleton_cache[path] = skeleton;
  }
  return skeleton;
}

std::shared_ptr<TextureBlob> LoadTexture(const std::string& input_path,
                                         CpuScene& stats) {
  if (input_path.empty()) return {};
  const std::string path = NormalizePath(input_path.c_str(), "textures\\");
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    auto found = g_texture_cache.find(path);
    if (found != g_texture_cache.end()) {
      ++stats.texture_cache_hits;
      return found->second;
    }
  }
  ++stats.texture_cache_misses;
  const uint64_t start = GetTickCount64();
  auto texture = std::make_shared<TextureBlob>();
  texture->path = path;
  if (!ReadGameAsset(path, texture->bytes)) return {};
  Log("[SKYRIM-PREVIEW-TEXTURE]", "loaded path=\"%s\" bytes=%zu elapsed_ms=%llu",
      path.c_str(), texture->bytes.size(),
      static_cast<unsigned long long>(GetTickCount64() - start));
  {
    std::lock_guard<std::mutex> guard(g_asset_mutex);
    g_texture_cache[path] = texture;
  }
  return texture;
}

bool NameContains(const std::string& value, const char* token) {
  if (!token || !*token) return false;
  std::string lhs = value;
  std::string rhs = token;
  std::transform(lhs.begin(), lhs.end(), lhs.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::transform(rhs.begin(), rhs.end(), rhs.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lhs.find(rhs) != std::string::npos;
}

uint32_t AnimationGroup(const std::vector<std::string>& bones,
                        const RawVertex& vertex, ComponentKind kind) {
  if (kind == ComponentKind::kRightWeapon) return 1;
  if (kind == ComponentKind::kLeftWeapon || kind == ComponentKind::kShield)
    return 2;
  float best_weight = 0.0f;
  std::string best;
  for (const Influence& influence : vertex.influences) {
    if (influence.weight <= best_weight || influence.bone >= bones.size())
      continue;
    best_weight = influence.weight;
    best = bones[influence.bone];
  }
  if (NameContains(best, "R UpperArm") || NameContains(best, "R Forearm") ||
      NameContains(best, "R Hand"))
    return 1;
  if (NameContains(best, "L UpperArm") || NameContains(best, "L Forearm") ||
      NameContains(best, "L Hand"))
    return 2;
  return 0;
}

bool IsArmBoneName(const std::string& name, bool right) {
  const char side = right ? 'r' : 'l';
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string marker = std::string("npc ") + side + " ";
  if (lower.find(marker) == std::string::npos) return false;
  return lower.find("upperarm") != std::string::npos ||
         lower.find("forearm") != std::string::npos ||
         lower.find(" hand") != std::string::npos ||
         lower.find(" finger") != std::string::npos ||
         lower.find(" weapon") != std::string::npos;
}

bool IsArmDescendant(const Skeleton* skeleton, const std::string& name,
                     bool right) {
  if (IsArmBoneName(name, right)) return true;
  if (!skeleton) return false;
  std::string cursor = name;
  // Skeletons are shallow, but cap the walk so a malformed replacement NIF
  // cannot create an infinite parent cycle on the asset worker.
  for (uint32_t depth = 0; depth < 32; ++depth) {
    const auto parent = skeleton->parent_bones.find(cursor);
    if (parent == skeleton->parent_bones.end() || parent->second.empty())
      return false;
    cursor = parent->second;
    if (IsArmBoneName(cursor, right)) return true;
  }
  return false;
}

float NeutralArmAngle(const Skeleton* skeleton, bool right) {
  if (!skeleton) return 0.0f;
  const char* upper_name = right ? "NPC R UpperArm [RUar]"
                                 : "NPC L UpperArm [LUar]";
  const char* forearm_name = right ? "NPC R Forearm [RLar]"
                                   : "NPC L Forearm [LLar]";
  const auto upper = skeleton->global_bones.find(upper_name);
  const auto forearm = skeleton->global_bones.find(forearm_name);
  if (upper == skeleton->global_bones.end() ||
      forearm == skeleton->global_bones.end())
    return 0.0f;
  const nifly::Vector3 vector =
      forearm->second.translation - upper->second.translation;
  const float outward = right ? vector.x : -vector.x;
  const float current_down = std::atan2(-vector.z, std::max(0.001f, outward));
  // The shipped third-person skeleton is a bind-pose skeleton.  Fold the arm
  // chain almost vertically alongside the torso for a quiet mannequin pose;
  // do this once while assembling vertices, never as a live idle animation.
  constexpr float target_down = 78.0f * 3.1415926535f / 180.0f;
  const float delta = std::clamp(target_down - current_down, -0.45f, 1.50f);
  return right ? delta : -delta;
}

nifly::MatTransform PoseArmBone(const Skeleton* skeleton,
                                const std::string& name,
                                const nifly::MatTransform& input) {
  const bool right = IsArmDescendant(skeleton, name, true);
  const bool left = !right && IsArmDescendant(skeleton, name, false);
  if (!right && !left) return input;
  const char* upper_name = right ? "NPC R UpperArm [RUar]"
                                 : "NPC L UpperArm [LUar]";
  const auto upper = skeleton->global_bones.find(upper_name);
  if (upper == skeleton->global_bones.end()) return input;
  const float angle = NeutralArmAngle(skeleton, right);
  if (std::abs(angle) < 0.001f) return input;
  const float c = std::cos(angle), s = std::sin(angle);
  const nifly::Matrix3 rotate_y(c, 0, s, 0, 1, 0, -s, 0, c);
  nifly::MatTransform output = input;
  output.rotation = rotate_y * input.rotation;
  output.translation = upper->second.translation +
      rotate_y * (input.translation - upper->second.translation);
  return output;
}

nifly::MatTransform BoneGlobal(const Skeleton* skeleton, const RawMesh& mesh,
                               uint32_t bone) {
  if (bone < mesh.bones.size() && skeleton) {
    const auto found = skeleton->global_bones.find(mesh.bones[bone]);
    if (found != skeleton->global_bones.end())
      return PoseArmBone(skeleton, mesh.bones[bone], found->second);
  }
  nifly::MatTransform identity;
  identity.Clear();
  return identity;
}

void SkinVertex(const RawMesh& mesh, const Skeleton* skeleton,
                const RawVertex& input, nifly::Vector3& position,
                nifly::Vector3& normal, nifly::Vector3& tangent) {
  float total = 0.0f;
  position.Zero();
  normal.Zero();
  tangent.Zero();
  if (mesh.skinned && !mesh.bones.empty()) {
    for (const Influence& influence : input.influences) {
      if (influence.weight <= 0.0f || influence.bone >= mesh.bones.size() ||
          influence.bone >= mesh.skin_to_bone.size())
        continue;
      const nifly::MatTransform transform =
          BoneGlobal(skeleton, mesh, influence.bone)
              .ComposeTransforms(mesh.skin_to_bone[influence.bone]);
      position += transform.ApplyTransform(input.position) * influence.weight;
      normal += transform.ApplyTransformToDir(input.normal) * influence.weight;
      tangent += transform.ApplyTransformToDir(input.tangent) * influence.weight;
      total += influence.weight;
    }
  }
  if (total <= 0.0001f) {
    position = mesh.shape_to_global.ApplyTransform(input.position);
    normal = mesh.shape_to_global.ApplyTransformToDir(input.normal);
    tangent = mesh.shape_to_global.ApplyTransformToDir(input.tangent);
  } else if (std::abs(total - 1.0f) > 0.001f) {
    position /= total;
    normal /= total;
    tangent /= total;
  }
  normal.Normalize();
  tangent.Normalize();
}

bool IsWeaponKind(ComponentKind kind) {
  return kind == ComponentKind::kRightWeapon ||
         kind == ComponentKind::kLeftWeapon || kind == ComponentKind::kShield;
}

const nifly::MatTransform* FindSkeletonTransform(
    const Skeleton* skeleton, const std::string& requested,
    std::string& resolved_name) {
  if (!skeleton || requested.empty()) return nullptr;
  const auto exact = skeleton->global_bones.find(requested);
  if (exact != skeleton->global_bones.end()) {
    resolved_name = exact->first;
    return &exact->second;
  }
  std::string needle = requested;
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  const bool wants_left = needle.find("left") != std::string::npos ||
                          needle.find(" l ") != std::string::npos ||
                          needle.find("[l") != std::string::npos;
  const bool wants_right = needle.find("right") != std::string::npos ||
                           needle.find(" r ") != std::string::npos ||
                           needle.find("[r") != std::string::npos;
  const nifly::MatTransform* best = nullptr;
  int best_score = 100;
  for (const auto& [name, transform] : skeleton->global_bones) {
    std::string candidate = name;
    std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    const bool candidate_left = candidate.find("left") != std::string::npos ||
                                candidate.find(" l ") != std::string::npos ||
                                candidate.find("[l") != std::string::npos;
    const bool candidate_right = candidate.find("right") != std::string::npos ||
                                 candidate.find(" r ") != std::string::npos ||
                                 candidate.find("[r") != std::string::npos;
    if ((wants_left && candidate_right && !candidate_left) ||
        (wants_right && candidate_left && !candidate_right))
      continue;
    int score = 100;
    if (candidate == needle)
      score = 0;
    else if (candidate.rfind(needle, 0) == 0)
      score = 1;
    else if (needle.size() >= 6 && candidate.find(needle) != std::string::npos)
      score = 2;
    if (score < best_score) {
      best_score = score;
      best = &transform;
      resolved_name = name;
    }
  }
  return best;
}

nifly::MatTransform AttachmentTransform(const Skeleton* skeleton,
                                         const Component& component,
                                         const ParsedModel& model) {
  nifly::MatTransform identity;
  identity.Clear();
  const ComponentKind kind = component.kind;
  if (!skeleton || !IsWeaponKind(kind)) return identity;
  const bool left = kind == ComponentKind::kLeftWeapon ||
                    kind == ComponentKind::kShield;
  std::vector<std::string> candidates;
  auto add_candidate = [&](const std::string& candidate) {
    if (candidate.empty()) return;
    for (const std::string& existing : candidates)
      if (_stricmp(existing.c_str(), candidate.c_str()) == 0) return;
    candidates.push_back(candidate);
  };

  // The model-authored Prn value is the same attachment contract Skyrim's
  // BGSAttachTechniques consumes.  Prefer the left-hand companion node for a
  // left equip when the skeleton supplies one, then the exact authored node.
  if (!model.attachment_node.empty()) {
    if (left) add_candidate(model.attachment_node + "Left");
    add_candidate(model.attachment_node);
  }
  if (kind == ComponentKind::kShield) {
    add_candidate("Shield");
    add_candidate("ShieldBack");
  } else {
    const char* type_node = nullptr;
    switch (component.weapon_type) {
      case 1: type_node = "WeaponSword"; break;
      case 2: type_node = "WeaponDagger"; break;
      case 3: type_node = "WeaponAxe"; break;
      case 4: type_node = "WeaponMace"; break;
      case 5:
      case 6: type_node = "WeaponBack"; break;
      case 7:
      case 9: type_node = "WeaponBow"; break;
      case 8: type_node = "Weapon"; break;
      default: break;
    }
    if (type_node) {
      if (left) add_candidate(std::string(type_node) + "Left");
      add_candidate(type_node);
    }
  }
  add_candidate(left ? "NPC L Weapon [LWpn]" : "NPC R Weapon [RWpn]");
  add_candidate(left ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
  if (kind == ComponentKind::kShield)
    add_candidate("NPC L Forearm [LLar]");

  for (const std::string& name : candidates) {
    std::string resolved_name;
    if (const nifly::MatTransform* found =
            FindSkeletonTransform(skeleton, name, resolved_name)) {
      const nifly::MatTransform posed =
          PoseArmBone(skeleton, resolved_name, *found);
      Log("[SKYRIM-PREVIEW-EQUIP]",
          "attachment form=%08X kind=%u weapon_type=%u prn=\"%s\" node=\"%s\" position=(%.2f %.2f %.2f)",
          component.form_id, static_cast<unsigned>(kind),
          static_cast<unsigned>(component.weapon_type),
          model.attachment_node.c_str(), resolved_name.c_str(),
          posed.translation.x, posed.translation.y, posed.translation.z);
      return posed;
    }
  }
  Log("[SKYRIM-PREVIEW-EQUIP]",
      "attachment node unavailable form=%08X kind=%u weapon_type=%u prn=\"%s\" candidates=%zu",
      component.form_id, static_cast<unsigned>(kind),
      static_cast<unsigned>(component.weapon_type),
      model.attachment_node.c_str(), candidates.size());
  return identity;
}

std::shared_ptr<CpuScene> BuildScene(const PlayerManifest& manifest) {
  const uint64_t total_start = GetTickCount64();
  auto scene = std::make_shared<CpuScene>();
  scene->world_epoch = manifest.world_epoch;
  scene->revision = manifest.revision;
  std::shared_ptr<Skeleton> skeleton =
      LoadSkeleton(manifest.skeleton_path, *scene);
  if (!skeleton) {
    Log("[SKYRIM-PREVIEW-SKELETON]", "failed path=\"%s\" revision=%llu",
        manifest.skeleton_path,
        static_cast<unsigned long long>(manifest.revision));
    return scene;
  }
  const auto right_upper =
      skeleton->global_bones.find("NPC R UpperArm [RUar]");
  const auto left_upper =
      skeleton->global_bones.find("NPC L UpperArm [LUar]");
  if (right_upper != skeleton->global_bones.end()) {
    scene->right_arm_pivot[0] = right_upper->second.translation.x;
    scene->right_arm_pivot[1] = right_upper->second.translation.y;
    scene->right_arm_pivot[2] = right_upper->second.translation.z;
  }
  if (left_upper != skeleton->global_bones.end()) {
    scene->left_arm_pivot[0] = left_upper->second.translation.x;
    scene->left_arm_pivot[1] = left_upper->second.translation.y;
    scene->left_arm_pivot[2] = left_upper->second.translation.z;
  }
  std::array<std::shared_ptr<ParsedModel>, kMaxComponents> component_models{};
  std::array<std::shared_ptr<ParsedModel>, kMaxComponents> high_models{};
  uint32_t hidden_slots = 0;
  for (uint32_t i = 0; i < manifest.component_count; ++i) {
    const Component& component = manifest.components[i];
    // The bottom-screen portrait is deliberately armor-only.  Reject weapon
    // and shield components before asset resolution so a stale producer,
    // queued manifest, or modded hand record cannot load or cache an
    // attachment that the preview is not allowed to display.
    if (IsWeaponKind(component.kind)) {
      Log("[SKYRIM-PREVIEW-EQUIP]",
          "armor-only preview omitted attachment form=%08X kind=%u",
          component.form_id, static_cast<unsigned>(component.kind));
      continue;
    }
    if (!component.model_path[0]) continue;
    component_models[i] = LoadModel(component.model_path, *scene);
    if (component.model_path_high[0] && manifest.weight > 0.01f)
      high_models[i] = LoadModel(component.model_path_high, *scene);
    // Never suppress skin merely because an equipped ARMO/ARMA record names a
    // slot.  Skyrim only hides that partition after the replacement geometry
    // has resolved.  This prevents a missing/modded NIF or a transient equip
    // snapshot from leaving a permanent hole in the body.
    if (!component.base_skin && component.slot_mask && component_models[i] &&
        !component_models[i]->meshes.empty())
      hidden_slots |= component.slot_mask;
  }
  const uint8_t tint_r = static_cast<uint8_t>(manifest.body_tint_rgba & 0xFF);
  const uint8_t tint_g = static_cast<uint8_t>((manifest.body_tint_rgba >> 8) & 0xFF);
  const uint8_t tint_b = static_cast<uint8_t>((manifest.body_tint_rgba >> 16) & 0xFF);
  bool bounds_set = false;
  bool character_bounds_set = false;
  uint64_t base_triangles_total = 0;
  uint64_t base_triangles_visible = 0;
  uint64_t base_triangles_hidden = 0;
  for (uint32_t component_index = 0;
       component_index < manifest.component_count; ++component_index) {
    const Component& component = manifest.components[component_index];
    if (IsWeaponKind(component.kind)) continue;
    if (!component.model_path[0]) continue;
    if ((component.kind == ComponentKind::kHair) &&
        (hidden_slots & ((1u << 0) | (1u << 1)))) {
      Log("[SKYRIM-PREVIEW-BODY]",
          "hair hidden by helmet slots mask=%08X form=%08X", hidden_slots,
          component.form_id);
      continue;
    }
    std::shared_ptr<ParsedModel> model = component_models[component_index];
    if (!model) continue;
    std::shared_ptr<ParsedModel> high_model = high_models[component_index];
    const float weight_lerp = std::clamp(manifest.weight / 100.0f, 0.0f, 1.0f);
    const nifly::MatTransform attachment =
        AttachmentTransform(skeleton.get(), component, *model);
    for (size_t raw_index = 0; raw_index < model->meshes.size(); ++raw_index) {
      const RawMesh& raw = model->meshes[raw_index];
      const RawMesh* high = high_model && raw_index < high_model->meshes.size() &&
                                    high_model->meshes[raw_index].vertices.size() ==
                                        raw.vertices.size()
                                ? &high_model->meshes[raw_index]
                                : nullptr;
      CpuMesh mesh;
      mesh.form_id = component.form_id;
      mesh.kind = component.kind;
      mesh.double_sided = raw.double_sided;
      mesh.diffuse = LoadTexture(raw.textures[0], *scene);
      mesh.normal = LoadTexture(raw.textures[1], *scene);
      // Bethesda skin textures ending in _msn store complete model-space
      // normals. They cannot use the tangent-space path used by armor _n maps.
      mesh.model_space_normal = NameContains(raw.textures[1], "_msn.");
      mesh.skin_material = raw.skin_tinted;
      // Skin shader slot 2 is its subsurface/detail (_sk) map, not emissive
      // color. Only the explicit glow-map flag may route slot 2 to glow.
      if (raw.glow_map)
        mesh.glow = LoadTexture(raw.textures[2], *scene);
      if (raw.environment_scale > 0.001f)
        mesh.environment_mask = LoadTexture(raw.textures[5], *scene);
      mesh.material[0] = raw.specular
                             ? std::clamp(raw.specular_strength, 0.0f, 4.0f)
                             : 0.0f;
      mesh.material[1] = std::clamp(raw.glossiness / 100.0f, 0.0f, 1.0f);
      mesh.material[2] = raw.glow_map
                             ? std::clamp(raw.emissive_multiple, 0.0f, 8.0f)
                             : 0.0f;
      mesh.material[3] = std::clamp(raw.environment_scale, 0.0f, 4.0f);
      if (raw.skin_tinted) {
        mesh.tint[0] = tint_r / 255.0f;
        mesh.tint[1] = tint_g / 255.0f;
        mesh.tint[2] = tint_b / 255.0f;
      }
      mesh.vertices.reserve(raw.vertices.size());
      for (size_t vertex_index = 0; vertex_index < raw.vertices.size();
           ++vertex_index) {
        RawVertex vertex = raw.vertices[vertex_index];
        if (high) {
          const RawVertex& high_vertex = high->vertices[vertex_index];
          vertex.position = vertex.position * (1.0f - weight_lerp) +
                            high_vertex.position * weight_lerp;
          vertex.normal = vertex.normal * (1.0f - weight_lerp) +
                          high_vertex.normal * weight_lerp;
          vertex.tangent = vertex.tangent * (1.0f - weight_lerp) +
                           high_vertex.tangent * weight_lerp;
        }
        nifly::Vector3 position;
        nifly::Vector3 normal;
        nifly::Vector3 tangent;
        SkinVertex(raw, skeleton.get(), vertex, position, normal, tangent);
        if (IsWeaponKind(component.kind) && !raw.skinned) {
          position = attachment.ApplyTransform(position);
          normal = attachment.ApplyTransformToDir(normal);
          tangent = attachment.ApplyTransformToDir(tangent);
        }
        mesh.vertices.push_back({
            position.x, position.y, position.z, normal.x, normal.y, normal.z,
            tangent.x, tangent.y, tangent.z, vertex.uv.u, vertex.uv.v,
            AnimationGroup(raw.bones, vertex, component.kind)});
        if (!bounds_set) {
          scene->min_x = scene->max_x = position.x;
          scene->min_y = scene->max_y = position.y;
          scene->min_z = scene->max_z = position.z;
          bounds_set = true;
        } else {
          scene->min_x = std::min(scene->min_x, position.x);
          scene->min_y = std::min(scene->min_y, position.y);
          scene->min_z = std::min(scene->min_z, position.z);
          scene->max_x = std::max(scene->max_x, position.x);
          scene->max_y = std::max(scene->max_y, position.y);
          scene->max_z = std::max(scene->max_z, position.z);
        }
        if (!IsWeaponKind(component.kind)) {
          if (!character_bounds_set) {
            scene->character_min_x = scene->character_max_x = position.x;
            scene->character_min_y = scene->character_max_y = position.y;
            scene->character_min_z = scene->character_max_z = position.z;
            character_bounds_set = true;
          } else {
            scene->character_min_x = std::min(scene->character_min_x, position.x);
            scene->character_min_y = std::min(scene->character_min_y, position.y);
            scene->character_min_z = std::min(scene->character_min_z, position.z);
            scene->character_max_x = std::max(scene->character_max_x, position.x);
            scene->character_max_y = std::max(scene->character_max_y, position.y);
            scene->character_max_z = std::max(scene->character_max_z, position.z);
          }
        }
      }
      mesh.indices.reserve(raw.triangles.size() * 3);
      for (size_t triangle_index = 0; triangle_index < raw.triangles.size();
           ++triangle_index) {
        if (component.base_skin) ++base_triangles_total;
        if (component.base_skin &&
            triangle_index < raw.triangle_partitions.size()) {
          const int partition = raw.triangle_partitions[triangle_index];
          if (partition >= 0 &&
              static_cast<size_t>(partition) < raw.partition_ids.size()) {
            const uint16_t part_id = raw.partition_ids[partition];
            if (part_id >= 30 && part_id <= 61 &&
                (hidden_slots & (1u << (part_id - 30)))) {
              ++base_triangles_hidden;
              continue;
            }
          }
        }
        if (component.base_skin) ++base_triangles_visible;
        const nifly::Triangle& triangle = raw.triangles[triangle_index];
        if (triangle.p1 >= mesh.vertices.size() ||
            triangle.p2 >= mesh.vertices.size() ||
            triangle.p3 >= mesh.vertices.size())
          continue;
        mesh.indices.push_back(triangle.p1);
        mesh.indices.push_back(triangle.p2);
        mesh.indices.push_back(triangle.p3);
      }
      if (!mesh.vertices.empty() && !mesh.indices.empty())
        scene->meshes.push_back(std::move(mesh));
    }
  }
  if (!character_bounds_set && bounds_set) {
    scene->character_min_x = scene->min_x;
    scene->character_min_y = scene->min_y;
    scene->character_min_z = scene->min_z;
    scene->character_max_x = scene->max_x;
    scene->character_max_y = scene->max_y;
    scene->character_max_z = scene->max_z;
  }
  Log("[SKYRIM-PREVIEW-BODY]",
      "assembled revision=%llu race=%08X sex=%u weight=%.1f components=%u meshes=%zu hidden_slots=%08X base_triangles=%llu/%llu hidden=%llu bounds=[%.2f %.2f %.2f]-[%.2f %.2f %.2f] character_bounds=[%.2f %.2f %.2f]-[%.2f %.2f %.2f]",
      static_cast<unsigned long long>(manifest.revision), manifest.race_form_id,
      manifest.sex, manifest.weight, manifest.component_count,
      scene->meshes.size(), hidden_slots,
      static_cast<unsigned long long>(base_triangles_visible),
      static_cast<unsigned long long>(base_triangles_total),
      static_cast<unsigned long long>(base_triangles_hidden),
      scene->min_x, scene->min_y, scene->min_z,
      scene->max_x, scene->max_y, scene->max_z,
      scene->character_min_x, scene->character_min_y,
      scene->character_min_z, scene->character_max_x,
      scene->character_max_y, scene->character_max_z);
  Log("[SKYRIM-PREVIEW-PERF]",
      "assembly_ms=%llu nif_hits=%u nif_misses=%u texture_hits=%u texture_misses=%u revision=%llu",
      static_cast<unsigned long long>(GetTickCount64() - total_start),
      scene->nif_cache_hits, scene->nif_cache_misses,
      scene->texture_cache_hits, scene->texture_cache_misses,
      static_cast<unsigned long long>(manifest.revision));
  return scene;
}

DWORD WINAPI WorkerMain(void*) {
  while (g_running.load(std::memory_order_acquire)) {
    WaitForSingleObject(g_work_event, 500);
    if (!g_running.load(std::memory_order_acquire)) break;
    if (!g_build_pending.exchange(false, std::memory_order_acq_rel)) continue;
    PlayerManifest manifest{};
    AcquireSRWLockShared(&g_manifest_lock);
    manifest = g_pending_manifest;
    ReleaseSRWLockShared(&g_manifest_lock);
    if (!manifest.revision || !manifest.world_epoch) continue;
    std::shared_ptr<CpuScene> built = BuildScene(manifest);
    AcquireSRWLockShared(&g_manifest_lock);
    const bool current = manifest.revision == g_pending_manifest.revision &&
                         manifest.world_epoch == g_pending_manifest.world_epoch;
    ReleaseSRWLockShared(&g_manifest_lock);
    if (!current) {
      Log("[SKYRIM-PREVIEW]", "discarded stale assembly revision=%llu",
          static_cast<unsigned long long>(manifest.revision));
      continue;
    }
    AcquireSRWLockExclusive(&g_scene_lock);
    g_ready_scene = std::move(built);
    ReleaseSRWLockExclusive(&g_scene_lock);
  }
  return 0;
}

template <class T>
void ReleaseCom(T*& value) {
  if (value) value->Release();
  value = nullptr;
}

#pragma pack(push, 1)
struct DdsPixelFormat {
  uint32_t size, flags, four_cc, rgb_bits, r_mask, g_mask, b_mask, a_mask;
};
struct DdsHeader {
  uint32_t size, flags, height, width, pitch_or_linear, depth, mip_count;
  uint32_t reserved1[11];
  DdsPixelFormat pixel_format;
  uint32_t caps, caps2, caps3, caps4, reserved2;
};
struct DdsHeaderDx10 {
  uint32_t format, dimension, misc, array_size, misc2;
};
#pragma pack(pop)

constexpr uint32_t FourCc(char a, char b, char c, char d) {
  return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
         (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

DXGI_FORMAT SrgbFormat(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return format;
  }
}

bool IsBlockCompressed(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
      return true;
    default:
      return false;
  }
}

ID3D11ShaderResourceView* CreateDdsTexture(ID3D11Device* device,
                                           const TextureBlob& blob,
                                           bool srgb) {
  if (!device || blob.bytes.size() < 4 + sizeof(DdsHeader) ||
      std::memcmp(blob.bytes.data(), "DDS ", 4) != 0)
    return nullptr;
  const DdsHeader* header = reinterpret_cast<const DdsHeader*>(
      blob.bytes.data() + 4);
  if (header->size != 124 || header->pixel_format.size != 32 ||
      !header->width || !header->height)
    return nullptr;
  size_t offset = 4 + sizeof(DdsHeader);
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  const uint32_t four = header->pixel_format.four_cc;
  if (four == FourCc('D', 'X', '1', '0')) {
    if (blob.bytes.size() < offset + sizeof(DdsHeaderDx10)) return nullptr;
    const auto* dx10 = reinterpret_cast<const DdsHeaderDx10*>(
        blob.bytes.data() + offset);
    format = static_cast<DXGI_FORMAT>(dx10->format);
    if (dx10->array_size != 1 || dx10->dimension != 3) return nullptr;
    offset += sizeof(DdsHeaderDx10);
  } else if (four == FourCc('D', 'X', 'T', '1')) {
    format = DXGI_FORMAT_BC1_UNORM;
  } else if (four == FourCc('D', 'X', 'T', '3')) {
    format = DXGI_FORMAT_BC2_UNORM;
  } else if (four == FourCc('D', 'X', 'T', '5')) {
    format = DXGI_FORMAT_BC3_UNORM;
  } else if (four == FourCc('A', 'T', 'I', '1') ||
             four == FourCc('B', 'C', '4', 'U')) {
    format = DXGI_FORMAT_BC4_UNORM;
  } else if (four == FourCc('A', 'T', 'I', '2') ||
             four == FourCc('B', 'C', '5', 'U')) {
    format = DXGI_FORMAT_BC5_UNORM;
  } else if (header->pixel_format.rgb_bits == 32 &&
             header->pixel_format.r_mask == 0x000000FF &&
             header->pixel_format.g_mask == 0x0000FF00 &&
             header->pixel_format.b_mask == 0x00FF0000) {
    format = DXGI_FORMAT_R8G8B8A8_UNORM;
  } else if (header->pixel_format.rgb_bits == 32 &&
             header->pixel_format.r_mask == 0x00FF0000 &&
             header->pixel_format.g_mask == 0x0000FF00 &&
             header->pixel_format.b_mask == 0x000000FF) {
    format = DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  if (format == DXGI_FORMAT_UNKNOWN) {
    Log("[SKYRIM-PREVIEW-TEXTURE]", "unsupported DDS format path=\"%s\" fourcc=%08X",
        blob.path.c_str(), four);
    return nullptr;
  }
  if (srgb) format = SrgbFormat(format);
  const uint32_t mip_count = std::max(1u, header->mip_count);
  std::vector<D3D11_SUBRESOURCE_DATA> initial;
  initial.reserve(mip_count);
  uint32_t width = header->width;
  uint32_t height = header->height;
  for (uint32_t mip = 0; mip < mip_count; ++mip) {
    const bool bc = IsBlockCompressed(format);
    uint32_t row_pitch = 0;
    uint32_t slice_pitch = 0;
    if (bc) {
      const uint32_t block_bytes =
          (format == DXGI_FORMAT_BC1_UNORM ||
           format == DXGI_FORMAT_BC1_UNORM_SRGB ||
           format == DXGI_FORMAT_BC4_UNORM || format == DXGI_FORMAT_BC4_SNORM)
              ? 8u
              : 16u;
      row_pitch = std::max(1u, (width + 3) / 4) * block_bytes;
      slice_pitch = row_pitch * std::max(1u, (height + 3) / 4);
    } else {
      row_pitch = width * 4;
      slice_pitch = row_pitch * height;
    }
    if (offset + slice_pitch > blob.bytes.size()) return nullptr;
    initial.push_back({blob.bytes.data() + offset, row_pitch, slice_pitch});
    offset += slice_pitch;
    width = std::max(1u, width / 2);
    height = std::max(1u, height / 2);
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = header->width;
  desc.Height = header->height;
  desc.MipLevels = mip_count;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, initial.data(), &texture)))
    return nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  device->CreateShaderResourceView(texture, nullptr, &view);
  texture->Release();
  return view;
}

struct GpuMesh {
  ID3D11Buffer* vertices = nullptr;
  ID3D11Buffer* indices = nullptr;
  ID3D11ShaderResourceView* diffuse = nullptr;
  ID3D11ShaderResourceView* normal = nullptr;
  ID3D11ShaderResourceView* glow = nullptr;
  ID3D11ShaderResourceView* environment_mask = nullptr;
  uint32_t index_count = 0;
  float tint[4] = {1, 1, 1, 1};
  float material[4] = {0, 0, 0, 0};
  float flags[4] = {0, 0, 0, 0};
  bool double_sided = false;
  ComponentKind kind = ComponentKind::kAccessory;
};

struct ReadbackSlot {
  ID3D11Texture2D* staging = nullptr;
  ID3D11Query* complete = nullptr;
  uint64_t revision = 0;
  uint64_t world_epoch = 0;
  bool pending = false;
};

struct GpuState {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* immediate = nullptr;
  ID3D11DeviceContext* deferred = nullptr;
  ID3D11Texture2D* target = nullptr;
  ID3D11RenderTargetView* target_view = nullptr;
  ID3D11Texture2D* depth = nullptr;
  ID3D11DepthStencilView* depth_view = nullptr;
  ID3D11VertexShader* vertex_shader = nullptr;
  ID3D11PixelShader* pixel_shader = nullptr;
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* constants = nullptr;
  ID3D11SamplerState* sampler = nullptr;
  ID3D11RasterizerState* cull_back = nullptr;
  ID3D11RasterizerState* cull_none = nullptr;
  ID3D11DepthStencilState* depth_state = nullptr;
  ID3D11BlendState* blend = nullptr;
  ID3D11ShaderResourceView* white = nullptr;
  ID3D11ShaderResourceView* flat_normal = nullptr;
  ID3D11ShaderResourceView* black = nullptr;
  std::vector<GpuMesh> meshes;
  std::vector<GpuMesh> transition_old_weapons;
  std::unordered_map<std::string, ID3D11ShaderResourceView*> textures;
  std::array<ReadbackSlot, 3> readbacks{};
  unsigned next_readback = 0;
  uint64_t scene_revision = 0;
  uint64_t world_epoch = 0;
  uint64_t rendered_control_revision = 0;
  uint64_t frame_revision = 0;
  uint64_t last_render_ms = 0;
  uint64_t last_perf_log_ms = 0;
  uint64_t animation_start_ms = 0;
  uint32_t old_right_weapon = 0;
  uint32_t old_left_weapon = 0;
  uint32_t new_right_weapon = 0;
  uint32_t new_left_weapon = 0;
  float bounds[6] = {};
  float character_bounds[6] = {};
  float right_arm_pivot[3] = {};
  float left_arm_pivot[3] = {};
};

GpuState g_gpu;

void ReleaseMeshBuffers(std::vector<GpuMesh>& meshes) {
  for (GpuMesh& mesh : meshes) {
    ReleaseCom(mesh.vertices);
    ReleaseCom(mesh.indices);
  }
  meshes.clear();
}

void ReleaseSceneBuffers() {
  ReleaseMeshBuffers(g_gpu.meshes);
  ReleaseMeshBuffers(g_gpu.transition_old_weapons);
}

void ReleaseGpuMeshes() {
  ReleaseSceneBuffers();
  for (auto& [path, view] : g_gpu.textures) ReleaseCom(view);
  g_gpu.textures.clear();
}

void ReleaseGpu() {
  ReleaseGpuMeshes();
  for (ReadbackSlot& slot : g_gpu.readbacks) {
    ReleaseCom(slot.staging);
    ReleaseCom(slot.complete);
    slot = {};
  }
  ReleaseCom(g_gpu.white);
  ReleaseCom(g_gpu.flat_normal);
  ReleaseCom(g_gpu.black);
  ReleaseCom(g_gpu.blend);
  ReleaseCom(g_gpu.depth_state);
  ReleaseCom(g_gpu.cull_none);
  ReleaseCom(g_gpu.cull_back);
  ReleaseCom(g_gpu.sampler);
  ReleaseCom(g_gpu.constants);
  ReleaseCom(g_gpu.input_layout);
  ReleaseCom(g_gpu.pixel_shader);
  ReleaseCom(g_gpu.vertex_shader);
  ReleaseCom(g_gpu.depth_view);
  ReleaseCom(g_gpu.depth);
  ReleaseCom(g_gpu.target_view);
  ReleaseCom(g_gpu.target);
  ReleaseCom(g_gpu.deferred);
  ReleaseCom(g_gpu.immediate);
  ReleaseCom(g_gpu.device);
  g_gpu = {};
}

ID3D11ShaderResourceView* SolidTexture(ID3D11Device* device, uint32_t rgba) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{&rgba, 4, 4};
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &data, &texture))) return nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  device->CreateShaderResourceView(texture, nullptr, &view);
  texture->Release();
  return view;
}

const char* kShader = R"HLSL(
cbuffer PreviewConstants : register(b0) {
  row_major float4x4 mvp;
  float4 tint;
  float4 bounds;
  float4 animation;
  float4 material;
  float4 flags;
};
struct VSIn {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float3 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  uint group : BLENDINDICES0;
};
struct VSOut {
  float4 position : SV_POSITION;
  float3 normal : NORMAL;
  float3 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float3 view : TEXCOORD1;
};
float3 RotateAroundY(float3 p, float3 pivot, float angle) {
  float s = sin(angle), c = cos(angle);
  p -= pivot;
  float2 r = float2(p.x * c + p.z * s, -p.x * s + p.z * c);
  return float3(r.x, p.y, r.y) + pivot;
}
float3 RotateAroundZ(float3 p, float angle) {
  float s = sin(angle), c = cos(angle);
  return float3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
}
VSOut VSMain(VSIn input) {
  VSOut output;
  float3 p = input.position;
  float3 n = input.normal;
  float3 t = input.tangent;
  if (input.group == 1 && abs(animation.x) > 0.0001) {
    float3 pivot = float3(bounds.x, bounds.z, bounds.w);
    p = RotateAroundY(p, pivot, animation.x);
    n = RotateAroundY(n, 0, animation.x);
    t = RotateAroundY(t, 0, animation.x);
  } else if (input.group == 2 && abs(animation.y) > 0.0001) {
    float3 pivot = float3(bounds.y, bounds.z, bounds.w);
    p = RotateAroundY(p, pivot, animation.y);
    n = RotateAroundY(n, 0, animation.y);
    t = RotateAroundY(t, 0, animation.y);
  }
  // The MVP rotates positions around the upright Z axis. Rotate the tangent
  // frame by the identical user + neutral-facing angle so preview lighting is
  // stable in camera space instead of remaining stuck to the unrotated mesh.
  n = RotateAroundZ(n, animation.z);
  t = RotateAroundZ(t, animation.z);
  output.position = mul(float4(p, 1), mvp);
  output.normal = normalize(n);
  output.tangent = normalize(t);
  output.uv = input.uv;
  output.view = float3(0, -1, 0);
  return output;
}
Texture2D diffuseMap : register(t0);
Texture2D normalMap : register(t1);
Texture2D glowMap : register(t2);
Texture2D environmentMask : register(t3);
SamplerState materialSampler : register(s0);
float4 PSMain(VSOut input) : SV_TARGET {
  float4 albedo = diffuseMap.Sample(materialSampler, input.uv) * tint;
  clip(albedo.a - 0.035);
  float3 N = normalize(input.normal);
  float4 normalSample = normalMap.Sample(materialSampler, input.uv);
  if (flags.x > 0.5) {
    // _msn is a full model-space direction. Rotate it with the character root;
    // treating it as a tangent-space map caused the unstable skin shading.
    float3 modelN = normalSample.xyz * 2 - 1;
    N = normalize(RotateAroundZ(modelN, animation.z));
  } else {
    float3 T = normalize(input.tangent - N * dot(N, input.tangent));
    float3 B = normalize(cross(N, T));
    float2 normalXY = normalSample.xy * 2 - 1;
    float3 mapN = float3(normalXY,
                         sqrt(saturate(1 - dot(normalXY, normalXY))));
    N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
  }
  float3 keyDir = normalize(float3(-0.38, -0.82, 0.44));
  float3 fillDir = normalize(float3(0.72, -0.58, 0.28));
  float keyDot = dot(N, keyDir);
  float fillDot = dot(N, fillDir);
  float key = saturate(keyDot);
  float fill = saturate(fillDot);
  float3 viewDir = normalize(input.view);
  float rim = pow(1 - saturate(dot(N, viewDir)), 2.8);
  // A preview should remain legible independently of Skyrim's cave/night
  // lighting.  This is a fixed, inexpensive key/fill/rim rig in preview space.
  float lighting = 0.52 + key * 0.62 + fill * 0.28 + rim * 0.12;
  // Skin needs a broad portrait light, while armor keeps the more directional
  // key/fill response. This is still a fixed, allocation-free shader path.
  float skinKey = saturate((keyDot + 0.38) / 1.38);
  float skinFill = saturate((fillDot + 0.30) / 1.30);
  float skinLighting = 0.70 + skinKey * 0.42 + skinFill * 0.16 + rim * 0.06;
  lighting = lerp(lighting, skinLighting, saturate(flags.y));
  // Headgear must not make the face unreadable. Head, eye and hair meshes can
  // carry tangent frames authored for Skyrim's deferred face-lighting path;
  // those normals are occasionally nearly perpendicular to this small fixed
  // preview rig. animation.w is a per-mesh face-light flag (it is not used by
  // the static-pose animation), so retain material detail while guaranteeing a
  // soft frontal illumination floor for exposed facial geometry.
  lighting = max(lighting, 0.96 * saturate(animation.w));
  float3 reflected = reflect(-keyDir, N);
  float specPower = lerp(8.0, 80.0, material.y);
  float specular = pow(saturate(dot(reflected, viewDir)), specPower) *
                   material.x * normalSample.a;
  float3 glow = glowMap.Sample(materialSampler, input.uv).rgb * material.z;
  float environment = environmentMask.Sample(materialSampler, input.uv).r *
                      material.w * rim * 0.18;
  return float4(albedo.rgb * lighting + specular + glow + environment,
                albedo.a);
}
)HLSL";

bool CompileShaders(ID3D11Device* device) {
  ID3DBlob* vs_blob = nullptr;
  ID3DBlob* ps_blob = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT result = D3DCompile(kShader, std::strlen(kShader), "SkyrimPreview",
                              nullptr, nullptr, "VSMain", "vs_4_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_blob,
                              &errors);
  if (FAILED(result)) {
    Log("[SKYRIM-PREVIEW-RENDER]", "vertex shader compile failed hr=%08lX %s",
        static_cast<unsigned long>(result),
        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    ReleaseCom(errors);
    return false;
  }
  ReleaseCom(errors);
  result = D3DCompile(kShader, std::strlen(kShader), "SkyrimPreview", nullptr,
                      nullptr, "PSMain", "ps_4_0",
                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_blob, &errors);
  if (FAILED(result)) {
    Log("[SKYRIM-PREVIEW-RENDER]", "pixel shader compile failed hr=%08lX %s",
        static_cast<unsigned long>(result),
        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    ReleaseCom(errors);
    ReleaseCom(vs_blob);
    return false;
  }
  ReleaseCom(errors);
  result = device->CreateVertexShader(vs_blob->GetBufferPointer(),
                                      vs_blob->GetBufferSize(), nullptr,
                                      &g_gpu.vertex_shader);
  if (SUCCEEDED(result))
    result = device->CreatePixelShader(ps_blob->GetBufferPointer(),
                                       ps_blob->GetBufferSize(), nullptr,
                                       &g_gpu.pixel_shader);
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
       static_cast<UINT>(offsetof(CpuVertex, px)), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
       static_cast<UINT>(offsetof(CpuVertex, nx)), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
       static_cast<UINT>(offsetof(CpuVertex, tx)), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
       static_cast<UINT>(offsetof(CpuVertex, u)), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 0,
       static_cast<UINT>(offsetof(CpuVertex, animation_group)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (SUCCEEDED(result))
    result = device->CreateInputLayout(layout, std::size(layout),
                                       vs_blob->GetBufferPointer(),
                                       vs_blob->GetBufferSize(),
                                       &g_gpu.input_layout);
  ReleaseCom(vs_blob);
  ReleaseCom(ps_blob);
  return SUCCEEDED(result);
}

bool CreateGpuState(IDXGISwapChain* chain) {
  if (!chain) return false;
  ID3D11Device* device = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&device))) ||
      !device)
    return false;
  if (g_gpu.device == device) {
    device->Release();
    return true;
  }
  ReleaseGpu();
  g_gpu.device = device;
  device->GetImmediateContext(&g_gpu.immediate);
  if (!g_gpu.immediate || FAILED(device->CreateDeferredContext(0, &g_gpu.deferred))) {
    ReleaseGpu();
    return false;
  }
  D3D11_TEXTURE2D_DESC target{};
  target.Width = kRenderWidth;
  target.Height = kRenderHeight;
  target.MipLevels = target.ArraySize = 1;
  // Render into an sRGB RTV so the readback bytes can be alpha-composited by
  // the lower-screen GDI canvas without displaying linear-light RGB too dark.
  target.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  target.SampleDesc.Count = 1;
  target.Usage = D3D11_USAGE_DEFAULT;
  target.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device->CreateTexture2D(&target, nullptr, &g_gpu.target)) ||
      FAILED(device->CreateRenderTargetView(g_gpu.target, nullptr,
                                             &g_gpu.target_view))) {
    ReleaseGpu();
    return false;
  }
  D3D11_TEXTURE2D_DESC depth = target;
  depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device->CreateTexture2D(&depth, nullptr, &g_gpu.depth)) ||
      FAILED(device->CreateDepthStencilView(g_gpu.depth, nullptr,
                                             &g_gpu.depth_view))) {
    ReleaseGpu();
    return false;
  }
  D3D11_TEXTURE2D_DESC staging = target;
  staging.Usage = D3D11_USAGE_STAGING;
  staging.BindFlags = 0;
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
  for (ReadbackSlot& slot : g_gpu.readbacks) {
    if (FAILED(device->CreateTexture2D(&staging, nullptr, &slot.staging)) ||
        FAILED(device->CreateQuery(&query, &slot.complete))) {
      ReleaseGpu();
      return false;
    }
  }
  if (!CompileShaders(device)) {
    ReleaseGpu();
    return false;
  }
  D3D11_BUFFER_DESC constants{};
  constants.ByteWidth = 144;
  constants.Usage = D3D11_USAGE_DYNAMIC;
  constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device->CreateBuffer(&constants, nullptr, &g_gpu.constants))) {
    ReleaseGpu();
    return false;
  }
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_ANISOTROPIC;
  sampler.AddressU = sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxAnisotropy = 8;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  device->CreateSamplerState(&sampler, &g_gpu.sampler);
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_BACK;
  raster.FrontCounterClockwise = FALSE;
  raster.DepthClipEnable = TRUE;
  device->CreateRasterizerState(&raster, &g_gpu.cull_back);
  raster.CullMode = D3D11_CULL_NONE;
  device->CreateRasterizerState(&raster, &g_gpu.cull_none);
  D3D11_DEPTH_STENCIL_DESC depth_state{};
  depth_state.DepthEnable = TRUE;
  depth_state.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_state.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  device->CreateDepthStencilState(&depth_state, &g_gpu.depth_state);
  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].BlendEnable = TRUE;
  blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  device->CreateBlendState(&blend, &g_gpu.blend);
  g_gpu.white = SolidTexture(device, 0xFFFFFFFFu);
  // RGBA (128,128,255,255), interpreted as a flat tangent-space normal.
  g_gpu.flat_normal = SolidTexture(device, 0xFFFF8080u);
  g_gpu.black = SolidTexture(device, 0xFF000000u);
  Log("[SKYRIM-PREVIEW-RENDER]",
      "private D3D11 target initialized size=%ux%u device=%p", kRenderWidth,
      kRenderHeight, device);
  return g_gpu.sampler && g_gpu.cull_back && g_gpu.cull_none &&
         g_gpu.depth_state && g_gpu.blend && g_gpu.white &&
         g_gpu.flat_normal && g_gpu.black;
}

ID3D11ShaderResourceView* GpuTexture(const std::shared_ptr<TextureBlob>& blob,
                                     bool srgb) {
  if (!blob || !g_gpu.device) return nullptr;
  const std::string key = blob->path + (srgb ? "#s" : "#l");
  auto found = g_gpu.textures.find(key);
  if (found != g_gpu.textures.end()) return found->second;
  ID3D11ShaderResourceView* view = CreateDdsTexture(g_gpu.device, *blob, srgb);
  if (view) {
    g_gpu.textures[key] = view;
    Log("[SKYRIM-PREVIEW-TEXTURE]", "GPU upload path=\"%s\" srgb=%u",
        blob->path.c_str(), srgb ? 1u : 0u);
  }
  return view;
}

bool UploadScene(const CpuScene& scene) {
  const uint64_t start = GetTickCount64();
  // Weapon transitions are intentionally disabled for the armor-only
  // portrait.  Drop both current and retained transition meshes on every
  // scene replacement so an old hand/back/hip attachment cannot survive a
  // live update from a previous build.
  ReleaseMeshBuffers(g_gpu.transition_old_weapons);
  ReleaseMeshBuffers(g_gpu.meshes);
  g_gpu.animation_start_ms = 0;
  for (const CpuMesh& source : scene.meshes) {
    if (source.vertices.empty() || source.indices.empty()) continue;
    GpuMesh mesh;
    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = static_cast<UINT>(source.vertices.size() * sizeof(CpuVertex));
    vb.Usage = D3D11_USAGE_IMMUTABLE;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vdata{source.vertices.data(), 0, 0};
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth = static_cast<UINT>(source.indices.size() * sizeof(uint32_t));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA idata{source.indices.data(), 0, 0};
    if (FAILED(g_gpu.device->CreateBuffer(&vb, &vdata, &mesh.vertices)) ||
        FAILED(g_gpu.device->CreateBuffer(&ib, &idata, &mesh.indices))) {
      ReleaseCom(mesh.vertices);
      ReleaseCom(mesh.indices);
      continue;
    }
    mesh.diffuse = GpuTexture(source.diffuse, true);
    mesh.normal = GpuTexture(source.normal, false);
    mesh.glow = GpuTexture(source.glow, true);
    mesh.environment_mask = GpuTexture(source.environment_mask, false);
    mesh.index_count = static_cast<uint32_t>(source.indices.size());
    std::memcpy(mesh.tint, source.tint, sizeof(mesh.tint));
    std::memcpy(mesh.material, source.material, sizeof(mesh.material));
    mesh.flags[0] = source.model_space_normal ? 1.0f : 0.0f;
    mesh.flags[1] = source.skin_material ? 1.0f : 0.0f;
    mesh.double_sided = source.double_sided;
    mesh.kind = source.kind;
    g_gpu.meshes.push_back(mesh);
  }
  g_gpu.old_right_weapon = 0;
  g_gpu.old_left_weapon = 0;
  g_gpu.new_right_weapon = 0;
  g_gpu.new_left_weapon = 0;
  g_gpu.scene_revision = scene.revision;
  g_gpu.world_epoch = scene.world_epoch;
  g_gpu.last_render_ms = 0;
  g_gpu.bounds[0] = scene.min_x;
  g_gpu.bounds[1] = scene.min_y;
  g_gpu.bounds[2] = scene.min_z;
  g_gpu.bounds[3] = scene.max_x;
  g_gpu.bounds[4] = scene.max_y;
  g_gpu.bounds[5] = scene.max_z;
  g_gpu.character_bounds[0] = scene.character_min_x;
  g_gpu.character_bounds[1] = scene.character_min_y;
  g_gpu.character_bounds[2] = scene.character_min_z;
  g_gpu.character_bounds[3] = scene.character_max_x;
  g_gpu.character_bounds[4] = scene.character_max_y;
  g_gpu.character_bounds[5] = scene.character_max_z;
  std::memcpy(g_gpu.right_arm_pivot, scene.right_arm_pivot,
              sizeof(g_gpu.right_arm_pivot));
  std::memcpy(g_gpu.left_arm_pivot, scene.left_arm_pivot,
              sizeof(g_gpu.left_arm_pivot));
  Log("[SKYRIM-PREVIEW-PERF]", "mesh_upload_ms=%llu meshes=%zu revision=%llu",
      static_cast<unsigned long long>(GetTickCount64() - start),
      g_gpu.meshes.size(), static_cast<unsigned long long>(scene.revision));
  return !g_gpu.meshes.empty();
}

struct Matrix4 {
  float m[16] = {};
};

Matrix4 Multiply(const Matrix4& a, const Matrix4& b) {
  Matrix4 out{};
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      for (int k = 0; k < 4; ++k)
        out.m[row * 4 + col] += a.m[row * 4 + k] * b.m[k * 4 + col];
  return out;
}

Matrix4 PreviewMvp(float rotation_degrees, float zoom) {
  const float min_x = g_gpu.character_bounds[0];
  const float min_y = g_gpu.character_bounds[1];
  const float min_z = g_gpu.character_bounds[2];
  const float max_x = g_gpu.character_bounds[3];
  const float max_y = g_gpu.character_bounds[4];
  const float max_z = g_gpu.character_bounds[5];
  const float center_x =
      (g_gpu.character_bounds[0] + g_gpu.character_bounds[3]) * 0.5f;
  const float center_y =
      (g_gpu.character_bounds[1] + g_gpu.character_bounds[4]) * 0.5f;
  const float center_z =
      (g_gpu.character_bounds[2] + g_gpu.character_bounds[5]) * 0.5f;
  const float height = std::max(1.0f, max_z - min_z);
  const float width = std::max(1.0f, std::max(max_x - min_x, max_y - min_y));
  const float panel_aspect = static_cast<float>(kRenderWidth) / kRenderHeight;
  // Fit the character itself to the portrait. Long weapons no longer force a
  // full-body model down to thumbnail size; the relaxed arm/weapon pose keeps
  // held equipment naturally inside the same character framing.
  // Use the complete portrait height, retaining only a one-percent silhouette
  // guard. Long weapons are excluded from these framing bounds.
  const float half_height = std::max(height * 0.505f,
                                     width / panel_aspect * 0.505f) /
                              std::clamp(zoom, 0.65f, 2.4f);
  const float half_width = half_height * panel_aspect;
  const float angle = (rotation_degrees + kNeutralFacingYawDegrees) *
                      3.1415926535f / 180.0f;
  const float c = std::cos(angle), s = std::sin(angle);
  Matrix4 world{{c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0,
                 -center_x * c + center_y * s,
                 -center_x * s - center_y * c, -center_z, 1}};
  // View along +Y with Z up: x is screen X, z is screen Y, y is depth.
  Matrix4 view{{1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, width * 2.0f, 1}};
  Matrix4 projection{{1.0f / half_width, 0, 0, 0,
                      0, 1.0f / half_height, 0, 0,
                      0, 0, 1.0f / (width * 6.0f), 0,
                      0, 0, 0.5f, 1}};
  return Multiply(Multiply(world, view), projection);
}

struct ShaderConstants {
  Matrix4 mvp;
  float tint[4];
  float bounds[4];
  float animation[4];
  float material[4];
  float flags[4];
};
static_assert(sizeof(ShaderConstants) == 144);

void RetireReadbacks() {
  for (ReadbackSlot& slot : g_gpu.readbacks) {
    if (!slot.pending) continue;
    if (g_gpu.immediate->GetData(slot.complete, nullptr, 0, 0) != S_OK)
      continue;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(g_gpu.immediate->Map(slot.staging, 0, D3D11_MAP_READ, 0,
                                       &mapped))) {
      if (g_submit && slot.world_epoch == g_gpu.world_epoch &&
          slot.world_epoch ==
              g_requested_reset_epoch.load(std::memory_order_acquire))
        g_submit(kRenderWidth, kRenderHeight,
                 static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch,
                 slot.revision);
      g_gpu.immediate->Unmap(slot.staging, 0);
    }
    slot.pending = false;
  }
}

bool RenderFrame(const CharacterPreviewSurface& surface, uint64_t now) {
  ReadbackSlot& slot = g_gpu.readbacks[g_gpu.next_readback];
  if (slot.pending) return false;
  g_gpu.next_readback = (g_gpu.next_readback + 1) % g_gpu.readbacks.size();
  const uint64_t cpu_start = GetTickCount64();
  const float clear[4] = {0, 0, 0, 0};
  g_gpu.deferred->ClearRenderTargetView(g_gpu.target_view, clear);
  g_gpu.deferred->ClearDepthStencilView(g_gpu.depth_view,
                                        D3D11_CLEAR_DEPTH, 1.0f, 0);
  g_gpu.deferred->OMSetRenderTargets(1, &g_gpu.target_view, g_gpu.depth_view);
  D3D11_VIEWPORT viewport{0, 0, static_cast<float>(kRenderWidth),
                          static_cast<float>(kRenderHeight), 0, 1};
  g_gpu.deferred->RSSetViewports(1, &viewport);
  g_gpu.deferred->OMSetDepthStencilState(g_gpu.depth_state, 0);
  const float blend_factor[4] = {0, 0, 0, 0};
  g_gpu.deferred->OMSetBlendState(g_gpu.blend, blend_factor, 0xFFFFFFFFu);
  g_gpu.deferred->IASetInputLayout(g_gpu.input_layout);
  g_gpu.deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_gpu.deferred->VSSetShader(g_gpu.vertex_shader, nullptr, 0);
  g_gpu.deferred->PSSetShader(g_gpu.pixel_shader, nullptr, 0);
  g_gpu.deferred->VSSetConstantBuffers(0, 1, &g_gpu.constants);
  g_gpu.deferred->PSSetConstantBuffers(0, 1, &g_gpu.constants);
  g_gpu.deferred->PSSetSamplers(0, 1, &g_gpu.sampler);
  ShaderConstants constants{};
  constants.mvp = PreviewMvp(surface.rotation_degrees, surface.zoom);
  constants.bounds[0] = g_gpu.right_arm_pivot[0];
  constants.bounds[1] = g_gpu.left_arm_pivot[0];
  constants.bounds[2] =
      (g_gpu.right_arm_pivot[1] + g_gpu.left_arm_pivot[1]) * 0.5f;
  constants.bounds[3] =
      (g_gpu.right_arm_pivot[2] + g_gpu.left_arm_pivot[2]) * 0.5f;
  constants.animation[0] = 0.0f;
  constants.animation[1] = 0.0f;
  constants.animation[2] =
      (surface.rotation_degrees + kNeutralFacingYawDegrees) *
      3.1415926535f / 180.0f;
  auto draw_mesh = [&](const GpuMesh& mesh) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_gpu.deferred->Map(g_gpu.constants, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      return;
    std::memcpy(constants.tint, mesh.tint, sizeof(constants.tint));
    std::memcpy(constants.material, mesh.material, sizeof(constants.material));
    std::memcpy(constants.flags, mesh.flags, sizeof(constants.flags));
    constants.animation[3] =
        (mesh.kind == ComponentKind::kHead ||
         mesh.kind == ComponentKind::kEyes) ? 1.0f : 0.0f;
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    g_gpu.deferred->Unmap(g_gpu.constants, 0);
    const UINT stride = sizeof(CpuVertex), offset = 0;
    g_gpu.deferred->IASetVertexBuffers(0, 1, &mesh.vertices, &stride, &offset);
    g_gpu.deferred->IASetIndexBuffer(mesh.indices, DXGI_FORMAT_R32_UINT, 0);
    g_gpu.deferred->RSSetState(mesh.double_sided ? g_gpu.cull_none
                                                 : g_gpu.cull_back);
    ID3D11ShaderResourceView* views[4] = {
        mesh.diffuse ? mesh.diffuse : g_gpu.white,
        mesh.normal ? mesh.normal : g_gpu.flat_normal,
        mesh.glow ? mesh.glow : g_gpu.black,
        mesh.environment_mask ? mesh.environment_mask : g_gpu.black};
    g_gpu.deferred->PSSetShaderResources(0, 4, views);
    g_gpu.deferred->DrawIndexed(mesh.index_count, 0, 0);
  };
  for (const GpuMesh& mesh : g_gpu.meshes) {
    // Final submission guard: even a stale GPU scene from an older manifest
    // cannot draw weapons or shields into the armor-only portrait.
    if (IsWeaponKind(mesh.kind)) continue;
    draw_mesh(mesh);
  }
  ID3D11CommandList* commands = nullptr;
  if (FAILED(g_gpu.deferred->FinishCommandList(FALSE, &commands)) || !commands)
    return false;
  g_gpu.immediate->ExecuteCommandList(commands, TRUE);
  commands->Release();
  g_gpu.immediate->CopyResource(slot.staging, g_gpu.target);
  g_gpu.immediate->End(slot.complete);
  slot.pending = true;
  slot.revision = ++g_gpu.frame_revision;
  slot.world_epoch = g_gpu.world_epoch;
  g_gpu.rendered_control_revision = surface.control_revision;
  g_gpu.last_render_ms = now;
  if (now - g_gpu.last_perf_log_ms >= kStaticPerfLogIntervalMs) {
    g_gpu.last_perf_log_ms = now;
    Log("[SKYRIM-PREVIEW-PERF]",
        "draw_submit_cpu_ms=%llu meshes=%zu animated=%u revision=%llu",
        static_cast<unsigned long long>(GetTickCount64() - cpu_start),
        g_gpu.meshes.size(), g_gpu.animation_start_ms ? 1u : 0u,
        static_cast<unsigned long long>(g_gpu.scene_revision));
  }
  return true;
}

}  // namespace

void Initialize(uintptr_t skyrim_base, SubmitCharacterPreviewFn submit,
                LogLineFn log_line) {
  g_skyrim_base = skyrim_base;
  g_submit = submit;
  g_log_line = log_line;
  if (g_running.exchange(true, std::memory_order_acq_rel)) return;
  g_work_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_work_event) {
    g_running.store(false, std::memory_order_release);
    Log("[SKYRIM-PREVIEW]", "worker event creation failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return;
  }
  g_worker = CreateThread(nullptr, 0, &WorkerMain, nullptr, 0, nullptr);
  if (!g_worker) {
    CloseHandle(g_work_event);
    g_work_event = nullptr;
    g_running.store(false, std::memory_order_release);
    return;
  }
  SetThreadPriority(g_worker, THREAD_PRIORITY_BELOW_NORMAL);
  Log("[SKYRIM-PREVIEW]",
      "custom renderer initialized; actor clone and gameplay-camera paths remain disabled");
}

void PublishManifest(const PlayerManifest& manifest) {
  if (!g_running.load(std::memory_order_acquire) || !manifest.revision ||
      !manifest.world_epoch || !manifest.skeleton_path[0])
    return;
  g_requested_reset_epoch.store(manifest.world_epoch,
                                std::memory_order_release);
  AcquireSRWLockExclusive(&g_manifest_lock);
  if (manifest.revision == g_pending_manifest.revision &&
      manifest.world_epoch == g_pending_manifest.world_epoch) {
    ReleaseSRWLockExclusive(&g_manifest_lock);
    return;
  }
  g_pending_manifest = manifest;
  g_last_published_revision = manifest.revision;
  ReleaseSRWLockExclusive(&g_manifest_lock);
  g_build_pending.store(true, std::memory_order_release);
  SetEvent(g_work_event);
  Log("[SKYRIM-PREVIEW-EQUIP]",
      "manifest revision=%llu epoch=%llu components=%u race=%08X sex=%u weight=%.1f",
      static_cast<unsigned long long>(manifest.revision),
      static_cast<unsigned long long>(manifest.world_epoch),
      manifest.component_count, manifest.race_form_id, manifest.sex,
      manifest.weight);
}

void ResetWorld(uint64_t world_epoch) {
  AcquireSRWLockExclusive(&g_manifest_lock);
  g_pending_manifest = {};
  g_pending_manifest.world_epoch = world_epoch;
  ReleaseSRWLockExclusive(&g_manifest_lock);
  g_build_pending.store(false, std::memory_order_release);
  AcquireSRWLockExclusive(&g_scene_lock);
  g_ready_scene.reset();
  ReleaseSRWLockExclusive(&g_scene_lock);
  // GPU objects are owned by Present. Publish only a reset generation here so
  // SKSE lifecycle callbacks can never race buffer release or a readback map.
  g_requested_reset_epoch.store(world_epoch, std::memory_order_release);
  Log("[SKYRIM-PREVIEW]", "world reset epoch=%llu",
      static_cast<unsigned long long>(world_epoch));
}

bool HasPendingWork() {
  if (g_build_pending.load(std::memory_order_acquire)) return true;
  AcquireSRWLockShared(&g_scene_lock);
  const bool new_scene = g_ready_scene &&
                         g_ready_scene->revision != g_gpu.scene_revision;
  ReleaseSRWLockShared(&g_scene_lock);
  bool readback = false;
  for (const ReadbackSlot& slot : g_gpu.readbacks)
    readback |= slot.pending;
  return new_scene || g_gpu.animation_start_ms != 0 || readback;
}

bool NeedsPresent(const CharacterPreviewSurface& surface) {
  return surface.visible &&
         (HasPendingWork() || !g_gpu.scene_revision ||
          surface.control_revision != g_gpu.rendered_control_revision);
}

void RenderPresent(IDXGISwapChain* chain,
                   const CharacterPreviewSurface& surface) {
  if (!surface.visible || surface.width <= 0 || surface.height <= 0 || !chain)
    return;
  if (!CreateGpuState(chain)) return;
  const uint64_t reset_epoch =
      g_requested_reset_epoch.load(std::memory_order_acquire);
  if (reset_epoch && reset_epoch != g_gpu.world_epoch) {
    ReleaseGpuMeshes();
    g_gpu.scene_revision = 0;
    g_gpu.world_epoch = reset_epoch;
    g_gpu.animation_start_ms = 0;
    g_gpu.last_render_ms = 0;
  }
  RetireReadbacks();
  std::shared_ptr<CpuScene> scene;
  AcquireSRWLockShared(&g_scene_lock);
  scene = g_ready_scene;
  ReleaseSRWLockShared(&g_scene_lock);
  if (scene && scene->revision != g_gpu.scene_revision) UploadScene(*scene);
  if (g_gpu.meshes.empty()) return;
  const uint64_t now = GetTickCount64();
  const bool animation = g_gpu.animation_start_ms != 0;
  const bool control_changed =
      surface.control_revision != g_gpu.rendered_control_revision;
  const bool scene_changed = scene && scene->revision == g_gpu.scene_revision &&
                             g_gpu.last_render_ms == 0;
  if (!scene_changed && !control_changed && !animation) return;
  if (animation && now - g_gpu.last_render_ms < kAnimationFrameMs) return;
  RenderFrame(surface, now);
}

void Shutdown() {
  if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
  if (g_work_event) SetEvent(g_work_event);
  if (g_worker) {
    WaitForSingleObject(g_worker, 2000);
    CloseHandle(g_worker);
    g_worker = nullptr;
  }
  if (g_work_event) {
    CloseHandle(g_work_event);
    g_work_event = nullptr;
  }
  ReleaseGpu();
}

}  // namespace dis::player_preview
