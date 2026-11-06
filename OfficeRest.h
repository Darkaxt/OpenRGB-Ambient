#pragma once
#include <QFile>
#include <QSaveFile>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace office {
// A screensaver's disappearance is not evidence that the user has returned.
// Persist only this resume gate and the session input-event token, never input
// contents. Timestamp equality deliberately handles non-incremental Win32 ticks.
class OfficeRest final {
    using Json=nlohmann::json;
    QString path;
    bool persistent=true,held=false,saver=false;
    std::optional<uint32_t> entryInput,lastInput;
    std::string error;
    static Json token(std::optional<uint32_t> value){return value?Json(*value):Json(nullptr);}
    void save(bool next,std::optional<uint32_t> input){
        if(!persistent)return;
        QSaveFile file(path);auto bytes=QByteArray::fromStdString(Json{{"held",next},{"entry_input_tick",token(input)}}.dump(2));
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("Cannot save screensaver resume state");
    }
public:
    OfficeRest(const std::filesystem::path& root,bool persist=true):path(QString::fromStdWString((root/"office-rest.json").wstring())),persistent(persist){
        if(!persistent||!QFile::exists(path))return;
        try{
            QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read screensaver resume state");auto bytes=file.readAll();auto value=Json::parse(bytes.constData(),bytes.constData()+bytes.size());
            held=value.at("held").get<bool>();auto input=value.at("entry_input_tick");
            if(!input.is_null()){if(!input.is_number_integer()||input<0||input>UINT32_MAX)throw std::runtime_error("Invalid screensaver input timestamp");entryInput=input.get<uint32_t>();}
        }catch(const std::exception& e){held=true;entryInput.reset();error=e.what();}
    }
    bool active() const{return held;}
    bool inputChanged(std::optional<uint32_t> input) const{return input!=lastInput;}
    void observe(bool automaticSaver,bool normalDesktop,std::optional<uint32_t> input){
        saver=automaticSaver;lastInput=input;
        auto nextHeld=held;auto nextInput=entryInput;
        if(automaticSaver&&!held){nextHeld=true;nextInput=input;}
        else if(held&&!entryInput&&input)nextInput=input; // First valid sample establishes a baseline.
        else if(held&&!automaticSaver&&normalDesktop&&entryInput&&input&&input!=entryInput){nextHeld=false;nextInput.reset();}
        if(nextHeld==held&&nextInput==entryInput)return;
        try{save(nextHeld,nextInput);held=nextHeld;entryInput=nextInput;error.clear();}
        catch(const std::exception& e){error=e.what();if(nextHeld){held=true;entryInput=nextInput;}} // A failed release stays black.
    }
    Json status() const{return {{"held",held},{"awaiting_input",held&&!saver},{"input_query_ok",lastInput.has_value()},
        {"entry_input_tick",token(entryInput)},{"last_input_tick",token(lastInput)},{"error",error}};}
};
}
