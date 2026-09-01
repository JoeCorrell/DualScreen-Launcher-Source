// The mod's entry points, called from the dxgi proxy.
//
// Named Probe for the file that first held them; the diagnostic pass and the
// mod are the same code path now, because everything the diagnostic learned is
// something the mod has to decide at run time anyway.

#pragma once

struct IUnknown;

namespace dxs {

// Called from DllMain under the loader lock. Records that the module is present
// and does nothing else -- see the comment at the call site.
void ProbeStage();

// The first moment there is something real to hold: a DXGI factory the game has
// just created. Everything downstream hangs off this.
void ProbeOnFactory(IUnknown* factory);

void ProbeShutdown();

}  // namespace dxs
