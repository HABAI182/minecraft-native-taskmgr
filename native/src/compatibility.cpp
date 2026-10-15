#include "runtime_profile.h"
#include "pe_image.h"
#include "symbol_cache.h"
#include "profile.h"
#include "Zydis.h"
#include <set>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

const char* NativeFunctionName(NativeFn function){
    static constexpr const char* names[]={"LoadLogical","LoadHeatMap","LogicalCount","Initialize","UpdateData","Layout","Destructor","SetBlockData","GetBlockColors","SwitchChart","GetCpuIdList","ViewUpdate","DeleteObject","ViewDestructor","DeleteArray"};
    size_t i=size_t(function);return i<kNativeFunctions?names[i]:"unknown";
}
bool KnownNativeProfile(const uint8_t hash[32],NativeProfile& p){
    static constexpr uint8_t expected[]={0x82,0x15,0xac,0xab,0x6f,0xb1,0xc7,0x47,0x27,0x99,0x54,0xfd,0xf5,0x5c,0x05,0xe9,0x15,0x38,0x1f,0xc3,0x2f,0x8b,0x9e,0x99,0x6c,0x54,0xee,0xcf,0x18,0x9f,0x12,0x27};
    if(memcmp(hash,expected,32)!=0)return false;
    p.timestamp=Profile::Timestamp;p.imageSize=Profile::ImageSize;p.globalSettings=0x5420d0;p.countOffset=0x938;p.viewHeatmapOffset=0x150;p.viewModeOffset=0x288;
    p.pdbGuid={0x6a6407a6,0xb640,0x6a9f,{0xa5,0x51,0xc3,0xa7,0x2b,0x19,0x26,0x49}};p.pdbAge=1;memcpy(p.sha256,hash,32);
#define COPY(n) p[NativeFn::n].rva=Profile::n;memcpy(p[NativeFn::n].preimage,Profile::n##Bytes,16)
    COPY(LoadLogical);COPY(LoadHeatMap);COPY(LogicalCount);COPY(Initialize);COPY(UpdateData);COPY(Layout);COPY(Destructor);COPY(SetBlockData);COPY(GetBlockColors);COPY(SwitchChart);COPY(GetCpuIdList);COPY(ViewUpdate);COPY(DeleteObject);COPY(ViewDestructor);
    p[NativeFn::DeleteArray]=p[NativeFn::DeleteObject]; // Same native stub in this exact verified binary.
#undef COPY
    return true;
}
namespace{
struct Instruction{uint64_t address;ZydisDecodedInstruction code;ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];};
using Instructions=std::vector<Instruction>;
[[noreturn]]void Abi(const char* reason){throw CompatibilityError(std::string("unsupported_layout:")+reason,L"已找到函数，但此 Taskmgr 的内部布局尚未通过验证（"+std::wstring(reason,reason+strlen(reason))+L"）。不会向任务管理器加载 DLL；请保留兼容性报告以补充适配。");}
Instructions Decode(const PeImage& image,uint32_t rva){
    Instructions result;ZydisDecoder decoder;
    ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
    // Profile-guided MSVC builds split a function into distant hot/cold blocks.
    // Walk reachable basic blocks, not the contiguous .pdata range or bytes past
    // a leaf RET (which may belong to a jump table or a different function).
    std::vector<uint32_t> pending={rva};std::set<uint32_t> visited;
    while(!pending.empty()){
        uint32_t address=pending.back();pending.pop_back();
        while(visited.insert(address).second){
            if(result.size()>=8192||!image.Executable(address,15))Abi("instruction_range");
            Instruction i{};i.address=address;
            if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,image.Rva(address,15),15,&i.code,i.operands))||!i.code.length)Abi("instruction_decode");
            result.push_back(i);uint32_t next=address+i.code.length;
            if(i.code.meta.category==ZYDIS_CATEGORY_COND_BR||i.code.meta.category==ZYDIS_CATEGORY_UNCOND_BR){
                if(i.operands[0].type==ZYDIS_OPERAND_TYPE_IMMEDIATE&&i.operands[0].imm.is_relative){
                    int64_t target=int64_t(next)+i.operands[0].imm.value.s;
                    if(target<0||target>=image.Header().OptionalHeader.SizeOfImage||!image.Executable(uint32_t(target)))Abi("branch_range");
                    pending.push_back(uint32_t(target));
                }
                if(i.code.meta.category==ZYDIS_CATEGORY_UNCOND_BR)break;
            }
            if(i.code.meta.category==ZYDIS_CATEGORY_RET||i.code.mnemonic==ZYDIS_MNEMONIC_INT3||i.code.mnemonic==ZYDIS_MNEMONIC_UD2)break;
            address=next;
        }
    }
    return result;
}
ZydisRegister Full(ZydisRegister reg){return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,reg);}
std::set<ZydisRegister> ThisRegisters(const Instructions& instructions){
    std::set<ZydisRegister> result={ZYDIS_REGISTER_RCX};
    for(auto& i:instructions){if(i.code.mnemonic==ZYDIS_MNEMONIC_CALL)break;
        if(i.code.mnemonic==ZYDIS_MNEMONIC_MOV&&i.code.operand_count_visible==2&&i.operands[0].type==ZYDIS_OPERAND_TYPE_REGISTER&&i.operands[1].type==ZYDIS_OPERAND_TYPE_REGISTER&&result.count(Full(i.operands[1].reg.value)))result.insert(Full(i.operands[0].reg.value));}
    return result;
}
bool Member(const ZydisDecodedOperand& op,const std::set<ZydisRegister>& regs,int offset,int bits=0){
    return op.type==ZYDIS_OPERAND_TYPE_MEMORY&&op.mem.index==ZYDIS_REGISTER_NONE&&regs.count(Full(op.mem.base))&&op.mem.disp.value==offset&&(!bits||op.size==bits);
}
bool Reference(const Instructions& code,int offset,int bits=0){
    auto regs=ThisRegisters(code);
    for(auto& i:code)for(unsigned n=0;n<i.code.operand_count_visible;n++)if(Member(i.operands[n],regs,offset,bits))return true;return false;
}
uint32_t Single(const std::set<uint32_t>& values,const char* reason){if(values.size()!=1)Abi(reason);return *values.begin();}
void ValidateLayout(const PeImage& image,NativeProfile& p){
    std::set<uint32_t> counts;
    auto getter=p[NativeFn::LogicalCount].rva?Decode(image,p[NativeFn::LogicalCount].rva):Instructions{};
    for(auto& i:getter)if(i.code.mnemonic==ZYDIS_MNEMONIC_MOVZX&&i.code.operand_count_visible==2&&i.operands[0].type==ZYDIS_OPERAND_TYPE_REGISTER&&i.operands[0].reg.value==ZYDIS_REGISTER_EAX
        &&i.operands[1].type==ZYDIS_OPERAND_TYPE_MEMORY&&i.operands[1].mem.base==ZYDIS_REGISTER_RCX&&i.operands[1].size==16&&i.operands[1].mem.disp.value>0&&i.operands[1].mem.disp.value<0x10000)counts.insert(uint32_t(i.operands[1].mem.disp.value));
    if(getter.empty()){
        // MSVC may inline the ushort getter. Require independent chart loaders
        // to read the same RIP-relative word inside g_GlobalSettings.
        auto inlineCount=[&](NativeFn function){
            std::set<uint32_t> members;
            for(auto& i:Decode(image,p[function].rva)){
                if(i.code.mnemonic!=ZYDIS_MNEMONIC_MOVZX||i.code.operand_count_visible!=2)continue;
                auto& src=i.operands[1];
                if(src.type!=ZYDIS_OPERAND_TYPE_MEMORY||src.size!=16||src.mem.base!=ZYDIS_REGISTER_RIP)continue;
                int64_t address=int64_t(i.address+i.code.length)+src.mem.disp.value;
                if(address>p.globalSettings&&address<int64_t(p.globalSettings)+0x10000)members.insert(uint32_t(address-p.globalSettings));
            }
            return Single(members,"inline_count_member");
        };
        auto a=inlineCount(NativeFn::LoadLogical),b=inlineCount(NativeFn::LoadHeatMap);
        if(a!=b)Abi("inline_count_cross_reference");counts.insert(a);
    }
    p.countOffset=Single(counts,"logical_count_member");
    auto load=Decode(image,p[NativeFn::LoadHeatMap].rva);auto regs=ThisRegisters(load);std::set<uint32_t> heatmaps;
    bool allocation=false;
    for(auto& i:load){
        if(i.code.mnemonic==ZYDIS_MNEMONIC_MOV&&i.code.operand_count_visible==2){auto& dest=i.operands[0];auto& src=i.operands[1];
            if(dest.type==ZYDIS_OPERAND_TYPE_REGISTER&&dest.reg.value==ZYDIS_REGISTER_ECX&&src.type==ZYDIS_OPERAND_TYPE_IMMEDIATE&&src.imm.value.u==0x30)allocation=true;
            if(dest.type==ZYDIS_OPERAND_TYPE_MEMORY&&dest.size==64&&dest.mem.index==ZYDIS_REGISTER_NONE&&regs.count(Full(dest.mem.base))&&dest.mem.disp.value>=0x80&&dest.mem.disp.value<0x1000&&src.type==ZYDIS_OPERAND_TYPE_REGISTER)heatmaps.insert(uint32_t(dest.mem.disp.value));}}
    if(!allocation)Abi("heatmap_allocation_size");
    p.viewHeatmapOffset=Single(heatmaps,"view_heatmap_member");
    auto update=Decode(image,p[NativeFn::ViewUpdate].rva);regs=ThisRegisters(update);std::set<uint32_t> modes;
    for(auto& i:update)if(i.code.mnemonic==ZYDIS_MNEMONIC_CMP&&i.code.operand_count_visible==2){auto& mem=i.operands[0];auto& value=i.operands[1];
        if(mem.type==ZYDIS_OPERAND_TYPE_MEMORY&&mem.size==32&&regs.count(Full(mem.mem.base))&&mem.mem.disp.value>=0x80&&mem.mem.disp.value<0x1000&&value.type==ZYDIS_OPERAND_TYPE_IMMEDIATE&&value.imm.value.u==3)modes.insert(uint32_t(mem.mem.disp.value));}
    p.viewModeOffset=Single(modes,"view_mode_member");
    auto change=Decode(image,p[NativeFn::SwitchChart].rva);
    if(!Reference(change,p.viewModeOffset,32)||!Reference(change,p.viewHeatmapOffset,64))Abi("chart_cross_reference");
    auto initialize=Decode(image,p[NativeFn::Initialize].rva);
    if(!Reference(initialize,0x10,32)||!Reference(initialize,0,64)||!Reference(initialize,8,64)||!Reference(initialize,0x18,64)||!Reference(initialize,0x20))Abi("heatmap_members");
    auto block=Decode(image,p[NativeFn::SetBlockData].rva);
    if(!Reference(block,0x18,64)||!Reference(block,0x28,64))Abi("block_members");
    auto viewDestructor=Decode(image,p[NativeFn::ViewDestructor].rva);
    if(!Reference(viewDestructor,p.viewHeatmapOffset,64))Abi("view_destructor_member");
    if(p[NativeFn::Destructor].rva){
        auto destructor=Decode(image,p[NativeFn::Destructor].rva);
        if(!Reference(destructor,0,64)||!Reference(destructor,0x18,64))Abi("destructor_members");
    }else{
        // Older compilers inline CpuHeatMap::~CpuHeatMap in the CPU view.
        // Verify its two array frees and final object free use the native sized
        // delete entry before allowing the explicit equivalent cleanup adapter.
        auto viewRegs=ThisRegisters(viewDestructor);std::set<ZydisRegister> heatmapRegs;
        for(auto& i:viewDestructor)if(i.code.mnemonic==ZYDIS_MNEMONIC_MOV&&i.code.operand_count_visible==2&&i.operands[0].type==ZYDIS_OPERAND_TYPE_REGISTER&&Member(i.operands[1],viewRegs,p.viewHeatmapOffset,64))heatmapRegs.insert(Full(i.operands[0].reg.value));
        bool ids=false,cells=false;unsigned arrayFrees=0,objectFrees=0;
        for(auto& i:viewDestructor){
            for(unsigned n=0;n<i.code.operand_count_visible;n++){ids=ids||Member(i.operands[n],heatmapRegs,0,64);cells=cells||Member(i.operands[n],heatmapRegs,0x18,64);}
            if(i.code.mnemonic==ZYDIS_MNEMONIC_CALL&&i.operands[0].type==ZYDIS_OPERAND_TYPE_IMMEDIATE&&i.operands[0].imm.is_relative){
                int64_t target=int64_t(i.address+i.code.length)+i.operands[0].imm.value.s;
                if(target==p[NativeFn::DeleteArray].rva)++arrayFrees;
                if(target==p[NativeFn::DeleteObject].rva)++objectFrees;
            }
        }
        if(!ids||!cells||arrayFrees<2||!objectFrees)Abi("inlined_heatmap_destructor");
    }
    if(p.viewHeatmapOffset%8||p.viewModeOffset%4||uint64_t(p.globalSettings)+p.countOffset+2>p.imageSize)Abi("member_range");
}
std::string Hex(const uint8_t* bytes,size_t count){std::ostringstream s;s<<std::hex<<std::setfill('0');for(size_t i=0;i<count;i++)s<<std::setw(2)<<unsigned(bytes[i]);return s.str();}
std::string Narrow(const std::wstring& s){std::string out;for(wchar_t c:s)out+=c<128?char(c):'?';return out;}
}
CompatibilityResult ResolveCompatibility(const std::wstring& imagePath,const CompatibilityOptions& options){
    if(options.cancelled&&options.cancelled())throw CompatibilityError("cancelled",L"已取消匹配。");
    if(options.progress)options.progress(L"检测 Taskmgr 版本与体系结构…");
    PeImage image(imagePath);CompatibilityResult result;result.fileVersion=image.version;result.pdbKey=image.Key();
    if(!options.forceSymbols&&KnownNativeProfile(image.hash.data(),result.profile)){result.method="embedded_verified_profile";return result;}
    auto& p=result.profile;p.timestamp=image.Header().FileHeader.TimeDateStamp;p.imageSize=image.Header().OptionalHeader.SizeOfImage;p.pdbGuid=image.guid;p.pdbAge=image.age;memcpy(p.sha256,image.hash.data(),32);
    if(options.progress)options.progress(L"匹配 Taskmgr "+image.version+L" 的微软符号（首次需要联网）…");
    auto pdb=FindMatchingPdb(image,options,result.symbolDownloaded);
    if(options.cancelled&&options.cancelled())throw CompatibilityError("cancelled",L"已取消匹配。");
    if(options.progress)options.progress(L"核对符号身份、函数签名与原生对象布局…");
    ResolvePublicSymbols(imagePath,image,pdb,p);ValidateLayout(image,p);
    result.method=result.symbolDownloaded?"microsoft_symbols_downloaded":"microsoft_symbols_cached";return result;
}
std::string CompatibilityResult::Json()const{
    std::ostringstream s;s<<"{\"compatible\":true,\"file_version\":\""<<Narrow(fileVersion)<<"\",\"method\":\""<<method<<"\",\"pdb_key\":\""<<Narrow(pdbKey)<<"\",\"sha256\":\""<<Hex(profile.sha256,32)<<"\",\"runtime_tested\":false,\"global_settings\":"<<profile.globalSettings
        <<",\"count_offset\":"<<profile.countOffset<<",\"heatmap_offset\":"<<profile.viewHeatmapOffset<<",\"mode_offset\":"<<profile.viewModeOffset<<",\"functions\":{";
    for(size_t i=0;i<kNativeFunctions;i++){if(i)s<<",";s<<"\""<<NativeFunctionName(NativeFn(i))<<"\":{\"rva\":"<<profile.entries[i].rva<<",\"preimage\":\""<<Hex(profile.entries[i].preimage,16)<<"\"}";}
    return s.str()+"}}";
}
std::string CompatibilityFailureJson(const std::wstring& imagePath,const CompatibilityError& error){
    // Include a portable identity for reports from another PC, without recording
    // user names, game session secrets, proxy credentials or local file paths.
    std::string reason=error.what();for(char& c:reason)if(c=='"'||c=='\\'||static_cast<unsigned char>(c)<32)c='_';
    std::ostringstream s;s<<"{\"compatible\":false,\"injected\":false,\"error\":\""<<reason<<"\"";
    try{PeImage image(imagePath);s<<",\"file_version\":\""<<Narrow(image.version)<<"\",\"pdb_key\":\""<<Narrow(image.Key())<<"\",\"sha256\":\""<<Hex(image.hash.data(),32)<<"\"";}catch(...){}
    return s.str()+"}";
}
