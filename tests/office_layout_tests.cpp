#include "../OfficeLayout.h"
#include "../OfficeProcessing.h"
#include <iostream>
using namespace office;
int main() {
    try {
        std::vector<Region> points={{.1f,.2f,.2f,.3f},{.3f,.2f,.4f,.3f}};
        auto turned=transform(points,{.1f,.2f,.4f,.3f},{.4f,.1f,.6f,.7f},90,false);
        if(std::abs(turned[0].left-.4f)>.0001f || std::abs(turned[0].top-.1f)>.0001f)
            throw std::runtime_error("Quarter-turn must preserve LED0 and rotate its rectangle");
        auto reversed=transform(points,{.1f,.2f,.4f,.3f},{.1f,.2f,.4f,.3f},0,true);
        if(reversed[0].left!=points[1].left) throw std::runtime_error("Reverse must change sampling order without renumbering hardware LEDs");
        bool rejected=false;
        try { validateRegions(Json::array({Json::array({0,0,2,1})}),1); } catch(std::exception const&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Off-screen rectangles must be rejected before persistence");
        rejected=false;
        try { validateRegions(Json::array({Json::array({0,0,0,1})}),1); } catch(std::exception const&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Empty rectangle must be rejected");
        auto identity=transform(points,{.1f,.2f,.4f,.3f},{.1f,.2f,.4f,.3f},0,false);
        for(size_t i=0;i<points.size();++i) if(std::abs(identity[i].left-points[i].left)>.0001f)
            throw std::runtime_error("Opening/saving the editor must not move current samples");
        Json settings=processingDefaults();settings["smoothing_ms"]=140;validateProcessing(settings);
        if(smoothingWeight(settings,67,256)!=1) throw std::runtime_error("Scene-cut bypass failed");
        settings["smoothing_mode"]="ema";auto a=smoothingWeight(settings,40,0),b=smoothingWeight(settings,80,0);
        if(std::abs((1-a)*(1-a)-(1-b))>.0001) throw std::runtime_error("EMA changes with output FPS");
        settings["adaptive_min"]=30;rejected=false;try {validateProcessing(settings);}catch(std::exception const&){rejected=true;}
        if(!rejected) throw std::runtime_error("Inverted adaptive range accepted");
        auto tiny=resizeBounds({0,0,.00001f,.00001f},5,-1,-1);
        validateRegions(Json::array({encode(tiny)}),1);
        for(unsigned handle:{1u,2u,4u,8u,5u,6u,9u,10u}) {
            auto r=resizeBounds({.2f,.3f,.6f,.7f},handle,100,-100);
            validateRegions(Json::array({encode(r)}),1);
        }
        settings=processingDefaults();
        for(auto mode:{"hold","fade","dim","pulse","rotate","aurora","ember","off"}) {
            settings["standby"]=mode;validateProcessing(settings);auto c=standbyColor(settings,{120,60,30},4,.2f,.8f);
            if(std::string(mode)=="hold" && c!=std::array<unsigned char,3>{120,60,30}) throw std::runtime_error("Hold standby changed the original colour");
            if(std::string(mode)=="off" && c!=std::array<unsigned char,3>{0,0,0}) throw std::runtime_error("Off standby emitted light");
        }
        std::cout<<"PASS: real mapping transform, identity, hardware order and bounds contracts\n";
        return 0;
    } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
