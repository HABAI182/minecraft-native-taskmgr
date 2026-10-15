#include "symbol_cache.h"
#include <dbghelp.h>
#include <winhttp.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <cwchar>

namespace{
std::mutex symbolsMutex;
struct Internet{HINTERNET value=nullptr;Internet(HINTERNET p):value(p){}~Internet(){if(value)WinHttpCloseHandle(value);}operator HINTERNET()const{return value;}};
[[noreturn]]void NetworkError(){throw CompatibilityError("symbol_download",L"无法取得微软匹配符号。请检查网络或系统代理后重试；已缓存的版本可离线使用。任务管理器尚未修改。");}
bool Matching(const std::filesystem::path& path,const PeImage& image){
    // MSF7 states its complete block count in the superblock. A truncated proxy
    // response may still contain a valid PDB GUID, so check the full length too.
    std::ifstream stream(path,std::ios::binary|std::ios::ate);if(!stream||stream.tellg()<56)return false;
    uint64_t length=uint64_t(stream.tellg());stream.seekg(0);std::array<uint8_t,56> header{};stream.read(reinterpret_cast<char*>(header.data()),header.size());
    uint32_t block=0,blocks=0;memcpy(&block,header.data()+32,4);memcpy(&blocks,header.data()+40,4);
    if(memcmp(header.data(),"Microsoft C/C++ MSF 7.00",23)!=0||block<512||block>65536||(block&(block-1))||uint64_t(block)*blocks!=length)return false;
    std::lock_guard<std::mutex> lock(symbolsMutex);
    SYMSRV_INDEX_INFOW info{};info.sizeofstruct=sizeof(info);
    return SymSrvGetFileIndexInfoW(path.c_str(),&info,0)&&IsEqualGUID(info.guid,image.guid)&&info.age==image.age;
}
void Download(const PeImage& image,const std::filesystem::path& path,const CompatibilityOptions& options){
    // Only Microsoft symbols, over normal validated HTTPS. No executable or DLL
    // is downloaded. Range responses are checked to avoid caching a truncated PDB.
    wchar_t proxyText[2048]{};std::wstring proxy;
    if(GetEnvironmentVariableW(L"HTTPS_PROXY",proxyText,2048)&&wcslen(proxyText)<2000){
        URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=DWORD(-1);
        if(WinHttpCrackUrl(proxyText,0,0,&parts)&&parts.dwHostNameLength){
            proxy.assign(parts.lpszHostName,parts.dwHostNameLength);proxy+=L":"+std::to_wstring(parts.nPort);}}
    Internet session(WinHttpOpen(L"MCNativeTaskmgr/0.2.0",proxy.empty()?WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY:WINHTTP_ACCESS_TYPE_NAMED_PROXY,
        proxy.empty()?WINHTTP_NO_PROXY_NAME:proxy.c_str(),WINHTTP_NO_PROXY_BYPASS,0));if(!session)NetworkError();
    WinHttpSetTimeouts(session,10000,10000,20000,30000);
    Internet connection(WinHttpConnect(session,L"msdl.microsoft.com",INTERNET_DEFAULT_HTTPS_PORT,0));if(!connection)NetworkError();
    std::wstring url=L"/download/symbols/"+image.pdbName+L"/"+image.Key()+L"/"+image.pdbName;
    std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file)throw CompatibilityError("symbol_cache_write",L"无法写入本用户的符号缓存目录，请检查可用空间与目录权限。");
    constexpr uint64_t maximum=128*1024*1024,chunk=4*1024*1024;
    uint64_t offset=0,total=0;
    do{
        if(options.cancelled&&options.cancelled())throw CompatibilityError("cancelled",L"已取消匹配。");
        Internet request(WinHttpOpenRequest(connection,L"GET",url.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));if(!request)NetworkError();
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
        std::wstring range=L"Range: bytes="+std::to_wstring(offset)+L"-"+std::to_wstring(offset+chunk-1)+L"\r\nAccept-Encoding: identity\r\nCache-Control: no-cache\r\n";
        if(!WinHttpSendRequest(request,range.c_str(),DWORD(range.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))NetworkError();
        DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&length,nullptr))NetworkError();
        uint64_t expected=0;
        if(status==206){
            wchar_t header[256]{};length=sizeof(header);
            unsigned long long start=0,end=0,size=0;
            if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_CUSTOM,L"Content-Range",header,&length,nullptr)||swscanf_s(header,L"bytes %llu-%llu/%llu",&start,&end,&size)!=3
                ||start!=offset||end<start||end>=size||size>maximum||(total&&total!=size))NetworkError();
            total=size;expected=end-start+1;
        }else if(status==200&&offset==0){
            wchar_t header[64]{};length=sizeof(header);
            if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH,nullptr,header,&length,nullptr))expected=_wcstoui64(header,nullptr,10);
            if(expected>maximum)NetworkError();total=expected;
        }else if(status==404)throw CompatibilityError("symbols_unavailable",L"微软尚未发布此 Taskmgr 版本的公开符号，暂不能自动适配。任务管理器尚未修改；可导出兼容性报告以继续适配。");
        else NetworkError();
        std::array<char,64*1024> buffer{};DWORD got=0;uint64_t received=0;
        while(true){
            if(options.cancelled&&options.cancelled())throw CompatibilityError("cancelled",L"已取消匹配。");
            if(!WinHttpReadData(request,buffer.data(),DWORD(buffer.size()),&got))NetworkError();if(!got)break;
            received+=got;if(offset+received>maximum||(expected&&received>expected))NetworkError();file.write(buffer.data(),got);if(!file)NetworkError();
        }
        if(!received||(expected&&received!=expected))NetworkError();offset+=received;
        if(options.progress)options.progress(L"首次匹配：正在下载微软符号 "+std::to_wstring(offset/(1024*1024))+L" / "+(total?std::to_wstring(total/(1024*1024)):L"?")+L" MB");
        if(status==200){total=offset;break;}
    }while(offset<total);
    file.close();if(!file||offset!=total)NetworkError();
}
constexpr const char* symbolNames[kNativeFunctions]={
    "?LoadLogicalCpuChart@AtmCpuView@@AEAAJXZ",
    "?LoadHeatMap@AtmCpuView@@AEAAJPEAU_WDC_NUMA_ID@@@Z",
    "?GetProcessorDetailsLogicalCount@RunTimeSettings@@QEAAGXZ",
    "?Initialize@CpuHeatMap@@QEAAJPEAVTmTraceControl@@PEAVDUIParserHelper@@PEAVElement@DirectUI@@PEBU_WDC_NUMA_ID@@@Z",
    "?UpdateData@CpuHeatMap@@QEAAJXZ","?LayoutCpuBlocks@CpuHeatMap@@AEAAJXZ","??1CpuHeatMap@@QEAA@XZ",
    "?SetBlockData@CpuHeatMap@@AEAAJIPEBGKK@Z",
    "?GetBlockColors@CpuHeatMap@@AEAAXHPEAK0@Z",
    "?SwitchChart@AtmCpuView@@QEAAJW4ATM_CPU_CHART_MODE@@_N@Z",
    "?GetCpuIdList@AtmCpuView@@AEAAJPEAU_WDC_NUMA_ID@@K@Z",
    "?Update@AtmCpuView@@MEAAJXZ","??3@YAXPEAX_K@Z","??1AtmCpuView@@UEAA@XZ","??_V@YAXPEAX@Z"
};
}
std::filesystem::path CompatibilityCache(){
    wchar_t local[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    if(!n||n>=32768)throw CompatibilityError("localappdata",L"无法定位本用户的 LOCALAPPDATA 缓存目录。");
    return std::filesystem::path(local)/L"MCNativeTaskmgr"/L"symbols";
}
std::filesystem::path FindMatchingPdb(const PeImage& image,const CompatibilityOptions& options,bool& downloaded){
    auto directory=(options.cacheDirectory.empty()?CompatibilityCache():options.cacheDirectory)/image.Key();
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)throw CompatibilityError("symbol_cache_write",L"无法创建符号缓存目录。");
    auto target=directory/image.pdbName;
    if(!options.pdbOverride.empty()){
        if(!Matching(options.pdbOverride,image))throw CompatibilityError("pdb_identity",L"提供的离线 PDB 与 Taskmgr 的 GUID/age 不匹配。");
        if(!std::filesystem::equivalent(options.pdbOverride,target,error))std::filesystem::copy_file(options.pdbOverride,target,std::filesystem::copy_options::overwrite_existing);
    }
    if(std::filesystem::exists(target)&&Matching(target,image))return target;
    if(!options.allowDownload)throw CompatibilityError("offline_cache_miss",L"此 Taskmgr 版本尚未缓存匹配符号；请首次联网匹配，或提供 GUID/age 相同的离线 PDB。");
    // DbgHelp identifies the file format by its final extension as well.
    auto temporary=directory/(L"part-"+std::to_wstring(GetCurrentProcessId())+L"-"+image.pdbName);
    try{
        Download(image,temporary,options);
        if(!Matching(temporary,image))throw CompatibilityError("downloaded_pdb_identity",L"下载的符号不完整或 GUID/age 不匹配，已拒绝使用。请重新匹配。");
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw CompatibilityError("symbol_cache_write",L"无法保存已验证的符号缓存。");
    }catch(...){std::filesystem::remove(temporary,error);throw;}
    downloaded=true;return target;
}
void ResolvePublicSymbols(const std::wstring& imagePath,const PeImage& image,const std::filesystem::path& pdb,NativeProfile& output){
    std::lock_guard<std::mutex> lock(symbolsMutex);
    HANDLE process=GetCurrentProcess();
    SymSetOptions(SYMOPT_EXACT_SYMBOLS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS|SYMOPT_IGNORE_NT_SYMPATH|SYMOPT_DEFERRED_LOADS);
    if(!SymInitializeW(process,pdb.parent_path().c_str(),FALSE))throw CompatibilityError("symbol_reader",L"无法初始化 Windows 符号读取器。");
    struct Cleanup{HANDLE p;~Cleanup(){SymCleanup(p);}} cleanup{process};
    DWORD64 base=SymLoadModuleExW(process,nullptr,imagePath.c_str(),L"Taskmgr",0x180000000,image.Header().OptionalHeader.SizeOfImage,nullptr,0);
    if(!base)throw CompatibilityError("symbol_reader",L"Windows 无法加载当前文件的匹配符号。缓存可能不完整，请清理该版本缓存后重试。");
    auto find=[&](const char* name)->uint32_t{
        alignas(SYMBOL_INFO) uint8_t storage[sizeof(SYMBOL_INFO)+1024]{};
        auto info=reinterpret_cast<SYMBOL_INFO*>(storage);info->SizeOfStruct=sizeof(SYMBOL_INFO);info->MaxNameLen=1024;
        std::string qualified=std::string("Taskmgr!")+name;
        if(!SymFromName(process,qualified.c_str(),info)||info->Address<base||info->Address-base>=image.Header().OptionalHeader.SizeOfImage)return 0;
        return uint32_t(info->Address-base);
    };
    for(size_t i=0;i<kNativeFunctions;i++){
        uint32_t rva=find(symbolNames[i]);
        // Earlier builds inline the count getter into both chart loaders.
        // The decoder verifies those two loads instead; this optional entry is
        // never hooked or called by the bridge.
        if(!rva&&(NativeFn(i)==NativeFn::LogicalCount||NativeFn(i)==NativeFn::Destructor))continue;
        if(!rva&&NativeFn(i)==NativeFn::DeleteObject)rva=find("??3@YAXPEAX@Z");
        if(!rva)throw CompatibilityError(std::string("missing_symbol:")+NativeFunctionName(NativeFn(i)),L"当前版本缺少所需的原生热力图函数："+std::wstring(NativeFunctionName(NativeFn(i)),NativeFunctionName(NativeFn(i))+strlen(NativeFunctionName(NativeFn(i)))));
        if(!image.Executable(rva))throw CompatibilityError("symbol_range",L"符号没有指向当前 Taskmgr 的可执行代码区。");
        output.entries[i].rva=rva;memcpy(output.entries[i].preimage,image.Rva(rva,16),16);
    }
    output.globalSettings=find("?g_GlobalSettings@@3VRunTimeSettings@@A");
    if(!output.globalSettings)throw CompatibilityError("missing_global_settings",L"无法定位当前版本的原生设置对象。");
    IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);
    if(!SymGetModuleInfo64(process,base,&module)||module.PdbUnmatched||!IsEqualGUID(module.PdbSig70,image.guid)||module.PdbAge!=image.age)
        throw CompatibilityError("pdb_identity",L"Windows 符号读取器没有确认 GUID/age 匹配，已拒绝适配。");
}
