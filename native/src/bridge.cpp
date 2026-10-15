#include "protocol.h"
#include "profile.h"
#include "heatmap_geometry.h"
#include "MinHook.h"
#include <commctrl.h>
#include <windowsx.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace {
HMODULE module;uintptr_t base;SharedState* state=nullptr;HWND window=nullptr;
HANDLE stopEvent=nullptr,frameMutex=nullptr;
HHOOK bootstrap=nullptr,inputHook=nullptr;
std::atomic<LONG> calls{0};std::atomic<bool> enabled{false};
constexpr UINT InitMessage=WM_APP+0x461,CleanupMessage=WM_APP+0x462;
constexpr UINT_PTR Timer=0x4d434e54,Subclass=0x4d434e54;
thread_local bool expanding=false;
thread_local bool layingOut=false;
using One=HRESULT(__fastcall*)(void*);
using Count=uint16_t(__fastcall*)(void*);
using NumaList=HRESULT(__fastcall*)(void*,void*,ULONG);
using SwitchChart=HRESULT(__fastcall*)(void*,int,bool);
using Initialize=HRESULT(__fastcall*)(void*,void*,void*,void*,const void*);
using Destructor=void(__fastcall*)(void*);
using Block=HRESULT(__fastcall*)(void*,UINT,const wchar_t*,ULONG,ULONG);
using Colors=void(__fastcall*)(void*,int,ULONG*,ULONG*);
One originalLoad=nullptr,originalUpdate=nullptr,originalLayout=nullptr,originalViewUpdate=nullptr;
SwitchChart originalSwitch=nullptr;
Count originalCount=nullptr;Initialize originalInitialize=nullptr;Destructor originalDestructor=nullptr;
Block setBlock=nullptr;Colors getColors=nullptr;
struct Call{Call(){++calls;}~Call(){--calls;}};
struct BitArray{uint32_t count,padding;uint8_t* bytes;};
struct Model {
    void* object=nullptr;void* originalBits=nullptr;uint32_t realCount=0;
    void* view=nullptr;int restoreMode=0;bool shown=false;
    bool gridInstalled=false,layoutReady=false;std::array<ULONG,6> palette{};
    BitArray bits{};std::array<uint8_t,(kPixels+7)/8> parked{};
    std::array<int,kPixels> previous{};int width=0,height=0,containerHeight=-1;
} model;
template<class T>T Function(uint32_t rva){return reinterpret_cast<T>(base+rva);}
template<class T>T Dui(const char* name){
    auto p=GetProcAddress(GetModuleHandleW(L"dui70.dll"),name);
    if(!p)throw std::runtime_error(std::string("Missing DirectUI export: ")+name);return reinterpret_cast<T>(p);
}
// 这些签名取自匹配当前 Taskmgr 的导入表，DLL 不自行绘制任何像素。
using GetExtent=const SIZE*(__fastcall*)(void*,void**);
using Rect=HRESULT(__fastcall*)(void*,RECT*);
using Parent=void*(__fastcall*)(void*);
using GetHWND=HWND(__fastcall*)(void*);
using IntProperty=HRESULT(__fastcall*)(void*,int);
using GetInt=int(__fastcall*)(void*);
using FourProperty=HRESULT(__fastcall*)(void*,int,int,int,int);
using TwoProperty=HRESULT(__fastcall*)(void*,int,int);
using BoolProperty=HRESULT(__fastcall*)(void*,bool);
using GetBool=bool(__fastcall*)(void*);
using TextProperty=HRESULT(__fastcall*)(void*,const wchar_t*);
using LayoutCreate=HRESULT(__fastcall*)(int,int,void**);
using LayoutSet=HRESULT(__fastcall*)(void*,void*);
using VoidMethod=void(__fastcall*)(void*);
GetExtent extent;Rect bounds;Parent parent,root;GetHWND hwnd;
IntProperty setWidth,setHeight;FourProperty padding,border;TwoProperty minimum;
IntProperty fontSize;FourProperty margin;TextProperty content;
BoolProperty visible,tooltip;LayoutCreate grid;LayoutSet layoutSet;VoidMethod releaseValue,updateLayout,destroyLayout;
GetBool getVisible;
GetInt getHeight;BoolProperty destroyAll;
HRESULT __fastcall LayoutHook(void* self);
HRESULT __fastcall UpdateHook(void* self);
HRESULT RenderFailure(HRESULT hr){state->renderHresult=hr;state->renderStage=RenderError;return hr;}

void CheckProfile(){
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||pe->Signature!=IMAGE_NT_SIGNATURE||
       pe->FileHeader.TimeDateStamp!=Profile::Timestamp||pe->OptionalHeader.SizeOfImage!=Profile::ImageSize)
        throw std::runtime_error("Unsupported Taskmgr binary");
#define VERIFY(n) if(memcmp(reinterpret_cast<void*>(base+Profile::n),Profile::n##Bytes,16)!=0)throw std::runtime_error("Taskmgr preimage mismatch")
    VERIFY(LoadLogical);VERIFY(LogicalCount);VERIFY(Initialize);VERIFY(UpdateData);
    VERIFY(Layout);VERIFY(Destructor);VERIFY(SetBlockData);VERIFY(GetBlockColors);
    VERIFY(SwitchChart);VERIFY(LoadHeatMap);VERIFY(GetCpuIdList);VERIFY(ViewUpdate);VERIFY(DeleteObject);
#undef VERIFY
}
void ResolveDui(){
    extent=Dui<GetExtent>("?GetExtent@Element@DirectUI@@QEAAPEBUtagSIZE@@PEAPEAVValue@2@@Z");
    bounds=Dui<Rect>("?GetRootRelativeBounds@Element@DirectUI@@QEAAJPEAUtagRECT@@@Z");
    parent=Dui<Parent>("?GetParent@Element@DirectUI@@QEAAPEAV12@XZ");
    root=Dui<Parent>("?GetRoot@Element@DirectUI@@QEAAPEAV12@XZ");
    hwnd=Dui<GetHWND>("?GetHWND@HWNDElement@DirectUI@@UEAAPEAUHWND__@@XZ");
    setWidth=Dui<IntProperty>("?SetWidth@Element@DirectUI@@QEAAJH@Z");
    setHeight=Dui<IntProperty>("?SetHeight@Element@DirectUI@@QEAAJH@Z");
    fontSize=Dui<IntProperty>("?SetFontSize@Element@DirectUI@@QEAAJH@Z");
    margin=Dui<FourProperty>("?SetMargin@Element@DirectUI@@QEAAJHHHH@Z");
    content=Dui<TextProperty>("?SetContentString@Element@DirectUI@@QEAAJPEBG@Z");
    padding=Dui<FourProperty>("?SetPadding@Element@DirectUI@@QEAAJHHHH@Z");
    border=Dui<FourProperty>("?SetBorderThickness@Element@DirectUI@@QEAAJHHHH@Z");
    minimum=Dui<TwoProperty>("?SetMinSize@Element@DirectUI@@QEAAJHH@Z");
    visible=Dui<BoolProperty>("?SetVisible@Element@DirectUI@@QEAAJ_N@Z");
    getVisible=Dui<GetBool>("?GetVisible@Element@DirectUI@@QEAA_NXZ");
    getHeight=Dui<GetInt>("?GetHeight@Element@DirectUI@@QEAAHXZ");
    destroyAll=Dui<BoolProperty>("?DestroyAll@Element@DirectUI@@QEAAJ_N@Z");
    tooltip=Dui<BoolProperty>("?SetTooltip@Element@DirectUI@@QEAAJ_N@Z");
    grid=Dui<LayoutCreate>("?Create@GridLayout@DirectUI@@SAJHHPEAPEAVLayout@2@@Z");
    layoutSet=Dui<LayoutSet>("?SetLayout@Element@DirectUI@@QEAAJPEAVLayout@2@@Z");
    releaseValue=Dui<VoidMethod>("?Release@Value@DirectUI@@QEAAXXZ");
    updateLayout=Dui<VoidMethod>("?UpdateLayout@Element@DirectUI@@QEAAXXZ");
    destroyLayout=Dui<VoidMethod>("?Destroy@Layout@DirectUI@@QEAAXXZ");
}
template<class T>T& Field(void* p,size_t offset){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+offset);}
void PublishBounds(void* container){
    RECT r{};
    if(SUCCEEDED(bounds(container,&r))){
        void* top=root(container);HWND host=top?hwnd(top):nullptr;
        if(host&&IsWindow(host))MapWindowPoints(host,window,reinterpret_cast<POINT*>(&r),2);
        state->nativeBounds=r;
    }
}
void Push(InputEvent event){
    if(!state)return;
    LONG write=InterlockedCompareExchange(&state->inputWrite,0,0);
    LONG read=InterlockedCompareExchange(&state->inputRead,0,0);
    if(static_cast<uint32_t>(write-read)>=kInputCapacity){
        InterlockedIncrement(&state->inputOverflow);SetEvent(stopEvent);return;
    }
    state->input[static_cast<uint32_t>(write)%kInputCapacity]=event;
    MemoryBarrier();InterlockedExchange(&state->inputWrite,write+1);
}
bool hiddenCursor=false;
void Pointer(bool capture){
    if(capture){RECT r=state->nativeBounds;MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&r),2);
        if(r.right>r.left&&r.bottom>r.top)ClipCursor(&r);
        if(!hiddenCursor){ShowCursor(FALSE);hiddenCursor=true;}
    }else{ClipCursor(nullptr);if(hiddenCursor){ShowCursor(TRUE);hiddenCursor=false;}}
}
void Control(bool value){
    bool old=InterlockedExchange(&state->controlling,value?1:0)!=0;
    if(value!=old){InputEvent e{};e.type=15;e.a=value?1:0;Push(e);}
    if(!value)Pointer(false);
}
HRESULT DiscardHeatmap(void* view,void* object){
    if(!object)return S_OK;
    void* container=Field<void*>(object,8);
    // Visibility alone does not remove old template wrappers from GridLayout.
    // DestroyAll first detaches every child, then lets DirectUI destroy it safely.
    HRESULT hr=container?destroyAll(container,true):S_OK;
    if(FAILED(hr))return hr;
    Field<void*>(view,0x150)=nullptr;
    originalDestructor(object);
    // Use the same native sized-delete entry as AtmCpuView::~AtmCpuView.
    Function<void(__fastcall*)(void*,size_t)>(Profile::DeleteObject)(object,0x30);
    return S_OK;
}
HRESULT __fastcall LoadHook(void* self){
    Call call;
    if(!enabled)return originalLoad(self);
    if(model.object)return S_OK;
    HRESULT discarded=DiscardHeatmap(self,Field<void*>(self,0x150));
    if(FAILED(discarded))return RenderFailure(discarded);
    // 不 Hook 小型计数 getter。Taskmgr 的内部调用者依赖其保留部分易失寄存器；
    // 普通 C++ detour 会破坏这种二进制内部约定，波及功耗计算线程。
    model.realCount=originalCount(reinterpret_cast<void*>(base+0x5420d0));
    std::array<uint32_t,kPixels> numa{};
    auto getIds=Function<NumaList>(Profile::GetCpuIdList);
    HRESULT hr=getIds(self,numa.data(),model.realCount);
    if(FAILED(hr))return hr;
    // 直接进入原生热力图创建路径；原版 getter 与全局核心数完全保持原值。
    // 只在 Initialize 的已确认实例上扩容，NUMA 数组也同步提供等长存储。
    auto loadHeatMap=Function<HRESULT(__fastcall*)(void*,void*)>(Profile::LoadHeatMap);
    model.view=self;
    expanding=true;hr=loadHeatMap(self,numa.data());expanding=false;return hr;
}
HRESULT __fastcall SwitchHook(void* self,int mode,bool redraw){
    Call call;
    if(!enabled)return originalSwitch(self,mode,redraw);
    if(mode==1||mode==3){
        if(!model.object){model.restoreMode=int(Field<uint32_t>(self,0x288));HRESULT hr=LoadHook(self);if(FAILED(hr))return hr;}
        mode=3; // 让原生 UI 显示热力图，并走原生 CPU_HEATMAP 更新分支。
    }
    HRESULT hr=originalSwitch(self,mode,redraw);
    model.shown=SUCCEEDED(hr)&&mode==3;
    if(!model.shown)Control(false);
    else if(model.object){LayoutHook(model.object);UpdateHook(model.object);}
    return hr;
}
HRESULT __fastcall ViewUpdateHook(void* self){
    Call call;
    // Opening the CPU page is sufficient, including an already cached view.
    // Remember its current mode so Stop can restore what the user was viewing.
    if(enabled&&!model.object){
        HRESULT hr=SwitchHook(self,3,false);if(FAILED(hr))return hr;
    }
    return originalViewUpdate(self);
}
HRESULT __fastcall InitializeHook(void* self,void* trace,void* parser,void* element,const void* numa){
    Call call;
    if(enabled&&expanding)Field<uint32_t>(self,0x10)=kPixels;
    HRESULT hr=originalInitialize(self,trace,parser,element,numa);
    if(SUCCEEDED(hr)&&enabled&&expanding&&Field<uint32_t>(self,0x10)==kPixels){
        model.object=self;model.originalBits=Field<void*>(self,0x28);
        model.parked.fill(0);model.bits={kPixels,0,model.parked.data()};
        // 原生停泊位图只有真实核心数量。替换为等长的零位图，防止越界读。
        Field<void*>(self,0x28)=&model.bits;model.previous.fill(-1);model.width=model.height=0;
        model.gridInstalled=false;model.layoutReady=false;model.palette.fill(0);
        model.containerHeight=getHeight(Field<void*>(self,8));
        state->initialContainerVisible=getVisible(Field<void*>(self,8))?1:0;
        state->renderStage=AwaitLayout;state->renderHresult=S_OK;
        InterlockedExchange(&state->nativeCells,kPixels);
    }
    return hr;
}
void __fastcall DestructorHook(void* self){
    Call call;
    if(self==model.object){Field<void*>(self,0x28)=model.originalBits;model.object=nullptr;model.view=nullptr;model.shown=false;model.layoutReady=false;state->renderStage=AwaitChart;InterlockedExchange(&state->nativeCells,0);Control(false);}
    originalDestructor(self);
}
HRESULT __fastcall LayoutHook(void* self){
    Call call;
    if(!enabled||self!=model.object)return originalLayout(self);
    if(layingOut)return S_OK;
    struct LayoutGuard{LayoutGuard(){layingOut=true;}~LayoutGuard(){layingOut=false;}} guard;
    void* container=Field<void*>(self,8);void* value=nullptr;
    // The native LayoutCpuBlocks explicitly makes this inner container visible.
    // SwitchChart only shows its outer page. Omitting this leaves every cell hidden.
    HRESULT hr=visible(container,true);if(FAILED(hr))return RenderFailure(hr);
    state->containerVisible=getVisible(container)?1:0;
    if(!model.gridInstalled){
        void* layout=nullptr;hr=grid(-1,kWidth,&layout);
        if(FAILED(hr))return RenderFailure(hr);
        hr=layoutSet(container,layout);
        if(FAILED(hr)){if(layout)destroyLayout(layout);return RenderFailure(hr);}
        model.gridInstalled=true;
    }
    updateLayout(container);
    const SIZE* size=extent(container,&value);SIZE s=size?*size:SIZE{};
    if(value)releaseValue(value);
    // Height is auto-sized in the native template. Derive rows from the viewport
    // or use square pixels on first layout; retry on subsequent UI timer ticks.
    void* host=parent(container);int viewportHeight=0;
    value=nullptr;const SIZE* hostSize=host?extent(host,&value):nullptr;
    if(hostSize){viewportHeight=hostSize->cy;if(s.cx<=0)s.cx=hostSize->cx;}
    if(value)releaseValue(value);
    state->layoutWidth=s.cx;state->layoutHeight=s.cy;
    HeatmapGeometry fit=FitHeatmap(s.cx,viewportHeight,kWidth,kHeight);
    if(fit.width==0){model.layoutReady=false;state->renderStage=AwaitLayout;return S_FALSE;}
    int w=fit.width,h=fit.height;
    auto cells=Field<void**>(self,0x18);
    if(w!=model.width||h!=model.height){
        for(int i=0;i<kPixels;i++){
            // 同时压缩原生模板外壳和内部热力图单元，清空百分比文本。
            void* outer=parent(cells[i]);
            for(void* e:{cells[i],outer})if(e&&e!=container){
                // An empty native text element still reserves its inherited font
                // line height. Shrink that too, otherwise 36 rows overflow the page.
                for(HRESULT result:{content(e,L""),fontSize(e,1),minimum(e,0,0),margin(e,0,0,0,0),padding(e,0,0,0,0),border(e,0,0,0,0),setWidth(e,w),setHeight(e,h),tooltip(e,false)})
                    if(FAILED(result))return RenderFailure(result);
            }
        }
        model.width=w;model.height=h;
        hr=setHeight(container,h*kHeight);if(FAILED(hr))return RenderFailure(hr);
    }
    updateLayout(container);PublishBounds(container);
    bounds(cells[0],&state->firstCell);bounds(cells[kPixels-1],&state->lastCell);
    state->cellWidth=w;state->cellHeight=h;
    model.layoutReady=true;state->renderHresult=S_OK;
    if(state->renderStage!=Painted)state->renderStage=AwaitFrame;
    return S_OK;
}
HRESULT __fastcall UpdateHook(void* self){
    Call call;
    if(!enabled||self!=model.object)return originalUpdate(self);
    if(!model.layoutReady){state->renderStage=AwaitLayout;return S_FALSE;}
    std::array<uint8_t,kPixels*3> frame{};LONG sequence=0;
    DWORD wait=WaitForSingleObject(frameMutex,0);
    if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED)return S_OK;
    sequence=state->frameSequence;memcpy(frame.data(),state->rgb,frame.size());ReleaseMutex(frameMutex);
    if(sequence==0){state->renderStage=AwaitFrame;return S_FALSE;}
    // 原生主题的六档 CPU 蓝色。高亮度映射为低占用，让浅色区域对应明亮画面。
    constexpr int levels[]={0,1,21,41,61,81};
    std::array<ULONG,6> colors{},edges{};
    for(int b=0;b<6;b++){getColors(self,levels[b],&colors[b],&edges[b]);state->palette[b]=colors[b];}
    if(colors!=model.palette){model.previous.fill(-1);model.palette=colors;}
    int minimumLuma=255,maximumLuma=0,buckets=0;LONG changed=0;
    for(UINT i=0;i<kPixels;i++){
        int luma=(int(frame[i*3])*54+int(frame[i*3+1])*183+int(frame[i*3+2])*19)>>8;
        int bucket=std::clamp((255-luma)*6/256,0,5);
        minimumLuma=std::min(minimumLuma,luma);maximumLuma=std::max(maximumLuma,luma);buckets|=1<<bucket;
        if(model.previous[i]==bucket)continue;
        HRESULT hr=setBlock(self,i,L"",colors[bucket],edges[bucket]);
        if(FAILED(hr))return RenderFailure(hr);model.previous[i]=bucket;++changed;
    }
    state->sourceLumaMin=minimumLuma;state->sourceLumaMax=maximumLuma;
    state->sourceBuckets=0;for(int b=0;b<6;b++)if(buckets&(1<<b))++state->sourceBuckets;
    state->paintedCells=changed;state->renderHresult=S_OK;state->renderStage=Painted;
    // Flush native property invalidation through its own HWND paint path.
    if(changed){void* top=root(Field<void*>(self,8));HWND host=top?hwnd(top):nullptr;
        if(host&&IsWindow(host))InvalidateRect(host,nullptr,FALSE);}
    if(sequence!=state->consumedSequence){InterlockedIncrement(&state->paintedFrames);InterlockedExchange(&state->consumedSequence,sequence);}
    return S_OK;
}
int Mods(){return (GetKeyState(VK_SHIFT)<0?1:0)|(GetKeyState(VK_CONTROL)<0?2:0)|(GetKeyState(VK_MENU)<0?4:0)|(GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0?8:0);}
int Key(WPARAM vk,LPARAM lp){
    if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))return int(vk);
    if(vk>=VK_F1&&vk<=VK_F24)return 290+int(vk-VK_F1);
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return 320+int(vk-VK_NUMPAD0);
    bool extended=(lp&(1LL<<24))!=0;
    switch(vk){
    case VK_SPACE:return 32;case VK_ESCAPE:return 256;case VK_RETURN:return extended?335:257;
    case VK_TAB:return 258;case VK_BACK:return 259;case VK_INSERT:return 260;case VK_DELETE:return 261;
    case VK_RIGHT:return 262;case VK_LEFT:return 263;case VK_DOWN:return 264;case VK_UP:return 265;
    case VK_PRIOR:return 266;case VK_NEXT:return 267;case VK_HOME:return 268;case VK_END:return 269;
    case VK_CAPITAL:return 280;case VK_SCROLL:return 281;case VK_NUMLOCK:return 282;
    case VK_SNAPSHOT:return 283;case VK_PAUSE:return 284;
    case VK_SHIFT:return ((lp>>16)&255)==0x36?344:340;case VK_CONTROL:return extended?345:341;
    case VK_MENU:return extended?346:342;case VK_LWIN:return 343;case VK_RWIN:return 347;
    case VK_LSHIFT:return 340;case VK_RSHIFT:return 344;case VK_LCONTROL:return 341;case VK_RCONTROL:return 345;
    case VK_OEM_1:return 59;case VK_OEM_PLUS:return 61;case VK_OEM_COMMA:return 44;case VK_OEM_MINUS:return 45;
    case VK_OEM_PERIOD:return 46;case VK_OEM_2:return 47;case VK_OEM_3:return 96;
    case VK_OEM_4:return 91;case VK_OEM_5:return 92;case VK_OEM_6:return 93;case VK_OEM_7:return 39;
    case VK_DECIMAL:return 330;case VK_DIVIDE:return 331;case VK_MULTIPLY:return 332;case VK_SUBTRACT:return 333;case VK_ADD:return 334;
    default:return -1;}
}
LRESULT CALLBACK InputHook(int code,WPARAM wp,LPARAM lp){
    Call call;
    if(code>=0&&wp==PM_REMOVE&&enabled){
        auto msg=reinterpret_cast<MSG*>(lp);
        bool focused=GetAncestor(GetForegroundWindow(),GA_ROOT)==window;
        if(!focused&&state->controlling)Control(false);
        bool consume=false;
        if(focused&&(msg->message==WM_KEYDOWN||msg->message==WM_SYSKEYDOWN)&&!(msg->lParam&(1LL<<30))){
            if(msg->wParam==VK_F8){if(model.object&&model.shown)Control(!state->controlling);consume=true;}
            if(msg->wParam==VK_F9){SetEvent(stopEvent);consume=true;}
        }
        if(focused&&state->controlling&&!consume){
            InputEvent e{};
            if(msg->message==WM_KEYDOWN||msg->message==WM_KEYUP||msg->message==WM_SYSKEYDOWN||msg->message==WM_SYSKEYUP){
                int key=Key(msg->wParam,msg->lParam);
                if(key>=0&&key!=297&&key!=298){e.type=10;e.a=key;e.b=int((msg->lParam>>16)&255);
                    e.c=(msg->message==WM_KEYUP||msg->message==WM_SYSKEYUP)?0:(msg->lParam&(1LL<<30)?2:1);e.d=Mods();Push(e);
                    // 移除 WM_KEYDOWN 后 TranslateMessage 不会生成 WM_CHAR；在此转成字符。
                    if(e.c!=0){BYTE keyboard[256]{};wchar_t text[8]{};GetKeyboardState(keyboard);
                        int n=ToUnicodeEx(UINT(msg->wParam),UINT(e.b),keyboard,text,8,0,GetKeyboardLayout(0));
                        for(int i=0;i<n;i++){InputEvent ch{};ch.type=14;ch.a=text[i];ch.b=e.d;Push(ch);}
                    }
                }
                consume=true;
            }else if(msg->message==WM_CHAR||msg->message==WM_SYSCHAR){e.type=14;e.a=int(msg->wParam);e.b=Mods();Push(e);consume=true;
            }else if(msg->message==WM_MOUSEMOVE){
                POINT p{GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam)};MapWindowPoints(msg->hwnd,window,&p,1);
                RECT r=state->nativeBounds;e.type=12;
                e.z=std::clamp(double(p.x-r.left)/std::max(1L,r.right-r.left),0.0,1.0);
                e.w=std::clamp(double(p.y-r.top)/std::max(1L,r.bottom-r.top),0.0,1.0);Push(e);consume=true;
            }else if(msg->message==WM_INPUT&&(state->frameFlags&1)){
                RAWINPUT raw{};UINT n=sizeof(raw);
                if(GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),RID_INPUT,&raw,&n,sizeof(RAWINPUTHEADER))!=UINT(-1)
                    &&raw.header.dwType==RIM_TYPEMOUSE&&!(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
                    e.type=12;e.x=raw.data.mouse.lLastX;e.y=raw.data.mouse.lLastY;e.z=e.w=0.5;Push(e);
                }
                // WM_INPUT 的系统清理由原生窗口过程执行。
            }else if(msg->message==WM_MOUSEWHEEL||msg->message==WM_MOUSEHWHEEL){
                e.type=13;if(msg->message==WM_MOUSEWHEEL)e.y=GET_WHEEL_DELTA_WPARAM(msg->wParam)/120.0;
                else e.x=GET_WHEEL_DELTA_WPARAM(msg->wParam)/120.0;Push(e);consume=true;
            }else if(msg->message>=WM_LBUTTONDOWN&&msg->message<=WM_MBUTTONDBLCLK){
                UINT m=msg->message;
                if(m!=WM_LBUTTONDBLCLK&&m!=WM_RBUTTONDBLCLK&&m!=WM_MBUTTONDBLCLK){
                    e.type=11;e.a=m<=WM_LBUTTONDBLCLK?0:(m<=WM_RBUTTONDBLCLK?1:2);
                    e.b=(m==WM_LBUTTONUP||m==WM_RBUTTONUP||m==WM_MBUTTONUP)?0:1;e.c=Mods();Push(e);
                }consume=true;
            }
        }
        if(consume){msg->message=WM_NULL;msg->wParam=0;msg->lParam=0;}
    }
    return CallNextHookEx(inputHook,code,wp,lp);
}
void RestoreModel(){
    if(!model.object)return;
    void* self=model.object;void* container=Field<void*>(self,8);void* view=model.view;
    Field<void*>(self,0x28)=model.originalBits;
    visible(container,false);setHeight(container,model.containerHeight);
    // Fully remove the virtual cells. Hidden wrappers still consume grid rows;
    // keeping them made later sessions appear white or vertically compressed.
    DiscardHeatmap(view,self);
    model.object=nullptr;model.view=nullptr;model.shown=false;model.layoutReady=false;state->nativeCells=0;
    if(view){
        if(model.restoreMode==3){
            std::array<uint32_t,kPixels> ids{};
            Function<NumaList>(Profile::GetCpuIdList)(view,ids.data(),model.realCount);
            Function<HRESULT(__fastcall*)(void*,void*)>(Profile::LoadHeatMap)(view,ids.data());
        }
        // 热力图与原版曲线可以同时存在；确保恢复的图表确实已创建。
        if(model.restoreMode==1&&!Field<void*>(view,0x140))originalLoad(view);
        originalSwitch(view,model.restoreMode,false);
    }
}
LRESULT CALLBACK WindowProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    Call call;
    if(msg==WM_TIMER&&wp==Timer){
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=window)Control(false);
        Pointer(state->controlling&&model.shown&&(state->frameFlags&1));
        if(model.object&&model.shown){LayoutHook(model.object);UpdateHook(model.object);}return 0;
    }
    if(msg==CleanupMessage){Control(false);Pointer(false);KillTimer(h,Timer);enabled=false;RestoreModel();
        RAWINPUTDEVICE device{1,2,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&device,1,sizeof(device));
        RemoveWindowSubclass(h,WindowProc,Subclass);return 1;
    }
    if(msg==WM_NCDESTROY){SetEvent(stopEvent);window=nullptr;}
    return DefSubclassProc(h,msg,wp,lp);
}
LRESULT CALLBACK Bootstrap(int code,WPARAM wp,LPARAM lp){
    if(code>=0){auto message=reinterpret_cast<CWPSTRUCT*>(lp);
        if(message->message==InitMessage&&message->hwnd==window){
            if(SetWindowSubclass(window,WindowProc,Subclass,0)){
                SetTimer(window,Timer,100,nullptr); // UI 线程原生更新 10 FPS，避免修改任务管理器刷新设置。
                RAWINPUTDEVICE device{1,2,RIDEV_INPUTSINK,window};RegisterRawInputDevices(&device,1,sizeof(device));
                state->visible=1;
            }
        }}return CallNextHookEx(bootstrap,code,wp,lp);
}
template<class T>void Hook(uint32_t rva,T detour,T* original){
    if(MH_CreateHook(reinterpret_cast<void*>(base+rva),reinterpret_cast<void*>(detour),reinterpret_cast<void**>(original))!=MH_OK)
        throw std::runtime_error("Cannot create trampoline");
}
}
extern "C" __declspec(dllexport) DWORD WINAPI RunBridge(void* argument){
    StartArgs args=*static_cast<StartArgs*>(argument);HANDLE mapping=nullptr,ready=nullptr;
    bool hooks=false,ui=false;DWORD error=0;
    try{
        if(args.bytes!=sizeof(args)||args.version!=kVersion)throw std::runtime_error("Protocol mismatch");
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,args.mappingName);
        if(!mapping)throw std::runtime_error("Mapping unavailable");
        state=static_cast<SharedState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState)));
        if(!state||state->magic!=kMagic||state->version!=kVersion||state->bytes!=sizeof(SharedState))throw std::runtime_error("State mismatch");
        ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,state->readyEvent);
        stopEvent=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,state->stopEvent);
        frameMutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,state->frameMutex);
        window=reinterpret_cast<HWND>(state->targetHwnd);base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(!ready||!stopEvent||!frameMutex)throw std::runtime_error("IPC unavailable");
        CheckProfile();ResolveDui();
        setBlock=Function<Block>(Profile::SetBlockData);getColors=Function<Colors>(Profile::GetBlockColors);
        originalCount=Function<Count>(Profile::LogicalCount);
        if(MH_Initialize()!=MH_OK)throw std::runtime_error("MinHook busy");hooks=true;
        Hook(Profile::LoadLogical,LoadHook,&originalLoad);
        Hook(Profile::Initialize,InitializeHook,&originalInitialize);Hook(Profile::Destructor,DestructorHook,&originalDestructor);
        Hook(Profile::Layout,LayoutHook,&originalLayout);Hook(Profile::UpdateData,UpdateHook,&originalUpdate);
        Hook(Profile::SwitchChart,SwitchHook,&originalSwitch);
        Hook(Profile::ViewUpdate,ViewUpdateHook,&originalViewUpdate);
        enabled=true;if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)throw std::runtime_error("Cannot enable hooks");
        DWORD thread=GetWindowThreadProcessId(window,nullptr);
        bootstrap=SetWindowsHookExW(WH_CALLWNDPROC,Bootstrap,module,thread);
        if(!bootstrap)throw std::runtime_error("UI bootstrap hook failed");
        DWORD_PTR answer=0;
        auto initialized=SendMessageTimeoutW(window,InitMessage,0,0,SMTO_ABORTIFHUNG,5000,&answer);
        ui=state->visible!=0;
        if(!initialized||!ui)throw std::runtime_error("UI initialization failed");
        UnhookWindowsHookEx(bootstrap);bootstrap=nullptr;
        inputHook=SetWindowsHookExW(WH_GETMESSAGE,InputHook,module,thread);
        if(!inputHook)throw std::runtime_error("Input hook failed");
        state->workerPid=GetCurrentProcessId();InterlockedExchange(&state->status,Running);SetEvent(ready);
        while(WaitForSingleObject(stopEvent,100)==WAIT_TIMEOUT){
            if(!IsWindow(window)){state->stopReason=TargetGone;break;}
            if(GetTickCount64()-uint64_t(InterlockedCompareExchange64(&state->heartbeat,0,0))>5000){state->stopReason=ControllerGone;break;}
        }
    }catch(const std::exception& e){error=ERROR_REVISION_MISMATCH;
        if(state)MultiByteToWideChar(CP_UTF8,0,e.what(),-1,state->diagnostic,256);
    }catch(...){error=ERROR_GEN_FAILURE;}
    if(bootstrap){UnhookWindowsHookEx(bootstrap);bootstrap=nullptr;}
    if(inputHook){UnhookWindowsHookEx(inputHook);inputHook=nullptr;}
    if(ui&&window&&IsWindow(window)){
        DWORD_PTR answer=0;
        if(!SendMessageTimeoutW(window,CleanupMessage,0,0,SMTO_ABORTIFHUNG,5000,&answer)||answer!=1){
            // UI 卡死时不能卸载仍被窗口过程引用的 DLL；停止后保持加载，关闭 Taskmgr 回收。
            if(state){state->lastHresult=ERROR_TIMEOUT;state->status=Failed;if(ready)SetEvent(ready);}
            return ERROR_TIMEOUT;
        }
    }
    enabled=false;if(hooks){MH_DisableHook(MH_ALL_HOOKS);while(calls.load()!=0)Sleep(1);MH_Uninitialize();}
    if(state){state->lastHresult=error;InterlockedExchange(&state->status,error?Failed:Stopped);if(ready)SetEvent(ready);UnmapViewOfFile(state);state=nullptr;}
    for(HANDLE h:{mapping,ready,stopEvent,frameMutex})if(h)CloseHandle(h);
    FreeLibraryAndExitThread(module,error);return error;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void*){
    if(reason==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);}return TRUE;
}
