#pragma once
#include <windows.h>
#include <cstdint>
#include "runtime_profile.h"
constexpr uint32_t kMagic=0x4d434e54,kVersion=4;
constexpr int kWidth=64,kHeight=36,kPixels=kWidth*kHeight,kInputCapacity=512;
enum BridgeStatus:LONG{Starting=0,Running=1,Stopped=2,Failed=3};
enum StopReason:LONG{None=0,StopButton=1,Hotkey=2,ControllerGone=3,SourceGone=4,TargetGone=5,ApiError=6};
enum RenderStage:LONG{AwaitChart=0,AwaitLayout=1,AwaitFrame=2,Painted=3,RenderError=4};
// 单 UI 线程写入输入、单控制端线程读取。帧通过命名 mutex 交换，UI 尝试锁失败即跳过。
struct InputEvent{int32_t type,a,b,c,d;double x,y,z,w;};
struct SharedState{
    uint32_t magic,version,bytes,controllerPid;
    uint32_t targetPid,sourcePid,fixture,flags;
    NativeProfile profile;
    uint64_t targetHwnd,sourceHwnd;
    double left,top,right,bottom;
    wchar_t stopEvent[128],readyEvent[128];
    alignas(8) volatile LONG64 heartbeat;
    volatile LONG status,lastHresult,workerPid,stopReason,visible,hotkeyRegistered;
    wchar_t frameMutex[128];
    volatile LONG frameSequence,sourceWidth,sourceHeight,frameFlags;
    uint8_t rgb[kPixels*3];
    volatile LONG inputWrite,inputRead,inputOverflow,controlling;
    volatile LONG nativeCells,paintedFrames,consumedSequence;
    volatile LONG renderStage,renderHresult,layoutWidth,layoutHeight,cellWidth,cellHeight;
    volatile LONG initialContainerVisible,containerVisible,sourceLumaMin,sourceLumaMax,sourceBuckets,paintedCells;
    ULONG palette[6];RECT firstCell,lastCell;
    wchar_t diagnostic[256];
    RECT nativeBounds; // 真正的 DirectUI 热力图区域（任务管理器客户坐标）
    InputEvent input[kInputCapacity];
};
struct StartArgs{uint32_t bytes,version;wchar_t mappingName[128];};
static_assert(sizeof(StartArgs)==264);
static_assert(sizeof(void*)==8,"Windows x64 only");
