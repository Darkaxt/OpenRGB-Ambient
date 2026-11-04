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
    static void run() {
        QTemporaryDir dir;require(dir.isValid(),"Temporary config directory");
        auto path=std::filesystem::path(dir.path().toStdWString())/"office-ambience.json";
        {std::ofstream out(path);out<<Json{{"monitor","test-unavailable"},{"fps",15},{"smoothing_ms",0}};}
        Controller strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),
            laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        Api api;api.controllers={&strip,&bars,&keys,&laptop,&desk,&mouse};
        OfficeAmbience::Impl owner(&api,path,false);
        owner.latest.assign(314,ToRGBColor(255,0,0));owner.dirty=false;
        owner.tick();require(strip.writes==20 && mouse.writes==8,"Initial six-device output");

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
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {OfficeLifecycleTest::run();std::cout<<"PASS: removal barrier, offline inventory, reconnect, persisted bootstrap and rescan\n";return 0;}
    catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
