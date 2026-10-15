#pragma once
#include "runtime_profile.h"
#include <vector>
#include <cstring>
#include <stdexcept>

class PeImage{
    std::vector<uint8_t> bytes_;
    IMAGE_NT_HEADERS64 header_{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
public:
    GUID guid{};uint32_t age=0;std::wstring pdbName,version;
    std::array<uint8_t,32> hash{};
    explicit PeImage(const std::wstring& filename);
    const IMAGE_NT_HEADERS64& Header()const{return header_;}
    const uint8_t* Rva(uint32_t rva,size_t size)const;
    bool Executable(uint32_t rva,size_t size=16)const;
    uint32_t FunctionLength(uint32_t rva)const;
    std::wstring Key()const;
    static std::wstring GuidKey(const GUID& guid,uint32_t age);
    template<class T>T ReadRva(uint32_t rva)const{T result;memcpy(&result,Rva(rva,sizeof(T)),sizeof(T));return result;}
};
