#pragma once
// Adapted from Tyrrrz/LightBulb f74285784be7bc191872c06b1b5e644eb209b98b.
// Cycle.cs, SolarTimes.cs and GammaService.cs; see LICENSE-LightBulb-MIT.txt.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace office::screen {
using Json=nlohmann::json;
constexpr double pi=3.14159265358979323846;
inline double wrap(double n,double range){return n-std::floor(n/range)*range;}
struct Color {double temperature=6600,brightness=1;bool operator==(const Color&) const=default;};
inline Color cycle(double seconds,double sunrise,double sunset,double duration,double offset,Color day,Color night){
    seconds=wrap(seconds,86400);
    const double riseStart=wrap(sunrise-duration*(1-offset),86400),riseEnd=wrap(sunrise+duration*offset,86400);
    const double setStart=wrap(sunset-duration*offset,86400),setEnd=wrap(sunset+duration*(1-offset),86400);
    auto blend=[&](double weight){return Color{day.temperature+(night.temperature-day.temperature)*weight,day.brightness+(night.brightness-day.brightness)*weight};};
    if(duration>0 && wrap(seconds-riseStart,86400)+wrap(riseEnd-seconds,86400)<=duration+.000001)
        return blend(std::cos(wrap(seconds-riseStart,86400)/duration*pi/2));
    if(duration>0 && wrap(seconds-setStart,86400)+wrap(setEnd-seconds,86400)<=duration+.000001)
        return blend(std::sin(wrap(seconds-setStart,86400)/duration*pi/2));
    return wrap(setEnd-seconds,86400)<wrap(riseEnd-seconds,86400)?day:night;
}
inline std::pair<double,double> solar(double latitude,double longitude,int dayOfYear,double offsetHours){
    auto radians=[](double d){return d*pi/180;};auto degrees=[](double r){return r*180/pi;};
    auto time=[&](bool rise){
        const double lng=longitude/15,t=dayOfYear+((rise?6.:18.)-lng)/24,anomaly=.9856*t-3.289;
        const double sun=wrap(anomaly+282.634+1.916*std::sin(radians(anomaly))+.020*std::sin(2*radians(anomaly)),360);
        double asc=wrap(degrees(std::atan(.91764*std::tan(radians(sun)))),360);
        asc=(asc+std::floor(sun/90)*90-std::floor(asc/90)*90)/15;
        const double sinDec=.39782*std::sin(radians(sun)),cosDec=std::cos(std::asin(sinDec));
        const double cosHours=std::clamp((std::cos(radians(90.83))-sinDec*std::sin(radians(latitude)))/(cosDec*std::cos(radians(latitude))),-1.,1.);
        const double hours=(rise?360-degrees(std::acos(cosHours)):degrees(std::acos(cosHours)))/15;
        return wrap(wrap(hours+asc-.06571*t-6.622-lng,24)+offsetHours,24)*3600;
    };
    return {time(true),time(false)};
}
inline std::array<double,3> multipliers(Color c){
    double red=1,green=1,blue=1;
    if(c.temperature>6600){red=std::clamp(std::pow(c.temperature/100-60,-.1332047592)*329.698727446/255,0.,1.);green=std::clamp(std::pow(c.temperature/100-60,-.0755148492)*288.1221695283/255,0.,1.);}
    else green=std::clamp((std::log(c.temperature/100)*99.4708025861-161.1195681661)/255,0.,1.);
    if(c.temperature<6600)blue=c.temperature<=1900?0:std::clamp((std::log(c.temperature/100-10)*138.5177312231-305.0447927307)/255,0.,1.);
    return {red*c.brightness,green*c.brightness,blue*c.brightness};
}
struct Pause {
    std::string kind="none";int64_t started=0,until=0;
    bool active(int64_t now) const{return kind=="forever" || (kind!="none" && now<until);}
    void clear(){kind="none";started=until=0;}
    void disable(int64_t now,int64_t duration){started=now;until=duration?now+duration:0;kind=duration?"duration":"forever";}
    Json json() const{return {{"kind",kind},{"started",started},{"until",until}};}
    static Pause from(const Json& v){Pause p;if(v.is_object()){p.kind=v.value("kind",std::string("none"));p.started=v.value("started",int64_t{});p.until=v.value("until",int64_t{});}if(p.kind!="none"&&p.kind!="duration"&&p.kind!="sunrise"&&p.kind!="forever")throw std::runtime_error("Invalid pause state");return p;}
};
}
