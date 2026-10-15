#include "injector.h"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <algorithm>
#include <memory>
#include <sstream>

namespace {
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
};
DWORD WindowPid(HWND h){DWORD p=0;GetWindowThreadProcessId(h,&p);return p;}
void Check(bool ok,const wchar_t* what){if(!ok)throw WinError(what);}
void CheckX64(HANDLE p){
    USHORT machine=0,native=0;
    Check(IsWow64Process2(p,&machine,&native)!=FALSE,L"IsWow64Process2");
    if(machine!=IMAGE_FILE_MACHINE_UNKNOWN || native!=IMAGE_FILE_MACHINE_AMD64)
        throw WinError(L"该版本只支持原生 x64 Windows 与 x64 任务管理器",ERROR_BAD_EXE_FORMAT);
}
// GetProcAddress 可能返回 KernelBase 中的转发地址，不能假设远端系统 DLL 地址相同。
uintptr_t RemoteSystemFunction(DWORD pid,const char* name){
    auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);
    Check(address!=nullptr,L"GetProcAddress");
    HMODULE owner=nullptr;
    Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&owner)!=FALSE,
          L"定位系统函数所属模块");
    wchar_t path[32768]{};
    Check(GetModuleFileNameW(owner,path,32768)!=0,L"GetModuleFileNameW");
    auto base=RemoteModule(pid,FileName(path),false);
    Check(base!=0,L"远端系统模块未找到");
    return base+(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(owner));
}
std::wstring UniqueName(){
    unsigned char bytes[16]{};
    if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
        throw WinError(L"BCryptGenRandom",ERROR_GEN_FAILURE);
    const wchar_t* digits=L"0123456789abcdef";
    std::wstring name=L"Local\\MCInternal.";
    for(auto b:bytes){name+=digits[b>>4];name+=digits[b&15];}
    return name;
}
}

WinError::WinError(const std::wstring& what,DWORD value):std::runtime_error("Windows operation failed"),code(value){
    wchar_t text[1024]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,value,0,text,1024,nullptr);
    message=what+L"\n错误 "+std::to_wstring(value)+L"："+text;
}
std::wstring FileName(const std::wstring& path){auto p=path.find_last_of(L"\\/");return p==path.npos?path:path.substr(p+1);}
std::wstring ProcessPath(DWORD pid){
    Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
    if(!p.h)return L"";
    wchar_t path[32768]{};DWORD n=32768;
    return QueryFullProcessImageNameW(p.h,0,path,&n)?std::wstring(path,n):L"";
}
uintptr_t RemoteModule(DWORD pid,const std::wstring& path,bool exact){
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(snapshot.h==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W m{};m.dwSize=sizeof(m);
    if(Module32FirstW(snapshot.h,&m))do{
        if(_wcsicmp(exact?m.szExePath:m.szModule,path.c_str())==0)
            return reinterpret_cast<uintptr_t>(m.modBaseAddr);
    }while(Module32NextW(snapshot.h,&m));
    return 0;
}

std::wstring ExtractPayload(HINSTANCE instance){
    HRSRC res=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
    Check(res!=nullptr,L"内嵌 DLL 资源缺失");
    DWORD size=SizeofResource(instance,res);
    auto data=LockResource(LoadResource(instance,res));
    Check(data && size>0,L"内嵌 DLL 资源损坏");
    wchar_t temp[32768]{},file[32768]{};
    DWORD n=GetTempPathW(32768,temp);Check(n && n<32768,L"GetTempPathW");
    std::wstring directory=std::wstring(temp)+L"MCInTaskmgr";
    if(!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
        throw WinError(L"创建临时目录");
    Check(GetTempFileNameW(directory.c_str(),L"mct",0,file)!=0,L"GetTempFileNameW");
    Handle f(CreateFileW(file,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(f.h==INVALID_HANDLE_VALUE){DeleteFileW(file);throw WinError(L"写出内嵌 DLL");}
    DWORD written=0;
    if(!WriteFile(f.h,data,size,&written,nullptr) || written!=size){
        CloseHandle(f.h);f.h=nullptr;DeleteFileW(file);throw WinError(L"写出完整 DLL");
    }
    return file; // .tmp 扩展名不影响 LoadLibraryW，内容仍是标准 PE DLL。
}

void Session::Start(HWND target,HWND source,RECT area,const std::wstring& path,const NativeProfile& profile,bool fixture,bool ownsFile){
    if(process_)throw WinError(L"会话已启动",ERROR_BUSY);
    DWORD targetId=WindowPid(target),sourceId=WindowPid(source);
    if(!targetId || !sourceId || target==source)throw WinError(L"窗口无效",ERROR_INVALID_WINDOW_HANDLE);
    auto image=FileName(ProcessPath(targetId));
    wchar_t cls[128]{};GetClassNameW(target,cls,128);
    bool allowed=fixture ? (_wcsicmp(image.c_str(),L"MCInternalFixture.exe")==0 &&
                            wcscmp(cls,L"MCInternalFixtureTarget")==0)
                         : (_wcsicmp(image.c_str(),L"Taskmgr.exe")==0);
    if(!allowed)throw WinError(L"只允许任务管理器或专用测试夹具",ERROR_ACCESS_DENIED);
    RECT client{};Check(GetClientRect(target,&client)!=FALSE,L"GetClientRect");
    int w=client.right,h=client.bottom;
    if(w<1 || h<1 || area.left<0 || area.top<0 || area.right>w || area.bottom>h ||
       area.right<=area.left || area.bottom<=area.top)
        throw WinError(L"显示区域必须位于任务管理器客户区内",ERROR_INVALID_PARAMETER);
    targetPid=targetId;dllPath_=path;ownsFile_=ownsFile;
    std::wstring lockName=L"Local\\MCInternal.Lock."+std::to_wstring(targetPid);
    lock_=CreateMutexW(nullptr,FALSE,lockName.c_str());
    Check(lock_!=nullptr,L"创建会话互斥体");
    if(GetLastError()==ERROR_ALREADY_EXISTS)throw WinError(L"该窗口已有改造会话，请先停止旧会话",ERROR_BUSY);
    process_=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
                         PROCESS_VM_READ|PROCESS_VM_WRITE|SYNCHRONIZE,FALSE,targetPid);
    Check(process_!=nullptr,L"打开任务管理器进程（请允许启动程序的 UAC 提升）");
    CheckX64(process_);
    mapName_=UniqueName();
    mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedState),mapName_.c_str());
    Check(mapping_!=nullptr,L"创建共享状态");
    state=static_cast<SharedState*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState)));
    Check(state!=nullptr,L"映射共享状态");
    ZeroMemory(state,sizeof(*state));
    state->magic=kMagic;state->version=kVersion;state->bytes=sizeof(*state);
    state->profile=profile;
    state->controllerPid=GetCurrentProcessId();state->targetPid=targetPid;state->sourcePid=sourceId;
    state->fixture=fixture?1:0;state->targetHwnd=reinterpret_cast<uint64_t>(target);
    state->flags=1;
    state->sourceHwnd=reinterpret_cast<uint64_t>(source);
    state->left=double(area.left)/w;state->top=double(area.top)/h;
    state->right=double(area.right)/w;state->bottom=double(area.bottom)/h;
    auto stopName=mapName_+L".stop",readyName=mapName_+L".ready";
    wcscpy_s(state->stopEvent,stopName.c_str());wcscpy_s(state->readyEvent,readyName.c_str());
    stop_=CreateEventW(nullptr,TRUE,FALSE,state->stopEvent);
    ready_=CreateEventW(nullptr,TRUE,FALSE,state->readyEvent);
    wcscpy_s(state->frameMutex,(mapName_+L".frame").c_str());
    frameMutex_=CreateMutexW(nullptr,FALSE,state->frameMutex);
    Check(stop_ && ready_ && frameMutex_,L"创建控制事件");Heartbeat();

    // 第一阶段：标准 LoadLibraryW 远程加载。只写 DLL 路径，没有自定义 shellcode。
    uintptr_t remoteLoader=RemoteSystemFunction(targetPid,"LoadLibraryW");
    SIZE_T pathBytes=(path.size()+1)*sizeof(wchar_t),written=0;
    void* remotePath=VirtualAllocEx(process_,nullptr,pathBytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remotePath!=nullptr,L"分配 DLL 路径");
    if(!WriteProcessMemory(process_,remotePath,path.c_str(),pathBytes,&written) || written!=pathBytes){
        VirtualFreeEx(process_,remotePath,0,MEM_RELEASE);throw WinError(L"写入 DLL 路径");
    }
    Handle loader(CreateRemoteThread(process_,nullptr,0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoader),remotePath,0,nullptr));
    if(!loader.h){VirtualFreeEx(process_,remotePath,0,MEM_RELEASE);throw WinError(L"启动 DLL 加载线程");}
    DWORD wait=WaitForSingleObject(loader.h,10000);
    if(wait!=WAIT_OBJECT_0){
        // 加载线程可能仍在读参数：不释放其远端内存，也不强行终止目标线程。
        loaderPending_=true;
        throw WinError(L"加载 DLL 超时；请关闭并重新打开任务管理器后再试",ERROR_TIMEOUT);
    }
    VirtualFreeEx(process_,remotePath,0,MEM_RELEASE);
    uintptr_t base=RemoteModule(targetPid,path);
    if(!base)throw WinError(L"DLL 未进入目标模块列表（可能被系统策略阻止）",ERROR_MOD_NOT_FOUND);
    loadedModule_=base;

    // 第二阶段：显式调用导出函数，所有 Hook/窗口操作在 DllMain 外执行。
    HMODULE local=LoadLibraryW(path.c_str());
    Check(local!=nullptr,L"读取本地 DLL 导出");
    auto function=GetProcAddress(local,"RunBridge");
    uintptr_t rva=function?reinterpret_cast<uintptr_t>(function)-reinterpret_cast<uintptr_t>(local):0;
    FreeLibrary(local);
    if(!rva)throw WinError(L"DLL 缺少 RunBridge 导出",ERROR_PROC_NOT_FOUND);
    StartArgs args{};args.bytes=sizeof(args);args.version=kVersion;
    wcscpy_s(args.mappingName,mapName_.c_str());
    remoteArgs_=VirtualAllocEx(process_,nullptr,sizeof(args),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remoteArgs_!=nullptr,L"分配启动参数");
    Check(WriteProcessMemory(process_,remoteArgs_,&args,sizeof(args),&written)!=FALSE && written==sizeof(args),L"写入启动参数");
    thread_=CreateRemoteThread(process_,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(base+rva),remoteArgs_,0,nullptr);
    Check(thread_!=nullptr,L"启动进程内显示线程");
    HANDLE waits[]={ready_,thread_,process_};
    wait=WaitForMultipleObjects(3,waits,FALSE,10000);
    if(wait!=WAIT_OBJECT_0)throw WinError(L"显示线程未完成初始化",ERROR_TIMEOUT);
    // ready 表示 DLL 已复制参数，随后可以安全释放远端启动块。
    VirtualFreeEx(process_,remoteArgs_,0,MEM_RELEASE);remoteArgs_=nullptr;
    if(InterlockedCompareExchange(&state->status,0,0)!=Running)
        throw WinError((std::wstring(L"原生热力图适配初始化失败：")+state->diagnostic).c_str(),static_cast<DWORD>(state->lastHresult));
}

void Session::Heartbeat(){if(state)InterlockedExchange64(&state->heartbeat,static_cast<LONG64>(GetTickCount64()));}
void Session::RequestStop(){if(stop_)SetEvent(stop_);}
bool Session::Ended() const{return thread_ && WaitForSingleObject(thread_,0)==WAIT_OBJECT_0;}
bool Session::Stop(DWORD timeout){
    RequestStop();
    bool ended=!thread_ || WaitForSingleObject(thread_,timeout)==WAIT_OBJECT_0;
    if(ended && remoteArgs_ && process_){VirtualFreeEx(process_,remoteArgs_,0,MEM_RELEASE);remoteArgs_=nullptr;}
    // 若加载已完成但启动导出之前失败，释放那一次 LoadLibrary 引用。
    // 有工作线程时由 DLL 自己释放，控制端绝不能重复 FreeLibrary。
    if(!thread_ && loadedModule_ && process_){
        try{
            Handle cleanup(CreateRemoteThread(process_,nullptr,0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(RemoteSystemFunction(targetPid,"FreeLibrary")),
                reinterpret_cast<void*>(loadedModule_),0,nullptr));
            if(cleanup.h){loadedModule_=0;if(WaitForSingleObject(cleanup.h,timeout)!=WAIT_OBJECT_0)ended=false;}
            else ended=false;
        }catch(...){ended=false;}
    }
    return ended;
}
Session::~Session(){
    Stop();
    if(state)UnmapViewOfFile(state);
    for(HANDLE h:{thread_,process_,mapping_,stop_,ready_,lock_,frameMutex_})if(h)CloseHandle(h);
    // 正常停止后 DLL 已卸载，删除本次提取文件；异常加载时可能仍被映射，保留即可。
    if(ownsFile_ && !loaderPending_ && !dllPath_.empty())DeleteFileW(dllPath_.c_str());
}
