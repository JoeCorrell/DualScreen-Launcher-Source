#pragma once

#include <windows.h>
#include <dxgi.h>

namespace fo4ds::proxy_abi {

using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using DXGIGetDebugInterface1Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using DXGIReportAdapterConfigurationFn = HRESULT(WINAPI*)(void*);
using DXGIDeclareAdapterRemovalSupportFn = HRESULT(WINAPI*)();
using DXGIDisableVBlankVirtualizationFn = HRESULT(WINAPI*)();

// These undocumented D3D10 layering exports are part of DXGI's proxy ABI.
// Keep pointer-shaped parameters pointer-sized: using UINT here truncates
// incoming x64 addresses before they reach the real system DLL.
using DXGID3D10CreateDeviceFn =
    HRESULT(WINAPI*)(HMODULE, void*, void*, UINT, void*, void**);
using DXGID3D10CreateLayeredDeviceFn =
    HRESULT(WINAPI*)(void*, void*, void*, void*, void*);
using DXGID3D10GetLayeredDeviceSizeFn = SIZE_T(WINAPI*)(const void*, UINT);
using DXGID3D10RegisterLayersFn = HRESULT(WINAPI*)(const void*, UINT);

}  // namespace fo4ds::proxy_abi

extern "C" {

HRESULT WINAPI CreateDXGIFactory(REFIID iid, void** output);
HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void** output);
HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid, void** output);
HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid, void** output);
HRESULT WINAPI DXGIReportAdapterConfiguration(void* configuration);
HRESULT WINAPI DXGIDeclareAdapterRemovalSupport();
HRESULT WINAPI DXGIDisableVBlankVirtualization();
HRESULT WINAPI DXGID3D10CreateDevice(HMODULE module, void* factory,
                                     void* adapter, UINT flags,
                                     void* unknown, void** device);
HRESULT WINAPI DXGID3D10CreateLayeredDevice(void* a, void* b, void* c,
                                            void* d, void* e);
SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(const void* layers, UINT count);
HRESULT WINAPI DXGID3D10RegisterLayers(const void* layers, UINT count);

}  // extern "C"
