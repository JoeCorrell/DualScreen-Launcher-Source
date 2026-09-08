#pragma once
#include <cstddef>
#include <cstdint>
namespace c3::research {
struct NativeWeaponFingerprint { uint32_t address; size_t size; const char* sha256; };
// Exact 1.2.1.1000 loaded text/rdata captured at image base 0x37000000.
// Check source/header profile FIRST, then all entries before setting the reader's
// profileVerified argument. These are read-contract guards, not callable hooks.
// Covers ReadRegularSingle plus prerequisite inventory/item and raw-ammo readers.
// Includes vtable contents as well as code, so a matching object vptr alone cannot
// silently adopt changed virtual targets. No heap addresses are included.
inline constexpr NativeWeaponFingerprint NativeWeaponProfile[] = {
    {0x3803e6b0,4,"68b70ba1e6ad2356c69cac8e846649e599945994fa7690a0bf4af28fa25c24a5"}, // actor inventory
    {0x380419b0,93,"acd77862b5bf5c7f116723cd65d7153805b9b6b89bb8dbb40c81974aa3bf1691"}, // actor current item
    {0x3749cb70,17,"faebfdecdec91f85c383d3542c6e336076e9313a712baab825dabd578970a6af"}, // framework item system
    {0x37283230,69,"3e04e9fe7b41f9529aeca8de6bd4ac7b81badbf3c92f2dc07134fadd264e8edf"}, // item lookup
    {0x37279e30,7,"923d1a98e397f42b18b1fe647cb0d836cdfa25ef31f2788ff06a51f12dcb3c64"}, // current EntityId
    {0x3727a190,10,"d1a408055bb0fba67bae5c2e6066b33a52bdb3b11ae32fc89a924b06c860fb6b"}, // item vector count
    {0x3727a1a0,35,"119a71beca960f7e25b9722b8efd839e4e5ac87618264440bfb3e8fc8c64a635"}, // item vector index
    {0x3727a310,10,"1353ec7d280ec06a80f5bde0b1ea7fd414a979b3869204a0e4ba969c7bb34f99"}, // class vector count
    {0x3727aa80,35,"671ad2232c576deb917d60096fc5c59666e94cac8ffe29c57fb44ed15f0e1e1d"}, // class vector index
    {0x3727aba0,63,"fe431452250ab2ff424071470aa52cc798676c30d4057e8a6bf10f6c6d23abaf"}, // inventory ammo count
    {0x3727abe0,63,"ca493c2216abee3a79a3ad4d1d151c03303ff6e309d59a2a8e4f477014d50a08"}, // inventory ammo capacity
    {0x37f6c960,14,"d712a02d1b418a7a9d9629085c013d249e7a89dd6a60f6190a76fd5dff89a791"}, // item to IWeapon
    {0x3824e130,7,"c4ead592093c82fcaec09324e11beabd00c48b6e26ef7f17fbd1695949037ade"}, // fire-mode index
    {0x382532b0,35,"1b04fc435239a67f9b3563c933ff3de782efb9c2d5bf60ca2e43fa88b0ff8a65"}, // fire-mode vector
    {0x38278910,33,"a201873e07e881f4b6aae2e3325776692c65c4c59e9d22fca413e50204606146"}, // Single ammunition
    {0x38278940,83,"4f65c0460a2233306ebace56796dc5f3c680145591685561d50e1fade5367e91"}, // Single clip limit
    {0x38278cc0,10,"9730c1f45c057f6297a68050e8fff11a5386756dd56f60271a4dc979629febff"}, // Single ammo class
    {0x38250890,178,"06ac22d6fada12219cc87fa80e133b520ca3eb06404300dcf7c3f657fe65b25e"}, // normal magazine + branch gate
    {0x3803dc60,4,"c979f6eaf0fee01e57ad97bb5e1859f15517e30c80300980833adce41c95f73d"}, // owner predicate
    {0x3821e8b0,64,"d84815733d2ac1463ea382556c7578afae5ac3924dd72a5ae9897b0b462a5812"}, // weak owner
    {0x378a6cb0,4,"0b78473b3a56c63ff2a64431fb600078066cbe407d3dee7ea8fd7575bd7f82d5"}, // IEntity class
    {0x378b9070,4,"916ae10aa3fd76a4d5b25b2fb66e9df0fd04957c7ab10930268d1ed7808be689"}, // class name
    {0x382cf760,228,"f0ee5273ffd9e3e917c6bda47a95504f3e867f7b048b27e8b3b370284a7e6974"}, // HUD loaded-round/overflow update
    {0x382d04f0,2067,"934cacd814fac1d3473ddf3216608a98afff5bac9e15c57eb83e0dd4d55ea2a7"}, // HUD clipped display and overflow
    {0x399b5940,0x110,"33441d69114e3c916ab21423ff2ca6fa4fb4a9f64fa9bc49e732f49e0e21b765"}, // actor table
    {0x399e3d08,0x12c,"9f20ea6d3754a5365065402b897890085ef9b46b6c2122597fe67c8ef3ed9d77"}, // item table
    {0x399e3b70,0x84,"640e6bf52226f162260d02691501c1a12104bfe4f7cdf0d7d85d8d491c0c6e78"}, // IWeapon table
    {0x399e6b28,0xb4,"4839c4a89b639907e0b79e87c8e9b149eb8abda31210c6998515290077019296"}, // Single table
    {0x3988c288,0x10c,"88bfcfb1973fb01f74e1f26c7aba9f295efa3ac5810591f97cfa259f4e59caa6"}, // inventory table
    {0x398eccc0,0x12c,"e2adb7f275506af8bf1223408aaefdb944dde3f9f538b073f88ee631b23b4e16"}, // IEntity table
    {0x398f4638,0x28,"6a39a82b2274f2310b9e6ad30c979eaec550cc463ef614f89092528c043dc5a5"}, // entity class table
    {0x3988cf40,0x58,"f431e7aa0f34c8dbc0943fc26ca812209f20bd89c02ed874aa06619dcf4c1871"}, // item-system table
    {0x399ba1d8,12,"e3919ce74d3a6adb7fd6c3623b7b53b364b10af99ead1fef3529482d06587aa7"}, // weak-owner control table
};
inline constexpr size_t NativeWeaponProfileCount=sizeof(NativeWeaponProfile)/sizeof(NativeWeaponProfile[0]);
// Additional closure for ReadOwnedWeaponName. Check BOTH profiles before its
// profileVerified=true. Families share the exact CItem entity/owner methods and
// non-null IWeapon getter; this does not extend RegularSingle ammo support.
inline constexpr NativeWeaponFingerprint NativeWeaponNameProfile[] = {
    {0x3821bac0,57,"e3da738381f428c0c353812f0a92206def0d60c14e5ac9ed68e6920264891cf2"},
    {0x3821bb70,7,"7ae3bbf974d887ad8912de6a67b7545cb3173c6a4b3ad7a4234712d65cd8d9db"},
    {0x3998ef18,0x12c,"ff201b599c38bd10f23e26c17bed1621d9cb26444b38e2dfb9a5a0cb49b5f0fd"},
    {0x399a1ab0,0x12c,"6339b7c949e8baba6f2e49cf62b8d4ed54b0ab5570c48f86c2318a1b1843433a"},
    {0x399a2208,0x12c,"5afe6ef3af5179597614cbc8c4488906cc74315b4bec033c6b01147893cc578a"},
    {0x399a93d0,0x12c,"e86be5dfb09c46f5531a6096d2f743bd0fd537c2bd0b3213199ab8587d95a230"},
    {0x399b2250,0x12c,"ffe68ec18d88ccfc3503158481baa96e12c0aaf50e2903fe63e9778f11025a2e"},
    {0x399b9e00,0x12c,"9d7f67bee98e6c46d52734425347a5052c5bb3961840dea521cf343913810e2a"},
    {0x399d8ff8,0x12c,"2a80151b95e09016c74350fc2d95c39b3c78c0d8851df95bbb0cc490d6129d49"},
    {0x399e3d08,0x12c,"9f20ea6d3754a5365065402b897890085ef9b46b6c2122597fe67c8ef3ed9d77"},
    {0x399e83f0,0x12c,"0e967c6e514c5267dce2942750948de1faeb8a718f34685fec27b5258a662cef"},
    {0x399e8968,0x12c,"d971e477f6367133cf945ae455ba727131ccc96476d1394c2955bf231657129d"},
    {0x399e8fc8,0x12c,"43e9a804b3dde268008529826468df7926cfeb7287084d204cf97572a7fcb538"},
    {0x399e95c8,0x12c,"8dcd15aa383397d926997fd76e5a641c3cfd716348d279d4fbc2d9a3d37b11b1"},
    {0x399e9c50,0x12c,"0c770f4e40b186f6ac9cc3b5bd589797afb52c99745faa5edb6ad36d751186a9"},
    {0x399ea1c0,0x12c,"176e6cf699fe8442ccf1149e78a5053a95380165cb48426020728e0d0a2b116e"},
    {0x399ea778,0x12c,"0bc7501195cb25bac272db10956dd0eebfefbbece70be944f3339cf8abce1daa"},
    {0x399eb100,0x12c,"4d30976d7707c4b4887ed3bdadac9652829333731846ce4bdae0523a95eaac00"},
    {0x399eb680,0x12c,"9e2c777a60101f5db1377550936ac4d643bd673f1e0cd8fcdcd9727850c5413c"},
    {0x399ebd38,0x12c,"ac5af60a161945c7f93de588d00ad0c66f0624f66a625d9553d73291f6c76189"},
    {0x399ec2b0,0x12c,"f7371bfe00e58d962d85c8ae8c32d3f67587bdf4ffb2d5a775bb33b3e5f37586"},
    {0x399ec828,0x12c,"651a2d0afd21da19c4ca33ef03de04f3935318cfb356c84c9dfa7dd545817950"},
    {0x399ed100,0x12c,"70ab5bf52feeb9301bb252014ef6ae9e2d5cf792943990953c79805002d18976"},
    {0x399ed790,0x12c,"dfa325e4076ac83256f47634d1a8ca532600a2d9c2a5a0031cb05c370a4f9821"},
    {0x399edd60,0x12c,"cbbc6a5b06feb681c0ecc4920f4880367241f0ed9621ee90a57e90af13096022"},
    {0x399ee2e0,0x12c,"f1bb7343bff4f5bbfa2fb8d0319614962509357d03f123d79ebdadf5744128c9"},
    {0x399ee878,0x12c,"a9775f19af5beab9c21fe786cd3f4c77779ed6d6c1e5f4d979510952010a2210"},
};
inline constexpr size_t NativeWeaponNameProfileCount=sizeof(NativeWeaponNameProfile)/sizeof(NativeWeaponNameProfile[0]);
inline bool IsWeaponNameFamily(uint32_t vtable) {
    for(size_t i=2;i<NativeWeaponNameProfileCount;++i)
        if(NativeWeaponNameProfile[i].address==vtable)return true;
    return false;
}
}
