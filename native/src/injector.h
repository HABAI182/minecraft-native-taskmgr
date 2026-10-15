#pragma once
#include "protocol.h"
#include <string>
#include <vector>
#include <stdexcept>

struct WinError : std::runtime_error {
    DWORD code;
    std::wstring message;
    WinError(const std::wstring& what, DWORD value=GetLastError());
};

std::wstring ProcessPath(DWORD pid);
std::wstring FileName(const std::wstring& path);
uintptr_t RemoteModule(DWORD pid, const std::wstring& path, bool exactPath=true);
std::wstring ExtractPayload(HINSTANCE instance);

// 一个会话只允许 Taskmgr.exe 或专用测试夹具。GUI 不接受任意 PID/DLL 参数。
class Session {
    HANDLE process_=nullptr, thread_=nullptr, mapping_=nullptr, stop_=nullptr,
           ready_=nullptr, lock_=nullptr, frameMutex_=nullptr;
    void* remoteArgs_=nullptr;
    uintptr_t loadedModule_=0;
    bool ownsFile_=false,loaderPending_=false;
    std::wstring dllPath_, mapName_;
public:
    SharedState* state=nullptr;
    DWORD targetPid=0;
    Session()=default;
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    ~Session();
    void Start(HWND target, HWND source, RECT area, const std::wstring& dllPath, const NativeProfile& profile, bool fixture=false, bool ownsFile=true);
    void Heartbeat();
    void RequestStop();
    bool Ended() const;
    bool Stop(DWORD timeout=5000);
};
