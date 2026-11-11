#pragma once
// Native adaptation of LightBulb's screen behavior; source map: LIGHTBULB.md.
#include "OfficeScreenPolicy.h"
#include <windows.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QTimeZone>
#include <QTimer>
#include <QKeySequence>
#include <QStringList>
#include <map>
#include <optional>
#include <filesystem>
#include <functional>
#include <vector>

namespace office {
class OfficeScreen final {
    using Json=screen::Json;
    struct Display {
        std::wstring name;HDC dc{};std::array<std::array<WORD,256>,3> baseline{},applied{};
        bool baselineValid=false,appliedValid=false,owned=false;
        std::array<std::array<WORD,256>,3> observed{};
    };
    QString path;Json document,disk;
    std::vector<Display> displays;
    QTimer timer;QElapsedTimer elapsed,previewClock,monotonic;
    uint64_t revision=1;
    HWND window{};
    bool active=false,invalid=true,foregroundInvalid=true,refreshGamma=true,preview=false,appliedValid=false;
    bool closed=false;
    int64_t previewStart=0,lastPoll=0,lastObservation=0;
    unsigned gammaRevision=0;
    screen::Color current{},target{};
    std::optional<screen::Color> lastApplied;
    std::optional<screen::Color> smoothingSource,smoothingTarget;
    std::string pauseReason,error,hotkeyError;
    std::map<int,std::string> hotkeyActions;
    static constexpr const char* hotkeys[]={"ToggleHotKey","FocusWindowHotKey","IncreaseTemperatureOffsetHotKey","DecreaseTemperatureOffsetHotKey","IncreaseBrightnessOffsetHotKey","DecreaseBrightnessOffsetHotKey","ResetConfigurationOffsetHotKey"};
    std::function<void()> reveal;
    bool standalone=false;int64_t standaloneChecked=-1;
    // Original ramps survive a crash here; normal restoration removes it.
    QString baselinePath;std::map<std::wstring,std::array<std::array<WORD,256>,3>> recovered;

    static Json readFile(const QString& name) {
        QFile file(name);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read screen settings");
        auto bytes=file.readAll();return Json::parse(bytes.constData(),bytes.constData()+bytes.size());
    }
    void persist(Json next) {
        if(QFile::exists(path) && readFile(path)!=disk)throw std::runtime_error("Screen settings changed externally; restart before saving");
        QSaveFile file(path);auto bytes=next.dump(2);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),bytes.size())!=static_cast<qint64>(bytes.size())||!file.commit())throw std::runtime_error("Cannot save screen settings");
        disk=document=std::move(next);++revision;
    }
    static double duration(const Json& value) {
        auto s=QString::fromStdString(value.get<std::string>());auto split=s.split(':');
        if(split.size()!=3)throw std::runtime_error("Transition duration must be HH:mm:ss");
        bool a=false,b=false,c=false;double h=split[0].toDouble(&a),m=split[1].toDouble(&b),sec=split[2].toDouble(&c);
        if(!a||!b||!c||h<0||m<0||m>=60||sec<0||sec>=60)throw std::runtime_error("Invalid transition duration");
        return h*3600+m*60+sec;
    }
    static double timeSeconds(const Json& value) {
        auto t=QTime::fromString(QString::fromStdString(value.get<std::string>()),"HH:mm:ss");
        if(!t.isValid())throw std::runtime_error("Sunrise/sunset must be HH:mm:ss");
        return QTime(0,0).secsTo(t);
    }
    static screen::Color color(const Json& value){return {value.at("Temperature").get<double>(),value.at("Brightness").get<double>()};}
    static void validate(const Json& s) {
        for(auto key:{"DayConfiguration","NightConfiguration"}){auto c=color(s.at(key));if(!std::isfinite(c.temperature)||!std::isfinite(c.brightness)||c.temperature<500||c.temperature>20000||c.brightness<.1||c.brightness>1)throw std::runtime_error("Screen temperature must be 500–20000K and brightness 10–100%");}
        auto t=duration(s.at("ConfigurationTransitionDuration")),smooth=duration(s.at("ConfigurationSmoothingMaxDuration"));double shift=s.at("ConfigurationTransitionOffset");
        if(t>10800||smooth>60||!std::isfinite(shift)||shift<0||shift>1)throw std::runtime_error("Invalid screen transition settings");
        for(auto key:{"IsManualSunriseSunsetEnabled","IsDefaultToDayConfigurationEnabled","IsConfigurationSmoothingEnabled","IsPauseWhenFullScreenEnabled","IsGammaPollingEnabled","IsApplicationWhitelistEnabled"})if(!s.at(key).is_boolean())throw std::runtime_error("Invalid screen option");
        timeSeconds(s.at("ManualSunriseTime"));timeSeconds(s.at("ManualSunsetTime"));
        if(!s.at("IsManualSunriseSunsetEnabled").get<bool>()) {
            auto l=s.at("Location");double lat=l.at("Latitude"),lon=l.at("Longitude");
            if(!std::isfinite(lat)||!std::isfinite(lon)||lat<-90||lat>90||lon<-180||lon>180)throw std::runtime_error("Enter valid latitude/longitude or use manual times");
        }
        const auto& apps=s.at("WhitelistedApplications");if(!apps.is_null()){if(!apps.is_array()||apps.size()>200)throw std::runtime_error("Invalid application exclusions");for(auto& app:apps)if(!app.at("ExecutableFilePath").is_string())throw std::runtime_error("Invalid excluded application");}
        for(auto key:hotkeys)if(s.contains(key)&&!s.at(key).is_object()&&!s.at(key).is_string())throw std::runtime_error("Invalid screen shortcut");
    }
    static Json defaults(){return {{"DayConfiguration",{{"Temperature",6600},{"Brightness",1.}}},{"NightConfiguration",{{"Temperature",3900},{"Brightness",.85}}},{"ConfigurationTransitionDuration","00:40:00"},{"ConfigurationTransitionOffset",0.},{"ConfigurationSmoothingMaxDuration","00:00:05"},{"IsManualSunriseSunsetEnabled",true},{"ManualSunriseTime","07:20:00"},{"ManualSunsetTime","16:30:00"},{"Location",nullptr},{"IsDefaultToDayConfigurationEnabled",false},{"IsConfigurationSmoothingEnabled",true},{"IsPauseWhenFullScreenEnabled",false},{"IsGammaPollingEnabled",false},{"IsApplicationWhitelistEnabled",false},{"WhitelistedApplications",Json::array()}};}
    std::pair<double,double> solarAt(const QDateTime& instant) const {
        const auto& s=document.at("settings");if(s.at("IsManualSunriseSunsetEnabled").get<bool>())return {timeSeconds(s.at("ManualSunriseTime")),timeSeconds(s.at("ManualSunsetTime"))};
        // Use the day's noon offset so a DST change at night does not shift the whole day.
        const auto noon=QDateTime(instant.date(),QTime(12,0),QTimeZone::systemTimeZone());
        return screen::solar(s.at("Location").at("Latitude"),s.at("Location").at("Longitude"),instant.date().dayOfYear(),noon.offsetFromUtc()/3600.);
    }
    int64_t nextSunrise(int64_t after) const {
        const auto start=QDateTime::fromMSecsSinceEpoch(after).toLocalTime();
        for(int i=0;i<3;++i){auto date=start.date().addDays(i);QDateTime reference(date,QTime(12,0),QTimeZone::systemTimeZone());auto times=solarAt(reference);
            auto result=QDateTime(date,QTime(0,0).addSecs(qRound(times.first)),QTimeZone::systemTimeZone()).toMSecsSinceEpoch();if(result>after)return result;}
        throw std::runtime_error("Cannot resolve next sunrise");
    }
    screen::Pause pause(const char* name) const{return screen::Pause::from(document.value(name,Json::object()));}
    // Re-resolve "until sunrise" pauses after schedule, location or clock changes.
    bool resolveSunrise(Json& next) const {
        bool changed=false;for(auto key:{"lighting_pause","screen_pause"}){auto p=screen::Pause::from(next.value(key,Json::object()));if(p.kind=="sunrise"){p.until=nextSunrise(p.started);next[key]=p.json();changed=true;}}return changed;
    }
    // A process snapshot costs milliseconds of kernel time. The 50 ms gamma
    // tick reuses a recent answer; explicit enabling always checks afresh.
    bool standaloneRunning(bool fresh=false){
        const auto now=monotonic.elapsed();if(!fresh&&standaloneChecked>=0&&now-standaloneChecked<3000)return standalone;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE)return standaloneChecked<0||standalone;
        PROCESSENTRY32W p{};p.dwSize=sizeof(p);bool found=false;
        if(Process32FirstW(snapshot,&p))do{if(_wcsicmp(p.szExeFile,L"LightBulb.exe")==0){found=true;break;}}while(Process32NextW(snapshot,&p));
        CloseHandle(snapshot);standalone=found;standaloneChecked=now;return found;
    }
    void loadBaselines(){
        QFile file(baselinePath);if(!file.open(QIODevice::ReadOnly))return;auto bytes=file.readAll();
        auto saved=Json::parse(bytes.constData(),bytes.constData()+bytes.size(),nullptr,false);if(!saved.is_object())return;
        for(auto& [name,ramp]:saved.items()){if(!ramp.is_array()||ramp.size()!=768)continue;std::array<std::array<WORD,256>,3> value{};bool valid=true;
            for(size_t i=0;i<768&&valid;++i){valid=ramp[i].is_number_unsigned()&&ramp[i].get<unsigned>()<=65535;if(valid)value[i/256][i%256]=static_cast<WORD>(ramp[i].get<unsigned>());}
            if(valid)recovered[QString::fromStdString(name).toStdWString()]=value;}
    }
    void saveBaselines(){
        Json saved=Json::object();for(auto& d:displays){auto ramp=Json::array();for(auto& channel:d.baseline)for(auto v:channel)ramp.push_back(v);saved[QString::fromStdWString(d.name).toStdString()]=ramp;}
        QSaveFile file(baselinePath);auto bytes=saved.dump();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),bytes.size())!=static_cast<qint64>(bytes.size())||!file.commit())error="Cannot record the original gamma ramps for crash recovery";
    }
    void restoreDisplays() noexcept {
        bool restored=true;
        for(auto& d:displays){if(d.baselineValid&&d.owned){if(!SetDeviceGammaRamp(d.dc,d.baseline.data())){error="Could not restore a display gamma baseline";restored=false;}}if(d.dc)DeleteDC(d.dc);}
        if(restored&&!displays.empty())QFile::remove(baselinePath);
        displays.clear();appliedValid=false;
    }
    void ensureDisplays(){
        if(!invalid)return;
        // Restore each surviving display before releasing its original DC. A new
        // context must never capture our already dimmed ramp as its baseline.
        restoreDisplays();invalid=false;refreshGamma=true;
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{
            auto& self=*reinterpret_cast<OfficeScreen*>(data);MONITORINFOEXW info{};info.cbSize=sizeof(info);
            if(!GetMonitorInfoW(monitor,&info)){self.error="Cannot inspect connected display";return TRUE;}
            Display d;d.name=info.szDevice;d.dc=CreateDCW(L"DISPLAY",d.name.c_str(),nullptr,nullptr);
            d.baselineValid=d.dc&&GetDeviceGammaRamp(d.dc,d.baseline.data());
            if(!d.baselineValid){if(d.dc)DeleteDC(d.dc);self.error="Cannot read original gamma ramp";return TRUE;}
            // After a crash the current ramp is still our adjusted one; the
            // recorded original is the only correct calibration baseline.
            if(auto found=self.recovered.find(d.name);found!=self.recovered.end()){d.baseline=found->second;self.recovered.erase(found);}
            self.displays.push_back(d);return TRUE;
        },reinterpret_cast<LPARAM>(this));
        if(!displays.empty())saveBaselines();
    }
    void updateForeground(){
        if(!foregroundInvalid)return;foregroundInvalid=false;pauseReason.clear();
        HWND foreground=GetForegroundWindow();if(!foreground||!IsWindowVisible(foreground))return;
        DWORD pid{};GetWindowThreadProcessId(foreground,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        wchar_t pathBuffer[32768]{};DWORD length=32768;QString executable;
        if(process){if(QueryFullProcessImageNameW(process,0,pathBuffer,&length))executable=QString::fromWCharArray(pathBuffer,length);CloseHandle(process);}
        auto basename=executable.section('\\',-1).toLower();
        if(basename=="explorer.exe"||basename=="shellexperiencehost.exe"||basename=="startmenuexperiencehost.exe")return;
        const auto& s=document.at("settings");
        if(s.at("IsApplicationWhitelistEnabled").get<bool>()&&!s.at("WhitelistedApplications").is_null())for(auto& item:s.at("WhitelistedApplications"))
            if(QString::compare(executable,QString::fromStdString(item.at("ExecutableFilePath")),Qt::CaseInsensitive)==0&&!executable.isEmpty())pauseReason="excluded-application";
        // A captioned maximized window covers the monitor when the taskbar
        // auto-hides; only borderless fullscreen applications pause.
        const bool captioned=IsZoomed(foreground)&&(GetWindowLongPtrW(foreground,GWL_STYLE)&WS_CAPTION)==WS_CAPTION;
        if(s.at("IsPauseWhenFullScreenEnabled").get<bool>()&&!captioned){
            RECT bounds{};MONITORINFO info{};info.cbSize=sizeof(info);auto monitor=MonitorFromWindow(foreground,MONITOR_DEFAULTTONULL);
            if(monitor&&GetWindowRect(foreground,&bounds)&&GetMonitorInfoW(monitor,&info)&&bounds.left<=info.rcMonitor.left&&bounds.top<=info.rcMonitor.top&&bounds.right>=info.rcMonitor.right&&bounds.bottom>=info.rcMonitor.bottom)pauseReason="fullscreen-application";
        }
    }
    static std::pair<UINT,UINT> shortcut(const Json& value){
        if(value.is_object()){
            // Avalonia PhysicalKey enum values (not Windows VK or USB HID).
            // See Avalonia.Base/Input/PhysicalKey.cs and LightBulb HotKeyService.
            auto key=value.value("Key",Json(0));UINT vk=0;
            if(key.is_number_integer()){int k=key.get<int>();if(k==0)return {};if(k>=20&&k<=45)vk='A'+k-20;else if(k>=6&&k<=15)vk='0'+k-6;else if(k>=107&&k<=130)vk=VK_F1+k-107;else if(k==63)vk=VK_SPACE;else if(k==80)vk=VK_DOWN;else if(k==81)vk=VK_LEFT;else if(k==82)vk=VK_RIGHT;else if(k==83)vk=VK_UP;else throw std::runtime_error("Imported shortcut key requires reassignment");}
            else {auto s=QString::fromStdString(key.get<std::string>());if(s.startsWith("Key"))s=s.mid(3);if(s.startsWith("Digit"))s=s.mid(5);auto parsed=shortcut(s.toStdString());vk=parsed.second;}
            auto mods=value.value("Modifiers",Json(0));UINT m=0;
            if(mods.is_number_integer())m=mods.get<UINT>();else{auto s=mods.get<std::string>();if(s.find("Alt")!=s.npos)m|=MOD_ALT;if(s.find("Control")!=s.npos)m|=MOD_CONTROL;if(s.find("Shift")!=s.npos)m|=MOD_SHIFT;if(s.find("Meta")!=s.npos)m|=MOD_WIN;}
            return {m,vk};
        }
        QKeySequence seq(QString::fromStdString(value.get<std::string>()),QKeySequence::PortableText);if(seq.isEmpty())return {};
        auto combination=seq[0];auto modifiers=combination.keyboardModifiers();auto key=combination.key();UINT m=0,vk=0;
        if(modifiers&Qt::ControlModifier)m|=MOD_CONTROL;if(modifiers&Qt::ShiftModifier)m|=MOD_SHIFT;if(modifiers&Qt::AltModifier)m|=MOD_ALT;if(modifiers&Qt::MetaModifier)m|=MOD_WIN;
        if(key>=Qt::Key_A&&key<=Qt::Key_Z)vk='A'+key-Qt::Key_A;else if(key>=Qt::Key_0&&key<=Qt::Key_9)vk='0'+key-Qt::Key_0;else if(key>=Qt::Key_F1&&key<=Qt::Key_F24)vk=VK_F1+key-Qt::Key_F1;else if(key==Qt::Key_Space)vk=VK_SPACE;else if(key==Qt::Key_Up)vk=VK_UP;else if(key==Qt::Key_Down)vk=VK_DOWN;else if(key==Qt::Key_Left)vk=VK_LEFT;else if(key==Qt::Key_Right)vk=VK_RIGHT;else throw std::runtime_error("Choose a letter, digit, arrow, Space or function key shortcut");
        return {m,vk};
    }
    void registerHotkeys(){
        if(!window)return;for(auto [id,name]:hotkeyActions)UnregisterHotKey(window,id);hotkeyActions.clear();hotkeyError.clear();
        if(!document.value("managed",false))return;
        for(int i=0;i<7;++i)try{auto key=shortcut(document.at("settings").value(hotkeys[i],Json("")));if(!key.second)continue;
            if(!RegisterHotKey(window,0x4c00+i,key.first|MOD_NOREPEAT,key.second))throw std::runtime_error("Shortcut is in use: "+std::string(hotkeys[i]));hotkeyActions[0x4c00+i]=hotkeys[i];
        }catch(const std::exception& e){if(!hotkeyError.empty())hotkeyError+="; ";hotkeyError+=e.what();}
    }
    void offset(const std::string& name,double delta){auto next=document;next[name]=document.value(name,0.)+delta;persist(next);refreshGamma=true;}
    void applyGamma(screen::Color next){
        if(!invalid&&!refreshGamma&&lastApplied&&std::abs(next.temperature-lastApplied->temperature)<=15&&std::abs(next.brightness-lastApplied->brightness)<=.01)return;
        ensureDisplays();bool all=!displays.empty();const auto gain=screen::multipliers(next);
        if(refreshGamma)++gammaRevision;
        for(auto& display:displays){
            std::array<std::array<WORD,256>,3> desired{};
            // Multiply the original ramp, preserving ICC calibration and its shape.
            for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i)desired[c][i]=static_cast<WORD>(std::clamp(std::lround(display.baseline[c][i]*gain[c]),0l,65535l));
            // LightBulb's documented driver workaround: a tiny endpoint change
            // forces a native invalidation to reapply an otherwise equal ramp.
            for(size_t c=0;c<3;++c)desired[c][255]=static_cast<WORD>(std::max(0,int(desired[c][255])-int(gammaRevision%5)));
            if(!refreshGamma&&desired==display.applied){all=all&&display.appliedValid;continue;}
            display.applied=desired;display.appliedValid=false;
            if(!SetDeviceGammaRamp(display.dc,desired.data())){error="Windows rejected display gamma (error "+std::to_string(GetLastError())+")";all=false;continue;}
            display.owned=true;
            std::array<std::array<WORD,256>,3> observed{};
            if(!GetDeviceGammaRamp(display.dc,observed.data())){error="Cannot verify applied display gamma";all=false;}
            else {display.observed=observed;int difference=0;for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i)difference=std::max(difference,std::abs(int(observed[c][i])-int(desired[c][i])));if(difference>8){error="Display gamma readback differs from requested adjustment";all=false;}else display.appliedValid=true;}
        }
        appliedValid=all;if(all){error.clear();lastApplied=next;}refreshGamma=false;
    }
public:
    OfficeScreen(const std::filesystem::path& root,bool activate):path(QString::fromStdWString((root/"office-screen.json").wstring())),active(activate){
        baselinePath=QString::fromStdWString((root/"office-screen-baseline.json").wstring());if(activate)loadBaselines();
        auto settings=defaults();document={{"settings",settings},{"managed",false},{"lighting_pause",screen::Pause{}.json()},{"screen_pause",screen::Pause{}.json()},{"temperature_offset",0.},{"brightness_offset",0.}};
        if(QFile::exists(path)){document=readFile(path);disk=document;}
        else if(activate){auto source=qEnvironmentVariable("APPDATA")+"/LightBulb/Settings.json";if(QFile::exists(source)){auto imported=readFile(source);settings.update(imported);document["settings"]=settings;document["import_source"]=source.toStdString();document["imported"]=true;}}
        settings=defaults();settings.update(document.at("settings"));document["settings"]=settings;validate(settings);
        elapsed.start();monotonic.start();
        if(activate){QObject::connect(&timer,&QTimer::timeout,[this]{try{tick();}catch(const std::exception& e){error=e.what();}});timer.setTimerType(Qt::PreciseTimer);timer.start(50);}
    }
    ~OfficeScreen(){shutdown();}
    void shutdown() noexcept {if(closed)return;closed=true;timer.stop();for(auto [id,name]:hotkeyActions)if(window)UnregisterHotKey(window,id);hotkeyActions.clear();restoreDisplays();}
    void attach(HWND eventWindow,std::function<void()> show){window=eventWindow;reveal=std::move(show);registerHotkeys();}
    void foregroundChanged(bool gammaChanged=false){foregroundInvalid=true;if(gammaChanged)refreshGamma=true;}
    void nativeEvent(UINT message,WPARAM w,LPARAM){
        if(message==WM_ENDSESSION && w){shutdown();return;}
        if(message==WM_DISPLAYCHANGE)invalid=true;
        if(message==WM_SETTINGCHANGE||message==WM_TIMECHANGE){foregroundInvalid=true;refreshGamma=true;auto next=document;if(resolveSunrise(next))persist(next);}
        if(message==WM_POWERBROADCAST||message==WM_WTSSESSION_CHANGE)refreshGamma=true;
        if(message==WM_HOTKEY&&hotkeyActions.contains(static_cast<int>(w))){auto name=hotkeyActions.at(static_cast<int>(w));
            if(name=="FocusWindowHotKey"){if(reveal)reveal();}
            else if(name=="ToggleHotKey")command({{"action","pause"},{"target","screen"},{"mode",pause("screen_pause").active(QDateTime::currentMSecsSinceEpoch())?"enable":"forever"}});
            else if(name=="ResetConfigurationOffsetHotKey")command({{"action","offset_reset"}});
            else offset(name.find("Temperature")!=name.npos?"temperature_offset":"brightness_offset",name.find("Increase")!=name.npos?(name.find("Temperature")!=name.npos?100:.05):(name.find("Temperature")!=name.npos?-100:-.05));
        }
    }
    bool lightingPaused() const{return pause("lighting_pause").active(QDateTime::currentMSecsSinceEpoch());}
    std::optional<double> appliedBrightness(const std::string& name) const {
        if(!document.value("managed",false)||!appliedValid)return {};
        for(auto& display:displays)if(QString::fromStdWString(display.name).toStdString()==name&&display.appliedValid&&lastApplied)return lastApplied->brightness;
        return {};
    }
    // Cycle position used by lights with their own night level; empty when
    // integrated screen adjustment is off (lights then follow measured gamma).
    std::optional<double> nightWeight() const {
        if(!document.value("managed",false))return {};
        return std::clamp(current.night,0.,1.);
    }
    QDateTime cycleInstant(const QDateTime& real) const {
        return preview&&previewClock.elapsed()<14400?QDateTime::fromMSecsSinceEpoch(previewStart+previewClock.elapsed()*6000).toLocalTime():real;
    }
    void tick(){
        if(closed||!active)return;auto real=QDateTime::currentDateTime();auto now=real.toMSecsSinceEpoch();
        auto next=document;bool expired=false;for(auto key:{"lighting_pause","screen_pause"}){auto p=pause(key);if(p.kind!="none"&&p.kind!="forever"&&!p.active(now)){p.clear();next[key]=p.json();expired=true;}}
        // An expired pause ends even when it cannot be saved; otherwise every
        // tick would throw here and freeze screen adjustment.
        if(expired)try{persist(next);}catch(const std::exception& e){document=std::move(next);++revision;error=e.what();}
        if(!document.value("managed",false))return;
        // Process enumeration is an ownership check, never a retry/cancellation deadline.
        if(standaloneRunning()){error="Standalone LightBulb is running; close it before enabling integrated screen adjustments";appliedValid=false;for(auto& d:displays){d.owned=false;d.appliedValid=false;}refreshGamma=true;return;}
        // GDI exposes no gamma-change notification. Observe once per second and
        // react only to an actual changed ramp, rather than a timed retry window.
        const auto observedTime=monotonic.elapsed();
        if(observedTime-lastObservation>=1000){lastObservation=observedTime;for(auto& d:displays)if(d.owned){std::array<std::array<WORD,256>,3> observed{};if(GetDeviceGammaRamp(d.dc,observed.data())&&observed!=d.observed){d.observed=observed;int difference=0;for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i)difference=std::max(difference,std::abs(int(observed[c][i])-int(d.applied[c][i])));if(difference>8)refreshGamma=true;}}}
        updateForeground();if(preview&&previewClock.elapsed()>=14400)preview=false;auto instant=cycleInstant(real);
        auto times=solarAt(instant);const auto& s=document.at("settings");
        bool on=!pause("screen_pause").active(now)&&pauseReason.empty();auto previousTarget=target,previousCurrent=current;
        target=(on||preview)?screen::cycle(QTime(0,0).secsTo(instant.time()),times.first,times.second,duration(s.at("ConfigurationTransitionDuration")),s.at("ConfigurationTransitionOffset"),color(s.at("DayConfiguration")),color(s.at("NightConfiguration"))):s.at("IsDefaultToDayConfigurationEnabled").get<bool>()?color(s.at("DayConfiguration")):screen::Color{};
        if(on||preview){target.temperature=std::clamp(target.temperature+document.value("temperature_offset",0.),500.,20000.);target.brightness=std::clamp(target.brightness+document.value("brightness_offset",0.),.1,1.);}
        double step=std::max<int64_t>(0,elapsed.restart())/1000.;auto smooth=duration(s.at("ConfigurationSmoothingMaxDuration"));
        if(s.at("IsConfigurationSmoothingEnabled").get<bool>()&&smooth>=.1&&!preview){if(!smoothingSource||!smoothingTarget||*smoothingTarget!=target){smoothingSource=current;smoothingTarget=target;}auto approach=[](double a,double b,double d){return a+std::clamp(b-a,-d,d);};current.temperature=approach(current.temperature,target.temperature,std::max(std::abs(smoothingTarget->temperature-smoothingSource->temperature)/smooth,600.)*step);current.brightness=approach(current.brightness,target.brightness,std::max(std::abs(smoothingTarget->brightness-smoothingSource->brightness)/smooth,1.6)*step);current.night=approach(current.night,target.night,std::max(std::abs(smoothingTarget->night-smoothingSource->night)/smooth,1.6)*step);}
        else {current=target;smoothingSource.reset();smoothingTarget.reset();}
        if(current==target&&target==previousTarget&&current!=previousCurrent)refreshGamma=true;
        if(s.at("IsGammaPollingEnabled").get<bool>()&&observedTime-lastPoll>=1000){refreshGamma=true;lastPoll=observedTime;}
        applyGamma(current);
    }
    Json status() const {
        auto real=QDateTime::currentDateTime();auto instant=cycleInstant(real);auto times=solarAt(instant);auto p=pause("screen_pause"),rgb=pause("lighting_pause");
        return {{"managed",document.value("managed",false)},{"screen_disabled",p.active(real.toMSecsSinceEpoch())},{"lighting_disabled",rgb.active(real.toMSecsSinceEpoch())},{"screen_pause",p.json()},{"lighting_pause",rgb.json()},{"automatic_pause",pauseReason},{"temperature",appliedValid&&lastApplied?lastApplied->temperature:current.temperature},{"brightness",appliedValid&&lastApplied?lastApplied->brightness:current.brightness},{"target_temperature",target.temperature},{"target_brightness",target.brightness},{"gamma_valid",appliedValid},{"night_weight",std::clamp(current.night,0.,1.)},{"display_count",displays.size()},{"error",error},{"hotkey_error",hotkeyError},{"preview",preview},{"sunrise",QTime(0,0).addSecs(qRound(times.first)).toString("HH:mm").toStdString()},{"sunset",QTime(0,0).addSecs(qRound(times.second)).toString("HH:mm").toStdString()},{"revision",revision}};
    }
    Json snapshot() const {
        auto result=status();const auto& s=document.at("settings");result["settings"]=s;result["temperature_offset"]=document.value("temperature_offset",0.);result["brightness_offset"]=document.value("brightness_offset",0.);result["imported"]=document.value("imported",false);
        auto instant=cycleInstant(QDateTime::currentDateTime());auto solar=solarAt(instant);auto t=screen::cycleTimes(solar.first,solar.second,duration(s.at("ConfigurationTransitionDuration")),s.at("ConfigurationTransitionOffset"));
        double seconds=QTime(0,0).msecsTo(instant.time())/1000.;
        result["cycle_seconds"]=seconds;result["cycle_times"]={{"sunrise_start",t.riseStart},{"sunrise_end",t.riseEnd},{"sunset_start",t.setStart},{"sunset_end",t.setEnd}};
        result["cycle_state"]=!document.value("managed",false)?"disabled":!preview&&(result.at("screen_disabled").get<bool>()||!pauseReason.empty())?"paused":current!=target?"transition":
            screen::wrap(seconds-t.riseEnd,86400)<screen::wrap(t.setStart-t.riseEnd,86400)?"day":screen::wrap(seconds-t.setEnd,86400)<screen::wrap(t.riseStart-t.setEnd,86400)?"night":"transition";
        return result;
    }
    Json command(const Json& request){
        auto action=request.at("action").get<std::string>();
        if(action=="pause"){
            auto target=request.at("target").get<std::string>();if(target!="lighting"&&target!="screen")throw std::runtime_error("Unknown pause target");
            auto key=target+"_pause";auto p=pause(key.c_str());auto mode=request.at("mode").get<std::string>();auto now=QDateTime::currentMSecsSinceEpoch();
            if(mode=="enable")p.clear();else if(mode=="forever")p.disable(now,0);else if(mode=="sunrise"){p.disable(now,1);p.kind="sunrise";p.until=nextSunrise(now);}else if(mode=="duration"){int minutes=request.at("minutes");if(std::find(std::begin(pauseMinutes),std::end(pauseMinutes),minutes)==std::end(pauseMinutes))throw std::runtime_error("Unsupported pause duration");p.disable(now,minutes*60000LL);}else throw std::runtime_error("Unknown pause mode");
            auto next=document;next[key]=p.json();persist(next);preview=false;refreshGamma=true;
        }else if(action=="save"){
            if(request.at("revision").get<uint64_t>()!=revision)throw std::runtime_error("Screen settings changed; reload before saving");auto settings=request.at("settings");validate(settings);
            for(auto key:hotkeys)if(settings.contains(key))shortcut(settings.at(key));auto next=document;next["settings"]=settings;persist(next);foregroundInvalid=refreshGamma=true;registerHotkeys();
            if(auto resolved=document;resolveSunrise(resolved))persist(resolved);
        }else if(action=="managed"){
            bool enable=request.at("enabled");if(enable&&standaloneRunning(true))throw std::runtime_error("Close standalone LightBulb before enabling integrated screen adjustments");
            auto next=document;next["managed"]=enable;persist(next);if(!enable)restoreDisplays();else invalid=true;registerHotkeys();refreshGamma=true;elapsed.restart();
        }else if(action=="preview"){if(!document.value("managed",false))throw std::runtime_error("Enable integrated screen adjustment before previewing");preview=!preview;if(preview){previewStart=QDateTime::currentMSecsSinceEpoch();previewClock.start();}refreshGamma=true;
        }else if(action=="offset_reset"){auto next=document;next["temperature_offset"]=0.;next["brightness_offset"]=0.;persist(next);refreshGamma=true;
        }else if(action=="offset"){auto name=request.at("name").get<std::string>();if(name!="temperature_offset"&&name!="brightness_offset")throw std::runtime_error("Unknown screen offset");auto delta=request.at("delta").get<double>();if(!std::isfinite(delta)||std::abs(delta)>(name=="temperature_offset"?5000:1))throw std::runtime_error("Invalid screen offset");offset(name,delta);
        }else throw std::runtime_error("Unknown screen action");
        tick();return snapshot();
    }
    static constexpr int pauseMinutes[]={1,5,15,30,60,180,360,720,1440};
};
}
