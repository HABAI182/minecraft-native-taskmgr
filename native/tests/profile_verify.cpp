#include <windows.h>
#include "profile.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
using Count=uint16_t(__fastcall*)(void*);
Count original=nullptr;
extern "C" uint16_t ProbeLeafCount(Count,void*,uint64_t*);
uint16_t __fastcall Detour(void*){return 2304;}
int main(){
    // 只映射本机文件，不运行 Taskmgr 入口，不访问真实任务管理器进程。
    wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\Taskmgr.exe");
    HMODULE image=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image)return 1;auto base=reinterpret_cast<uintptr_t>(image);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->FileHeader.TimeDateStamp!=Profile::Timestamp||pe->OptionalHeader.SizeOfImage!=Profile::ImageSize)return 2;
    uint32_t addresses[]={Profile::LoadLogical,Profile::SwitchChart,Profile::Initialize,Profile::UpdateData,Profile::Layout,Profile::Destructor,Profile::ViewUpdate};
    const uint8_t* bytes[]={Profile::LoadLogicalBytes,Profile::SwitchChartBytes,Profile::InitializeBytes,Profile::UpdateDataBytes,Profile::LayoutBytes,Profile::DestructorBytes,Profile::ViewUpdateBytes};
    if(MH_Initialize()!=MH_OK)return 3;
    for(int i=0;i<7;i++){
        void* code=reinterpret_cast<void*>(base+addresses[i]);
        if(memcmp(code,bytes[i],16)!=0)return 4;
        void* gateway=nullptr;
        if(MH_CreateHook(code,reinterpret_cast<void*>(Detour),&gateway)!=MH_OK)return 5;
    }
    auto getter=reinterpret_cast<Count>(base+Profile::LogicalCount);
    alignas(16) unsigned char settings[0x950]{};*reinterpret_cast<uint16_t*>(settings+0x938)=32;
    if(getter(settings)!=32||MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)return 6;
    if(getter(settings)!=32||memcmp(reinterpret_cast<void*>(getter),Profile::LogicalCountBytes,16)!=0)return 7;
    uint64_t registers[5]{};
    const uint64_t expected[]={0x1122334455667788,0x2233445566778899,0x33445566778899aa,0x445566778899aabb,0x5566778899aabbcc};
    if(ProbeLeafCount(getter,settings,registers)!=32||memcmp(registers,expected,sizeof(expected))!=0)return 9;
    if(MH_DisableHook(MH_ALL_HOOKS)!=MH_OK||getter(settings)!=32)return 8;
    MH_Uninitialize();FreeLibrary(image);
    puts("{\"binary_profile\":true,\"seven_trampolines\":true,\"core_getter_untouched\":true,\"volatile_registers_preserved\":true,\"native_count_stays_32\":true,\"restored\":true,\"taskmgr_runtime\":false}");return 0;
}
