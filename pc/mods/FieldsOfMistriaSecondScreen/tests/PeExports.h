#pragma once

#include <windows.h>

#include <cstdint>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

struct PeExport {
  std::string name;
  std::uint32_t ordinal = 0;
  std::string forwarder;
};

inline bool ReadPeExports(const wchar_t* path, std::vector<PeExport>* output,
                          std::string* error) {
  if (!path || !output) return false;
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    if (error) *error = "could not open PE";
    return false;
  }
  const std::streamoff length = stream.tellg();
  if (length <= 0 || length > 512ll * 1024ll * 1024ll) {
    if (error) *error = "implausible PE size";
    return false;
  }
  stream.seekg(0);
  std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
  stream.read(reinterpret_cast<char*>(bytes.data()), length);
  if (!stream) {
    if (error) *error = "truncated PE read";
    return false;
  }
  auto range = [&](std::size_t at, std::size_t count) {
    return at <= bytes.size() && count <= bytes.size() - at;
  };
  if (!range(0, sizeof(IMAGE_DOS_HEADER))) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const std::size_t pe = static_cast<std::size_t>(dos->e_lfanew);
  if (!range(pe, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER))) return false;
  const DWORD signature = *reinterpret_cast<const DWORD*>(bytes.data() + pe);
  const auto* file = reinterpret_cast<const IMAGE_FILE_HEADER*>(
      bytes.data() + pe + sizeof(DWORD));
  if (signature != IMAGE_NT_SIGNATURE ||
      file->Machine != IMAGE_FILE_MACHINE_AMD64 ||
      file->NumberOfSections == 0 || file->NumberOfSections > 96) {
    return false;
  }
  const std::size_t optional_at = pe + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
  if (!range(optional_at, file->SizeOfOptionalHeader) ||
      file->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) {
    return false;
  }
  const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(
      bytes.data() + optional_at);
  if (optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
    return false;
  }
  const std::size_t sections_at = optional_at + file->SizeOfOptionalHeader;
  if (!range(sections_at,
             static_cast<std::size_t>(file->NumberOfSections) *
                 sizeof(IMAGE_SECTION_HEADER))) {
    return false;
  }
  const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
      bytes.data() + sections_at);
  auto rva_to_offset = [&](std::uint32_t rva, std::size_t count,
                           std::size_t* offset) {
    if (!offset) return false;
    if (rva < optional->SizeOfHeaders) {
      *offset = rva;
      return range(*offset, count);
    }
    for (unsigned index = 0; index < file->NumberOfSections; ++index) {
      const std::uint32_t start = sections[index].VirtualAddress;
      const std::uint32_t span =
          (std::max)(sections[index].Misc.VirtualSize,
                     sections[index].SizeOfRawData);
      if (rva < start || rva - start >= span) continue;
      const std::uint64_t candidate =
          static_cast<std::uint64_t>(sections[index].PointerToRawData) +
          (rva - start);
      if (candidate > static_cast<std::uint64_t>(SIZE_MAX)) return false;
      *offset = static_cast<std::size_t>(candidate);
      return range(*offset, count);
    }
    return false;
  };
  auto string_at_rva = [&](std::uint32_t rva, std::string* value) {
    std::size_t at = 0;
    if (!value || !rva_to_offset(rva, 1, &at)) return false;
    std::size_t end = at;
    while (end < bytes.size() && bytes[end] != 0 && end - at < 4096) ++end;
    if (end == bytes.size() || bytes[end] != 0) return false;
    value->assign(reinterpret_cast<const char*>(bytes.data() + at), end - at);
    return true;
  };

  const IMAGE_DATA_DIRECTORY directory =
      optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  std::size_t export_at = 0;
  if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
      !rva_to_offset(directory.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY),
                     &export_at)) {
    return false;
  }
  const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
      bytes.data() + export_at);
  if (!exports->NumberOfFunctions ||
      exports->NumberOfNames != exports->NumberOfFunctions ||
      exports->NumberOfFunctions > 16384) {
    if (error) *error = "export table is sparse or implausible";
    return false;
  }
  std::size_t functions_at = 0;
  std::size_t names_at = 0;
  std::size_t ordinals_at = 0;
  if (!rva_to_offset(exports->AddressOfFunctions,
                     exports->NumberOfFunctions * sizeof(std::uint32_t),
                     &functions_at) ||
      !rva_to_offset(exports->AddressOfNames,
                     exports->NumberOfNames * sizeof(std::uint32_t),
                     &names_at) ||
      !rva_to_offset(exports->AddressOfNameOrdinals,
                     exports->NumberOfNames * sizeof(std::uint16_t),
                     &ordinals_at)) {
    return false;
  }
  const auto* functions = reinterpret_cast<const std::uint32_t*>(
      bytes.data() + functions_at);
  const auto* names = reinterpret_cast<const std::uint32_t*>(bytes.data() + names_at);
  const auto* ordinals = reinterpret_cast<const std::uint16_t*>(
      bytes.data() + ordinals_at);
  output->clear();
  output->reserve(exports->NumberOfNames);
  for (std::uint32_t index = 0; index < exports->NumberOfNames; ++index) {
    if (ordinals[index] >= exports->NumberOfFunctions) return false;
    PeExport entry;
    if (!string_at_rva(names[index], &entry.name) || entry.name.empty()) return false;
    entry.ordinal = exports->Base + ordinals[index];
    const std::uint32_t function = functions[ordinals[index]];
    const std::uint64_t export_end =
        static_cast<std::uint64_t>(directory.VirtualAddress) + directory.Size;
    if (function >= directory.VirtualAddress && function < export_end) {
      if (!string_at_rva(function, &entry.forwarder) || entry.forwarder.empty())
        return false;
    }
    output->push_back(std::move(entry));
  }
  return true;
}
