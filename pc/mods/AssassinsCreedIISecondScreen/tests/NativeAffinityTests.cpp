#include "NativeAffinity.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>
namespace {
char report[512]{};
void Log(const char* format,...) {va_list args;va_start(args,format);std::vsnprintf(report,sizeof(report),format,args);va_end(args);}
DWORD WINAPI Worker(void*) {return 0;}
}
int main() {
    DWORD_PTR process{},system{};
    assert(GetProcessAffinityMask(GetCurrentProcess(),&process,&system));
    DWORD id{};HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,CREATE_SUSPENDED,&id);assert(worker);
    assert(SetThreadAffinityMask(worker,process & (~process+1)));
    ac2ds::affinity::original=&SetThreadAffinityMask;
    ac2ds::affinity::allowed=process;ac2ds::affinity::log=Log;
    ac2ds::affinity::RepairThreads();
    // Requires query AND set access on the enumerated handle. Without query
    // rights the API returns zero and the existing restricted mask survives.
    assert(SetThreadAffinityMask(worker,process)==process);
    assert(std::strstr(report,"unavailable=0 error=0"));
    ResumeThread(worker);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    std::cout << "Existing thread affinity restored through enumerated handles with required API rights\n";
}
