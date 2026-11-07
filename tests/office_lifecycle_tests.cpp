// Exercise the actual output owner against OpenRGB's synchronous removal contract.
#include "../OfficeAmbience.cpp"
#include "interface_stubs.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <cstdio>
#include <QEventLoop>

struct Controller : StubRGBControllerInterface {
    std::string name, description;
    unsigned count, writes=0, modes=0, submissions=0;
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
    void UpdateLEDs() override {alive();++submissions;}
};
struct Api : StubOpenRGBPluginAPIInterface {
    std::vector<RGBControllerInterface*> controllers;
    std::vector<RGBControllerInterface*> GetRGBControllers() override {return controllers;}
};
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}

struct OfficeLifecycleTest {
    static void wallpaperImagePolicy(){
        QTemporaryDir dir;auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","primary"},{"fps",15},{"smoothing_ms",0},{"follow_display_brightness",false}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};OfficeAmbience::Impl owner(&api,path,false);
        auto settings=owner.config;auto layout=owner.layout;Monitor monitor{reinterpret_cast<HMONITOR>(101),{0,0,32,16},"image-test"};
        QImage image(32,16,QImage::Format_ARGB32);image.fill(Qt::blue);
        owner.wallpaperImageBegan=Clock::now();owner.primeWallpaperImage(image,monitor,D3D_DRIVER_TYPE_WARP);owner.dirty=false;owner.tick();
        auto all=[&](RGBColor expected){for(auto& d:owner.devices)require(std::all_of(d.colors.begin(),d.colors.end(),[&](auto c){return c==expected;}),"Wallpaper image did not reach every mapped output");};
        all(ToRGBColor(0,0,255));require(owner.wallpaperImageActive&&owner.wallpaperImageSubmitted&&owner.inputFrames==0&&!owner.capture,"Static image was misreported as live capture");
        owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","forever"}});owner.tick();all(0);
        owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","enable"}});
        owner.primeWallpaperImage(image,monitor,D3D_DRIVER_TYPE_WARP);owner.tick();all(ToRGBColor(0,0,255));
        owner.automaticSaver=true;owner.observeRest(true,false,100);owner.dirty=false;owner.tick();all(0);
        require(!owner.wallpaperImageActive&&owner.latest.empty()&&owner.wallpaperImageRequested,"Rest retained a static wallpaper image");
        owner.automaticSaver=false;owner.observeRest(false,true,100);owner.tick();all(0);
        require(owner.latest.empty()&&owner.rest.active(),"Wallpaper image bypassed unchanged-input rest gate");
        owner.observeRest(false,true,101);image.fill(Qt::red);owner.primeWallpaperImage(image,monitor,D3D_DRIVER_TYPE_WARP);owner.dirty=false;owner.tick();all(ToRGBColor(255,0,0));
        require(owner.config==settings&&owner.layout==layout,"Image-first resume changed processing or topology");
    }
    static void restPolicy(){
        QTemporaryDir dir;auto root=std::filesystem::path(dir.path().toStdWString());
        office::OfficeRest rest(root);require(!rest.active(),"New profile must not be asleep");
        rest.observe(true,false,100);require(rest.active(),"Saver did not latch rest");
        rest.observe(false,true,100);require(rest.active(),"Default desktop resumed without new input");
        office::OfficeRest restarted(root);restarted.observe(false,true,100);require(restarted.active(),"Cold restart lost held rest");
        restarted.observe(false,false,101);require(restarted.active(),"Input on unavailable desktop resumed");
        restarted.observe(false,true,101);require(!restarted.active(),"New input on usable desktop failed to resume");
        office::OfficeRest saved(root);require(!saved.active(),"Released state did not persist");
        saved.observe(true,false,std::nullopt);saved.observe(false,true,std::nullopt);require(saved.active(),"Query failure resumed output");
        saved.observe(false,true,200);require(saved.active(),"First successful query fabricated new input");
        saved.observe(false,true,150);require(!saved.active(),"Non-incremental input token was ignored");
        saved.observe(true,false,UINT32_MAX);saved.observe(false,true,0);require(!saved.active(),"Wrapped input token failed to resume");
        office::OfficeRest failing(root/"missing-parent");failing.observe(true,false,10);require(failing.active()&&failing.status().at("error")!="","Storage failure did not remain visibly black");
        failing.observe(false,true,10);require(failing.active(),"Storage failure cleared without input");
        QDir().mkpath(QString::fromStdWString((root/"missing-parent").wstring()));failing.observe(false,true,11);require(!failing.active()&&failing.status().at("error")=="","New input did not recover valid state storage");
    }
    static void savePreservesUntouchedSettings() {
        QTemporaryDir dir;auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","\\\\.\\DISPLAY2"},{"fps",15},{"smoothing_ms",140}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8),keys("Keychron Q6 HE ISO",109);
        Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};OfficeAmbience::Impl owner(&api,path,false);
        owner.diskConfig["layout"]=owner.layout;auto original=owner.diskConfig.dump(2);{std::ofstream out(path);out<<original;}
        auto snapshot=owner.snapshot(false);
        auto save=[&](const Json& settings){owner.command({{"command","save"},{"revision",owner.revision},{"settings",settings},{"layout",snapshot.at("layout")}});};
        save(snapshot.at("settings"));std::ifstream saved(path);std::string bytes((std::istreambuf_iterator<char>(saved)),{});saved.close();
        require(bytes==original,"Topology Save rewrote the legacy display alias or injected untouched processing defaults");
        auto edited=snapshot.at("settings");edited["follow_display_brightness"]=false;save(edited);
        require(owner.diskConfig.at("follow_display_brightness")==false && owner.diskConfig.at("monitor")=="\\\\.\\DISPLAY2","Explicit settings edits did not persist independently of the runtime display alias");
    }
    static void hardwareOwnership() {
        struct Native : Controller {
            using Controller::Controller;
            int active=0;unsigned updates=0,saves=0;
            unsigned GetModeCount() override{return 2;}
            int GetActiveMode() override{return active;}
            std::string GetModeName(unsigned m) override{return m?"Pulse":"Direct";}
            unsigned GetModeFlags(unsigned m) override{return m?MODE_FLAG_MANUAL_SAVE:0;}
            void SetActiveMode(int m) override{active=m;}
            void UpdateMode() override{++updates;}
            void SaveMode() override{++saves;}
        };
        struct Profiles : Api {
            Native* keyboard=nullptr;unsigned saved=0,loaded=0;
            std::vector<std::string> GetProfileList() override{return {"Test"};}
            bool SaveProfile(std::string) override{++saved;return true;}
            bool LoadProfile(std::string) override{++loaded;keyboard->active=1;return true;}
        };
        QTemporaryDir dir;auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","primary"},{"fps",15},{"smoothing_ms",0}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        Native keys("Keychron Q6 HE ISO",109);Profiles api;api.keyboard=&keys;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};
        OfficeAmbience::Impl owner(&api,path,false);owner.dirty=false;owner.latest.assign(314,ToRGBColor(20,40,60));owner.tick();
        auto key=owner.nativeKey(&keys),identity=office::HardwareState::identity(&keys);auto request=Json{{"command","hardware"},{"action","mode"},{"device",key},{"identity",identity},{"mode",office::HardwareState::mode(&keys,1)}};
        owner.command(request);auto writes=keys.writes,updates=keys.updates;owner.latest.assign(314,ToRGBColor(60,40,20));owner.tick();
        require(keys.writes==writes && keys.updates==updates,"Ambience overwrote the explicit native effect");
        owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","forever"}});owner.tick();
        require(owner.devices[2].colors[0]==0&&owner.report().at("output_paused")==true,"Tray pause did not black native effects and ambience");
        owner.command({{"command","identify_all"}});owner.tick();require(owner.devices[2].colors[0]==0,"Identify overrode explicit lighting pause");owner.command({{"command","stop_test"}});
        owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","enable"}});
        owner.latest.assign(314,ToRGBColor(60,40,20));owner.tick();
        require(keys.active==1&&!owner.report().at("output_paused").get<bool>(),"Native effect failed to resume after RGB pause");
        owner.command({{"command","screen"},{"action","pause"},{"target","screen"},{"mode","forever"}});owner.tick();
        require(!owner.report().at("output_paused").get<bool>()&&owner.screen->status().at("screen_disabled")==true,"Screen pause disabled RGB");
        owner.command({{"command","screen"},{"action","pause"},{"target","screen"},{"mode","enable"}});
        writes=keys.writes;updates=keys.updates;
        owner.command({{"command","hardware"},{"action","save_mode"},{"device",key},{"identity",identity},{"mode",1},{"name","Pulse"}});require(keys.saves==1,"Native SaveMode did not reach hardware API");
        owner.command({{"command","identify_all"}});owner.tick();require(keys.writes>writes,"Temporary Identify did not take over native hardware");
        bool rejected=false;try{owner.command({{"command","hardware"},{"action","profile_load"},{"name","Test"}});}catch(const std::exception&){rejected=true;}
        require(rejected && api.loaded==0,"Native profile changed hardware during a temporary test");
        owner.command({{"command","stop_test"}});owner.tick();require(keys.active==1 && keys.updates==updates+1,"Native hardware effect did not resume after Identify");
        owner.command({{"command","hardware"},{"action","profile_save"},{"name","Test"}});owner.command({{"command","hardware"},{"action","profile_load"},{"name","Test"}});
        require(api.saved==1 && api.loaded==1 && owner.hardware->hardware(key),"Native profile API or retained ownership not wired");
        owner.command({{"command","pause_output"}});owner.tick();require(owner.devices[2].colors[0]==0,"Pause did not rest a native-owned device");
        owner.command({{"command","pause_output"}});owner.tick();require(keys.active==1,"Resume did not restore the native mode");
        owner.command({{"command","hardware"},{"action","ambience"},{"device",key},{"identity",identity}});owner.tick();require(!owner.hardware->hardware(key) && owner.devices[2].colors[0]==ToRGBColor(60,40,20),"Return to ambience did not restore mapped colour output");
    }
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
    static void awakePolicy() {
        BOOL saverBefore{};UINT timeoutBefore{};
        require(SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE,0,&saverBefore,0)&&SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT,0,&timeoutBefore,0),"Read original screensaver settings");
        auto name=L"OfficeAwakeTest-"+std::to_wstring(GetCurrentProcessId());
        HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        require(desktop!=nullptr,"Create private awake test desktop");
        std::exception_ptr failure;
        // QThread initializes its Windows dispatcher before run(), creating a
        // hidden HWND that prevents SetThreadDesktop. Bind a native thread
        // before its first Qt object/dispatcher is constructed.
        std::thread thread([&]{
            HWND window{},floating{},saver{};const wchar_t* saverClass=L"WindowsScreenSaverClass";
            try {
                require(SetThreadDesktop(desktop)!=FALSE,"Bind test thread to private desktop");
                QTemporaryDir dir;require(dir.isValid(),"Awake fixture directory");
                auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
                {std::ofstream out(path);out<<Json{{"monitor","primary"},{"fps",15},{"smoothing_ms",0}};}
                {std::ofstream out(path.parent_path()/"office-screen.json");out<<Json{{"settings",Json::object()},{"managed",false}};}
                Controller strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
                Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};
                OfficeAmbience::Impl owner(&api,path,true);auto main=primaryMonitor();auto r=main.bounds;
                owner.observeWallpaper=false; // The inactive desktop has no displayed Windows wallpaper.
                owner.chooseSource(main);
                require(owner.mode=="waiting"&&!owner.capture&&owner.dirty,"Unavailable source must remain scheduled for observation");
                auto waitingGeneration=owner.sourceGeneration;
                owner.tick();owner.tick();
                require(owner.mode=="waiting"&&owner.dirty&&owner.sourceGeneration==waitingGeneration,"Unchanged unavailable source must not churn capture generations");
                auto requests=[](){
                    FILE* pipe=_popen("powercfg.exe /requests","r");require(pipe!=nullptr,"Query native power request inventory");
                    std::string value;char buffer[4096];while(std::fgets(buffer,sizeof(buffer),pipe))value+=buffer;
                    require(_pclose(pipe)==0,"Native power request query must succeed on hosted Windows runner");return value;
                };
                const std::string reason="Office Ambilight: automatic or manual keep awake";
                auto expect=[&](bool on,const char* message){owner.chooseSource(main);require(owner.awake.active()==on,message);require(owner.awake.status().at("error")=="","Native awake request rejected");};
                window=CreateWindowW(L"STATIC",L"Ordinary app",WS_OVERLAPPEDWINDOW|WS_VISIBLE,r.left+40,r.top+40,240,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                require(window!=nullptr,"Create isolated app");expect(false,"Floating app kept display awake");
                // No Qt/WinEvent dispatch or explicit invalidation between the
                // waiting observation and the production tick discovering it.
                ShowWindow(window,SW_MAXIMIZE);owner.tick();
                // WGC rejects inactive-desktop capture on hosted Windows
                // (E_INVALIDARG). This fixture verifies source discovery and
                // its actual capture attempt, not successful image delivery.
                // GPU tests and the installed interactive owner verify frames.
                if(owner.source!=window||owner.mode!="app"||owner.sourceGeneration<=waitingGeneration||!owner.awake.active()) {
                    auto status=owner.report();
                    throw std::runtime_error("Event-free recovery: "+Json{{"expected_hwnd",reinterpret_cast<uintptr_t>(window)},
                        {"source_hwnd",status.at("source_hwnd")},{"mode",status.at("mode")},{"rest_reason",status.at("rest_reason")},
                        {"capture_created",owner.capture!=nullptr},{"awake",owner.awake.active()},{"error",status.at("error")}}.dump());
                }
                std::cout<<"Event-free private-desktop source discovery and native Awake recovered; capture_created="<<(owner.capture!=nullptr)<<'\n';
                ShowWindow(window,SW_RESTORE);expect(false,"Recovered source retained Awake after restore");
                ShowWindow(window,SW_MAXIMIZE);expect(true,"Maximized main-display app did not acquire awake requests");
                owner.latest.assign(314,ToRGBColor(20,40,60));
                owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","forever"}});owner.tick();
                auto pausedGeneration=owner.sourceGeneration;
                require(owner.report().at("capture_paused")==true&&!owner.capture&&!owner.source&&owner.latest.empty(),"Disabled lighting retained capture or stale imagery");
                require(owner.awake.active(),"Lighting pause released independent Awake eligibility");
                owner.chooseSource(main);owner.tick();owner.tick();
                auto preview=owner.snapshot(true);
                require(owner.sourceGeneration==pausedGeneration&&!owner.capture&&preview.at("preview_png")==""&&preview.at("preview_error")!="","Paused preview restarted capture or waited without a reason");
                owner.command({{"command","screen"},{"action","pause"},{"target","lighting"},{"mode","enable"}});owner.tick();
                require(owner.report().at("capture_paused")==false&&owner.source==window&&owner.sourceGeneration>pausedGeneration,"Lighting resume did not request fresh source acquisition");
                owner.config["enabled"]=false;owner.tick();
                require(owner.report().at("capture_paused")==true&&!owner.capture&&owner.latest.empty()&&owner.awake.active(),"Global ambience disable retained GPU input or disabled Awake");
                owner.config["enabled"]=true;owner.tick();
                auto native=requests();auto first=native.find(reason);require(first!=native.npos&&native.find(reason,first+reason.size())!=native.npos,"Windows must independently report display and system requests");
                floating=CreateWindowW(L"STATIC",L"Floating overlay",WS_OVERLAPPEDWINDOW|WS_VISIBLE,r.left+20,r.top+20,200,150,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                expect(true,"Non-foreground maximized app lost its awake lease");
                ShowWindow(window,SW_RESTORE);expect(false,"Restored window retained request");require(requests().find(reason)==std::string::npos,"Windows retained cleared power request");
                ShowWindow(window,SW_MAXIMIZE);expect(true,"Maximize failed to reacquire");ShowWindow(window,SW_MINIMIZE);expect(false,"Minimized app retained request");
                DestroyWindow(window);window=CreateWindowW(L"STATIC",L"Fullscreen app",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                require(window&&!IsZoomed(window),"Create borderless fullscreen app");expect(true,"Borderless fullscreen app did not acquire request");
                MONITORINFO secondary{};secondary.cbSize=sizeof(secondary);
                EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL{MONITORINFO info{sizeof(info)};if(GetMonitorInfoW(h,&info)&&!(info.dwFlags&MONITORINFOF_PRIMARY)){*reinterpret_cast<MONITORINFO*>(p)=info;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&secondary));
                auto other=secondary.rcMonitor;if(other.right==other.left)other={r.right+100,r.top,r.right+500,r.top+300};
                SetWindowPos(window,nullptr,other.left,other.top,other.right-other.left,other.bottom-other.top,SWP_NOACTIVATE|SWP_NOZORDER);expect(false,"App outside main display retained request");
                SetWindowPos(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_NOZORDER);expect(true,"Return to main display failed to reacquire");
                OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);require(!owner.awake.active(),"Lock did not release native request");expect(false,"Locked session reacquired request");
                OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);expect(true,"Unlock failed to reacquire");
                OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_APMSUSPEND,0);expect(false,"Suspended session retained request");
                OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);expect(true,"Resume failed to reacquire");
                struct Setting {GUID guid;DWORD length;DWORD state;} setting{OfficeAmbience::Impl::sessionDisplayState,sizeof(DWORD),0};
                OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&setting));expect(false,"Display off retained request");
                setting.state=1;OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&setting));expect(true,"Display on failed to reacquire");
                WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=saverClass;cls.hInstance=GetModuleHandleW(nullptr);require(RegisterClassW(&cls)!=0,"Register manual saver fixture");
                saver=CreateWindowExW(WS_EX_TOOLWINDOW,saverClass,L"Manual saver",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,nullptr);
                require(saver!=nullptr,"Create manual saver");expect(false,"Manual screensaver retained application awake request");
                DestroyWindow(saver);saver=nullptr;expect(true,"Leaving manual saver failed to reacquire app request");
                DestroyWindow(window);window=nullptr;expect(false,"Closed app retained request");
                require(requests().find(reason)==std::string::npos,"Final request remained registered with Windows");
                auto command=[&](Json request){request["command"]="awake";return owner.command(request);};
                command({{"action","mode"},{"mode","indefinite"}});require(owner.awake.active()&&!owner.awake.status().at("display_required").get<bool>(),"Unchecked Keep screen on must hold only system sleep");
                setting.state=0;OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&setting));owner.chooseSource(main);require(owner.awake.active()&&!owner.awake.status().at("display_required").get<bool>(),"Manual system-only Awake must survive display sleep");
                setting.state=1;OfficeAmbience::Impl::nativeEvent(owner.eventWindow,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&setting));owner.chooseSource(main);
                command({{"action","display"},{"enabled",true}});require(owner.awake.status().at("display_required")==true,"Keep screen on did not acquire display request");
                command({{"action","mode"},{"mode","off"}});require(!owner.awake.active(),"Off retained a manual request");
                auto timed=command({{"action","mode"},{"mode","interval"},{"seconds",1}});
                {office::OfficeAwake restored(path.parent_path(),false);require(restored.status().at("mode")=="interval"&&restored.status().at("until_ms")==timed.at("until_ms"),"Restart lost active interval deadline");}
                auto awaitExpiry=[&]{QEventLoop loop;QTimer observation;QObject::connect(&observation,&QTimer::timeout,[&]{owner.awake.tick();if(owner.awake.status().at("mode")=="off")loop.quit();});observation.start(20);loop.exec();require(!owner.awake.active(),"Deadline did not release actual request");};
                awaitExpiry();require(requests().find(reason)==std::string::npos,"Interval expiry left native requests");
                command({{"action","mode"},{"mode","expiration"},{"until_ms",QDateTime::currentMSecsSinceEpoch()+1000}});awaitExpiry();
                bool rejected=false;try{command({{"action","mode"},{"mode","expiration"},{"until_ms",QDateTime::currentMSecsSinceEpoch()-1}});}catch(const std::exception&){rejected=true;}require(rejected,"Past expiration was accepted");
                command({{"action","mode"},{"mode","automatic"}});expect(false,"Automatic without qualifying app held a request");
                // Replay the recorded OS observations without changing global
                // screensaver/input state; native request handles remain real.
                owner.observeNativeInput=false;
                window=CreateWindowW(L"STATIC",L"Resume replay",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                require(window!=nullptr,"Create resume replay app");expect(true,"Replay app failed to acquire native request");
                owner.latest.assign(314,ToRGBColor(0,0,255));owner.observeRest(true,false,100);owner.automaticSaver=true;owner.dirty=false;owner.tick();
                require(!owner.awake.active()&&requests().find(reason)==std::string::npos,"Screensaver gate did not release actual native requests");
                owner.automaticSaver=false;owner.observeRest(false,true,100);owner.chooseSource(main);owner.tick();
                require(owner.report().at("rest_reason")=="awaiting-user-input"&&!owner.awake.active()&&!owner.capture,"No-input desktop return resumed capture or Awake");
                for(auto& device:owner.devices)require(std::all_of(device.colors.begin(),device.colors.end(),[](auto c){return c==0;}),"No-input desktop return relit a mapped device");
                owner.observeRest(false,true,101);owner.chooseSource(main);require(owner.awake.active(),"New session input did not reacquire eligible app request");
                std::cout<<"Actual main-display window eligibility, native DISPLAY/SYSTEM request inventory, lifecycle release and manual saver behavior passed\n";
            }catch(...){failure=std::current_exception();}
            if(saver)DestroyWindow(saver);if(floating)DestroyWindow(floating);if(window)DestroyWindow(window);UnregisterClassW(saverClass,GetModuleHandleW(nullptr));
        });
        thread.join();CloseDesktop(desktop);if(failure)std::rethrow_exception(failure);
        BOOL saverAfter{};UINT timeoutAfter{};
        require(SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE,0,&saverAfter,0)&&SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT,0,&timeoutAfter,0)&&saverBefore==saverAfter&&timeoutBefore==timeoutAfter,"Awake changed screensaver configuration");
    }
    static void run() {
        wallpaperImagePolicy();
        restPolicy();
        savePreservesUntouchedSettings();
        hardwareOwnership();
        screensaverSelection();
        awakePolicy();
        auto primary=primaryMonitor();MONITORINFO primaryInfo{};primaryInfo.cbSize=sizeof(primaryInfo);
        require(primary.handle && GetMonitorInfoW(primary.handle,&primaryInfo) && (primaryInfo.dwFlags&MONITORINFOF_PRIMARY),"Resolve Windows main display, not enumeration order or saved DISPLAY name");
        require(EqualRect(&primary.bounds,&primaryInfo.rcMonitor) && !primary.name.empty(),"Primary capture uses actual native display bounds and identity");
        QTemporaryDir dir;require(dir.isValid(),"Temporary config directory");
        auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","test-unavailable"},{"fps",15},{"smoothing_ms",0}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),
            laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};
        OfficeAmbience::Impl owner(&api,path,false);
        require(owner.config["monitor"]=="primary" && owner.diskConfig["monitor"]=="test-unavailable","Legacy disconnected display name cannot pin capture or rewrite saved configuration on load");
        // Output tests below inject a fresh colour row, not an OS capture source.
        // Test the missing-monitor rest policy separately after those assertions.
        owner.dirty=false;
        owner.standbyPreview=true;owner.tick();
        require(strip.writes==20 && strip.submissions==1,"First delivery must transmit black to uninitialized hardware");
        owner.tick();require(strip.writes==20 && strip.submissions==1,"Settled black must not resend device output");
        require(!owner.report()["has_valid_frame"].get<bool>(),"Standby output must not claim a captured wallpaper frame");
        owner.standbyPreview=false;for(auto* c:{&strip,&bars,&keys,&laptop,&desk,&mouse}) c->writes=0;
        owner.latest.assign(314,ToRGBColor(255,0,0));owner.dirty=false;
        owner.tick();require(strip.writes==20 && mouse.writes==8,"Initial six-device output");
        // Retain exactly the same captured row: display dimming is an output
        // property, so it must not wait for a new wallpaper frame or GPU sample.
        owner.lastBrightnessRead=Clock::now();owner.displayBrightness.display=owner.selectedMonitor.name;
        owner.displayBrightness.factor=.5;owner.displayBrightness.valid=true;owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(128,0,0) && owner.devices.back().colors[0]==ToRGBColor(128,0,0),"Static retained image must dim across controllers without recapture");
        owner.command({{"command","identify_all"}});owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(255,255,255),"Identify test must retain its reference colour despite display dimming");
        owner.command({{"command","stop_test"}});owner.config["follow_display_brightness"]=false;owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(255,0,0),"Disabling display following must restore manual brightness baseline");
        owner.config["follow_display_brightness"]=true;owner.displayBrightness.factor=1;owner.tick();
        for(auto* c:{&strip,&bars,&keys,&laptop,&desk,&mouse}) c->writes=c->count;
        auto sent=owner.deviceSubmissions,writes=owner.ledWrites;
        owner.tick();owner.tick();
        require(owner.deviceSubmissions==sent && owner.ledWrites==writes,"Unchanged output skips all controller writes and submissions");
        owner.latest[4]=ToRGBColor(0,255,0);owner.tick();
        require(owner.deviceSubmissions==sent+1 && owner.ledWrites==writes+1 && mouse.writes==8,"Only one changed LED and its controller may update");
        owner.latest[4]=ToRGBColor(255,0,0);owner.tick();
        owner.config["smoothing_ms"]=140;owner.latest[4]=ToRGBColor(255,1,0);
        owner.lastOutput=Clock::now()-std::chrono::milliseconds(67);owner.tick();
        require(owner.devices[0].colors[4]==owner.latest[4],"One-byte pending adaptation must converge instead of stalling");
        sent=owner.deviceSubmissions;owner.tick();require(owner.deviceSubmissions==sent,"Settled smoothing stops hardware traffic");
        owner.latest[4]=ToRGBColor(0,0,255);
        unsigned steps=0;
        while(owner.devices[0].colors[4]!=owner.latest[4] && steps<256) {
            owner.lastOutput=Clock::now()-std::chrono::milliseconds(67);owner.tick();++steps;
        }
        require(steps>1 && steps<256,"Pending multi-step adaptation must finish without more captured frames");
        sent=owner.deviceSubmissions;owner.tick();require(owner.deviceSubmissions==sent,"Completed multi-step adaptation goes quiet");
        owner.config["smoothing_ms"]=0;owner.latest[4]=ToRGBColor(255,0,0);owner.tick();
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
        owner.automaticSaver=true;owner.observeRest(true,false,100);owner.dirty=false;rest("automatic-screensaver");
        owner.automaticSaver=false;
        owner.observeRest(false,true,100);rest("awaiting-user-input");
        SendMessageW(events,WM_DISPLAYCHANGE,32,MAKELPARAM(5120,2160));owner.tick();rest("awaiting-user-input");
        require(!owner.awake.active()&&owner.config==beforeRest,"No-input display event resumed Awake or changed settings");
        owner.observeRest(false,true,101);owner.dirty=false;
        owner.calibration=false;owner.identifyDevice.clear();owner.tick();
        require(owner.devices[0].colors[0]==0,"Idle saver exit must not replay pre-saver colours before a fresh frame");
        owner.latest.assign(314,ToRGBColor(0,255,0));owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(0,255,0),"Fresh imagery resumes after automatic saver exit");
        require(owner.config==beforeRest,"Automatic saver preserves custom settings and topology");
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
        owner.dirty=false;owner.invalidate=false;
        SendMessageW(events,WM_DISPLAYCHANGE,32,MAKELPARAM(2560,1600));
        require(owner.dirty && owner.invalidate,"Display topology/main-display change invalidates capture for fresh monitor resolution");
        owner.standbyPreview=false;owner.dirty=false;
        owner.latest.assign(314,ToRGBColor(0,255,0));owner.tick();
        require(owner.devices[0].colors[0]==ToRGBColor(0,255,0),"Fresh imagery restores lighting after wake");
        require(owner.config==beforeRest,"Lifecycle transitions preserve all settings and mapping");
        {
            std::ifstream log(path.parent_path()/"logs"/"office-ambience-events.jsonl");
            std::set<std::string> events;std::string line;bool locked=false,unlocked=false;
            while(std::getline(log,line)) {
                auto record=Json::parse(line);events.insert(record.at("event"));
                if(record["event"]=="session_event") {
                    locked=locked || record["data"]["notification"]==WTS_SESSION_LOCK;
                    unlocked=unlocked || record["data"]["notification"]==WTS_SESSION_UNLOCK;
                }
                require(!record.contains("colors") && !record.contains("token"),"Diagnostic journal excludes pixels and credentials");
            }
            require(locked && unlocked && events.contains("display_power") && events.contains("capture_invalidated") && events.contains("rest_transition") && events.contains("control_action") && events.contains("settings_applied"),"Production lifecycle and controls persist correlation events before process exit");
        }
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
        owner.latest[0]=ToRGBColor(0,0,255);
        std::thread output([&]{owner.tick();});
        {std::unique_lock lock(strip.barrier);strip.condition.wait(lock,[&]{return strip.entered;});}
        api.controllers.erase(api.controllers.begin());
        std::thread removal([&]{owner.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);strip.retired=true;});
        {std::lock_guard lock(strip.barrier);strip.release=true;strip.condition.notify_all();}
        output.join();removal.join();
        owner.latest[owner.devices.back().start]=ToRGBColor(0,0,255);
        owner.tick();require(owner.report()["online_devices"]==5,"Missing controller remains offline");
        auto snapshot=owner.snapshot(false);
        require(snapshot["layout"].size()==6 && snapshot["online"][0]==false,"Offline map and metadata retained");
        require(mouse.writes==9,"Only changed LEDs on other devices continue while one is offline");

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
        owner.chooseSource(Monitor{});owner.dirty=false;owner.tick();
        require(owner.report()["rest_reason"]=="monitor-disconnected" && owner.latest.empty(),"No Windows main display rests instead of replaying stale colours");

        // A persisted office map is portable without the four desk controllers.
        // Synthetic topology observations exercise production source selection;
        // they never reconfigure Windows or manufacture a captured frame.
        api.controllers={&laptop,&mouse};
        OfficeAmbience::Impl portable(&api,path,false);
        auto portableMap=portable.layout;auto portableSettings=portable.config;
        Monitor officeMain{reinterpret_cast<HMONITOR>(101),{10000,10000,15120,12160},"office-main"};
        Monitor laptopMain{reinterpret_cast<HMONITOR>(102),{-12560,10000,-10000,11600},"laptop-main"};
        portable.chooseSource(officeMain);auto generation=portable.sourceGeneration;
        portable.latest.assign(314,ToRGBColor(20,40,60));
        portable.chooseSource(laptopMain);
        require(!portable.monitorMissing && portable.sourceGeneration>generation && EqualRect(&portable.monitorBounds,&laptopMain.bounds),"Main-display replacement updates crop at different resolution and origin");
        require(portable.report()["capture_display"]=="laptop-main" && portable.report()["capture_bounds"]==Json::array({-12560,10000,-10000,11600}),"Diagnostics expose the resolved source display rather than a stale configured name");
        require(portable.layout==portableMap && portable.config==portableSettings,"Main-display changes preserve normalized map and processing settings");
        portable.dirty=false;portable.tick();
        require(portable.report()["online_devices"]==2 && portable.devices[3].colors[0]==ToRGBColor(20,40,60) && portable.devices[5].colors[0]==ToRGBColor(20,40,60),"Laptop and mouse output continues with desk controllers offline");
        portable.chooseSource(Monitor{});portable.dirty=false;portable.tick();
        require(portable.latest.empty() && portable.report()["rest_reason"]=="monitor-disconnected","Transient absent display releases stale image and rests lights");
        portable.chooseSource(laptopMain);portable.latest.assign(314,ToRGBColor(60,40,20));portable.dirty=false;portable.tick();
        require(portable.devices[3].colors[0]==ToRGBColor(60,40,20) && portable.devices[5].colors[0]==ToRGBColor(60,40,20),"Available main display resumes connected portable devices with new imagery");
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {OfficeLifecycleTest::run();std::cout<<"PASS: removal barrier, offline inventory, reconnect, persisted bootstrap and rescan\n";return 0;}
    catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
