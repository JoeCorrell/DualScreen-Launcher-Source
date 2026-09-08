// Windowless, bounded read-only diagnostic. Never calls the game or D3D.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include "../src/NativeCharacterDynamic.h"
using namespace ac2ds::native;
struct Remote final:Memory {
    HANDLE process;explicit Remote(HANDLE value):process(value){}
    bool Copy(uint32_t address,void* output,size_t bytes)const override {
        SIZE_T read{};return address>=0x10000 && bytes<=65536 && address<=UINT32_MAX-bytes &&
            ReadProcessMemory(process,reinterpret_cast<void*>(address),output,bytes,&read) && read==bytes;
    }
};
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){
    FILE* out=std::fopen("F:\\Download\\ac2-dynamic-probe.txt","wb");if(!out)return 1;
    struct Report {FILE* file;~Report(){std::fputs("probe-complete\n",file);std::fclose(file);}} report{out};
    HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(list==INVALID_HANDLE_VALUE)return 2;
    PROCESSENTRY32 entry{};entry.dwSize=sizeof(entry);DWORD target{};unsigned found{};
    if(Process32First(list,&entry))do{if(!_stricmp(entry.szExeFile,"AssassinsCreedIIGame.exe")){target=entry.th32ProcessID;++found;}}while(Process32Next(list,&entry));
    CloseHandle(list);if(found!=1){std::fputs("No unique AC II\n",out);return 3;}
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,target);if(!process)return 4;
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} cleanup{process};Remote memory(process);
    for(unsigned attempt=0;attempt<8;++attempt){
        Roots roots{};auto start=GetTickCount64();
        if(!ReadRoots(memory,roots) || !roots.player){std::fputs("No live player\n",out);return 5;}
        CharacterDynamicStage stage{};auto streams=ReadCharacterDynamicStreams(memory,roots,&stage);
        std::fprintf(out,"attempt=%u player=%08lx coherent=%d stage=%d elapsedMs=%llu\n",attempt,
            static_cast<unsigned long>(roots.player),bool(streams),int(stage),static_cast<unsigned long long>(GetTickCount64()-start));
        if(!streams && attempt==0){
            auto graph=ReadCharacterGeometry(memory,roots);
            if(graph)for(const auto& b:graph->bindings){if(b.kind!=CharacterRendererKind::Alternate)continue;
                std::fprintf(out,"alternate=%08lx\n",static_cast<unsigned long>(b.renderer));
                for(unsigned offset:{0x4cu,0x50u,0x54u,0x58u,0x60u,0x70u}){uint32_t v{};memory.Field(b.renderer,offset,v);std::fprintf(out," renderer+%x=%08lx",offset,static_cast<unsigned long>(v));}
                for(unsigned offset:{4u,8u,0x10u,0x14u,0x1cu,0x20u}){uint32_t v{};memory.Field(b.geometry,offset,v);std::fprintf(out," geometry+%x=%08lx",offset,static_cast<unsigned long>(v));}
                std::fputc('\n',out);
            }
        }
        if(streams){
            for(const auto& s:*streams){
                uint32_t hash=2166136261u;for(auto b:s.vertices)hash=(hash^b)*16777619u;
                std::fprintf(out,"dynamic instance=%08lx renderer=%08lx stride=%lu secondaryStride=%lu vertices=%lu primitives=%lu bytes=%u digest=%08lx\n",
                    static_cast<unsigned long>(s.instance),static_cast<unsigned long>(s.renderer),static_cast<unsigned long>(s.stride),
                    static_cast<unsigned long>(s.secondaryStride),static_cast<unsigned long>(s.vertexCount),
                    static_cast<unsigned long>(s.primitiveCount),unsigned(s.vertices.size()),static_cast<unsigned long>(hash));
            }
            return 0;
        }
        Sleep(250);
    }
    return 6;
}
