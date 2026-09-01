#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../src/F4SEBridgeClient.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
  return condition;
}

template <class T>
T Function(FARPROC address) {
  static_assert(sizeof(T) == sizeof(address));
  T result = nullptr;
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: F4SEBridgeClientTests <provider.dll>\n");
    return 2;
  }
  bool ok = true;

  fo4ds::F4SEBridgeClient absent(L"FO4DS-Definitely-Absent.dll", 1000);
  absent.Poll(1000);
  auto state = absent.Capture();
  ok &= Check(state.availability == fo4ds::F4SEBridgeAvailability::kAbsent &&
                  state.discovery_attempts == 1,
              "absent plugin is non-fatal");
  absent.Poll(1999);
  ok &= Check(absent.Capture().discovery_attempts == 1,
              "absent discovery is bounded");
  absent.Poll(2000);
  ok &= Check(absent.Capture().discovery_attempts == 2,
              "absent discovery retries at the configured interval");

  fo4ds::F4SEBridgeClient no_export(L"kernel32.dll", 1000);
  no_export.Poll(1000);
  state = no_export.Capture();
  ok &= Check(state.availability ==
                      fo4ds::F4SEBridgeAvailability::kMalformed &&
                  state.discovery_attempts == 1,
              "loaded module without bridge export is rejected");
  no_export.Poll(1500);
  ok &= Check(no_export.Capture().discovery_attempts == 1,
              "malformed discovery is bounded");

  HMODULE provider = LoadLibraryA(argv[1]);
  if (!Check(provider != nullptr, "test host loads provider explicitly"))
    return 1;
  using SetModeFn = void (*)(std::uint32_t);
  using AdvanceFn = void (*)();
  const SetModeFn set_mode =
      Function<SetModeFn>(GetProcAddress(provider, "FO4DS_TestSetMode"));
  const AdvanceFn advance = Function<AdvanceFn>(
      GetProcAddress(provider, "FO4DS_TestAdvanceGeneration"));
  ok &= Check(set_mode && advance, "provider controls are exported");

  fo4ds::F4SEBridgeClient client(L"F4SEBridgeTestProvider.dll", 1000);
  set_mode(1);
  client.Poll(1000);
  state = client.Capture();
  ok &= Check(state.availability ==
                      fo4ds::F4SEBridgeAvailability::kMalformed &&
                  state.discovery_attempts == 1,
              "invalid API ABI is rejected");
  set_mode(0);
  client.Poll(1500);
  ok &= Check(client.Capture().discovery_attempts == 1,
              "API rediscovery does not spin before retry deadline");
  client.Poll(2000);
  state = client.Capture();
  ok &= Check(state.availability == fo4ds::F4SEBridgeAvailability::kReady &&
                  state.discovery_attempts == 2 &&
                  state.runtime_version == 0x010B0F00u &&
                  state.f4se_release_index == 33,
              "valid provider publishes lifecycle POD state");

  const std::uint64_t ready_revision = state.revision;
  client.Poll(2001);
  ok &= Check(client.Capture().revision == ready_revision,
              "unchanged snapshots do not create transitions");
  advance();
  client.Poll(2002);
  state = client.Capture();
  ok &= Check(state.lifecycle_generation == 8 &&
                  state.revision > ready_revision,
              "lifecycle generation transition is published");

  set_mode(2);
  client.Poll(2003);
  state = client.Capture();
  ok &= Check(state.availability ==
                  fo4ds::F4SEBridgeAvailability::kMalformed,
              "snapshot copy rejection fails closed");
  set_mode(3);
  client.Poll(3003);
  state = client.Capture();
  ok &= Check(state.availability ==
                  fo4ds::F4SEBridgeAvailability::kMalformed,
              "invalid snapshot header fails closed");
  set_mode(0);
  client.Poll(4003);
  ok &= Check(client.Capture().availability ==
                  fo4ds::F4SEBridgeAvailability::kReady,
              "client recovers after a later valid bounded rediscovery");

  set_mode(4);
  fo4ds::F4SEBridgeClient short_api(L"F4SEBridgeTestProvider.dll", 1000);
  short_api.Poll(5000);
  ok &= Check(short_api.Capture().availability ==
                  fo4ds::F4SEBridgeAvailability::kMalformed,
              "undersized API header is rejected");
  set_mode(5);
  fo4ds::F4SEBridgeClient bad_api_pointer(L"F4SEBridgeTestProvider.dll", 1000);
  bad_api_pointer.Poll(5000);
  ok &= Check(bad_api_pointer.Capture().availability ==
                  fo4ds::F4SEBridgeAvailability::kMalformed,
              "unreadable API pointer is rejected without dereference");
  set_mode(6);
  fo4ds::F4SEBridgeClient null_copy(L"F4SEBridgeTestProvider.dll", 1000);
  null_copy.Poll(5000);
  ok &= Check(null_copy.Capture().availability ==
                  fo4ds::F4SEBridgeAvailability::kMalformed,
              "null snapshot function is rejected");
  set_mode(7);
  fo4ds::F4SEBridgeClient waiting(L"F4SEBridgeTestProvider.dll", 1000);
  waiting.Poll(5000);
  ok &= Check(waiting.Capture().availability ==
                  fo4ds::F4SEBridgeAvailability::kWaitingForPlugin,
              "loaded DLL may wait safely for F4SEPlugin_Load");
  set_mode(0);
  waiting.Poll(5001);
  state = waiting.Capture();
  ok &= Check(state.availability == fo4ds::F4SEBridgeAvailability::kReady &&
                  state.discovery_attempts == 1,
              "bound waiting bridge becomes ready without rediscovery");

  FreeLibrary(provider);
  if (!ok) return 1;
  std::puts("F4SE bridge client tests passed");
  return 0;
}
