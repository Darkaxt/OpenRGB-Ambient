#pragma once
#include "OfficeLayout.h"
#include <QString>

namespace office {
// Windows executable basename, case folded, survives application version/path
// changes. A profile deliberately covers every instance of that executable.
inline std::string fullscreenExecutableKey(QString name){
    return name.replace('\\','/').section('/',-1).trimmed().toCaseFolded().toStdString();
}
inline bool validFullscreenExecutable(const std::string& key){
    auto text=QString::fromStdString(key);
    return !key.empty()&&text.size()<=255&&text.endsWith(".exe")&&key==fullscreenExecutableKey(text)&&
        std::none_of(text.begin(),text.end(),[](QChar c){return c.unicode()<32||QString("/\\:*?\"<>|").contains(c);});
}
inline void validateFullscreenProfiles(const Json& profiles){
    if(!profiles.is_object())throw std::runtime_error("Fullscreen app profiles must be an object");
    for(auto& [exe,levels]:profiles.items()){
        if(!validFullscreenExecutable(exe)||!levels.is_object())throw std::runtime_error("Fullscreen profile requires an executable name and device percentages");
        for(auto& [id,value]:levels.items())
            if(id.empty()||!value.is_number()||!std::isfinite(value.get<double>())||value<0||value>100)
                throw std::runtime_error("Fullscreen profile intensity must be 0-100 percent");
    }
}
}
