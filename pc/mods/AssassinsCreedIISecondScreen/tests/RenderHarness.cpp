#include "AnimusUi.h"
#include "PanelSurface.h"
#include "MapRaster.h"
#include "MapLegend.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace ac2ds;
class PlayerIconFixture final : public ImageSource {
public:
    unsigned requests{};
    std::shared_ptr<const Image> Request(uint32_t id)override{
        if(id!=PlayerMarkerArtwork)return {};
        ++requests;auto image=std::make_shared<Image>();image->width=image->height=8;
        image->bgra.assign(64,0xff89181f);return image;
    }
    uint64_t Revision()const override{return 1;}
};
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return 3;
    {
        UINT count{}, size{}; Gdiplus::GetImageEncodersSize(&count, &size);
        std::vector<unsigned char> bytes(size); auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
        Gdiplus::GetImageEncoders(count, size, encoders); CLSID png{}; bool found = false;
        for (UINT i = 0; i < count; ++i) if (!wcscmp(encoders[i].MimeType, L"image/png")) { png = encoders[i].Clsid; found = true; }
        if (!found) return 4;
        std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
        Snapshot s; s.status = L"Awaiting synchronization";
        // Exercise the actual presentation buffer, including GDI reading pixels
        // written directly by GDI+. No visible window or gameplay input.
        {
            PanelSurface buffer(nullptr); assert(buffer);
            { Gdiplus::Graphics g(buffer.Image()); g.Clear(Gdiplus::Color(255,137,24,31));
              g.Flush(Gdiplus::FlushIntentionSync); }
            HDC target=CreateCompatibleDC(nullptr); assert(target);
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=Width; info.bmiHeader.biHeight=-Height;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
            void* pixels{}; HBITMAP dib=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            assert(dib && pixels); auto old=SelectObject(target,dib);
            for(int scale : {1,2}) {
                buffer.Present(target,Width/scale,Height/scale);
                assert((static_cast<uint32_t*>(pixels)[100*Width+100]&0xffffff)==0x89181f);
            }
            SelectObject(target,old); DeleteObject(dib); DeleteDC(target);
            UiState surfaceUi;
            { Gdiplus::Graphics g(buffer.Image()); DrawAnimus(g,s,surfaceUi,1200); }
            assert(buffer.Image()->Save((root/L"direct-dib.png").c_str(),&png,nullptr)==Gdiplus::Ok);
        }
        // No fabricated live values or static replacement Ezio image in this harness.
        for (int p = 0; p < 6; ++p) {
            Gdiplus::Bitmap image(Width, Height, PixelFormat32bppARGB);
            { Gdiplus::Graphics g(&image); UiState ui; ui.page = Page(p); DrawAnimus(g, s, ui, 1200); }
            auto path = root / (std::to_wstring(p) + L".png");
            assert(image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok);
            PanelSurface full(nullptr),base(nullptr),composite(nullptr); assert(full && base && composite);
            UiState reference; reference.page=Page(p); auto cached=reference;
            { Gdiplus::Graphics g(full.Image()); DrawAnimus(g,s,reference,1200); }
            { Gdiplus::Graphics g(base.Image()); DrawAnimus(g,s,cached,1200,false); }
            composite.CopyFrom(base);
            { Gdiplus::Graphics g(composite.Image()); DrawAnimusMotion(g,s,cached,1200); }
            for(int y=0;y<Height;y+=17)for(int x=0;x<Width;x+=17) {
                Gdiplus::Color a,b;
                assert(full.Image()->GetPixel(x,y,&a)==Gdiplus::Ok);
                assert(composite.Image()->GetPixel(x,y,&b)==Gdiplus::Ok);
                assert(a.GetValue()==b.GetValue());
            }
        }
        StateStore store; UiState ui;
        // Former navigation-button positions no longer switch pages.
        for(const char* kind:{"tap","hold"})for(int p=0;p<6;++p){
            std::string packet=std::string(kind)+" "+std::to_string((32.f+196.f*p+98.f)/Width)+" 0.11 0.12";
            auto touch=ParseTouch(packet.data(),packet.size());assert(touch);
            ui.TouchInput(*touch,s,store,900+p);assert(ui.page==Page::Map);
        }
        for(int p=0;p<6;++p){
            uint64_t now=1000+p*500;float start=ui.page==Page::Map?.98f:.8f;
            ui.TouchInput({TouchKind::Down,start,.5f},s,store,now);
            ui.TouchInput({TouchKind::Move,.45f,.5f},s,store,now+50);
            ui.TouchInput({TouchKind::Up,.2f,.5f},s,store,now+100);
            assert(ui.page==Page((p+1)%6) && ui.pageDirection==1);
            // Terminal tap must not click through into the incoming page.
            ui.TouchInput({TouchKind::Tap,.2f,.5f},s,store,now+101);assert(ui.page==Page((p+1)%6));
        }
        ui.TouchInput({TouchKind::Down,.02f,.5f},s,store,5000);
        ui.TouchInput({TouchKind::Move,.4f,.5f},s,store,5050);
        ui.TouchInput({TouchKind::Up,.8f,.5f},s,store,5100);assert(ui.page==Page::Progress && ui.pageDirection==-1);
        ui.page=Page::Inventory;ui.TouchInput({TouchKind::Next},s,store,5200);assert(ui.page==Page::Missions);
        ui.selected=12;s.generation=2;ui.Synchronize(s);assert(!ui.selected && !ui.scroll);
        // Multi-sequence test data exists only in this local harness.
        {
            Snapshot dna;dna.generation=20;dna.phase=Phase::Playing;dna.capabilities=Missions;
            dna.status=L"DNA navigation test fixture";
            for(unsigned sequence=1;sequence<=8;++sequence)for(unsigned index=1;index<=10;++index){
                Memory m;m.id=(uint64_t(sequence)<<32)|index;m.revision=1;
                m.sequence=L"Sequence "+std::to_wstring(sequence);m.name=L"Memory "+std::to_wstring(index);
                m.unlocked=true;m.exposed=true;m.complete=sequence<4 || (sequence==4 && index<8);
                m.active=sequence==4 && index==8;m.synchronization=m.complete?100:0;
                for(int line=0;line<35;++line)m.description+=L"Long mission description for scrolling verification. ";
                if(m.active)m.objective=L"Current objective from test fixture";
                dna.memories.push_back(m);
            }
            UiState navigation;navigation.page=Page::Missions;navigation.Synchronize(dna);
            assert(navigation.selectedSequence==4 && navigation.selected==((uint64_t(4)<<32)|8));
            assert(navigation.scroll>0 && navigation.categoryScroll>0);
            // Select another sequence, then return directly to the active row.
            navigation.categoryScroll=0;
            navigation.TouchInput({TouchKind::Tap,100.f/Width,402.f/Height},dna,store,1300);
            assert(navigation.selectedSequence==1 && navigation.selected==((uint64_t(1)<<32)|1));
            navigation.TouchInput({TouchKind::Tap,100.f/Width,328.f/Height},dna,store,1301);
            assert(navigation.selectedSequence==4 && navigation.scroll>0);
            Gdiplus::Bitmap image(Width,Height,PixelFormat32bppARGB);
            {Gdiplus::Graphics g(&image);DrawAnimus(g,dna,navigation,1400);}
            assert(navigation.maximumDetailScroll>0);
            assert(image.Save((root/L"dna-sequences.png").c_str(),&png,nullptr)==Gdiplus::Ok);
            auto listScroll=navigation.scroll;
            navigation.TouchInput({TouchKind::Down,.85f,.6f},dna,store,1401);
            navigation.TouchInput({TouchKind::Move,.85f,.4f},dna,store,1402);
            assert(navigation.detailScroll>0 && navigation.scroll==listScroll);
            navigation.TouchInput({TouchKind::Up,.85f,.4f},dna,store,1403);
            navigation.TouchInput({TouchKind::Down,.12f,.6f},dna,store,1404);
            navigation.TouchInput({TouchKind::Move,.12f,.4f},dna,store,1405);
            assert(navigation.categoryScroll>0 && navigation.scroll==listScroll);
            dna.memories.erase(dna.memories.begin()+30,dna.memories.begin()+40);
            navigation.Synchronize(dna);assert(navigation.selectedSequence==1 && navigation.detailScroll==0);
            dna.generation++;dna.capabilities=0;dna.memories.clear();navigation.Synchronize(dna);
            assert(!navigation.selectedSequence && !navigation.selected && !navigation.categoryScroll);
        }
        // Synthetic test fixtures exercise bounds only; they never enter the DLL.
        {
            Snapshot weapon;weapon.generation=2;weapon.phase=Phase::Playing;weapon.capabilities=Inventory;
            weapon.status=L"Layout test fixture";Item fixture;fixture.id=901;fixture.owned=true;
            fixture.name=L"Weapon rating layout";fixture.category=L"Primary weapon";fixture.details=L"Carried";
            fixture.weaponRatings=std::array<unsigned,3>{3,4,2};
            for(unsigned i=0;i<15;++i)fixture.description+=L"Description scrolling layout test. ";
            weapon.items.push_back(fixture);UiState detail;detail.page=Page::Inventory;
            detail.Synchronize(weapon);detail.selected=901;
            Gdiplus::Bitmap image(Width,Height,PixelFormat32bppARGB);
            {Gdiplus::Graphics g(&image);DrawAnimus(g,weapon,detail,1200);}
            assert(image.Save((root/L"weapon-ratings.png").c_str(),&png,nullptr)==Gdiplus::Ok);
            assert(detail.maximumDetailScroll>0);
            detail.TouchInput({TouchKind::Down,.9f,.75f},weapon,store,1201);
            detail.TouchInput({TouchKind::Move,.9f,.7f},weapon,store,1202);
            assert(detail.detailScroll>0 && detail.scroll==0 && detail.selected==901);
        }
        ui.page = Page::Inventory; s.items.resize(100); ui.Synchronize(s);
        ui.scroll = ui.maximumScroll - 1; ui.velocity = 8000;
        ui.Animate(.05f, 1200); assert(ui.scroll == ui.maximumScroll && ui.velocity == 0);
        const auto before = ui.scroll;
        ui.TouchInput({TouchKind::Down, .9f, .5f}, s, store, 1201);
        ui.TouchInput({TouchKind::Move, .9f, .3f}, s, store, 1202);
        assert(ui.scroll == before); // Details never scroll the inventory grid.
        ui.page = Page::Database; s.database.resize(100);
        s.database[0].unlocked = true; s.database[0].category = L"Test";
        ui.Synchronize(s); assert(ui.maximumScroll == 0 && ui.scroll == 0);
        s.database[0].id = 42; s.database[0].title = L"Scrolling test fixture";
        for (int i = 0; i < 120; ++i) s.database[0].text += L"Long description used only to exercise the scrolling bounds.\n";
        s.capabilities = Database; ui.selected = 42;
        {
            Gdiplus::Bitmap surface(Width, Height, PixelFormat32bppARGB); Gdiplus::Graphics g(&surface);
            DrawAnimus(g, s, ui, 1250); assert(ui.maximumDetailScroll > 1000);
        }
        ui.TouchInput({TouchKind::Down, .8f, .8f}, s, store, 1260);
        ui.TouchInput({TouchKind::Move, .8f, .7f}, s, store, 1261);
        assert(ui.detailScroll > 100 && ui.scroll == 0);
        ui.TouchInput({TouchKind::Cancel}, s, store, 1262);
        ui.page = Page::Progress; s.statistics.resize(24); ui.Synchronize(s); assert(ui.maximumScroll > 1000);
        ui.page = Page::Map; s.map = std::make_shared<ac2ds::Image>();
        s.mapX = .65f; s.mapY = .4f; ui.map.zoom = 4; ui.following = true;
        ui.TouchInput({TouchKind::Down, .5f, .5f}, s, store, 1300);
        ui.TouchInput({TouchKind::Move, .5f, .5f}, s, store, 1301);
        assert(std::abs(ui.map.x - .65f) < .0001f && std::abs(ui.map.y - .4f) < .0001f);
        // The live-map screenshot must contain a visible player indicator,
        // including when the independently guarded direction is unavailable.
        auto mapImage = std::make_shared<ac2ds::Image>();
        mapImage->width = mapImage->height = 4; mapImage->bgra.assign(16, 0xffdddddd);
        s.map = mapImage; s.capabilities = Map | Player; s.mapX = .55775f; s.mapY = .581096f;
        ui.map = {}; ui.following = true;
        auto stableView=DisplayMapViewport(s,ui); s.mapX=.6f;
        auto nextView=DisplayMapViewport(s,ui); assert(stableView.x==nextView.x && stableView.y==nextView.y);
        ui.map.zoom=4; stableView=DisplayMapViewport(s,ui); s.mapX=.65f;
        nextView=DisplayMapViewport(s,ui); assert(stableView.x!=nextView.x);
        ui.map={}; s.mapX=.55775f;
        {
            PanelSurface chrome(nullptr),city(nullptr),frame(nullptr);
            UiState rasterUi=ui;
            { Gdiplus::Graphics g(chrome.Image()); DrawAnimus(g,s,rasterUi,1400,false,false); }
            for(float zoom:{1.f,2.f,8.f}) {
                rasterUi.map.zoom=zoom;
                city.CopyFrom(chrome);
                assert(RasterMap(city.Pixels(),size_t(Width)*Height,*s.map,DisplayMapViewport(s,rasterUi)));
                frame.CopyFrom(city);
                { Gdiplus::Graphics g(frame.Image());DrawAnimusMotion(g,s,rasterUi,1400); }
                // The cached chrome must survive every viewport transform.
                assert(frame.Pixels()[300*Width+100]==chrome.Pixels()[300*Width+100]);
            }
            assert(frame.Image()->Save((root/L"map-direct-raster.png").c_str(),&png,nullptr)==Gdiplus::Ok);
        }
        // Marker changes must match a fresh render when composited over the
        // same city raster. Reuse the output to catch trails after move/remove.
        {
            PanelSurface base(nullptr),full(nullptr),composite(nullptr);
            assert(base && full && composite);
            UiState cached=ui;
            { Gdiplus::Graphics g(base.Image()); DrawAnimus(g,s,cached,1400,false); }
            Marker marker; marker.id=73; marker.x=.35f; marker.y=.45f;
            marker.name=L"Marker fixture"; marker.discovered=marker.visible=true;
            s.markers.push_back(marker);
            for(int step=0;step<3;++step) {
                if(step==1) { s.markers[0].x=.7f; s.markers[0].y=.3f; }
                if(step==2) s.markers.clear();
                if(step!=1){Gdiplus::Graphics g(base.Image());DrawAnimus(g,s,cached,1400,false);}
                UiState reference=ui;
                { Gdiplus::Graphics g(full.Image()); DrawAnimus(g,s,reference,1400); }
                composite.CopyFrom(base);
                { Gdiplus::Graphics g(composite.Image()); DrawAnimusMotion(g,s,cached,1400); }
                Gdiplus::Rect bounds(0,0,Width,Height);
                Gdiplus::BitmapData a{},b{};
                assert(full.Image()->LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&a)==Gdiplus::Ok);
                assert(composite.Image()->LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&b)==Gdiplus::Ok);
                for(int row=0;row<Height;++row)for(int col=0;col<Width;++col) {
                    auto av=reinterpret_cast<uint32_t*>(static_cast<char*>(a.Scan0)+row*a.Stride)[col];
                    auto bv=reinterpret_cast<uint32_t*>(static_cast<char*>(b.Scan0)+row*b.Stride)[col];
                    if(av!=bv)std::cerr<<"Map composite mismatch step="<<step<<" x="<<col<<" y="<<row
                        <<" full="<<std::hex<<av<<" cached="<<bv<<std::dec<<"\n";
                    assert(av==bv);
                }
                full.Image()->UnlockBits(&a); composite.Image()->UnlockBits(&b);
            }
        }
        // Every category remains reachable when the legend exceeds its viewport.
        // Vertical sidebar gestures must not navigate pages or pan the city.
        {
            auto crowded=s;crowded.markers.clear();
            for(unsigned i=0;i<24;++i){
                Marker m;m.id=i+100;m.categoryId=i+200;m.name=L"Category "+std::to_wstring(i+100);
                m.x=m.y=.5f;m.visible=m.discovered=true;crowded.markers.push_back(m);
            }
            UiState list;list.page=Page::Map;list.Synchronize(crowded);StateStore commands;
            assert(MapLegend(crowded).size()==24 && list.maximumCategoryScroll==24*48-402);
            auto input=[&](TouchKind kind,float x,float y,uint64_t now){Touch t{};t.kind=kind;t.x=x/Width;t.y=y/Height;list.TouchInput(t,crowded,commands,now);};
            input(TouchKind::Down,150,680,2000);input(TouchKind::Move,150,300,2100);input(TouchKind::Up,150,300,2200);
            assert(list.page==Page::Map && list.categoryScroll>0 && list.map.zoom==1);
            list.categoryScroll=list.maximumCategoryScroll;
            input(TouchKind::Tap,150,670,2300);assert(list.mapFilter==223);
            input(TouchKind::Tap,150,205,2400);assert(list.mapFilter==0);
            crowded.markers[23].visible=false;list.Synchronize(crowded);
            assert(MapLegend(crowded).size()==23 && list.categoryScroll==23*48-402);
        }
        PlayerIconFixture playerIcons;ui.images=&playerIcons;
        for (bool facing : {true, false}) {
            s.heading = facing ? std::optional<float>(-1.658148f) : std::nullopt;
            Gdiplus::Bitmap surface(Width, Height, PixelFormat32bppARGB); Gdiplus::Graphics g(&surface);
            DrawAnimus(g, s, ui, 1400); g.Flush(Gdiplus::FlushIntentionSync);
            unsigned red = 0;
            auto bounds=MapBounds(s.map.get());
            int px=int(bounds.x+*s.mapX*bounds.width),py=int(bounds.y+*s.mapY*bounds.height);
            for(int y=py-28;y<py+28;++y)for(int x=px-28;x<px+28;++x){
                Gdiplus::Color pixel; assert(surface.GetPixel(x, y, &pixel) == Gdiplus::Ok);
                if (pixel.GetR() > 100 && pixel.GetG() < 70 && pixel.GetB() < 70) ++red;
            }
            assert(red > 80);
        }
        assert(playerIcons.requests>=4); // Both map and legend use the original resource ID.
        ui.images=nullptr;
    }
    Gdiplus::GdiplusShutdown(token); std::cout << "Six Animus pages rendered; navigation and generation reset passed\n";
}
