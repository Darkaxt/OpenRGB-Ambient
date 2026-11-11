#pragma once
#include "OfficeLayout.h"
#include "OfficeFullscreenProfiles.h"
#include <array>
#include <map>
#include <string>

namespace office {
inline Json processingDefaults() {
    return {{"strategy","mapped"},{"depth",10.0},{"adaptive_min",3.0},{"adaptive_max",20.0},
        {"black_bar_detect",false},{"blur",0.0},{"saturation",1.0},{"brightness",1.0},{"follow_display_brightness",true},
        {"lighting_intensity",100.0},{"fullscreen_intensity",50.0},{"device_intensity",Json::object()},{"device_night_level",Json::object()},
        {"fullscreen_profiles",Json::object()},
        {"min_luminance",0.0},{"ema_alpha",0.35},{"scene_cut",255.0},
        {"smoothing_mode","time"},{"standby","hold"},{"standby_brightness",0.15},
        {"effect_center_x",0.5},{"effect_center_y",0.5}};
}
inline void processingDefaults(Json& config) {
    const auto defaults=processingDefaults();
    for(auto& [key,value]:defaults.items()) if(!config.contains(key)) config[key]=value;
}
inline void validateProcessing(const Json& c) {
    const std::vector<std::string> strategies{"mapped","fixed","adaptive","percentile","dominant"};
    if(std::find(strategies.begin(),strategies.end(),c.at("strategy").get<std::string>())==strategies.end()) throw std::runtime_error("Unknown sampling algorithm");
    for(auto [key,range]:std::map<std::string,std::pair<double,double>>{
        {"depth",{0.1,50}},{"adaptive_min",{0.1,50}},{"adaptive_max",{0.1,50}},
        {"blur",{0,10}},{"saturation",{0,2.5}},{"brightness",{0,2}},{"lighting_intensity",{0,100}},{"fullscreen_intensity",{0,100}},
        {"min_luminance",{0,255}},{"ema_alpha",{0.05,1}},{"scene_cut",{0,255}},
        {"standby_brightness",{0,1}},{"effect_center_x",{0,1}},{"effect_center_y",{0,1}}}) {
        if(!c.at(key).is_number()) throw std::runtime_error("Sampling settings must be numeric");
        double n=c.at(key);if(!std::isfinite(n) || n<range.first || n>range.second) throw std::runtime_error("Sampling setting outside its supported range: "+key);
    }
    if(c.at("adaptive_min")>c.at("adaptive_max")) throw std::runtime_error("Adaptive minimum exceeds maximum depth");
    if(!c.at("device_intensity").is_object())throw std::runtime_error("Device intensity must be an object");
    validateFullscreenProfiles(c.at("fullscreen_profiles"));
    for(auto& [id,value]:c.at("device_intensity").items())
        if(id.empty()||!value.is_number()||!std::isfinite(value.get<double>())||value<0||value>100)throw std::runtime_error("Device intensity must be 0-100 percent");
    if(!c.at("device_night_level").is_object())throw std::runtime_error("Device night levels must be an object");
    for(auto& [id,value]:c.at("device_night_level").items())
        if(id.empty()||!value.is_number()||!std::isfinite(value.get<double>())||value<0||value>100)throw std::runtime_error("Device night level must be 0-100 percent");
    if(!c.at("black_bar_detect").is_boolean()) throw std::runtime_error("Black bar detection must be a boolean");
    if(!c.at("follow_display_brightness").is_boolean()) throw std::runtime_error("Follow display brightness must be a boolean");
    if(c.at("smoothing_mode")!="time" && c.at("smoothing_mode")!="ema") throw std::runtime_error("Unknown smoothing mode");
    const std::vector<std::string> standby{"hold","fade","dim","pulse","rotate","aurora","ember","off"};
    if(std::find(standby.begin(),standby.end(),c.at("standby").get<std::string>())==standby.end()) throw std::runtime_error("Unknown screen standby effect");
}
inline std::array<float,16> shaderOptions(const Json& c) {
    int strategy=0;std::string s=c.at("strategy");
    if(s=="fixed") strategy=1;else if(s=="adaptive") strategy=2;else if(s=="percentile") strategy=3;else if(s=="dominant") strategy=4;
    return {static_cast<float>(strategy),c.at("depth").get<float>()/100,c.at("adaptive_min").get<float>()/100,c.at("adaptive_max").get<float>()/100,
        c.at("blur").get<float>()/100,c.at("saturation").get<float>(),c.at("brightness").get<float>(),c.at("min_luminance").get<float>()/255,
        c.at("black_bar_detect").get<bool>()?1.f:0.f,0,0,0,0,0,0,0};
}
inline float smoothingWeight(const Json& c,double elapsedMs,double delta) {
    if(delta>c.at("scene_cut").get<double>()) return 1;
    if(c.at("smoothing_mode")=="ema") return static_cast<float>(1-std::pow(1-c.at("ema_alpha").get<double>(),elapsedMs/(1000.0/15)));
    double ms=c.at("smoothing_ms");return ms<=0?1.f:static_cast<float>(1-std::exp(-elapsedMs/ms));
}
inline unsigned char adaptedChannel(unsigned char target,unsigned char previous,float weight) {
    if(target==previous || weight<=0) return previous;
    int next=std::lround(previous+(int(target)-int(previous))*std::min(weight,1.f));
    // Integer output must converge, rather than stall below a one-byte step.
    if(next==previous) next+=target>previous?1:-1;
    return static_cast<unsigned char>(next);
}
inline std::array<unsigned char,3> standbyColor(const Json& c,std::array<unsigned char,3> last,double seconds,float x,float y) {
    std::string mode=c.at("standby");double level=c.at("standby_brightness");
    std::array<double,3> color{last[0]/255.,last[1]/255.,last[2]/255.};
    if(mode=="off") color={0,0,0};
    else if(mode=="fade") for(auto& v:color) v*=std::exp(-seconds/2);
    else if(mode=="dim" || mode=="pulse") {
        if(mode=="pulse") level*=.25+.75*(.5+.5*std::sin(seconds*1.8));
        for(auto& v:color) v*=level;
    } else if(mode=="rotate") {
        double angle=std::atan2(y-c.at("effect_center_y").get<double>(),x-c.at("effect_center_x").get<double>());
        double h=std::fmod(seconds*.04+angle/6.28318530718+2,1)*6;
        double t=1-std::abs(std::fmod(h,2)-1);
        color=h<1?std::array<double,3>{1,t,0}:h<2?std::array<double,3>{t,1,0}:h<3?std::array<double,3>{0,1,t}:h<4?std::array<double,3>{0,t,1}:h<5?std::array<double,3>{t,0,1}:std::array<double,3>{1,0,t};
        for(auto& v:color) v*=level;
    } else if(mode=="aurora") {
        double wave=.5+.5*std::sin(seconds*.7+x*8-y*3);color={.12*level,level*(.3+.7*wave),level*(1-.6*wave)};
    } else if(mode=="ember") {
        double glow=.5+.25*std::sin(seconds*1.3+x*17)+.25*std::sin(seconds*.4-y*13);color={level*glow,level*glow*glow*.24,0};
    }
    return {static_cast<unsigned char>(std::clamp(std::lround(color[0]*255),0l,255l)),static_cast<unsigned char>(std::clamp(std::lround(color[1]*255),0l,255l)),static_cast<unsigned char>(std::clamp(std::lround(color[2]*255),0l,255l))};
}
}
