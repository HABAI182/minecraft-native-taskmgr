#include "injector.h"
#include "network.h"
#include "profile.h"
#include <shellapi.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include <array>
#include <bcrypt.h>
#include <algorithm>
namespace{
HINSTANCE instance;HWND mainWindow,label,startButton,stopButton;HFONT font;
std::unique_ptr<Session> session;std::unique_ptr<Network> network;DWORD gamePid=0;
bool nativeTest=false,rendererTest=false,rendererGame=false;uint64_t nativeTestStarted=0,renderStarted=0;
uint64_t nextRenderReport=0;
HANDLE testFrameMutex=nullptr;uint32_t testSequence=0;
std::wstring nativeTestDll;uint16_t testCpuBefore=0;
bool ReadCore(DWORD pid,uint16_t& count,bool& untouched){
    uintptr_t base=RemoteModule(pid,L"Taskmgr.exe",false);if(!base)return false;
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
    std::array<uint8_t,16> bytes{};SIZE_T read=0;
    bool result=ReadProcessMemory(process,reinterpret_cast<void*>(base+Profile::LogicalCount),bytes.data(),bytes.size(),&read)&&read==bytes.size();
    if(result)untouched=memcmp(bytes.data(),Profile::LogicalCountBytes,bytes.size())==0;
    result=result&&ReadProcessMemory(process,reinterpret_cast<void*>(base+0x5420d0+0x938),&count,sizeof(count),&read)&&read==sizeof(count);
    CloseHandle(process);return result;
}
void TestReport(const std::string& json){
    wchar_t executable[32768]{};GetModuleFileNameW(instance,executable,32768);
    auto directory=std::filesystem::path(executable).parent_path()/L"evidence";
    std::filesystem::create_directories(directory);
    std::ofstream(directory/(rendererGame?L"native-game-renderer-test.json":rendererTest?L"native-renderer-test.json":L"native-startup-test.json"),std::ios::binary)<<json;
}
std::string RenderSnapshot(SharedState* s){
    std::string json="{\"frame\":"+std::to_string(s->frameSequence)+",\"paintedFrames\":"+std::to_string(s->paintedFrames)+
        ",\"nativeCells\":"+std::to_string(s->nativeCells)+",\"stage\":"+std::to_string(s->renderStage)+",\"hresult\":"+std::to_string(s->renderHresult)+
        ",\"initialVisible\":"+std::to_string(s->initialContainerVisible)+",\"visible\":"+std::to_string(s->containerVisible)+
        ",\"extent\":["+std::to_string(s->layoutWidth)+","+std::to_string(s->layoutHeight)+"],\"cell\":["+std::to_string(s->cellWidth)+","+std::to_string(s->cellHeight)+"]"+
        ",\"luma\":["+std::to_string(s->sourceLumaMin)+","+std::to_string(s->sourceLumaMax)+"],\"buckets\":"+std::to_string(s->sourceBuckets)+",\"palette\":[";
    for(int i=0;i<6;i++){if(i)json+=",";json+=std::to_string(s->palette[i]);}
    auto rect=[](RECT r){return "["+std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom)+"]";};
    return json+"],\"first\":"+rect(s->firstCell)+",\"last\":"+rect(s->lastCell)+",\"bounds\":"+rect(s->nativeBounds)+
        ",\"source\":["+std::to_string(s->sourceWidth)+","+std::to_string(s->sourceHeight)+"],\"frameFlags\":"+std::to_string(s->frameFlags)+"}";
}
void RenderReport(SharedState* s){
    if(GetTickCount64()<nextRenderReport)return;
    nextRenderReport=GetTickCount64()+1000;
    wchar_t executable[32768]{};GetModuleFileNameW(instance,executable,32768);
    auto directory=std::filesystem::path(executable).parent_path()/L"evidence";
    if(!nativeTest){
        wchar_t local[32768]{};
        if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))return;
        directory=std::filesystem::path(local)/L"MCNativeTaskmgr";
    }
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return;
    std::ofstream(directory/L"renderer-live.json",std::ios::binary)<<RenderSnapshot(s);
}
void TestFrame(SharedState* s){
    if(!testFrameMutex||WaitForSingleObject(testFrameMutex,0)!=WAIT_OBJECT_0)return;
    // Only the explicit renderer test uses this moving grayscale test card.
    // Production frames still come exclusively from the unchanged game bridge.
    constexpr uint8_t shades[]={0,45,90,135,180,255};
    for(int y=0;y<kHeight;y++)for(int x=0;x<kWidth;x++){
        int band=std::min(5,x*6/kWidth);
        uint8_t v=((x-int(testSequence/2))%64+64)%64<3?255:shades[band];
        int i=(y*kWidth+x)*3;s->rgb[i]=s->rgb[i+1]=s->rgb[i+2]=v;
    }
    s->sourceWidth=kWidth;s->sourceHeight=kHeight;s->frameFlags=0;s->frameSequence=++testSequence;
    ReleaseMutex(testFrameMutex);
}
std::wstring Status(){
    if(!session)return L"进入 Minecraft 世界后点“开始”。\n任务管理器打开 性能 → CPU，程序会自动切换为原生热力图。";
    auto s=session->state;
    const wchar_t* stage[]={L"等待打开性能 → CPU",L"等待热力图布局",L"等待游戏帧",L"已绘制",L"绘制失败"};
    return L"游戏帧："+std::to_wstring(s->frameSequence)+L"  原生格子："+std::to_wstring(s->nativeCells)+
        L"  原生刷新："+std::to_wstring(s->paintedFrames)+L"\n源窗口："+((s->frameFlags&2)?L"已最小化":L"未最小化")+
        L"  输入："+(s->controlling?L"已接管":L"未接管")+L"\n显示："+stage[std::clamp(LONG(s->renderStage),LONG(0),LONG(4))]+
        L"\n任务管理器中 F8 接管/释放；F9 停止。暂停菜单请用 Esc 返回游戏。";
}
BOOL CALLBACK FindTask(HWND h,LPARAM parameter){
    if(!IsWindowVisible(h)||GetWindow(h,GW_OWNER))return TRUE;DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    if(_wcsicmp(FileName(ProcessPath(pid)).c_str(),L"Taskmgr.exe")==0){*reinterpret_cast<HWND*>(parameter)=h;return FALSE;}return TRUE;
}
BOOL CALLBACK FindGame(HWND h,LPARAM parameter){
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t cls[128]{};GetClassNameW(h,cls,128);
    if(pid==gamePid&&!GetWindow(h,GW_OWNER)&&(wcscmp(cls,L"GLFW30")==0||wcscmp(cls,L"LWJGL")==0)){
        *reinterpret_cast<HWND*>(parameter)=h;return FALSE;}return TRUE;
}
void ValidateFile(const std::wstring& path){
    // 完整 SHA-256 白名单 + DLL 内存指令前像检查。未知更新版本直接拒绝。
    std::ifstream stream(std::filesystem::path(path),std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<uint8_t,32> digest{};
    if(bytes.empty()||BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw WinError(L"无法校验任务管理器",ERROR_READ_FAULT);
    auto result=BCryptHash(algorithm,nullptr,0,bytes.data(),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(algorithm,0);
    const uint8_t expected[32]={0x82,0x15,0xac,0xab,0x6f,0xb1,0xc7,0x47,0x27,0x99,0x54,0xfd,0xf5,0x5c,0x05,0xe9,0x15,0x38,0x1f,0xc3,0x2f,0x8b,0x9e,0x99,0x6c,0x54,0xee,0xcf,0x18,0x9f,0x12,0x27};
    if(result<0||memcmp(digest.data(),expected,32)!=0)throw WinError(L"暂只适配 Taskmgr.exe 10.0.26100.9549。当前文件不匹配，未修改任务管理器。",ERROR_REVISION_MISMATCH);
}
void Stop(){
    if(testFrameMutex){CloseHandle(testFrameMutex);testFrameMutex=nullptr;}
    network.reset();if(session){session->Stop();session.reset();}
    EnableWindow(startButton,TRUE);EnableWindow(stopButton,FALSE);SetWindowTextW(label,Status().c_str());
}
void Start(){
    try{
        Discovery discovery{};
        if(!nativeTest||rendererGame){discovery=ReadDiscovery();gamePid=discovery.pid;}
        HWND task=nullptr,game=nativeTest&&!rendererGame?mainWindow:nullptr;EnumWindows(FindTask,reinterpret_cast<LPARAM>(&task));
        if(!nativeTest||rendererGame)EnumWindows(FindGame,reinterpret_cast<LPARAM>(&game));
        if(!game)throw WinError(L"找不到该模组会话的游戏窗口，请使用 Java 1.21.1 + Fabric。",ERROR_NOT_FOUND);
        if(!task){ShellExecuteW(nullptr,L"open",L"taskmgr.exe",nullptr,nullptr,SW_SHOW);
            for(int i=0;i<40&&!task;i++){Sleep(100);EnumWindows(FindTask,reinterpret_cast<LPARAM>(&task));}}
        if(!task)throw WinError(L"任务管理器未启动",ERROR_NOT_FOUND);
        DWORD pid=0;GetWindowThreadProcessId(task,&pid);ValidateFile(ProcessPath(pid));
        RECT r{};GetClientRect(task,&r);auto dll=ExtractPayload(instance);
        if(nativeTest){bool untouched=false;if(!ReadCore(pid,testCpuBefore,untouched)||!untouched)throw WinError(L"原始核心数查询函数已被其他模块修改",ERROR_INVALID_DATA);nativeTestDll=dll;}
        session=std::make_unique<Session>();session->Start(task,game,r,dll);
        if(nativeTest){
            nativeTestStarted=GetTickCount64();
            if(rendererGame)network=std::make_unique<Network>(session->state,discovery);
            else if(rendererTest){testFrameMutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,session->state->frameMutex);SetTimer(mainWindow,2,50,nullptr);}
        }else network=std::make_unique<Network>(session->state,discovery);
        EnableWindow(startButton,FALSE);EnableWindow(stopButton,TRUE);if(!nativeTest)SetForegroundWindow(task);
    }catch(const WinError& e){Stop();
        if(nativeTest){TestReport("{\"passed\":false,\"error\":"+std::to_string(e.code)+"}");PostMessageW(mainWindow,WM_CLOSE,0,0);}
        else MessageBoxW(mainWindow,e.message.c_str(),L"启动失败",MB_ICONERROR);
    }catch(const std::exception&){Stop();
        if(nativeTest){TestReport("{\"passed\":false,\"error\":1}");PostMessageW(mainWindow,WM_CLOSE,0,0);}
        else MessageBoxW(mainWindow,L"没有连接到游戏模组。请安装 Fabric API 和本项目 JAR，进入世界后重试。",L"游戏连接失败",MB_ICONERROR);
    }
}
LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        label=CreateWindowW(L"STATIC",Status().c_str(),WS_CHILD|WS_VISIBLE,20,20,570,116,h,nullptr,instance,nullptr);
        startButton=CreateWindowW(L"BUTTON",L"开始原生改造",WS_CHILD|WS_VISIBLE,20,146,180,38,h,reinterpret_cast<HMENU>(101),instance,nullptr);
        stopButton=CreateWindowW(L"BUTTON",L"停止并恢复",WS_CHILD|WS_VISIBLE,220,146,180,38,h,reinterpret_cast<HMENU>(102),instance,nullptr);EnableWindow(stopButton,FALSE);
        for(HWND control:{label,startButton,stopButton})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SetTimer(h,1,300,nullptr);return 0;
    case WM_COMMAND:if(LOWORD(wp)==101)Start();else if(LOWORD(wp)==102)Stop();return 0;
    case WM_TIMER:
        if(wp==2){if(session&&rendererTest)TestFrame(session->state);return 0;}
        if(session){session->Heartbeat();
            RenderReport(session->state);
            if(nativeTest){
                DWORD pid=session->targetPid;uint16_t after=0;bool untouched=false;
                bool running=!session->Ended()&&session->state->status==Running;
                if(rendererTest&&session->state->renderStage==Painted&&renderStarted==0)renderStarted=GetTickCount64();
                bool finished=rendererTest?((renderStarted&&GetTickCount64()-renderStarted>=90000)||GetTickCount64()-nativeTestStarted>=600000):GetTickCount64()-nativeTestStarted>=35000;
                if(!running||finished){
                    bool core=ReadCore(pid,after,untouched)&&untouched&&after==testCpuBefore;
                    LONG cells=session->state->nativeCells;
                    bool rendered=!rendererTest||(session->state->renderStage==Painted&&cells==kPixels);
                    std::string lastRender=RenderSnapshot(session->state);
                    double displaySeconds=renderStarted?double(GetTickCount64()-renderStarted)/1000.0:0.0;
                    double displayFps=displaySeconds>0?session->state->paintedFrames/displaySeconds:0.0;
                    network.reset();bool stopped=session->Stop();session.reset();
                    bool unloaded=RemoteModule(pid,nativeTestDll)==0;
                    bool passed=running&&core&&stopped&&unloaded&&rendered;
                    TestReport(std::string("{\"passed\":")+(passed?"true":"false")+
                        ",\"taskmgr_alive\":"+(running?"true":"false")+
                        ",\"elapsed_ms\":"+std::to_string(GetTickCount64()-nativeTestStarted)+
                        ",\"renderer_test\":"+(rendererTest?"true":"false")+
                        ",\"minecraft_source\":"+(rendererGame?"true":"false")+
                        ",\"stop_completed\":"+(stopped?"true":"false")+
                        ",\"rendered\":"+(rendered?"true":"false")+
                        ",\"display_seconds\":"+std::to_string(displaySeconds)+",\"display_fps\":"+std::to_string(displayFps)+
                        ",\"core_getter_untouched\":"+(core?"true":"false")+
                        ",\"dll_unloaded\":"+(unloaded?"true":"false")+
                        ",\"cpu_before\":"+std::to_string(testCpuBefore)+",\"cpu_after\":"+std::to_string(after)+
                        ",\"native_cells_created\":"+std::to_string(cells)+",\"taskmgr_pid\":"+std::to_string(pid)+",\"last_render\":"+lastRender+"}");
                    PostMessageW(h,WM_CLOSE,0,0);
                }
            }else if(session->Ended()||!network||!network->Live()){Stop();SetWindowTextW(label,L"会话已停止。重新打开任务管理器可恢复原布局。\n进入游戏后再次开始。");}
            else SetWindowTextW(label,Status().c_str());}
        return 0;
    case WM_DESTROY:Stop();PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR commandLine,int show){
    instance=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    rendererGame=commandLine&&wcscmp(commandLine,L"--self-test-game-renderer")==0;
    rendererTest=rendererGame||(commandLine&&wcscmp(commandLine,L"--self-test-renderer")==0);
    nativeTest=rendererTest||(commandLine&&wcscmp(commandLine,L"--self-test-native")==0);
    WSADATA wsa{};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return 1;
    font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Microsoft YaHei UI");
    WNDCLASSW cls{};cls.hInstance=h;cls.lpfnWndProc=Proc;cls.lpszClassName=L"MCNativeTaskmgrClient";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    mainWindow=CreateWindowW(cls.lpszClassName,L"Minecraft · 任务管理器原生热力图",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,630,410,nullptr,nullptr,h,nullptr);
    ShowWindow(mainWindow,nativeTest?SW_HIDE:show);
    // 可从快捷方式或 CLI 一步启动；仍执行二进制白名单与会话校验。
    if(nativeTest||(commandLine&&wcscmp(commandLine,L"--start")==0))PostMessageW(mainWindow,WM_COMMAND,101,0);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DeleteObject(font);WSACleanup();return 0;
}
