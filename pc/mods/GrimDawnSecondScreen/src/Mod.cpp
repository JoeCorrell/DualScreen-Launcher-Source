#include "Mod.h"

#include <cstdio>

#include "BuildIdentity.h"
#include "CharacterRenderer.h"
#include "Compatibility.h"
#include "Config.h"
#include "GameApi.h"
#include "Log.h"
#include "Panel.h"
#include "StateStore.h"

namespace gdds {
namespace {

volatile LONG g_started = 0;
HMODULE g_proxy_module = nullptr;
HMODULE g_game_module = nullptr;
HMODULE g_engine_module = nullptr;
BuildIdentity g_identity = {};
Config g_config = {};

DWORD WINAPI InitializeWorker(void*) {
  const BuildIdentity& identity = g_identity;
  const Config& config = g_config;
  SetSampleInterval(config.sample_interval_ms);
  SetTelemetryTier(config.telemetry_tier);
  Log(LogLevel::kInfo,
      "[MOD] build_tag=2026-08-31-full-texture-source-v3 snapshot_version=%u",
      kSnapshotVersion);
  Log(LogLevel::kInfo,
      "[CONFIG] sample_ms=%lu telemetry_tier=%u panel=%u input_port=%u startup_audio_guard=%u",
      static_cast<unsigned long>(config.sample_interval_ms),
      config.telemetry_tier,
      config.panel_enabled ? 1u : 0u,
      static_cast<unsigned>(config.input_port),
      config.startup_audio_guard ? 1u : 0u);
  ResetState(Lifecycle::kWaitingForGame, identity.label,
             "Waiting for exported Grim Dawn game systems.", true);
  if (config.panel_enabled && !StartPanel(g_proxy_module, config.input_port))
    Log(LogLevel::kError,
        "[PANEL] worker creation failed; telemetry hook will still initialize");

  HMODULE executable = GetModuleHandleW(nullptr);
  HMODULE game = GetModuleHandleW(L"Game.dll");
  HMODULE engine = GetModuleHandleW(L"Engine.dll");
  const BuildIdentity live_identity =
      IdentifyLoadedBuild(executable, game, engine);
  if (game != g_game_module || engine != g_engine_module ||
      !live_identity.supported) {
    ResetState(Lifecycle::kFaulted, identity.label,
               "Loaded module identity changed before hook installation.",
               false);
    Log(LogLevel::kError,
        "[INIT] fail-closed module revalidation game=%p expected_game=%p engine=%p expected_engine=%p supported=%u",
        game, g_game_module, engine, g_engine_module,
        live_identity.supported ? 1u : 0u);
    InterlockedExchange(&g_started, -1);
    return 0;
  }
  const bool api_ready = ResolveGameApi(executable, game, engine);
  const bool renderer_installed =
      api_ready && InstallCharacterRenderer(executable, game, engine);
  const bool game_hooks_installed =
      renderer_installed && InstallGameUpdateHook(executable);
  if (!api_ready || !renderer_installed || !game_hooks_installed) {
    if (game_hooks_installed && !UninstallGameUpdateHook())
      Log(LogLevel::kError, "[INIT] failed to roll back game hooks");
    if (renderer_installed && !UninstallCharacterRenderer())
      Log(LogLevel::kError, "[INIT] failed to roll back render hook");
    ResetState(Lifecycle::kFaulted, identity.label,
               "Exact-build export or update-hook validation failed.", true);
    InterlockedExchange(&g_started, -1);
    return 0;
  }
  Log(LogLevel::kInfo,
      "[INIT] diagnostic runtime and render-tail second-screen scene are active");
  InterlockedExchange(&g_started, 2);
  return 0;
}

}  // namespace

void StartMod(HMODULE proxy_module) {
  if (!proxy_module || InterlockedCompareExchange(&g_started, 1, 0) != 0)
    return;
  g_proxy_module = proxy_module;
  InitializeLog(g_proxy_module);
  HMODULE executable = GetModuleHandleW(nullptr);
  // This runs before the first real DXGI factory is created. Grim Dawn's
  // statically imported game modules must therefore already be present, and
  // the compatibility patch is installed before renderer/audio startup can
  // advance past the factory call.
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    g_game_module = GetModuleHandleW(L"Game.dll");
    g_engine_module = GetModuleHandleW(L"Engine.dll");
    if (g_game_module && g_engine_module) break;
    Sleep(10);
  }
  g_identity =
      IdentifyLoadedBuild(executable, g_game_module, g_engine_module);
  Log(LogLevel::kInfo,
      "[BUILD] label=%s pe=%u i386=%u timestamp=%08lx exe=%llu:%s game=%llu:%s engine=%llu:%s",
      g_identity.label, g_identity.valid_pe ? 1u : 0u,
      g_identity.is_i386 ? 1u : 0u,
      static_cast<unsigned long>(g_identity.timestamp),
      static_cast<unsigned long long>(g_identity.executable.size),
      g_identity.executable.sha256,
      static_cast<unsigned long long>(g_identity.game.size),
      g_identity.game.sha256,
      static_cast<unsigned long long>(g_identity.engine.size),
      g_identity.engine.sha256);
  if (!g_identity.supported) {
    Log(LogLevel::kError,
        "[BUILD] fail-closed: executable/loaded Game/loaded Engine identity does not match installed 1.3.0.7 I386 profile; no patch, window, or hook created");
    InterlockedExchange(&g_started, -1);
    return;
  }
  g_config = LoadConfig(g_proxy_module);
  if (g_config.startup_audio_guard) {
    if (!InstallStartupAudioGuard(g_engine_module)) {
      Log(LogLevel::kError,
          "[COMPAT] exact-build startup guard was requested but could not be installed; refusing telemetry initialization");
      InterlockedExchange(&g_started, -1);
      return;
    }
  }
  HANDLE worker = CreateThread(nullptr, 0, InitializeWorker, nullptr, 0, nullptr);
  if (!worker) {
    Log(LogLevel::kError,
        "[INIT] worker creation failed error=%lu; compatibility guard remains installed",
        static_cast<unsigned long>(GetLastError()));
    // Do not retry startup initialization: the guard has already changed the
    // exact Engine image and a second installation must fail its byte gate.
    InterlockedExchange(&g_started, -1);
    return;
  }
  CloseHandle(worker);
}

}  // namespace gdds
