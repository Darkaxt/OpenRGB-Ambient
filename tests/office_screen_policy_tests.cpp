// Source-derived LightBulb cycle/solar vectors and independent persistent pauses.
#include "../OfficeScreenPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace office::screen;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(){try{
    Color day{6600,1},night{3600,.85};
    require(cycle(14*3600,7*3600,18*3600,5400,0,day,night)==day,"Daytime contract");
    require(cycle(2*3600,7*3600,18*3600,5400,0,day,night)==Color{3600,.85,1},"Nighttime contract");
    require(cycle(14*3600,7*3600,18*3600,5400,0,day,night).night==0,"Day position for per-light night levels");
    auto between=cycle(18*3600+1800,7*3600,18*3600,5400,0,day,night);
    require(between.temperature>3600&&between.temperature<6600&&between.brightness>.85&&between.brightness<1,"Sunset interpolation");
    require(between.night>0&&between.night<1&&std::abs(between.brightness-(1-.15*between.night))<1e-9,"Night position follows the screen transition curve");
    auto midnight=cycle(0,7*3600,18*3600,5400,0,day,night);
    require(midnight==cycle(86399,7*3600,18*3600,5400,0,day,night),"Midnight continuity");
    for(int t=0;t<86400;t+=60){auto a=cycle(t,26400,84900,5400,0,day,night),b=cycle(t+60,26400,84900,5400,0,day,night);require(std::abs(a.temperature-b.temperature)<1500,"Cross-midnight cycle discontinuity");}
    require(cycle(14*3600,7*3600,18*3600,0,0,day,night)==day,"Zero-duration transition");
    struct Case{double lat,lon;int yearDay,offset;double sunrise,sunset;};
    for(auto c:{Case{40.7128,-74.0060,308,-5,6.5,16+49./60},Case{50.4547,30.5238,308,2,6+55./60,16+29./60},Case{22.5333,114.1333,319,8,6.6,17+40./60},Case{35.6762,139.6503,308,9,6+5./60,16+43./60},Case{69.6489,18.9551,3,1,11.8,11.8}}){auto s=solar(c.lat,c.lon,c.yearDay,c.offset);require(std::abs(s.first/3600-c.sunrise)<.05&&std::abs(s.second/3600-c.sunset)<.05,"Upstream solar vector");}
    auto ramp=multipliers({6600,.85});require(std::abs(ramp[0]-.85)<.000001&&std::abs(ramp[1]-.85)<.000001,"Brightness applied once");
    auto warm=multipliers({3900,.85});require(warm[2]<warm[0]&&std::abs(warm[0]-.85)<.000001,"Warm hue attenuation");
    Pause lighting,screen;lighting.disable(1000,60000);require(lighting.active(60999)&&!lighting.active(61000)&&!screen.active(1001),"Independent RGB pause deadline");
    screen.disable(1000,0);auto saved=screen.json();auto restored=Pause::from(saved);require(restored.active(999999999)&&!lighting.active(61000),"Persistent indefinite screen pause");
    lighting.disable(1000,60000);lighting.clear();require(!lighting.active(1001)&&restored.active(1001),"Enable cancels only its own pause");
    for(int minutes:{1,5,15,30,60,180,360,720,1440}){Pause p;p.disable(1234,minutes*60000LL);auto q=Pause::from(p.json());require(q.active(1234)&&!q.active(1234+minutes*60000LL),"Pause duration persistence");}
    std::cout<<"LightBulb cycle/solar/gamma and independent persistent pause contracts passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
