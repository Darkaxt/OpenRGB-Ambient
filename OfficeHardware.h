#pragma once
#include "OfficeLayout.h"
#include <OpenRGBPluginInterface.h>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <limits>

namespace office {
inline void validateHardwareConfiguration(const Json& schema,const Json& value) {
    if(schema.contains("enum") && std::find(schema.at("enum").begin(),schema.at("enum").end(),value)==schema.at("enum").end())throw std::runtime_error("Value is not a driver-supported choice");
    auto type=schema.value("type",std::string());
    if(type=="object") {
        if(!value.is_object())throw std::runtime_error("Driver configuration must be an object");
        if(schema.contains("required"))for(const auto& key:schema.at("required"))if(!value.contains(key.get<std::string>()))throw std::runtime_error("Required driver option is missing");
        auto properties=schema.value("properties",Json::object());
        for(const auto& [key,item]:value.items()) {if(properties.contains(key))validateHardwareConfiguration(properties.at(key),item);else throw std::runtime_error("Unknown driver option: "+key);}
    }else if(type=="array") {
        if(!value.is_array() || value.size()<schema.value("minItems",size_t(0)) || value.size()>schema.value("maxItems",size_t(65536)))throw std::runtime_error("Invalid driver array size");
        if(schema.contains("items"))for(const auto& item:value)validateHardwareConfiguration(schema.at("items"),item);
    }else if(type=="boolean") {if(!value.is_boolean())throw std::runtime_error("Driver option must be a boolean");}
    else if(type=="integer" || type=="number") {
        if(!value.is_number() || (type=="integer" && !value.is_number_integer()))throw std::runtime_error("Driver option must be numeric");
        double n=value;if(!std::isfinite(n) || n<schema.value("minimum",-std::numeric_limits<double>::max()) || n>schema.value("maximum",std::numeric_limits<double>::max()))throw std::runtime_error("Driver value outside supported range");
    }else if(type=="string") {
        if(!value.is_string())throw std::runtime_error("Driver option must be text");
        QString s=QString::fromStdString(value.get<std::string>());if(s.size()<schema.value("minLength",0) || s.size()>schema.value("maxLength",65536))throw std::runtime_error("Invalid driver text length");
        if(schema.contains("pattern") && !QRegularExpression(QString::fromStdString(schema.at("pattern"))).match(s).hasMatch())throw std::runtime_error("Driver text does not match its required pattern");
    }else if(!schema.contains("enum"))throw std::runtime_error("Unsupported driver schema; no configuration was changed");
    for(auto key:{"oneOf","anyOf","allOf","not","if","$ref","exclusiveMinimum","exclusiveMaximum","multipleOf","uniqueItems"})if(schema.contains(key))throw std::runtime_error("This driver schema requires its native editor; no configuration was changed");
}
// Called only behind the owner's controller-removal barrier. Persisted settings
// contain identities and values, never pointers or unstable discovery indices.
class HardwareState {
    QString path;
    Json saved=Json::object();
    std::set<std::string> applied,overridden,directReset;
    void persist(const Json& next) {
        QSaveFile file(path);auto bytes=next.dump(2);
        if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot save hardware selection");
        file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
        if(file.write(bytes.data(),bytes.size())!=qint64(bytes.size()) || !file.commit())throw std::runtime_error("Cannot commit hardware selection");
        saved=next;
    }
    static unsigned number(const Json& value,const char* name,unsigned low,unsigned high) {
        if(!value.is_number_integer() || value<low || value>high)throw std::runtime_error(std::string(name)+" is outside the driver's supported range");
        return value.get<unsigned>();
    }
public:
    explicit HardwareState(QString file):path(std::move(file)) {
        QFile input(path);if(input.exists()) {if(!input.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read hardware selections");auto bytes=input.readAll();saved=Json::parse(bytes.constData(),bytes.constData()+bytes.size());if(!saved.is_object())throw std::runtime_error("Invalid hardware selections");}
    }
    static std::string identity(RGBControllerInterface* c) {
        Json id={c->GetName(),c->GetSerial(),c->GetLocation(),c->GetLEDCount()};
        auto bytes=QByteArray::fromStdString(id.dump());return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
    }
    static Json mode(RGBControllerInterface* c,unsigned m,int z=-1) {
        bool zone=z>=0;auto flags=zone?c->GetZoneModeFlags(z,m):c->GetModeFlags(m);
        Json result={{"index",m},{"zone",z},{"name",zone?c->GetZoneModeName(z,m):c->GetModeName(m)},{"flags",flags},
            {"speed",zone?c->GetZoneModeSpeed(z,m):c->GetModeSpeed(m)},
            {"speed_min",zone?c->GetZoneModeSpeedMin(z,m):c->GetModeSpeedMin(m)},{"speed_max",zone?c->GetZoneModeSpeedMax(z,m):c->GetModeSpeedMax(m)},
            {"brightness",zone?c->GetZoneModeBrightness(z,m):c->GetModeBrightness(m)},
            {"brightness_min",zone?c->GetZoneModeBrightnessMin(z,m):c->GetModeBrightnessMin(m)},{"brightness_max",zone?c->GetZoneModeBrightnessMax(z,m):c->GetModeBrightnessMax(m)},
            {"direction",zone?c->GetZoneModeDirection(z,m):c->GetModeDirection(m)},
            {"color_mode",zone?c->GetZoneModeColorMode(z,m):c->GetModeColorMode(m)},
            {"colors_min",zone?c->GetZoneModeColorsMin(z,m):c->GetModeColorsMin(m)},{"colors_max",zone?c->GetZoneModeColorsMax(z,m):c->GetModeColorsMax(m)}};
        auto colors=Json::array();unsigned count=zone?c->GetZoneModeColorsCount(z,m):c->GetModeColorsCount(m);
        for(unsigned i=0;i<count;++i)colors.push_back(zone?c->GetZoneModeColor(z,m,i):c->GetModeColor(m,i));
        result["colors"]=colors;return result;
    }
    Json describe(const std::string& key,RGBControllerInterface* c,bool mapped) const {
        Json modes=Json::array(),zones=Json::array(),leds=Json::array();
        for(unsigned i=0;i<c->GetModeCount();++i)modes.push_back(mode(c,i));
        for(unsigned z=0;z<c->GetZoneCount();++z) {
            auto zm=Json::array();if(c->SupportsPerZoneModes())for(unsigned m=0;m<c->GetZoneModeCount(z);++m)zm.push_back(mode(c,m,z));
            auto segments=Json::array();for(unsigned s=0;s<c->GetZoneSegmentCount(z);++s)segments.push_back({{"name",c->GetZoneSegmentName(z,s)},{"first",c->GetZoneSegmentStartIndex(z,s)},{"count",c->GetZoneSegmentLEDsCount(z,s)}});
            zones.push_back({{"index",z},{"name",c->GetZoneDisplayName(z)},{"first",c->GetZoneStartIndex(z)},{"count",c->GetZoneLEDsCount(z)},
                {"min",c->GetZoneLEDsMin(z)},{"max",c->GetZoneLEDsMax(z)},{"flags",c->GetZoneFlags(z)},{"type",c->GetZoneType(z)},
                {"matrix_width",c->GetZoneMatrixMapWidth(z)},{"matrix_height",c->GetZoneMatrixMapHeight(z)},
                {"segments",segments},{"modes",zm},{"active_mode",c->GetZoneActiveMode(z)},
                {"schema",c->GetDeviceSpecificZoneConfigurationSchema(z)},{"configuration",c->GetDeviceSpecificZoneConfiguration(z)}});
        }
        for(unsigned i=0;i<c->GetLEDCount();++i)leds.push_back({{"name",c->GetLEDDisplayName(i)},{"color",c->GetColor(i)}});
        return {{"id",key},{"identity",identity(c)},{"name",c->GetDisplayName()},{"native_name",c->GetName()},{"vendor",c->GetVendor()},
            {"description",c->GetDescription()},{"version",c->GetVersion()},{"serial",c->GetSerial()},{"location",c->GetLocation()},
            {"mapped",mapped},{"leds",leds},{"zones",zones},{"modes",modes},{"active_mode",c->GetActiveMode()},
            {"owner",hardware(key)?"hardware":mapped?"ambience":"unmanaged"},{"selection",saved.value(key,Json::object())},
            {"schema",c->GetDeviceSpecificConfigurationSchema()},{"configuration",c->GetDeviceSpecificConfiguration()}};
    }
    bool hardware(const std::string& key) const {return saved.contains(key) && saved.at(key).value("owner",std::string())=="hardware";}
    bool any() const {return !saved.empty();}
    bool consumeReset(const std::string& key) {return directReset.erase(key)!=0;}
    void reconnected(const std::string& key) {applied.erase(key);overridden.erase(key);}
    void ambience(const std::string& key,RGBControllerInterface* c) {
        auto next=saved;next.erase(key);persist(next);applied.erase(key);overridden.erase(key);c->SetCustomMode();
    }
    void checkIdentity(RGBControllerInterface* c,const std::string& expected) const {if(identity(c)!=expected)throw std::runtime_error("Device identity or geometry changed; refresh hardware details");}
    // Validate the complete selection before making any hardware call.
    static Json validateMode(RGBControllerInterface* c,const Json& requested) {
        int z=requested.value("zone",-1);if(z<-1 || (z>=0 && (!c->SupportsPerZoneModes() || unsigned(z)>=c->GetZoneCount())))throw std::runtime_error("Unsupported hardware zone mode");
        unsigned count=z<0?c->GetModeCount():c->GetZoneModeCount(z);
        auto m=number(requested.at("index"),"Mode",0,count?count-1:0);if(!count)throw std::runtime_error("No native hardware modes");
        Json result=mode(c,m,z);if(requested.at("name")!=result.at("name"))throw std::runtime_error("Native mode changed; refresh hardware details");
        unsigned flags=result["flags"];
        if(z>=0 && (flags&MODE_FLAG_REQUIRES_ENTIRE_DEVICE))throw std::runtime_error("This mode requires the entire device");
        if(flags&MODE_FLAG_HAS_SPEED)result["speed"]=number(requested.at("speed"),"Speed",result["speed_min"],result["speed_max"]);
        if(flags&MODE_FLAG_HAS_BRIGHTNESS)result["brightness"]=number(requested.at("brightness"),"Brightness",result["brightness_min"],result["brightness_max"]);
        if(flags&(MODE_FLAG_HAS_DIRECTION_LR|MODE_FLAG_HAS_DIRECTION_UD|MODE_FLAG_HAS_DIRECTION_HV|MODE_FLAG_HAS_DIRECTION_DIAG)) {
            unsigned direction=number(requested.at("direction"),"Direction",0,9);
            unsigned required=direction<2?MODE_FLAG_HAS_DIRECTION_LR:direction<4?MODE_FLAG_HAS_DIRECTION_UD:direction<6?MODE_FLAG_HAS_DIRECTION_HV:MODE_FLAG_HAS_DIRECTION_DIAG;
            if(!(flags&required))throw std::runtime_error("Unsupported mode direction");result["direction"]=direction;
        }
        if(flags&(MODE_FLAG_HAS_PER_LED_COLOR|MODE_FLAG_HAS_MODE_SPECIFIC_COLOR|MODE_FLAG_HAS_RANDOM_COLOR)) {
            unsigned cm=number(requested.at("color_mode"),"Colour source",0,3);
            if(cm==MODE_COLORS_NONE || (cm==MODE_COLORS_PER_LED && !(flags&MODE_FLAG_HAS_PER_LED_COLOR)) || (cm==MODE_COLORS_MODE_SPECIFIC && !(flags&MODE_FLAG_HAS_MODE_SPECIFIC_COLOR)) || (cm==MODE_COLORS_RANDOM && !(flags&MODE_FLAG_HAS_RANDOM_COLOR)))throw std::runtime_error("Unsupported colour source");
            result["color_mode"]=cm;
            if(cm==MODE_COLORS_MODE_SPECIFIC) {
                const auto& colors=requested.at("colors");if(!colors.is_array() || colors.size()<result["colors_min"].get<unsigned>() || colors.size()>result["colors_max"].get<unsigned>())throw std::runtime_error("Unsupported colour count");
                for(const auto& color:colors)number(color,"RGB colour",0,0xffffff);result["colors"]=colors;
            }
            if(cm==MODE_COLORS_PER_LED && requested.contains("led_colors")) {
                auto colors=requested.at("led_colors");unsigned n=z<0?c->GetLEDCount():c->GetZoneLEDsCount(z);
                if(!colors.is_array() || colors.size()!=n)throw std::runtime_error("LED colours must match hardware geometry");for(const auto& color:colors)number(color,"RGB colour",0,0xffffff);result["led_colors"]=colors;
            }
        }
        return result;
    }
    static void applyMode(RGBControllerInterface* c,const Json& value) {
        unsigned m=value.at("index"),flags=value.at("flags");int z=value.at("zone");
        if(z<0)c->SetActiveMode(m);else c->SetZoneActiveMode(z,m);
        if(flags&MODE_FLAG_HAS_SPEED){if(z<0)c->SetModeSpeed(m,value.at("speed"));else c->SetZoneModeSpeed(z,m,value.at("speed"));}
        if(flags&MODE_FLAG_HAS_BRIGHTNESS){if(z<0)c->SetModeBrightness(m,value.at("brightness"));else c->SetZoneModeBrightness(z,m,value.at("brightness"));}
        if(flags&(MODE_FLAG_HAS_DIRECTION_LR|MODE_FLAG_HAS_DIRECTION_UD|MODE_FLAG_HAS_DIRECTION_HV|MODE_FLAG_HAS_DIRECTION_DIAG)){if(z<0)c->SetModeDirection(m,value.at("direction"));else c->SetZoneModeDirection(z,m,value.at("direction"));}
        auto cm=value.at("color_mode").get<unsigned>();
        if(cm) {
            if(z<0)c->SetModeColorMode(m,cm);else c->SetZoneModeColorMode(z,m,cm);
            if(cm==MODE_COLORS_MODE_SPECIFIC){unsigned n=unsigned(value.at("colors").size());if(z<0)c->SetModeColorsCount(m,n);else c->SetZoneModeColorsCount(z,m,n);
                for(unsigned i=0;i<n;++i){if(z<0)c->SetModeColor(m,i,value["colors"][i]);else c->SetZoneModeColor(z,m,i,value["colors"][i]);}}
        }
        if(z<0)c->UpdateMode();else c->UpdateZoneMode(z);
        if(value.contains("led_colors")) {unsigned start=z<0?0:c->GetZoneStartIndex(z);for(unsigned i=0;i<value["led_colors"].size();++i)c->SetColor(start+i,value["led_colors"][i]);if(z<0)c->UpdateLEDs();else c->UpdateZoneLEDs(z);}
    }
    void setMode(const std::string& key,RGBControllerInterface* c,const std::string& expected,const Json& requested) {
        checkIdentity(c,expected);auto valid=validateMode(c,requested);auto next=saved;
        auto selection=next.value(key,Json{{"owner","hardware"},{"identity",expected},{"modes",Json::array()}});
        if(selection.at("modes").empty() && c->SupportsPerZoneModes())for(unsigned z=0;z<c->GetZoneCount();++z) {
            int active=c->GetZoneActiveMode(z);if(active<0)continue;auto value=mode(c,active,z);
            if(value.at("color_mode")==MODE_COLORS_PER_LED){value["led_colors"]=Json::array();for(unsigned i=0;i<c->GetZoneLEDsCount(z);++i)value["led_colors"].push_back(c->GetColor(c->GetZoneStartIndex(z)+i));}
            selection["modes"].push_back(value);
        }
        selection["owner"]="hardware";selection["identity"]=expected;
        if(valid.at("zone")==-1)selection["modes"]=Json::array({valid});
        else {bool found=false;for(auto& m:selection["modes"])if(m.at("zone")==valid.at("zone")){m=valid;found=true;}if(!found)selection["modes"].push_back(valid);}
        next[key]=selection;persist(next);applied.erase(key);applyMode(c,valid);applied.insert(key);overridden.erase(key);
    }
    // Return true when the frame sender must leave this device alone.
    bool output(const std::string& key,RGBControllerInterface* c,bool overrideOutput,bool mapped) {
        if(!c || !hardware(key))return false;
        if(overrideOutput) {if(overridden.insert(key).second){c->SetCustomMode();directReset.insert(key);}applied.erase(key);return false;}
        if(!applied.contains(key)) {
            const auto& selection=saved.at(key);checkIdentity(c,selection.at("identity"));
            std::vector<Json> valid;for(const auto& m:selection.at("modes"))valid.push_back(validateMode(c,m));
            for(const auto& m:valid)applyMode(c,m);applied.insert(key);overridden.erase(key);
        }
        return true;
    }
};
}
