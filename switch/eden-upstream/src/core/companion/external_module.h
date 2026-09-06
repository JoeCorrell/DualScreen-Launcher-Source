// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "core/companion/module_api.h"
namespace ExternalCompanion {
struct Reservation { u32 mailbox_size, bridge_size; };
bool Load(const std::string& path, const DsHostApi& host, u64 title, const std::string& version);
u64 TitleId();
std::optional<Reservation> Probe(const DsNsoInfo& info, const std::string& name);
bool Install(std::vector<u8>& image, size_t start, u32 mailbox, u32 bridge, u32 size);
int Snapshot(void* out, size_t size);
int Action(int operation, int item, int target);
std::vector<u8> Asset(const std::string& path);
}
