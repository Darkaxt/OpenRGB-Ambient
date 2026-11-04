#pragma once
#include "OfficeLayout.h"
#include <array>
#include <map>
#include <string>

namespace office {
inline Json processingDefaults() {
    return {{"strategy","mapped"},{"depth",10.0},{"adaptive_min",3.0},{"adaptive_max",20.0},
        {"black_bar_detect",false},{"blur",0.0},{"saturation",1.0},{"brightness",1.0},
        {"min_luminance",0.0},{"ema_alpha",0.35},{"scene_cut",255.0},
        {"smoothing_mode","time"},{"standby","hold"},{"standby_brightness",0.15}};
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
        {"blur",{0,10}},{"saturation",{0,2.5}},{"brightness",{0,2}},
        {"min_luminance",{0,255}},{"ema_alpha",{0.05,1}},{"scene_cut",{0,255}},
        {"standby_brightness",{0,1}}}) {
        if(!c.at(key).is_number()) throw std::runtime_error("Sampling settings must be numeric");
        double n=c.at(key);if(!std::isfinite(n) || n<range.first || n>range.second) throw std::runtime_error("Sampling setting outside its supported range: "+key);
    }
    if(c.at("adaptive_min")>c.at("adaptive_max")) throw std::runtime_error("Adaptive minimum exceeds maximum depth");
    if(!c.at("black_bar_detect").is_boolean()) throw std::runtime_error("Black bar detection must be a boolean");
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
}
