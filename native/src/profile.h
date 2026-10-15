#pragma once
#include <cstdint>
namespace Profile {
constexpr uint32_t Timestamp=2253439176,ImageSize=5795840;
constexpr uint32_t LoadLogical=0x1c54f0;
constexpr uint32_t ViewUpdate=0xac00;
constexpr uint8_t ViewUpdateBytes[16]={72,137,92,36,8,87,72,131,236,32,72,139,217,232,98,4};
constexpr uint32_t DeleteObject=0x124ddc;
constexpr uint8_t DeleteObjectBytes[16]={233,47,7,0,0,204,204,204,204,204,204,204,204,204,204,204};
constexpr uint8_t LoadLogicalBytes[16]={72,137,92,36,8,72,137,116,36,24,72,137,124,36,32,65};
constexpr uint32_t LoadHeatMap=0x1c540c;
constexpr uint8_t LoadHeatMapBytes[16]={72,137,92,36,16,72,137,116,36,24,87,72,131,236,48,72};
constexpr uint32_t LogicalCount=0x74ad8;
constexpr uint8_t LogicalCountBytes[16]={15,183,129,56,9,0,0,102,133,192,116,2,195,204,184,1};
constexpr uint32_t Initialize=0x1d1684;
constexpr uint8_t InitializeBytes[16]={72,137,92,36,16,72,137,108,36,24,86,87,65,84,65,86};
constexpr uint32_t UpdateData=0x1d1eb0;
constexpr uint8_t UpdateDataBytes[16]={72,137,92,36,16,72,137,116,36,24,85,87,65,87,72,141};
constexpr uint32_t Layout=0x1d18b0;
constexpr uint8_t LayoutBytes[16]={72,139,196,72,137,88,24,72,137,112,32,87,72,131,236,48};
constexpr uint32_t Destructor=0x1d1350;
constexpr uint8_t DestructorBytes[16]={64,83,72,131,236,32,72,139,217,72,139,9,232,123,58,245};
constexpr uint32_t SetBlockData=0x1d1d58;
constexpr uint8_t SetBlockDataBytes[16]={72,137,92,36,8,72,137,108,36,16,72,137,116,36,24,87};
constexpr uint32_t GetBlockColors=0x1d1378;
constexpr uint8_t GetBlockColorsBytes[16]={64,85,72,139,236,72,131,236,96,72,139,5,56,123,54,0};
constexpr uint32_t BlockWidth=0x1d1470;
constexpr uint8_t BlockWidthBytes[16]={72,131,236,88,72,139,5,69,122,54,0,72,51,196,72,137};
constexpr uint32_t SwitchChart=0x1c61b8;
constexpr uint8_t SwitchChartBytes[16]={72,137,92,36,8,72,137,108,36,16,72,137,116,36,24,87};
constexpr uint32_t GetCpuIdList=0xa1e80;
constexpr uint8_t GetCpuIdListBytes[16]={72,139,196,68,137,64,24,72,137,72,8,83,86,87,65,84};
}
