// Explicit smoke test in a separate process. Never injects into or opens Crysis.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    HMODULE proxy=LoadLibraryA(argv[1]);
    if(!proxy) { std::printf("LoadLibrary failed %lu\n",GetLastError()); return 3; }
    FARPROC identityAddress=GetProcAddress(proxy,"Crysis3ResearchProbeIdentity");
    using Identity=DWORD(WINAPI*)(); Identity identity{};
    std::memcpy(&identity,&identityAddress,sizeof(identity));
    if(!identity || identity()!=0x43335031) {
        std::puts("FAIL: loader did not select the Crysis research DLL"); return 6;
    }
    char modulePath[MAX_PATH]{}; GetModuleFileNameA(proxy,modulePath,MAX_PATH);
    std::printf("Verified research DLL identity; module=%s\n",modulePath);
    for(const char* name:{"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer"}) {
        if(!GetProcAddress(proxy,name)) { std::printf("Missing export %s\n",name); return 4; }
    }
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    FARPROC address=GetProcAddress(proxy,"DirectInput8Create"); Create create{};
    std::memcpy(&create,&address,sizeof(create));
    IDirectInput8W* input{};
    const HRESULT result=create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr);
    std::printf("DirectInput8Create result=%08lx object=%d process=%lu\n",static_cast<unsigned long>(result),input!=nullptr,GetCurrentProcessId());
    if(FAILED(result) || !input) return 5;
    input->Release();
    // Let the worker reject this unrelated EXE; no game profile or hooks apply.
    Sleep(250);
    std::puts("PASS: forwarded interface released; no input devices acquired");
    return 0;
}
