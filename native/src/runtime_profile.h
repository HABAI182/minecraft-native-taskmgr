#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <filesystem>
#include <functional>

enum class NativeFn:uint32_t{
    LoadLogical,LoadHeatMap,LogicalCount,Initialize,UpdateData,Layout,Destructor,
    SetBlockData,GetBlockColors,SwitchChart,GetCpuIdList,ViewUpdate,DeleteObject,ViewDestructor,DeleteArray,Count
};
constexpr size_t kNativeFunctions=static_cast<size_t>(NativeFn::Count);
struct NativeEntry{uint32_t rva=0;uint8_t preimage[16]{};};
// Plain data copied into the session mapping. No pointers, PDB paths or code are
// accepted from a downloaded profile: the controller derives this from the local
// PE and its GUID/age-matched Microsoft PDB and verifies the native object layout.
struct NativeProfile{
    uint32_t schema=1,timestamp=0,imageSize=0;
    uint32_t globalSettings=0,countOffset=0,viewHeatmapOffset=0,viewModeOffset=0;
    GUID pdbGuid{};uint32_t pdbAge=0;
    uint8_t sha256[32]{};
    NativeEntry entries[kNativeFunctions]{};
    const NativeEntry& operator[](NativeFn f)const{return entries[static_cast<size_t>(f)];}
    NativeEntry& operator[](NativeFn f){return entries[static_cast<size_t>(f)];}
};
struct CompatibilityOptions{
    std::filesystem::path cacheDirectory;
    std::filesystem::path pdbOverride; // Developer/offline test only; identity is still verified.
    bool allowDownload=true,forceSymbols=false;
    std::function<void(const std::wstring&)> progress;
    std::function<bool()> cancelled;
};
struct CompatibilityResult{
    NativeProfile profile{};
    std::wstring fileVersion,pdbKey;
    std::string method;
    bool symbolDownloaded=false;
    std::string Json()const;
};
class CompatibilityError:public std::exception{
    std::string what_;std::wstring detail_;
public:
    CompatibilityError(std::string reason,std::wstring detail):what_(std::move(reason)),detail_(std::move(detail)){}
    const char* what()const noexcept override{return what_.c_str();}
    const std::wstring& Detail()const noexcept{return detail_;}
};
CompatibilityResult ResolveCompatibility(const std::wstring& imagePath,const CompatibilityOptions& options={});
std::string CompatibilityFailureJson(const std::wstring& imagePath,const CompatibilityError& error);
std::filesystem::path CompatibilityCache();
const char* NativeFunctionName(NativeFn function);
bool KnownNativeProfile(const uint8_t hash[32],NativeProfile& profile);
