#include "../tools/CaptureSelection.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace c3::research;
    for(const auto* name:{L"Crysis3.exe",L"CRYSIS3.EXE",L"F:\\Dualscreen\\Games\\Crysis 3\\Game\\Bin32\\Crysis3.exe",L"/game/Crysis3.exe"})
        assert(IsGameProcessName(name));
    for(const auto* name:{L"Crysis3.exe.bak",L"Crysis3RuntimeCapture.exe",L"Crysis2.exe",L"Crysis3.exe --auto",L"",L"/Crysis3.exe/"})
        assert(!IsGameProcessName(name));
    ProcessSelection s;
    s.Observe(12,L"SkyrimSE.exe"); s.Observe(0,L"Crysis3.exe"); assert(!s.Unique());
    s.Observe(25,L"Crysis3.exe"); assert(s.Unique() && s.pid==25);
    s.Observe(25,L"CRYSIS3.EXE"); assert(s.Unique());
    s.Observe(26,L"Crysis3.exe"); assert(s.ambiguous && !s.Unique());
    s.Observe(25,L"Crysis3.exe"); assert(!s.Unique());
    std::cout<<"PASS: exact process selection, path/case handling, zero PID and ambiguous-process rejection\n";
}
