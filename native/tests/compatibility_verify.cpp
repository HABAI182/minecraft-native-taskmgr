#include "runtime_profile.h"
#include "MinHook.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <fstream>
HRESULT __fastcall NeverCalled(void*){return S_OK;}
int wmain(int argc,wchar_t** argv){
    try{
        std::wstring image;if(argc>1)image=argv[1];else{wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);image=std::wstring(system)+L"\\Taskmgr.exe";}
        CompatibilityOptions options;options.forceSymbols=true;
        if(argc>2&&wcscmp(argv[2],L"-")!=0)options.pdbOverride=argv[2];
        if(argc>3)options.cacheDirectory=argv[3];
        for(int i=4;i<argc;i++){
            if(wcscmp(argv[i],L"--offline")==0)options.allowDownload=false;
            if(wcscmp(argv[i],L"--embedded")==0)options.forceSymbols=false;
            if(wcscmp(argv[i],L"--cancel")==0)options.cancelled=[](){return true;};
        }
        auto result=ResolveCompatibility(image,options);
        // Map the PE without resolving imports or running its entry point. The
        // trampoline checks never call any Taskmgr code and never inject a process.
        HMODULE module=LoadLibraryExW(image.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!module)return 2;
        bool valid=MH_Initialize()==MH_OK;
        unsigned count=0;
        NativeFn hooks[]={NativeFn::LoadLogical,NativeFn::Initialize,NativeFn::Destructor,NativeFn::Layout,NativeFn::UpdateData,NativeFn::SwitchChart,NativeFn::ViewUpdate,NativeFn::ViewDestructor};
        for(auto fn:hooks){if(!result.profile[fn].rva)continue;void* gateway=nullptr;valid=valid&&MH_CreateHook(reinterpret_cast<uint8_t*>(module)+result.profile[fn].rva,reinterpret_cast<void*>(NeverCalled),&gateway)==MH_OK;++count;}
        MH_Uninitialize();FreeLibrary(module);if(!valid)return 3;
        auto report=result.Json();report.pop_back();report+=",\"all_trampolines\":true,\"trampoline_count\":"+std::to_string(count)+",\"injected\":false}";
        puts(report.c_str());return 0;
    }catch(const CompatibilityError& error){puts(CompatibilityFailureJson(argc>1?argv[1]:L"",error).c_str());return 1;}
    catch(const std::exception&){puts("{\"compatible\":false,\"error\":\"unexpected\",\"injected\":false}");return 1;}
}
