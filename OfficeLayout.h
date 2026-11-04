#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace office {
using Json=nlohmann::json;
struct Region { float left,top,right,bottom; };
inline Json encode(const Region& r) { return Json::array({r.left,r.top,r.right,r.bottom}); }
inline Json encode(const std::vector<Region>& regions) {
    auto result=Json::array(); for(auto r:regions) result.push_back(encode(r)); return result;
}
inline std::vector<Region> validateRegions(const Json& values,size_t count) {
    if(!values.is_array() || values.size()!=count) throw std::runtime_error("Physical LED count differs from the connected controller");
    std::vector<Region> result;
    for(const auto& value:values) {
        if(!value.is_array() || value.size()!=4) throw std::runtime_error("Each LED needs four sampling coordinates");
        for(const auto& n:value) if(!n.is_number() || !std::isfinite(n.get<double>())) throw std::runtime_error("Sampling coordinates must be finite numbers");
        Region r{value[0].get<float>(),value[1].get<float>(),value[2].get<float>(),value[3].get<float>()};
        if(r.left<0 || r.top<0 || r.right>1 || r.bottom>1 || r.right<=r.left || r.bottom<=r.top)
            throw std::runtime_error("Sampling rectangles must have positive size and stay inside the LG image");
        result.push_back(r);
    }
    return result;
}
inline Region bounds(const std::vector<Region>& values) {
    Region r{1,1,0,0};
    for(auto p:values) { r.left=std::min(r.left,p.left);r.top=std::min(r.top,p.top);r.right=std::max(r.right,p.right);r.bottom=std::max(r.bottom,p.bottom); }
    return r;
}
// Handle bits: left=1, right=2, top=4, bottom=8. Anchor the opposite side;
// never cross it or leave the selected monitor, even with a far-away pointer.
inline Region resizeBounds(Region b,unsigned handle,float dx,float dy,float minimum=.002f,float snap=0) {
    auto quantize=[&](float v){return snap>0?std::round(v/snap)*snap:v;};
    float minWidth=std::min(minimum,b.right-b.left),minHeight=std::min(minimum,b.bottom-b.top);
    if(handle&1) b.left=std::clamp(quantize(b.left+dx),0.f,b.right-minWidth);
    if(handle&2) b.right=std::clamp(quantize(b.right+dx),b.left+minWidth,1.f);
    if(handle&4) b.top=std::clamp(quantize(b.top+dy),0.f,b.bottom-minHeight);
    if(handle&8) b.bottom=std::clamp(quantize(b.bottom+dy),b.top+minHeight,1.f);
    return b;
}
// Transform rectangles, with screen Y down, using the same geometry for drawing,
// hit testing and the GPU. Vector position always remains the hardware LED index.
inline std::vector<Region> transform(const std::vector<Region>& input,Region from,Region to,int rotation,bool reverse) {
    if(from.right<=from.left || from.bottom<=from.top || to.right<=to.left || to.bottom<=to.top || rotation%90)
        throw std::runtime_error("Choose a positive rectangle and a quarter-turn rotation");
    validateRegions(Json::array({encode(to)}),1);
    std::vector<Region> result;
    for(size_t i=0;i<input.size();++i) {
        auto r=input[reverse?input.size()-1-i:i];
        Region out{1,1,0,0};
        for(auto x:{r.left,r.right}) for(auto y:{r.top,r.bottom}) {
            float u=(x-from.left)/(from.right-from.left),v=(y-from.top)/(from.bottom-from.top);
            switch((rotation%360+360)%360) {
                case 90: { float old=u;u=1-v;v=old;break; }
                case 180:u=1-u;v=1-v;break;
                case 270:{ float old=u;u=v;v=1-old;break; }
            }
            float px=to.left+u*(to.right-to.left),py=to.top+v*(to.bottom-to.top);
            out.left=std::min(out.left,px);out.right=std::max(out.right,px);
            out.top=std::min(out.top,py);out.bottom=std::max(out.bottom,py);
        }
        out.left=std::clamp(out.left,0.f,1.f);out.right=std::clamp(out.right,0.f,1.f);
        out.top=std::clamp(out.top,0.f,1.f);out.bottom=std::clamp(out.bottom,0.f,1.f);
        result.push_back(out);
    }
    validateRegions(encode(result),result.size());
    return result;
}
}
