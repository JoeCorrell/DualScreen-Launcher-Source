#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "NifFile.hpp"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  nifly::NifLoadOptions options;
  options.isTerrain = true;
  nifly::NifFile file(std::filesystem::path(argv[1]), options);
  if (!file.IsValid()) return 3;
  auto shapes = file.GetShapes();
  std::printf("shapes=%zu\n", shapes.size());
  for (size_t index = 0; index < shapes.size(); ++index) {
    nifly::NiShape* shape = shapes[index];
    std::vector<nifly::Vector3> vertices;
    std::vector<nifly::Triangle> triangles;
    if (!file.GetVertsForShape(shape, vertices) ||
        !shape->GetTriangles(triangles)) continue;
    nifly::MatTransform transform = shape->GetTransformToParent();
    for (nifly::NiNode* parent = file.GetParentNode(shape); parent;
         parent = file.GetParentNode(parent)) {
      transform = parent->GetTransformToParent().ComposeTransforms(transform);
    }
    nifly::Vector3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    nifly::Vector3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const auto& local : vertices) {
      const auto point = transform.ApplyTransform(local);
      minimum.x = std::min(minimum.x, point.x);
      minimum.y = std::min(minimum.y, point.y);
      minimum.z = std::min(minimum.z, point.z);
      maximum.x = std::max(maximum.x, point.x);
      maximum.y = std::max(maximum.y, point.y);
      maximum.z = std::max(maximum.z, point.z);
    }
    std::printf("shape=%zu vertices=%zu triangles=%zu bounds="
                "%.2f,%.2f,%.2f..%.2f,%.2f,%.2f\n",
                index, vertices.size(), triangles.size(), minimum.x, minimum.y,
                minimum.z, maximum.x, maximum.y, maximum.z);
  }
  return 0;
}
