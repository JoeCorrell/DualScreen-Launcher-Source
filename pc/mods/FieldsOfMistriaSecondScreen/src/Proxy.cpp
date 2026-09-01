#include <windows.h>

#include "Mod.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    fomds::StartModAsync(instance);
  } else if (reason == DLL_PROCESS_DETACH) {
    // Never wait while the loader lock is held. The panel observes this request
    // on its own message thread; process teardown remains bounded by the OS.
    fomds::RequestModStop();
  }
  return TRUE;
}
