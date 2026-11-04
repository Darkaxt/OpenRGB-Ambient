#include "../OfficeLayout.h"
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
        std::cout<<"PASS: real mapping transform, identity, hardware order and bounds contracts\n";
        return 0;
    } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
