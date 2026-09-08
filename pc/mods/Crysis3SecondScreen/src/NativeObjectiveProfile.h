#pragma once
#include <cstddef>
#include <cstdint>
namespace c3native {
struct NativeObjectiveFingerprint { uint32_t address; size_t size; const char* sha256; };
// Exact private 1.2.1.1000 capture at image base 0x37000000. The host must
// verify the executable profile and ALL spans before enabling this reader.
// These are read-contract guards, never callable hooks. The 1024-byte Lua
// registration span is a bounded code block, not an asserted function extent.
inline constexpr NativeObjectiveFingerprint NativeObjectiveProfile[]{
    {0x3749cb50,7,"7e42d22dd5a41ef8347942880cde9b3fe6c6b133d345e859768c7341aa561f2d"}, // framework +4ac
    {0x378a6cb0,4,"0b78473b3a56c63ff2a64431fb600078066cbe407d3dee7ea8fd7575bd7f82d5"}, // system +1c
    {0x37221910,4,"4181cb1fec090d6487f323c8ce84a17ff37429613d7843b4f5f40f5eae266b77"}, // embedded info
    {0x374c4970,4,"f7e0208f5e988ae39c7a45dfddbd12921d02f9c3deb5695d4c534c8ab0ccdb0f"}, // name pointer
    {0x378b9070,4,"916ae10aa3fd76a4d5b25b2fb66e9df0fd04957c7ab10930268d1ed7808be689"}, // path pointer
    {0x383b4e10,1635,"0d9d0f0632b267e14c7947db30fc58ba5634fa1f100a72e7e7d5d8cc2e0900d0"}, // root +104, stride, UI fields
    {0x382c3a30,78,"9a9f4279846ca0e3369adc3c6a430e275a3d86765c506bcf7fbb8f723f4033f6"}, // identity +c lookup
    {0x382c3fa0,260,"15913b2990496dc2df79b21b0418b96758a92b2553fa83675d5abaa0aff2a423"}, // status +1c setter
    {0x382ce210,1024,"b281461010a32bf4a502f52284f7ab4f67c384f20e5f4337b65f0c427e9d1b3c"}, // native status registration
    {0x399bef80,4,"75a3b4bf96000b6ed4529f6b9853d724f4ca6b47b18e1a82976bccc4984238d7"}, // root vtable identity
    {0x398ba650,4,"8fa10faf513efa8c86cc777adda3ef4165a4ac04713b409dffff36d7ef898c22"}, // framework virtual +70
    {0x398868a0,60,"d290daecd0477dfd2055fbec0b692df81eb2a6e016ec8080629a44fdb6215bf6"}, // level-system table
    {0x398862c4,12,"ee2f1209deaa7cc30c470f7a57496ae8bb758782bf7e4b73ff072f1f7c3277d2"}, // level table
    {0x398862d0,12,"60c9d2416f30d8ce729b520bbff6ae85f2b832e1463ef0d41a3814ca4f67d7a0"}, // level-info table
    {0x399f32c0,4,"7b978f11f336e747e71deb3f39f070993ad7c03c259af4fedea59e4fbf5f5d0b"}, // objective-system identity
};
inline constexpr size_t NativeObjectiveProfileCount=sizeof(NativeObjectiveProfile)/sizeof(NativeObjectiveProfile[0]);
} // namespace c3native
