#pragma once
// PowerToys source mapping and native API distinction: AWAKE.md.
#include <windows.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <filesystem>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace office {
class OfficeAwake final {
    using Json=nlohmann::json;
    QString path;
    Json document={{"mode","automatic"},{"keep_screen_on",false},{"until_ms",int64_t(0)},
        {"intervals",Json::array({{{"label","30 minutes"},{"seconds",1800}},{{"label","1 hour"},{"seconds",3600}},{{"label","2 hours"},{"seconds",7200}}})}};
    HANDLE request=INVALID_HANDLE_VALUE;
    bool enabled=true,eligible=false,allowed=false,displayOn=true,wantedSystem=false,wantedDisplay=false,display=false,system=false;
    std::string error,storageError;
    static bool validMode(const std::string& mode){return mode=="automatic"||mode=="off"||mode=="interval"||mode=="expiration"||mode=="indefinite";}
    static Json read(const QString& name){QFile f(name);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read awake settings");auto bytes=f.readAll();return Json::parse(bytes.constData(),bytes.constData()+bytes.size());}
    void save(const Json& value){
        if(value==document)return;
        QSaveFile file(path);auto bytes=QByteArray::fromStdString(value.dump(2));
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("Cannot save awake settings");document=value;storageError.clear();
    }
    void close() noexcept {
        if(request!=INVALID_HANDLE_VALUE){if(display)PowerClearRequest(request,PowerRequestDisplayRequired);if(system)PowerClearRequest(request,PowerRequestSystemRequired);CloseHandle(request);request=INVALID_HANDLE_VALUE;}
        display=system=false;
    }
    void apply(bool needSystem,bool needDisplay){
        if(needSystem==wantedSystem&&needDisplay==wantedDisplay)return;
        close();wantedSystem=needSystem;wantedDisplay=needDisplay;error.clear();if(!needSystem)return;
        wchar_t reason[]=L"Office Ambilight: automatic or manual keep awake";
        REASON_CONTEXT context{};context.Version=POWER_REQUEST_CONTEXT_VERSION;context.Flags=POWER_REQUEST_CONTEXT_SIMPLE_STRING;context.Reason.SimpleReasonString=reason;
        request=PowerCreateRequest(&context);
        if(request!=INVALID_HANDLE_VALUE){system=PowerSetRequest(request,PowerRequestSystemRequired)!=FALSE;if(system&&needDisplay)display=PowerSetRequest(request,PowerRequestDisplayRequired)!=FALSE;}
        if(!system||(needDisplay&&!display)){auto code=GetLastError();close();error="Cannot keep awake (Win32 "+std::to_string(code)+")";}
    }
public:
    OfficeAwake(const std::filesystem::path& root,bool active=true):path(QString::fromStdWString((root/"office-awake.json").wstring())),enabled(active){
        if(QFile::exists(path)){auto loaded=read(path);document.update(loaded);}
        else if(active){
            auto original=qEnvironmentVariable("LOCALAPPDATA")+"/Microsoft/PowerToys/Awake/settings.json";
            if(QFile::exists(original)){
                auto source=read(original).value("properties",Json::object());document["keep_screen_on"]=source.value("keepDisplayOn",false);
                auto custom=source.value("customTrayTimes",Json::object());auto intervals=Json::array();
                if(custom.is_object())for(auto& [label,seconds]:custom.items())if(seconds.is_number_integer()&&seconds>0&&seconds<=UINT32_MAX&&!label.empty()&&label.size()<=100)intervals.push_back({{"label",label},{"seconds",seconds}});
                if(!intervals.empty())document["intervals"]=intervals;document["imported_preferences"]=true;
            }
        }
        auto mode=document.at("mode").get<std::string>();if(!validMode(mode)||!document.at("keep_screen_on").is_boolean()||!document.at("until_ms").is_number_integer())throw std::runtime_error("Invalid awake settings");
        if(!document.at("intervals").is_array()||document.at("intervals").empty())throw std::runtime_error("Awake intervals are missing");
        for(auto& option:document.at("intervals"))if(!option.at("label").is_string()||option.at("label").get<std::string>().empty()||!option.at("seconds").is_number_integer()||option.at("seconds")<=0||option.at("seconds")>UINT32_MAX)throw std::runtime_error("Invalid awake interval");
    }
    OfficeAwake(const OfficeAwake&)=delete;OfficeAwake& operator=(const OfficeAwake&)=delete;
    ~OfficeAwake(){close();}
    void reset() noexcept {close();eligible=allowed=wantedSystem=wantedDisplay=false;error.clear();}
    void tick(){
        auto mode=document.at("mode").get<std::string>();
        if((mode=="interval"||mode=="expiration")&&document.at("until_ms").get<int64_t>()<=QDateTime::currentMSecsSinceEpoch()){
            auto next=document;next["mode"]="off";next["until_ms"]=0;
            // Release at the user's deadline even if settings storage fails.
            // The expired on-disk deadline cannot rearm the request on restart.
            try{save(next);}catch(const std::exception& e){document=next;storageError=e.what();}mode="off";
        }
        bool wantsDisplay=mode=="automatic"||document.at("keep_screen_on").get<bool>();
        bool needed=enabled&&allowed&&(!wantsDisplay||displayOn)&&(mode=="automatic"?eligible:mode!="off");
        apply(needed,needed&&wantsDisplay);
    }
    void setContext(bool appEligible,bool sessionAllowed,bool screenOn=true){eligible=appEligible;allowed=sessionAllowed;displayOn=screenOn;tick();}
    bool active() const {return system;}
    Json status() const {auto out=document;out.update({{"required",wantedSystem},{"active",active()},{"display_required",display},{"system_required",system},{"error",storageError.empty()?error:storageError}});return out;}
    Json command(const Json& request){
        auto action=request.at("action").get<std::string>();auto next=document;
        if(action=="mode"){
            auto mode=request.at("mode").get<std::string>();if(!validMode(mode))throw std::runtime_error("Unknown awake mode");next["mode"]=mode;next["until_ms"]=0;
            if(mode=="interval"){
                auto seconds=request.at("seconds");if(!seconds.is_number_integer()||seconds<=0||seconds>UINT32_MAX)throw std::runtime_error("Invalid awake interval");
                next["interval_seconds"]=seconds;next["until_ms"]=QDateTime::currentMSecsSinceEpoch()+seconds.get<int64_t>()*1000;
            }
            if(mode=="expiration"){
                auto until=request.at("until_ms");if(!until.is_number_integer()||until.get<int64_t>()<=QDateTime::currentMSecsSinceEpoch()||!QDateTime::fromMSecsSinceEpoch(until.get<int64_t>()).isValid())throw std::runtime_error("Choose a future awake expiration");next["until_ms"]=until;
            }
        }else if(action=="display")next["keep_screen_on"]=request.at("enabled").get<bool>();
        else throw std::runtime_error("Unknown awake action");
        save(next);tick();return status();
    }
};
}
