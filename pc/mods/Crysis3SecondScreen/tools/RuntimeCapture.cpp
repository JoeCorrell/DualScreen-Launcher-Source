// Read-only owner-authorized native-code research. No injection, suspension,
// privilege adjustment, game-state writes, or execution inside the target.
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include "CaptureSelection.h"

namespace {
constexpr char ExpectedHash[]="f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e";
constexpr char PermissionTrialHash[]="fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055";
constexpr uint32_t CapturedFaultRva=0x02c169ce; // 2026-09-07 console evidence, not a hook.
// 1024 bytes independently retained from the live patched build on 2026-09-07.
// Five-section reconstructed header, NOT evidence of a native function ABI.
constexpr char ObservedHeaderHash[]="fb530657a66ea51a6a7495c36085e0c856d6daccffbacc1db8c037d48c55e25d";
bool SupportedHash(const std::string& hash) { return hash==ExpectedHash || hash==PermissionTrialHash; }
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE v):value(v) {}
    ~Handle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    bool valid() const { return value && value!=INVALID_HANDLE_VALUE; }
};
bool Read(HANDLE process,uintptr_t address,void* data,size_t size) {
    SIZE_T received{};
    return ReadProcessMemory(process,reinterpret_cast<LPCVOID>(address),data,size,&received) && received==size;
}
std::string Hash(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    DWORD objectSize{},received{};
    bool valid=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&received,0)>=0;
    std::vector<uint8_t> storage(valid?objectSize:0);
    valid=valid && BCryptCreateHash(algorithm,&hash,storage.data(),objectSize,nullptr,0,0)>=0;
    std::ifstream input(path,std::ios::binary); valid=valid && bool(input);
    std::array<char,65536> buffer{};
    while(valid && input) {
        input.read(buffer.data(),buffer.size());
        if(input.gcount()>0) valid=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(input.gcount()),0)>=0;
    }
    valid=valid && input.eof();
    std::array<uint8_t,32> digest{};
    valid=valid && BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!valid) return {};
    std::ostringstream out; for(uint8_t byte:digest) out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    return out.str();
}
std::string HashBytes(const uint8_t* data,ULONG size) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    DWORD objectSize{},received{};
    bool valid=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&received,0)>=0;
    std::vector<uint8_t> storage(valid?objectSize:0);
    valid=valid && BCryptCreateHash(algorithm,&hash,storage.data(),objectSize,nullptr,0,0)>=0;
    valid=valid && BCryptHashData(hash,const_cast<PUCHAR>(data),size,0)>=0;
    std::array<uint8_t,32> digest{};
    valid=valid && BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!valid) return {};
    std::ostringstream out; for(uint8_t byte:digest) out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    return out.str();
}
bool Alive(HANDLE process) {
    DWORD code{}; return GetExitCodeProcess(process,&code) && code==STILL_ACTIVE;
}
bool LayoutMatchesFile(const std::filesystem::path& source,const IMAGE_NT_HEADERS32& loaded,
    const std::vector<IMAGE_SECTION_HEADER>& sections,bool observedHeader) {
    std::ifstream input(source,std::ios::binary);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    input.read(reinterpret_cast<char*>(&dos),sizeof(dos));
    if(!input || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64 || dos.e_lfanew>4096) return false;
    input.seekg(dos.e_lfanew); input.read(reinterpret_cast<char*>(&nt),sizeof(nt));
    if(!input || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections!=sections.size()+(observedHeader?1:0) ||
        nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) ||
        (!observedHeader && nt.OptionalHeader.SizeOfImage!=loaded.OptionalHeader.SizeOfImage) ||
        nt.OptionalHeader.SizeOfHeaders!=loaded.OptionalHeader.SizeOfHeaders) return false;
    for(const auto& section:sections) {
        IMAGE_SECTION_HEADER disk{}; input.read(reinterpret_cast<char*>(&disk),sizeof(disk));
        if(!input || std::memcmp(disk.Name,section.Name,8)!=0 || disk.VirtualAddress!=section.VirtualAddress ||
            disk.Misc.VirtualSize!=section.Misc.VirtualSize) return false;
    }
    return true;
}
struct SectionCopy { uint64_t bytes{}; std::string hash; bool complete{}; };
SectionCopy CopyRegion(HANDLE process,uintptr_t address,uint32_t bytes,
    const std::filesystem::path& destination,uint64_t started) {
    std::ofstream output(destination,std::ios::binary);
    SectionCopy result{};
    std::array<uint8_t,4096> first{},second{};
    while(output && result.bytes<bytes) {
        const size_t count=std::min<size_t>(first.size(),bytes-size_t(result.bytes));
        if(GetTickCount64()-started>30000 || !Alive(process) ||
            !Read(process,address+uintptr_t(result.bytes),first.data(),count) ||
            !Read(process,address+uintptr_t(result.bytes),second.data(),count) ||
            !std::equal(first.begin(),first.begin()+count,second.begin())) break;
        output.write(reinterpret_cast<const char*>(first.data()),std::streamsize(count));
        if(!output) break;
        result.bytes+=count;
    }
    output.close(); result.hash=Hash(destination);
    result.complete=result.bytes==bytes && bool(output) && result.hash.size()==64;
    return result;
}
bool MemoryRegions(HANDLE process,uintptr_t base,uint32_t imageBytes,const std::filesystem::path& destination) {
    std::ofstream output(destination);
    uint64_t cursor=base,end=uint64_t(base)+imageBytes;
    unsigned count=0;
    while(cursor<end && count++<65536) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQueryEx(process,reinterpret_cast<LPCVOID>(uintptr_t(cursor)),&region,sizeof(region))) return false;
        const auto address=reinterpret_cast<uintptr_t>(region.BaseAddress);
        const uint64_t next=uint64_t(address)+region.RegionSize;
        if(next<=cursor || next>uint64_t(UINT32_MAX)+1) return false;
        output<<"{\"base\":"<<address<<",\"bytes\":"<<region.RegionSize
              <<",\"allocationBase\":"<<reinterpret_cast<uintptr_t>(region.AllocationBase)
              <<",\"state\":"<<region.State<<",\"protect\":"<<region.Protect
              <<",\"allocationProtect\":"<<region.AllocationProtect<<",\"type\":"<<region.Type<<"}\n";
        cursor=std::min(next,end);
    }
    output.close(); return cursor==end && bool(output);
}
void RejectedHeaderEvidence(HANDLE process,uintptr_t base,uint32_t moduleBytes,
    const std::string& sourceHash,const std::filesystem::path& output) {
    // Diagnostic evidence only: a packer may replace the PE headers. Preserve a
    // bounded double-read for inspection, without accepting the changed layout.
    if(moduleBytes<4096 || moduleBytes>128*1024*1024 ||
        uint64_t(base)+moduleBytes>uint64_t(UINT32_MAX)+1 || std::filesystem::exists(output) ||
        !std::filesystem::create_directory(output)) return;
    const auto copy=CopyRegion(process,base,4096,output/"rejected-headers.bin",GetTickCount64());
    std::ofstream report(output/"rejected-header-diagnostic.json");
    report<<"{\"sourceSha256\":\""<<sourceHash<<"\",\"imageBase\":"<<base
          <<",\"moduleBytes\":"<<moduleBytes<<",\"capturedBytes\":"<<copy.bytes
          <<",\"sha256\":\""<<copy.hash<<"\",\"copyComplete\":"<<(copy.complete?"true":"false")
          <<",\"acceptedCapture\":false,\"nativeHooksVerified\":false}\n";
    std::cout<<"Rejected header diagnostic retained; NOT an accepted native-code capture\n";
}
int CaptureGameRoot(HANDLE process,uintptr_t base,const std::string& sourceHash,
    const std::filesystem::path& output) {
    // This global is directly referenced by the captured HUD update at 382e0030.
    // Only diagnostic bytes are returned; no actor identity or field meaning is inferred.
    const uintptr_t slot=base+(0x3b9fde30u-0x37000000u);
    uint32_t object{},vtable{},finalObject{};
    if(!Read(process,slot,&object,4) || !object || uint64_t(object)+1024>uint64_t(UINT32_MAX)+1 ||
        !Read(process,object,&vtable,4) || !vtable || uint64_t(vtable)+512>uint64_t(UINT32_MAX)+1) return 16;
    if(std::filesystem::exists(output) || !std::filesystem::create_directory(output)) return 10;
    const uint64_t started=GetTickCount64();
    const auto objectCopy=CopyRegion(process,object,1024,output/"game-root.bin",started);
    const auto tableCopy=CopyRegion(process,vtable,512,output/"game-vtable.bin",started);
    const bool consistent=objectCopy.complete && tableCopy.complete && Alive(process) &&
        Read(process,slot,&finalObject,4) && object==finalObject;
    std::ofstream report(output/"root-probe.json");
    report<<"{\"sourceSha256\":\""<<sourceHash<<"\",\"rootSlot\":"<<slot
          <<",\"object\":"<<object<<",\"vtable\":"<<vtable<<",\"objectSha256\":\""<<objectCopy.hash
          <<"\",\"vtableSha256\":\""<<tableCopy.hash<<"\",\"consistent\":"<<(consistent?"true":"false")
          <<",\"nativeHooksVerified\":false}\n";
    report.close();
    return consistent && report?0:12;
}
int Inspect(HANDLE process,uintptr_t base,const std::string& sourceHash,const std::filesystem::path& output) {
    if(std::filesystem::exists(output) || !std::filesystem::create_directory(output)) return 10;
    SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS); // helper only, never the game
    std::ofstream metadata(output/"inspector.json");
    metadata<<"{\"sourceSha256\":\""<<sourceHash<<"\",\"headerSha256\":\""<<ObservedHeaderHash
            <<"\",\"imageBase\":"<<base<<",\"readOnly\":true,\"maximumReadBytes\":4096,"
            <<"\"maximumRequests\":128,\"lifetimeSeconds\":900,\"nativeHooksVerified\":false}\n";
    metadata.close();
    if(!metadata) return 11;
    const uint64_t started=GetTickCount64();
    uint32_t lastId=0;
    while(Alive(process) && GetTickCount64()-started<900000 && lastId<128 &&
        !std::filesystem::exists(output/"stop")) {
        // Fixed-size, bounded local mailbox. No ports, callbacks, injection,
        // process suspension, executable calls, input commands or write access.
        std::ifstream request(output/"request.bin",std::ios::binary);
        std::array<uint32_t,5> fields{};
        request.read(reinterpret_cast<char*>(fields.data()),sizeof(fields));
        const bool exact=bool(request) && request.peek()==std::char_traits<char>::eof();
        if(exact && fields[0]==0x51523343 && fields[1]==1 && fields[2]>lastId && fields[2]<=128 &&
            fields[3]!=0 && fields[4]>0 && fields[4]<=4096 && uint64_t(fields[3])+fields[4]<=uint64_t(UINT32_MAX)+1) {
            lastId=fields[2];
            std::vector<uint8_t> first(fields[4]),second(fields[4]);
            const bool read=Read(process,fields[3],first.data(),first.size());
            const DWORD readError=read?0:GetLastError();
            const bool stable=read && Read(process,fields[3],second.data(),second.size()) && first==second && Alive(process);
            const auto stem=std::string("read-")+std::to_string(lastId);
            std::string hash;
            if(stable) {
                const auto destination=output/(stem+".bin");
                if(std::filesystem::exists(destination)) return 10;
                std::ofstream binary(destination,std::ios::binary);
                binary.write(reinterpret_cast<const char*>(first.data()),std::streamsize(first.size()));
                binary.close();
                if(!binary) return 11;
                hash=Hash(destination);
            }
            if(std::filesystem::exists(output/(stem+".json"))) return 10;
            const auto pendingReport=output/(stem+".json.next");
            std::ofstream report(pendingReport);
            report<<"{\"id\":"<<lastId<<",\"address\":"<<fields[3]<<",\"requestedBytes\":"<<fields[4]
                  <<",\"readError\":"<<readError<<",\"stable\":"<<(stable?"true":"false")
                  <<",\"sha256\":\""<<hash<<"\",\"elapsedMs\":"<<GetTickCount64()-started
                  <<",\"atomicSnapshot\":false}\n";
            report.close();
            if(!report || !MoveFileExW(pendingReport.c_str(),(output/(stem+".json")).c_str(),0)) return 11;
        }
        Sleep(500);
    }
    std::ofstream ended(output/"inspector-ended.txt");
    ended<<"Read-only inspector ended; game was not stopped or modified.\n";
    return 0;
}
int PerformanceSurvey(HANDLE process,DWORD pid,const std::filesystem::path& output) {
    if(std::filesystem::exists(output) || !std::filesystem::create_directory(output)) return 10;
    std::ofstream report(output/"performance.txt");
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    DWORD_PTR targetMask{},systemMask{},helperMask{},helperSystem{};
    const BOOL targetOk=GetProcessAffinityMask(process,&targetMask,&systemMask);
    const DWORD targetError=targetOk?0:GetLastError();
    const BOOL helperOk=GetProcessAffinityMask(GetCurrentProcess(),&helperMask,&helperSystem);
    report<<"Read-only same-prefix performance survey; no affinity or game state changed\n"
          <<"gameWindowsPid="<<pid<<" helperWindowsPid="<<GetCurrentProcessId()
          <<" logicalProcessors="<<info.dwNumberOfProcessors<<'\n'
          <<"targetMaskOk="<<targetOk<<" targetError="<<targetError<<" targetMask="<<std::hex<<targetMask
          <<" targetSystemMask="<<systemMask<<" helperMaskOk="<<helperOk<<" helperMask="<<helperMask
          <<" helperSystemMask="<<helperSystem<<std::dec<<'\n';
    // These are inherited from WinHandler, not falsely reported as reading
    // the game's private environment block. No arbitrary environment dump.
    for(const char* key:{"WINE_CPU_TOPOLOGY","FEX_TSOENABLED","FEX_VECTORTSOENABLED",
        "FEX_MEMCPYSETTSOENABLED","FEX_HALFBARRIERTSOENABLED","FEX_X87REDUCEDPRECISION",
        "FEX_MULTIBLOCK","FEX_DYNAMICL1CACHE","FEX_DISABLEL2CACHE"}) {
        char value[256]{}; const DWORD count=GetEnvironmentVariableA(key,value,sizeof(value));
        report<<"inherited."<<key<<'=';
        if(count && count<sizeof(value)) {
            for(DWORD i=0;i<count;++i) report<<(value[i]>=32 && value[i]<127?value[i]:'?');
        } else report<<"<absent-or-too-long>";
        report<<'\n';
    }
    report.close(); return report?0:11;
}
int Capture(DWORD pid,const std::filesystem::path& output,int mode=0) {
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid));
    if(!process.valid()) { std::cerr<<"Cannot open target read-only: "<<GetLastError()<<'\n'; return 3; }
    Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(!modules.valid()) { std::cerr<<"Module enumeration failed: "<<GetLastError()<<'\n'; return 4; }
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    MODULEENTRY32W target{};
    if(Module32FirstW(modules.value,&entry)) do {
        if(_wcsicmp(entry.szModule,L"Crysis3.exe")==0) { target=entry; break; }
    } while(Module32NextW(modules.value,&entry));
    if(!target.modBaseAddr) { std::cerr<<"PID does not have a Crysis3.exe module\n"; return 5; }
    const auto source=std::filesystem::path(target.szExePath);
    const auto sourceHash=Hash(source);
    if(!SupportedHash(sourceHash)) { std::cerr<<"Refusing unrecognized on-disk executable\n"; return 6; }
    const uintptr_t base=reinterpret_cast<uintptr_t>(target.modBaseAddr);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    const bool dosRead=Read(process.value,base,&dos,sizeof(dos));
    const DWORD dosError=dosRead?0:GetLastError();
    const bool ntRead=dosRead && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=64 && dos.e_lfanew<=4096 &&
        Read(process.value,base+uintptr_t(dos.e_lfanew),&nt,sizeof(nt));
    std::array<uint8_t,1024> observedBytes{};
    const bool observedHeader=sourceHash==PermissionTrialHash && target.modBaseSize==0x052a3000 && ntRead &&
        nt.OptionalHeader.SizeOfImage==0x052a1000 && nt.OptionalHeader.SizeOfHeaders==observedBytes.size() &&
        Read(process.value,base,observedBytes.data(),observedBytes.size()) &&
        HashBytes(observedBytes.data(),ULONG(observedBytes.size()))==ObservedHeaderHash;
    std::cout<<"Candidate base=0x"<<std::hex<<base<<" moduleBytes=0x"<<target.modBaseSize
             <<" dosRead="<<dosRead<<" dosError="<<dosError<<" dosMagic=0x"<<dos.e_magic
             <<" ntOffset=0x"<<dos.e_lfanew<<" ntRead="<<ntRead<<" ntSignature=0x"<<nt.Signature
             <<" machine=0x"<<nt.FileHeader.Machine<<" optionalMagic=0x"<<nt.OptionalHeader.Magic
             <<" sections="<<std::dec<<nt.FileHeader.NumberOfSections
             <<" optionalBytes="<<nt.FileHeader.SizeOfOptionalHeader
             <<" imageBytes="<<nt.OptionalHeader.SizeOfImage<<'\n';
    if(!ntRead || nt.Signature!=IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.FileHeader.NumberOfSections<1 || nt.FileHeader.NumberOfSections>32 || nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) ||
        (!observedHeader && nt.OptionalHeader.SizeOfImage!=target.modBaseSize) || target.modBaseSize>128*1024*1024) {
        std::cerr<<"Loaded PE headers are unavailable or inconsistent\n";
        RejectedHeaderEvidence(process.value,base,target.modBaseSize,sourceHash,output); return 7;
    }
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    uintptr_t sectionAddress=base+uintptr_t(dos.e_lfanew)+sizeof(nt);
    if(!Read(process.value,sectionAddress,sections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER))) return 8;
    if(uint64_t(base)+target.modBaseSize>uint64_t(UINT32_MAX)+1 || !LayoutMatchesFile(source,nt,sections,observedHeader)) {
        std::cerr<<"Loaded section layout differs from the recognized disk image\n";
        RejectedHeaderEvidence(process.value,base,target.modBaseSize,sourceHash,output); return 8;
    }
    if(mode!=0) {
        if(!observedHeader) { std::cerr<<"Root probe requires the observed live header profile\n"; return 16; }
        if(mode==3) return PerformanceSurvey(process.value,pid,output);
        if(mode==2) return Inspect(process.value,base,sourceHash,output);
        return CaptureGameRoot(process.value,base,sourceHash,output);
    }
    auto text=std::find_if(sections.begin(),sections.end(),[](const auto& section) {
        constexpr BYTE name[]{'.','t','e','x','t',0,0,0}; return std::equal(std::begin(name),std::end(name),section.Name);
    });
    if(text==sections.end() || !(text->Characteristics&IMAGE_SCN_CNT_CODE) || !text->Misc.VirtualSize ||
        uint64_t(text->VirtualAddress)+text->Misc.VirtualSize>target.modBaseSize || text->Misc.VirtualSize>64*1024*1024) return 9;
    auto rdata=std::find_if(sections.begin(),sections.end(),[](const auto& section) {
        constexpr BYTE name[]{'.','r','d','a','t','a',0,0}; return std::equal(std::begin(name),std::end(name),section.Name);
    });
    auto faultSection=std::find_if(sections.begin(),sections.end(),[](const auto& section) {
        return section.VirtualAddress<=CapturedFaultRva && uint64_t(CapturedFaultRva)<uint64_t(section.VirtualAddress)+section.Misc.VirtualSize;
    });
    if(rdata==sections.end() || !rdata->Misc.VirtualSize || rdata->Misc.VirtualSize>8*1024*1024 ||
        uint64_t(rdata->VirtualAddress)+rdata->Misc.VirtualSize>target.modBaseSize ||
        faultSection==sections.end() || std::memcmp(faultSection->Name,".data\0\0\0",8)!=0 ||
        uint64_t(faultSection->VirtualAddress)+faultSection->Misc.VirtualSize>target.modBaseSize ||
        uint64_t(CapturedFaultRva & ~uint32_t(4095))+4096>target.modBaseSize ||
        nt.OptionalHeader.SizeOfHeaders<sizeof(nt) || nt.OptionalHeader.SizeOfHeaders>65536 ||
        nt.OptionalHeader.SizeOfHeaders>target.modBaseSize) return 9;
    // Never overwrite earlier evidence. The directory must be newly created.
    if(std::filesystem::exists(output) || !std::filesystem::create_directory(output)) {
        std::cerr<<"Output must be a new directory with an existing parent\n"; return 10;
    }
    auto destination=output/"loaded-text.bin";
    std::ofstream binary(destination,std::ios::binary);
    std::ofstream report(output/"capture.json");
    if(!binary || !report) return 11;
    const uint64_t started=GetTickCount64();
    const bool regionsCopied=MemoryRegions(process.value,base,target.modBaseSize,output/"memory-regions.jsonl");
    const auto regionsHash=Hash(output/"memory-regions.jsonl");
    const bool regionsComplete=regionsCopied && regionsHash.size()==64;
    const auto headers=CopyRegion(process.value,base,nt.OptionalHeader.SizeOfHeaders,output/"loaded-headers.bin",started);
    const auto constants=CopyRegion(process.value,base+rdata->VirtualAddress,rdata->Misc.VirtualSize,output/"loaded-rdata.bin",started);
    const uint32_t faultPageRva=CapturedFaultRva & ~uint32_t(4095);
    const auto faultPage=CopyRegion(process.value,base+faultPageRva,4096,output/"loaded-fault-page.bin",started);
    std::array<uint8_t,4096> block{},again{};
    uint64_t copied=0;
    bool complete=true;
    while(copied<text->Misc.VirtualSize) {
        size_t count=std::min<size_t>(block.size(),text->Misc.VirtualSize-size_t(copied));
        uintptr_t address=base+text->VirtualAddress+uintptr_t(copied);
        // Two identical reads catch page-level mutation, but do not constitute
        // an atomic process snapshot. This limitation is recorded below.
        if(GetTickCount64()-started>30000 || !Alive(process.value) || !Read(process.value,address,block.data(),count) ||
            !Read(process.value,address,again.data(),count) || !std::equal(block.begin(),block.begin()+count,again.begin())) {
            complete=false; break;
        }
        binary.write(reinterpret_cast<const char*>(block.data()),std::streamsize(count));
        if(!binary) { complete=false; break; }
        copied+=count;
    }
    binary.close();
    const auto captureHash=Hash(destination);
    complete=complete && bool(binary) && captureHash.size()==64 && regionsComplete && headers.complete && constants.complete && faultPage.complete;
    IMAGE_DOS_HEADER finalDos{}; IMAGE_NT_HEADERS32 finalNt{};
    complete=complete && Alive(process.value) && Read(process.value,base,&finalDos,sizeof(finalDos)) &&
        Read(process.value,base+uintptr_t(dos.e_lfanew),&finalNt,sizeof(finalNt)) &&
        std::memcmp(&dos,&finalDos,sizeof(dos))==0 && std::memcmp(&nt,&finalNt,sizeof(nt))==0;
    std::vector<IMAGE_SECTION_HEADER> finalSections(sections.size());
    complete=complete && Read(process.value,sectionAddress,finalSections.data(),finalSections.size()*sizeof(IMAGE_SECTION_HEADER)) &&
        std::memcmp(sections.data(),finalSections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER))==0 && Hash(source)==sourceHash;
    std::array<uint8_t,1024> finalObserved{};
    complete=complete && (!observedHeader || (headers.hash==ObservedHeaderHash &&
        Read(process.value,base,finalObserved.data(),finalObserved.size()) && finalObserved==observedBytes));
    report<<"{\n  \"schema\": 2,\n  \"pid\": "<<pid<<",\n  \"sourceSha256\": \""<<sourceHash<<"\",\n  \"imageBase\": "<<base
          <<",\n  \"sectionRva\": "<<text->VirtualAddress<<",\n  \"sectionVirtualBytes\": "<<text->Misc.VirtualSize
          <<",\n  \"capturedBytes\": "<<copied<<",\n  \"durationMs\": "<<GetTickCount64()-started
          <<",\n  \"captureSha256\": \""<<captureHash<<"\",\n  \"complete\": "<<(complete?"true":"false")
          <<",\n  \"memoryRegionsComplete\": "<<(regionsComplete?"true":"false")
          <<",\n  \"memoryRegionsSha256\": \""<<regionsHash<<"\""
          <<",\n  \"layoutProfile\": \""<<(observedHeader?"observed-20260907-5section":"disk-layout")<<"\""
          <<",\n  \"moduleBytes\": "<<target.modBaseSize
          <<",\n  \"rdata\": {\"rva\":"<<rdata->VirtualAddress<<",\"virtualBytes\":"<<rdata->Misc.VirtualSize
          <<",\"capturedBytes\":"<<constants.bytes<<",\"sha256\":\""<<constants.hash<<"\",\"complete\":"<<(constants.complete?"true":"false")<<"}"
          <<",\n  \"faultPage\": {\"rva\":"<<faultPageRva<<",\"capturedBytes\":"<<faultPage.bytes
          <<",\"sha256\":\""<<faultPage.hash<<"\",\"complete\":"<<(faultPage.complete?"true":"false")<<"}"
          <<",\n  \"headers\": {\"capturedBytes\":"<<headers.bytes<<",\"sha256\":\""<<headers.hash
          <<"\",\"complete\":"<<(headers.complete?"true":"false")<<"}"
          <<",\n  \"atomicSnapshot\": false,\n  \"decryptionVerified\": false,\n  \"nativeHooksVerified\": false\n}\n";
    report.close();
    if(!complete || !report) { std::cerr<<"INCOMPLETE capture retained only for diagnostics; do not use for hook contracts\n"; return 12; }
    std::cout<<"Read-only .text capture complete; decryption and native code still require analysis\n";
    return 0;
}
int CaptureAuto(const std::filesystem::path& output,int mode=0) {
    Handle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(!processes.valid()) { std::cerr<<"Process enumeration failed: "<<GetLastError()<<'\n'; return 4; }
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    c3::research::ProcessSelection selection;
    if(!Process32FirstW(processes.value,&entry)) {
        std::cerr<<"Cannot enumerate processes in this Windows/Wine environment\n"; return 4;
    }
    do { selection.Observe(entry.th32ProcessID,entry.szExeFile); }
    while(Process32NextW(processes.value,&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES) { std::cerr<<"Process enumeration was incomplete\n"; return 4; }
    if(selection.ambiguous) {
        std::cerr<<"Multiple Crysis3.exe processes: refusing to guess; supply the intended Windows PID\n"; return 14;
    }
    if(!selection.Unique()) {
        std::cerr<<"No Crysis3.exe process in this Windows/Wine environment; start the game first\n"; return 15;
    }
    std::cout<<"Found Crysis candidate PID "<<selection.pid<<"; verifying image before read-only capture\n";
    return Capture(selection.pid,output,mode);
}
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--check-image") {
        const auto hash=Hash(std::filesystem::path(argv[2]));
        if(!SupportedHash(hash)) { std::cerr<<"Unsupported research image\n"; return 6; }
        std::cout<<"Verified "<<(hash==PermissionTrialHash?"permission-trial":"original")<<" image "<<hash<<'\n';
        return 0;
    }
    if(argc!=3) { std::cerr<<"Usage inside the game's Windows/Wine environment: Crysis3RuntimeCapture PID|--auto|--probe-auto|--inspect-auto|--performance-auto NEW_OUTPUT_DIRECTORY\n"; return 2; }
    try {
        if(std::string(argv[1])=="--probe-auto") return CaptureAuto(std::filesystem::path(argv[2]),1);
        if(std::string(argv[1])=="--inspect-auto") return CaptureAuto(std::filesystem::path(argv[2]),2);
        if(std::string(argv[1])=="--performance-auto") return CaptureAuto(std::filesystem::path(argv[2]),3);
        if(std::string(argv[1])=="--auto") return CaptureAuto(std::filesystem::path(argv[2]));
        std::string input(argv[1]); size_t used{}; unsigned long pid=std::stoul(input,&used);
        if(!pid || used!=input.size() || pid>MAXDWORD) return 2;
        return Capture(DWORD(pid),std::filesystem::path(argv[2]));
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 13; }
}
