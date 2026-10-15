#pragma once
#include "pe_image.h"
#include <map>
std::filesystem::path FindMatchingPdb(const PeImage& image,const CompatibilityOptions& options,bool& downloaded);
void ResolvePublicSymbols(const std::wstring& imagePath,const PeImage& image,const std::filesystem::path& pdb,NativeProfile& output);
