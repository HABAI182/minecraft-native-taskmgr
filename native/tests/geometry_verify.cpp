#include "heatmap_geometry.h"
#include <cstdio>
int main(){
    auto first=FitHeatmap(640,0,64,36);
    if(first.width!=10||first.height!=10)return 1;
    auto viewport=FitHeatmap(640,180,64,36);
    if(viewport.width!=10||viewport.height!=5)return 2;
    if(FitHeatmap(0,0,64,36).width!=0)return 3;
    auto resize=FitHeatmap(960,540,64,36);
    if(resize.width!=15||resize.height!=15)return 4;
    puts("{\"auto_height_zero_bootstrap\":true,\"viewport_fit\":true,\"await_zero_width\":true,\"resize\":true}");return 0;
}
