#pragma once
#include <algorithm>
struct HeatmapGeometry{int width=0,height=0;};
// Native GridLayout sizes an auto-height container from its children. Its initial
// height may legally be zero; requiring a nonzero height prevents the first layout.
inline HeatmapGeometry FitHeatmap(int width,int viewportHeight,int columns,int rows){
    if(columns<1||rows<1||width<columns)return {};
    int w=width/columns;
    int h=viewportHeight>=rows?std::min(w,viewportHeight/rows):w;
    return {w,std::max(1,h)};
}
