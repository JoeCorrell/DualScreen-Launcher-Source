#include "NativeSynchronizationFixture.h"
#include "NativeGlyphs.h"
struct GlyphFixture : Fixture {
    GlyphFixture(){
        Word(0x22553b0,0x60000);Word(0x60000,0x1ebb9b8);Word(0x60028,0x61000);Word(0x6002c,0x20002);
        Word(0x61000,0x62000);Ref(0x62000,0x70000);Word(0x70000,0x1ebb42c);Word(0x70044,1);
        Word(0x61004,0x62010);Ref(0x62010,0x80000);Word(0x80000,0x1ebb42c);Word(0x80044,2);
    }
};
int main(){
    GlyphFixture f;auto g=ReadGlyphs(f,f.roots);assert(g && g->completed==1 && g->total==2);
    auto calls=f.calls;for(size_t i=1;i<=calls;++i){GlyphFixture broken;broken.fail=i;assert(!ReadGlyphs(broken,broken.roots));}
    for(uint32_t a:{0x22553b0u,0x60028u,0x62000u,0x62018u,0x70044u,0x80044u,0x223f814u}){
        GlyphFixture changed;changed.mutate=a;assert(!ReadGlyphs(changed,changed.roots));
    }
    GlyphFixture unavailable;unavailable.Word(0x62008,0);assert(!ReadGlyphs(unavailable,unavailable.roots));
    GlyphFixture complete;complete.Word(0x70044,3);g=ReadGlyphs(complete,complete.roots);assert(g && g->completed==2);
    GlyphFixture hidden;hidden.Word(0x80044,0);g=ReadGlyphs(hidden,hidden.roots);assert(g && g->completed==0);
    std::puts("Native glyph puzzle completion, bit semantics and transition tests passed");
}
