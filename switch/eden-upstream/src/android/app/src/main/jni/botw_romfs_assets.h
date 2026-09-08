#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "common/common_types.h"

class EmulationSession;

namespace DualScreenRomfsAssets {

std::vector<u8> LoadAsset(EmulationSession& session, std::string_view logical_path);
std::string GetRevision(EmulationSession& session);
void Reset();
uint64_t ExternalFileSize(EmulationSession&, const char* path);
size_t ExternalReadFile(EmulationSession&, const char* path, uint64_t offset, void* output, size_t size);
std::vector<u8> ExternalDecodeTexture(std::span<const u8> source, std::string_view name, bool first);
std::vector<u8> ExternalDecodeRgba(std::span<const u8>, std::string_view, bool, uint32_t&, uint32_t&);
std::vector<u8> ExternalEncodePng(std::span<const u8>, uint32_t, uint32_t);

}
