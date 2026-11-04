// Exercise the actual output owner against OpenRGB's synchronous removal contract.
#include "../OfficeAmbience.cpp"
#include "interface_stubs.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <condition_variable>
#include <iostream>
#include <thread>

struct Controller : StubRGBControllerInterface {
    std::string name, description;
    unsigned count, writes=0, modes=0;
    bool retired=false;
    std::mutex barrier;
    std::condition_variable condition;
    bool block=false,entered=false,release=false;
    Controller(std::string n,unsigned c,std::string d=""):name(n),description(d),count(c) {}
    void alive() {if(retired) throw std::runtime_error("Output touched a retired controller");}
    std::string GetName() override {alive();return name;}
    std::string GetDescription() override {alive();return description;}
    unsigned GetLEDCount() override {alive();return count;}
    std::string GetLEDName(unsigned i) override {alive();return "LED "+std::to_string(i);}
    void SetCustomMode() override {alive();++modes;}
    void SetColor(unsigned,RGBColor) override {
        alive();++writes;
        if(block) {std::unique_lock lock(barrier);entered=true;condition.notify_all();condition.wait(lock,[&]{return release;});block=false;}
    }
    void UpdateLEDs() override {alive();}
};
struct Api : StubOpenRGBPluginAPIInterface {
    std::vector<RGBControllerInterface*> controllers;
    std::vector<RGBControllerInterface*> GetRGBControllers() override {return controllers;}
};
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}

struct OfficeLifecycleTest {
    static void screensaverSelection() {
        // A private, never-activated desktop exercises the same cross-desktop
        // enumeration used for Windows' Screen-saver desktop. No host input.
        Monitor monitor{};
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT r,LPARAM p)->BOOL {
            *reinterpret_cast<Monitor*>(p)={h,*r};return FALSE;
        },reinterpret_cast<LPARAM>(&monitor));
        require(monitor.handle!=nullptr,"Test display available");
        auto name=L"OfficeSaverTest-"+std::to_wstring(GetCurrentProcessId());
        HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        require(desktop!=nullptr,"Create isolated screensaver test desktop");
        std::mutex mutex;std::condition_variable condition;bool ready=false,finish=false;
        HWND saver{},floating{},manualSaver{};DWORD failure{};bool manualRecognized=false,ordinaryRejected=false;
        std::thread thread([&] {
            auto previous=GetThreadDesktop(GetCurrentThreadId());
            if(!SetThreadDesktop(desktop)) failure=GetLastError();
            else {
                auto r=monitor.bounds;
                saver=CreateWindowW(L"STATIC",L"Test screensaver",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                floating=CreateWindowW(L"STATIC",L"Floating",WS_POPUP|WS_VISIBLE,r.left,r.top,50,50,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                ordinaryRejected=screensaverWindow(desktop,monitor,false)==nullptr;
                WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"WindowsScreenSaverClass";cls.hInstance=GetModuleHandleW(nullptr);
                RegisterClassW(&cls);
                manualSaver=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Manual saver",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,nullptr);
                manualRecognized=manualSaver && screensaverWindow(desktop,monitor,false)==manualSaver;
                if(manualSaver) {DestroyWindow(manualSaver);manualSaver=nullptr;}UnregisterClassW(cls.lpszClassName,cls.hInstance);
            }
            {std::unique_lock lock(mutex);ready=true;condition.notify_all();condition.wait(lock,[&]{return finish;});}
            if(floating) DestroyWindow(floating);if(saver) DestroyWindow(saver);
            SetThreadDesktop(previous);
        });
        {std::unique_lock lock(mutex);condition.wait(lock,[&]{return ready;});}
        bool passed=!failure && saver && floating && screensaverWindow(desktop,monitor)==saver;
        {std::lock_guard lock(mutex);finish=true;condition.notify_all();}
        thread.join();CloseDesktop(desktop);
        require(passed,"Screensaver enumeration selects full LG window from another desktop, excluding floating windows");
        require(ordinaryRejected && manualRecognized,"Recognize manually launched screensaver tool window without SPI flag; reject an ordinary fullscreen app");
    }
    static void run() {
        screensaverSelection();
        QTemporaryDir dir;require(dir.isValid(),"Temporary config directory");
        auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","test-unavailable"},{"fps",15},{"smoothing_ms",0}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),
            laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};
        OfficeAmbience::Impl owner(&api,path,false);
        // Output tests below inject a fresh colour row, not an OS capture source.
        // Test the missing-monitor rest policy separately after those assertions.
        owner.dirty=false;
        owner.standbyPreview=true;owner.tick();
        require(!owner.report()["has_valid_frame"].get<bool>(),"Standby output must not claim a captured wallpaper frame");
        owner.standbyPreview=false;for(auto* c:{&strip,&bars,&keys,&laptop,&desk,&mouse}) c->writes=0;
        owner.latest.assign(314,ToRGBColor(255,0,0));owner.dirty=false;
        owner.tick();require(strip.writes==20 && mouse.writes==8,"Initial six-device output");
        auto processing=owner.snapshot(false);auto modified=processing["settings"];
        modified["standby"]="off";
        owner.command({{"command","save"},{"revision",processing["revision"]},{"settings",modified},{"layout",processing["layout"]}});
        owner.command({{"command","preview_standby"}});owner.tick();
        require(std::all_of(owner.devices[0].colors.begin(),owner.devices[0].colors.end(),[](auto c){return c==0;}),"Standby off must reach actual native output");
        owner.command({{"command","stop_test"}});
        auto zoneMap=processing["layout"];zoneMap[0]["groups"][0]["enabled"]=false;
        owner.command({{"command","save"},{"revision",owner.revision},{"settings",processing["settings"]},{"layout",zoneMap}});owner.tick();
        require(owner.devices[0].colors[0]==0 && owner.devices[0].colors[3]!=0,"Zone enable must affect only its fixed hardware LED range");
        owner.command({{"command","identify_all"}});owner.tick();
        require(std::all_of(owner.devices[0].colors.begin(),owner.devices[0].colors.end(),[](auto c){return c==ToRGBColor(255,255,255);}),"Identify all must reach disabled zones too");owner.command({{"command","stop_test"}});
        // Restore output counters used by the exact removal-barrier assertions.
        for(auto* c:{&strip,&bars,&keys,&laptop,&desk,&mouse}) c->writes=0;
        owner.command({{"command","save"},{"revision",owner.revision},{"settings",processing["settings"]},{"layout",processing["layout"]}});
        owner.tick();

        // Deliver real Windows lifecycle messages to the production event handler.
        // This private test window does not lock Windows or change its display.
        WNDCLASSW eventClass{};eventClass.lpfnWndProc=OfficeAmbience::Impl::nativeEvent;
        eventClass.lpszClassName=L"OfficeLifecycleTestEvents";eventClass.hInstance=GetModuleHandleW(nullptr);
        require(RegisterClassW(&eventClass)!=0,"Register test event window");
        HWND events=CreateWindowW(eventClass.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,eventClass.hInstance,&owner);
        require(events!=nullptr,"Create test event window");
        auto rest=[&](const char* reason) {
            owner.tick();
            for(auto& d:owner.devices) require(std::all_of(d.colors.begin(),d.colors.end(),[](auto c){return c==0;}),"Lock/sleep must force every LED black, including identify/calibration");
            require(owner.report().value("rest_reason",std::string())==reason,"Report exact rest reason");
            require(!owner.capture && !owner.source && owner.latest.empty(),"Rest releases capture and stale input");
        };
        auto beforeRest=owner.config;
        owner.calibration=true;owner.identifyDevice="*";
        SendMessageW(events,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);rest("session-locked");
        SendMessageW(events,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
        require(!owner.sessionLocked && owner.dirty && owner.invalidate,"Unlock requests fresh source selection");
        // No capturable monitor in this test: still must not replay pre-lock red.
        owner.calibration=false;owner.identifyDevice.clear();owner.dirty=false;owner.tick();
        require(owner.devices[0].colors[0]==0,"Unlock waits for fresh imagery");
        struct Power {GUID guid;DWORD length;DWORD value;} power{OfficeAmbience::Impl::sessionDisplayState,sizeof(DWORD),0};
        owner.latest.assign(314,ToRGBColor(255,0,0));owner.standbyPreview=true;
        SendMessageW(events,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));rest("display-asleep");
        power.value=2;SendMessageW(events,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));
        require(!owner.displayOff && owner.dirty && owner.invalidate,"A dim/on display resumes source selection");
        owner.standbyPreview=false;owner.dirty=false;
        owner.latest.assign(314,ToRGBColor(0,255,0));owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(0,255,0),"Fresh imagery restores lighting after wake");
        require(owner.config==beforeRest,"Lifecycle transitions preserve all settings and mapping");
        auto fakeHook=reinterpret_cast<HWINEVENTHOOK>(1);OfficeAmbience::Impl::hooks[fakeHook]=&owner;
        owner.dirty=false;owner.invalidate=false;
        OfficeAmbience::Impl::winEvent(fakeHook,EVENT_SYSTEM_DESKTOPSWITCH,nullptr,0,0,0,0);
        require(owner.dirty && owner.invalidate,"Desktop switch invalidates a stale capture");
        OfficeAmbience::Impl::hooks.erase(fakeHook);DestroyWindow(events);UnregisterClassW(eventClass.lpszClassName,eventClass.hInstance);
        owner.dirty=false;
        owner.latest.assign(314,ToRGBColor(255,0,0));
        for(auto* c:{&strip,&bars,&keys,&laptop,&desk,&mouse}) c->writes=0;
        owner.tick();

        // Unregister removes the pointer, signals synchronously, then deletes it.
        // In-flight output must complete before that synchronous callback returns.
        strip.block=true;
        std::thread output([&]{owner.tick();});
        {std::unique_lock lock(strip.barrier);strip.condition.wait(lock,[&]{return strip.entered;});}
        api.controllers.erase(api.controllers.begin());
        std::thread removal([&]{owner.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);strip.retired=true;});
        {std::lock_guard lock(strip.barrier);strip.release=true;strip.condition.notify_all();}
        output.join();removal.join();
        owner.tick();require(owner.report()["online_devices"]==5,"Missing controller remains offline");
        auto snapshot=owner.snapshot(false);
        require(snapshot["layout"].size()==6 && snapshot["online"][0]==false,"Offline map and metadata retained");
        require(mouse.writes==24,"Other devices continue while one is offline");

        Controller replacement("Govee H6609",20);api.controllers.insert(api.controllers.begin(),&replacement);
        owner.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);owner.tick();
        require(replacement.modes==1 && replacement.writes==20 && owner.report()["online_devices"]==6,"Replacement rebound and direct mode restored");
        auto saved=owner.config;saved["layout"]=owner.layout;
        {std::ofstream out(path);out<<saved;}
        api.controllers.erase(api.controllers.begin());
        Controller unknown("Govee ",1);api.controllers.push_back(&unknown);
        OfficeAmbience::Impl restarted(&api,path,false);
        require(restarted.devices.size()==6 && restarted.samples.size()==314 && restarted.report()["online_devices"]==5,"Persisted map boots with a missing device");
        owner.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED);
        require(owner.report()["online_devices"]==0,"Rescan releases all borrowed pointers before native deletion");
        owner.dirty=true;owner.tick();
        require(owner.report()["rest_reason"]=="monitor-disconnected" && owner.latest.empty(),"Missing LG rests instead of replaying stale colours");
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {OfficeLifecycleTest::run();std::cout<<"PASS: removal barrier, offline inventory, reconnect, persisted bootstrap and rescan\n";return 0;}
    catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
