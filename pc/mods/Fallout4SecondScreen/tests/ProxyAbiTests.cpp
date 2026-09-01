#include <type_traits>

#include "ProxyExports.h"

using namespace fo4ds::proxy_abi;

static_assert(std::is_same_v<decltype(&CreateDXGIFactory),
                             CreateDXGIFactoryFn>);
static_assert(std::is_same_v<decltype(&CreateDXGIFactory1),
                             CreateDXGIFactoryFn>);
static_assert(std::is_same_v<decltype(&CreateDXGIFactory2),
                             CreateDXGIFactory2Fn>);
static_assert(std::is_same_v<decltype(&DXGIGetDebugInterface1),
                             DXGIGetDebugInterface1Fn>);
static_assert(std::is_same_v<decltype(&DXGIReportAdapterConfiguration),
                             DXGIReportAdapterConfigurationFn>);
static_assert(std::is_same_v<decltype(&DXGIDeclareAdapterRemovalSupport),
                             DXGIDeclareAdapterRemovalSupportFn>);
static_assert(std::is_same_v<decltype(&DXGIDisableVBlankVirtualization),
                             DXGIDisableVBlankVirtualizationFn>);
static_assert(std::is_same_v<decltype(&DXGID3D10CreateDevice),
                             DXGID3D10CreateDeviceFn>);
static_assert(std::is_same_v<decltype(&DXGID3D10CreateLayeredDevice),
                             DXGID3D10CreateLayeredDeviceFn>);
static_assert(std::is_same_v<decltype(&DXGID3D10GetLayeredDeviceSize),
                             DXGID3D10GetLayeredDeviceSizeFn>);
static_assert(std::is_same_v<decltype(&DXGID3D10RegisterLayers),
                             DXGID3D10RegisterLayersFn>);

int main() { return 0; }
