#include "pe_image.h"
#include <fstream>
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>

namespace{
[[noreturn]]void Bad(){throw CompatibilityError("invalid_image",L"任务管理器文件不是有效的原生 x64 PE，或其调试信息不完整。");}
template<class T>T At(const std::vector<uint8_t>& data,size_t offset){
    if(offset>data.size()||sizeof(T)>data.size()-offset)Bad();T value;memcpy(&value,data.data()+offset,sizeof(T));return value;
}
}
PeImage::PeImage(const std::wstring& filename){
    std::ifstream input(std::filesystem::path(filename),std::ios::binary|std::ios::ate);
    if(!input||input.tellg()<0||input.tellg()>128*1024*1024)Bad();
    bytes_.resize(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes_.data()),bytes_.size());if(!input)Bad();
    auto dos=At<IMAGE_DOS_HEADER>(bytes_,0);if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0)Bad();
    header_=At<IMAGE_NT_HEADERS64>(bytes_,size_t(dos.e_lfanew));
    if(header_.Signature!=IMAGE_NT_SIGNATURE||header_.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||header_.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        throw CompatibilityError("architecture",L"兼容版需要原生 x64 Windows 与 x64 任务管理器；ARM64 和 32 位版本暂未适配。");
    if(header_.FileHeader.NumberOfSections>96||header_.OptionalHeader.SizeOfImage>256*1024*1024)Bad();
    size_t offset=size_t(dos.e_lfanew)+24+header_.FileHeader.SizeOfOptionalHeader;
    for(unsigned i=0;i<header_.FileHeader.NumberOfSections;i++)sections_.push_back(At<IMAGE_SECTION_HEADER>(bytes_,offset+i*sizeof(IMAGE_SECTION_HEADER)));
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)Bad();
    NTSTATUS status=BCryptHash(algorithm,nullptr,0,bytes_.data(),ULONG(bytes_.size()),hash.data(),ULONG(hash.size()));BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)Bad();
    auto debug=header_.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    for(uint32_t pos=0;pos+sizeof(IMAGE_DEBUG_DIRECTORY)<=debug.Size;pos+=sizeof(IMAGE_DEBUG_DIRECTORY)){
        auto entry=ReadRva<IMAGE_DEBUG_DIRECTORY>(debug.VirtualAddress+pos);
        if(entry.Type!=IMAGE_DEBUG_TYPE_CODEVIEW||entry.SizeOfData<25)continue;
        auto p=Rva(entry.AddressOfRawData,entry.SizeOfData);
        if(memcmp(p,"RSDS",4)!=0)continue;
        memcpy(&guid,p+4,sizeof(guid));memcpy(&age,p+20,4);
        auto begin=reinterpret_cast<const char*>(p+24);auto end=static_cast<const char*>(memchr(begin,0,entry.SizeOfData-24));if(!end)Bad();
        std::string name(begin,end);auto slash=name.find_last_of("\\/");if(slash!=name.npos)name=name.substr(slash+1);
        if(_stricmp(name.c_str(),"Taskmgr.pdb")!=0)Bad();pdbName.assign(name.begin(),name.end());break;
    }
    if(pdbName.empty()||age==0)Bad();
    DWORD ignored=0,size=GetFileVersionInfoSizeW(filename.c_str(),&ignored);
    if(size&&size<4*1024*1024){std::vector<uint8_t> info(size);VS_FIXEDFILEINFO* fixed=nullptr;UINT length=0;
        if(GetFileVersionInfoW(filename.c_str(),0,size,info.data())&&VerQueryValueW(info.data(),L"\\",reinterpret_cast<void**>(&fixed),&length)&&length>=sizeof(*fixed)){
            version=std::to_wstring(HIWORD(fixed->dwFileVersionMS))+L"."+std::to_wstring(LOWORD(fixed->dwFileVersionMS))+L"."+std::to_wstring(HIWORD(fixed->dwFileVersionLS))+L"."+std::to_wstring(LOWORD(fixed->dwFileVersionLS));
            struct Translation{WORD language,codepage;}* translation=nullptr;
            if(VerQueryValueW(info.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translation),&length)&&length>=sizeof(Translation)){
                wchar_t key[128]{};swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\FileVersion",translation->language,translation->codepage);
                wchar_t* text=nullptr;if(VerQueryValueW(info.data(),key,reinterpret_cast<void**>(&text),&length)&&text&&length){version=text;auto space=version.find(L' ');if(space!=version.npos)version.resize(space);}
            }
        }}
}
const uint8_t* PeImage::Rva(uint32_t rva,size_t size)const{
    for(auto s:sections_)if(rva>=s.VirtualAddress&&uint64_t(rva)+size<=uint64_t(s.VirtualAddress)+s.SizeOfRawData){
        size_t offset=size_t(s.PointerToRawData)+rva-s.VirtualAddress;
        if(offset>bytes_.size()||size>bytes_.size()-offset)Bad();return bytes_.data()+offset;
    }
    Bad();
}
bool PeImage::Executable(uint32_t rva,size_t size)const{
    for(auto s:sections_)if((s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&rva>=s.VirtualAddress&&uint64_t(rva)+size<=uint64_t(s.VirtualAddress)+s.SizeOfRawData)return true;
    return false;
}
uint32_t PeImage::FunctionLength(uint32_t rva)const{
    auto table=header_.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    for(uint32_t pos=0;pos+sizeof(RUNTIME_FUNCTION)<=table.Size;pos+=sizeof(RUNTIME_FUNCTION)){
        auto f=ReadRva<RUNTIME_FUNCTION>(table.VirtualAddress+pos);
        if(f.BeginAddress==rva&&f.EndAddress>rva&&f.EndAddress-rva<=16384)return f.EndAddress-rva;
    }
    // Leaf getters and forwarding delete stubs need not have unwind records.
    return 64;
}
std::wstring PeImage::GuidKey(const GUID& id,uint32_t age){
    wchar_t key[64]{};swprintf_s(key,L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7],age);return key;
}
std::wstring PeImage::Key()const{return GuidKey(guid,age);}
