#pragma once

#include <windows.h>

namespace fomds {

bool StartModAsync(HMODULE module);
void RequestModStop();

}  // namespace fomds
