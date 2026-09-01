#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "NifFile.hpp"

namespace {

nifly::MatTransform ShapeToGlobal(nifly::NifFile& nif,
                                  nifly::NiShape* shape) {
  nifly::MatTransform transform = shape->GetTransformToParent();
  for (nifly::NiNode* parent = nif.GetParentNode(shape); parent;
       parent = nif.GetParentNode(parent)) {
    transform = parent->GetTransformToParent().ComposeTransforms(transform);
  }
  return transform;
}

void PrintTransform(const char* label, const nifly::MatTransform& transform) {
  float yaw = 0.0f;
  float pitch = 0.0f;
  float roll = 0.0f;
  transform.ToEulerDegrees(yaw, pitch, roll);
  std::printf(
      "%s translation=(%.3f %.3f %.3f) scale=%.4f euler=(%.2f %.2f "
      "%.2f) rotation=[%.3f %.3f %.3f; %.3f %.3f %.3f; %.3f %.3f "
      "%.3f]\n",
      label, transform.translation.x, transform.translation.y,
      transform.translation.z, transform.scale, yaw, pitch, roll,
      transform.rotation[0].x, transform.rotation[0].y,
      transform.rotation[0].z, transform.rotation[1].x,
      transform.rotation[1].y, transform.rotation[1].z,
      transform.rotation[2].x, transform.rotation[2].y,
      transform.rotation[2].z);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: Inspect-Character skeleton.nif model...\n");
    return 2;
  }

  nifly::NifFile skeleton_file{std::filesystem::path(argv[1])};
  if (!skeleton_file.IsValid()) {
    std::fprintf(stderr, "invalid skeleton: %s\n", argv[1]);
    return 3;
  }
  std::unordered_map<std::string, nifly::MatTransform> skeleton;
  std::vector<nifly::NiObject*> tree;
  skeleton_file.GetTree(tree);
  for (nifly::NiObject* object : tree) {
    auto* node = dynamic_cast<nifly::NiNode*>(object);
    if (!node || node->name.get().empty()) continue;
    nifly::MatTransform transform;
    if (skeleton_file.GetNodeTransformToGlobal(node->name.get(), transform))
      skeleton[node->name.get()] = transform;
  }
  std::printf("skeleton=%s bones=%zu\n", argv[1], skeleton.size());

  for (int argument = 2; argument < argc; ++argument) {
    nifly::NifFile nif{std::filesystem::path(argv[argument])};
    if (!nif.IsValid()) {
      std::printf("model=%s invalid=1\n", argv[argument]);
      continue;
    }
    const auto shapes = nif.GetShapes();
    std::printf("model=%s shapes=%zu\n", argv[argument], shapes.size());
    for (size_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
      nifly::NiShape* shape = shapes[shape_index];
      const auto* vertices = nif.GetVertsForShape(shape);
      if (!shape || !vertices || vertices->empty()) continue;
      const auto shape_to_global = ShapeToGlobal(nif, shape);
      std::vector<std::string> bones;
      nif.GetShapeBoneList(shape, bones);
      std::vector<nifly::MatTransform> skin_to_bone(bones.size());
      std::vector<std::unordered_map<uint16_t, float>> weights(bones.size());
      for (uint32_t bone = 0; bone < bones.size(); ++bone) {
        nif.GetShapeTransformSkinToBone(shape, bone, skin_to_bone[bone]);
        nif.GetShapeBoneWeights(shape, bone, weights[bone]);
      }

      nifly::Vector3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
      nifly::Vector3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
      nifly::Vector3 centroid;
      centroid.Zero();
      size_t weighted_vertices = 0;
      for (size_t vertex_index = 0; vertex_index < vertices->size();
           ++vertex_index) {
        nifly::Vector3 point;
        point.Zero();
        float total = 0.0f;
        if (shape->IsSkinned()) {
          for (uint32_t bone = 0; bone < bones.size(); ++bone) {
            const auto weight = weights[bone].find(
                static_cast<uint16_t>(vertex_index));
            if (weight == weights[bone].end() || weight->second <= 0.0f)
              continue;
            const auto skeleton_bone = skeleton.find(bones[bone]);
            if (skeleton_bone == skeleton.end()) continue;
            const auto transform = skeleton_bone->second.ComposeTransforms(
                skin_to_bone[bone]);
            point += transform.ApplyTransform((*vertices)[vertex_index]) *
                     weight->second;
            total += weight->second;
          }
        }
        if (total <= 0.0001f) {
          point = shape_to_global.ApplyTransform((*vertices)[vertex_index]);
        } else {
          ++weighted_vertices;
          if (std::abs(total - 1.0f) > 0.001f) point /= total;
        }
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
        centroid += point;
      }
      centroid /= static_cast<float>(vertices->size());
      std::printf(
          " shape=%zu name=\"%s\" skinned=%u bones=%zu weighted=%zu/%zu "
          "bounds=(%.3f %.3f %.3f)..(%.3f %.3f %.3f) centroid=(%.3f "
          "%.3f %.3f)\n",
          shape_index, shape->name.get().c_str(), shape->IsSkinned() ? 1u : 0u,
          bones.size(), weighted_vertices, vertices->size(), minimum.x,
          minimum.y, minimum.z, maximum.x, maximum.y, maximum.z, centroid.x,
          centroid.y, centroid.z);
      PrintTransform("  shape_global", shape_to_global);
      for (uint32_t bone = 0; bone < bones.size(); ++bone) {
        std::printf("  bone=%u name=\"%s\" weights=%zu resolved=%u\n", bone,
                    bones[bone].c_str(), weights[bone].size(),
                    skeleton.count(bones[bone]) ? 1u : 0u);
        PrintTransform("   skin_to_bone", skin_to_bone[bone]);
        const auto found = skeleton.find(bones[bone]);
        if (found != skeleton.end()) PrintTransform("   skeleton", found->second);
      }
    }
  }
  return 0;
}
