#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include <vector>

#include "NifFile.hpp"

namespace {

constexpr int kGridSize = 385;
constexpr float kWorldMin = -393216.0f;
constexpr float kWorldMax = 393216.0f;
constexpr float kWorldSpan = kWorldMax - kWorldMin;

#pragma pack(push, 1)
struct Header {
  char magic[4] = {'S', 'D', 'S', 'H'};
  uint32_t version = 1;
  uint32_t width = kGridSize;
  uint32_t height = kGridSize;
  float world_min = kWorldMin;
  float world_max = kWorldMax;
  float minimum_height = 0.0f;
  float maximum_height = 0.0f;
};
#pragma pack(pop)

bool ParseTile(const std::filesystem::path& path, int* x, int* y) {
  static const std::regex pattern(
      R"(^tamriel\.16\.(-?\d+)\.(-?\d+)\.btr$)",
      std::regex::icase);
  std::smatch match;
  const std::string name = path.filename().string();
  if (!std::regex_match(name, match, pattern)) return false;
  *x = std::stoi(match[1].str());
  *y = std::stoi(match[2].str());
  return true;
}

int GridCoordinate(float world) {
  return std::clamp(static_cast<int>(std::lround(
                        (world - kWorldMin) / kWorldSpan * (kGridSize - 1))),
                    0, kGridSize - 1);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: Build-TerrainHeight <btr-directory> <output>\n");
    return 2;
  }
  const std::filesystem::path directory(argv[1]);
  const std::filesystem::path output(argv[2]);
  std::vector<float> heights(static_cast<size_t>(kGridSize) * kGridSize,
                             -std::numeric_limits<float>::infinity());
  unsigned files = 0;
  uint64_t vertices_read = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    int tile_x = 0;
    int tile_y = 0;
    if (!ParseTile(entry.path(), &tile_x, &tile_y)) continue;
    nifly::NifLoadOptions options;
    options.isTerrain = true;
    nifly::NifFile file(entry.path(), options);
    if (!file.IsValid()) {
      std::fprintf(stderr, "invalid terrain: %s\n",
                   entry.path().string().c_str());
      continue;
    }
    ++files;
    for (nifly::NiShape* shape : file.GetShapes()) {
      std::vector<nifly::Vector3> vertices;
      if (!file.GetVertsForShape(shape, vertices)) continue;
      nifly::MatTransform transform = shape->GetTransformToParent();
      for (nifly::NiNode* parent = file.GetParentNode(shape); parent;
           parent = file.GetParentNode(parent)) {
        transform = parent->GetTransformToParent().ComposeTransforms(transform);
      }
      for (const auto& local : vertices) {
        const auto point = transform.ApplyTransform(local);
        const float world_x = static_cast<float>(tile_x) * 4096.0f + point.x;
        const float world_y = static_cast<float>(tile_y) * 4096.0f + point.y;
        if (!std::isfinite(world_x) || !std::isfinite(world_y) ||
            !std::isfinite(point.z) || world_x < kWorldMin - 1.0f ||
            world_x > kWorldMax + 1.0f || world_y < kWorldMin - 1.0f ||
            world_y > kWorldMax + 1.0f) {
          continue;
        }
        const int gx = GridCoordinate(world_x);
        const int gy = GridCoordinate(world_y);
        float& destination = heights[static_cast<size_t>(gy) * kGridSize + gx];
        destination = std::max(destination, point.z);
        ++vertices_read;
      }
    }
  }
  if (!files || !vertices_read) {
    std::fprintf(stderr, "no Tamriel terrain vertices found\n");
    return 3;
  }

  // The LOD tile edge topology contains a small number of intentionally
  // collapsed vertices. Fill only those absent samples from adjacent authored
  // heights; no smoothing is applied to the real samples.
  std::vector<float> next = heights;
  for (int pass = 0; pass < 16; ++pass) {
    bool changed = false;
    next = heights;
    for (int y = 0; y < kGridSize; ++y) {
      for (int x = 0; x < kGridSize; ++x) {
        const size_t at = static_cast<size_t>(y) * kGridSize + x;
        if (std::isfinite(heights[at])) continue;
        float total = 0.0f;
        int count = 0;
        static constexpr int offsets[4][2] = {
            {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& offset : offsets) {
          const int nx = x + offset[0];
          const int ny = y + offset[1];
          if (nx < 0 || ny < 0 || nx >= kGridSize || ny >= kGridSize) continue;
          const float value = heights[static_cast<size_t>(ny) * kGridSize + nx];
          if (std::isfinite(value)) {
            total += value;
            ++count;
          }
        }
        if (count) {
          next[at] = total / count;
          changed = true;
        }
      }
    }
    heights.swap(next);
    if (!changed) break;
  }

  Header header;
  std::vector<uint16_t> packed(heights.size());
  float minimum = std::numeric_limits<float>::infinity();
  float maximum = -std::numeric_limits<float>::infinity();
  for (float height : heights) {
    if (!std::isfinite(height)) continue;
    minimum = std::min(minimum, height);
    maximum = std::max(maximum, height);
  }
  if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
      maximum - minimum < 1.0f) return 4;
  for (size_t i = 0; i < heights.size(); ++i) {
    const float height = std::isfinite(heights[i]) ? heights[i] : -14000.0f;
    const float normal = std::clamp((height - minimum) / (maximum - minimum),
                                    0.0f, 1.0f);
    packed[i] = static_cast<uint16_t>(std::lround(normal * 65535.0f));
  }
  header.minimum_height = minimum;
  header.maximum_height = maximum;
  std::filesystem::create_directories(output.parent_path());
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
  stream.write(reinterpret_cast<const char*>(packed.data()),
               static_cast<std::streamsize>(packed.size() * sizeof(int16_t)));
  if (!stream) return 4;
  std::printf("wrote %s files=%u vertices=%llu grid=%dx%d height=%d..%d\n",
              output.string().c_str(), files,
              static_cast<unsigned long long>(vertices_read), kGridSize,
              kGridSize, static_cast<int>(std::lround(minimum)),
              static_cast<int>(std::lround(maximum)));
  return 0;
}
