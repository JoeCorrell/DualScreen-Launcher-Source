#pragma once
#include <cstddef>
#include <cstdint>
namespace c3native {
struct SuitFingerprint { uint32_t address; size_t size; const char* sha256; };
// Exact loaded-image profile; also require ACTOR-CONTRACT's source/header/actor
// gates. At most4096 bytes per fingerprint. No callable ABI is granted.
inline constexpr SuitFingerprint NativeSuitProfile[] = {
    {0x3818bc80,76,"9dc3a7db338feb1f926d055333befaa84d8ee58247deb8ce75c483268326a287"},
    {0x3841d8d0,99,"a09746d3af2b2d6f3c14b2abe7985afeae422ecfa218e6aaf08b1a4e86229d42"},
    {0x38594300,119,"6fdcb6a8b7dd59684ed9b245b4eb61e8c27fe903a9073f0dbcaa709ff4a65ba0"},
    {0x385941a0,144,"0a443ee41fb47d52ea6916cf86d6285cba84a9eff7ccce8ee1d63fd775d0014b"},
    {0x38592570,80,"435a0fc8c5e47dba95e82828fb5343ed9b4fd7931baaf58240249c34ddc82304"},
    {0x38592b60,119,"f7fd0858bd1a59ac3b43330329caae30cea9dcd52c8106988fc36fffa762bd91"},
    {0x383b6c00,4096,"da982003eec5de1e34fae5291414eb68dcf7784237127a9a5ccc4be2dd9c91b7"},
    {0x383b7c00,3008,"ea7c60063c166a6b783e6498b287bc682407110506fbad56d5f8d553ece32163"},
    {0x3840cfb0,16,"afc60669d2290c7819f906621fdf6e8ef40151221a9fe484a85a09316d2fabd3"},
    {0x38592830,64,"84dd737dcf49f38c39dd90c0e31dd23bfae67f87b32dc285b1f1137bbee68698"},
    {0x385925c0,96,"d890b7c9fdafbb0eaff847f26726f4fcb5453a7f76547752b4aa1d29726afc5a"},
    {0x3863ee00,64,"5e748c9b9254b90fdd97185cdb0bab7a3cf904a3f6e28cd20d283a40262d0103"},
    {0x39a45a1c,16,"1cedfd034eb377e7dfa421ce1937608591223a08bae945cd79637ee66b0c5857"},
    {0x39a454d0,4,"e3bdea1369a43a000345125c717d1d98e9b0e07b4b466637c0b782e299c0ec33"},
    {0x39a1a6b4,32,"c946b7900b339dbc2a7444842ce6419684c2d1c7c16ac11908bc68932ad2f078"},
    {0x399984a0,16,"0043d992cc2f3ce60edea239288aa52a2556c94e89227acd24829627028a2a30"}
};
inline constexpr size_t NativeSuitProfileCount=sizeof(NativeSuitProfile)/sizeof(NativeSuitProfile[0]);
}
