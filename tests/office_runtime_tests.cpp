// The production runtime, engine, event windows, timers and typed dispatcher.
// The GUI deliberately does not pump events while waiting for output below.
#include "../OfficeAmbience.cpp"
#include "../OfficeRuntime.h"
#include "interface_stubs.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <condition_variable>
#include <fstream>
#include <iostream>
struct RuntimeDevice : StubRGBControllerInterface {
    std::string name,description;unsigned count;std::thread::id modeThread,writeThread;
    std::mutex mutex;std::condition_variable changed;unsigned submissions=0;
    RuntimeDevice(std::string n,unsigned size,std::string d=""):name(n),description(d),count(size){}
    std::string GetName() override{return name;}
    std::string GetDescription() override{return description;}
    unsigned GetLEDCount() override{return count;}
    void SetCustomMode() override{modeThread=std::this_thread::get_id();}
    void SetColor(unsigned,RGBColor) override{writeThread=std::this_thread::get_id();}
    void UpdateLEDs() override{std::lock_guard lock(mutex);++submissions;changed.notify_all();}
};
struct RuntimeApi : StubOpenRGBPluginAPIInterface {
    std::vector<RGBControllerInterface*> devices;
    std::vector<RGBControllerInterface*> GetRGBControllers() override{return devices;}
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir directory;auto path=std::filesystem::path(directory.path().toStdWString())/"office-ambience.json";
        {std::ofstream file(path);file<<office::Json{{"monitor","primary"},{"fps",15},{"smoothing_ms",0}};}
        RuntimeDevice strip("Govee H6609",20),bars("Govee H8048",20),keys("Keychron Q6 HE ISO",109),laptop("Lenovo Legion 7 Gen 8",109),desk("Secretlab",48,"NL72S2"),mouse("G502 X PLUS",8);
        RuntimeApi api;api.devices={&strip,&bars,&keys,&laptop,&desk,&mouse};auto gui=std::this_thread::get_id();
        {
            OfficeRuntime runtime(&api,path);QObject recipient;auto dispatch=runtime.dispatch(&recipient);bool replied=false;
            dispatch({{"command","preview_standby"}},[&](const office::Json& value){if(!value.value("ok",false))throw std::runtime_error("Typed command rejected");replied=true;});
            // Diagnostic deadline only; it does not cancel the worker, capture
            // or output operation. Teardown is always the actual worker exit.
            std::unique_lock lock(strip.mutex);
            bool output=strip.changed.wait_for(lock,std::chrono::seconds(5),[&]{return strip.submissions>0;});lock.unlock();
            if(!output || strip.modeThread==gui || strip.writeThread==gui || strip.modeThread!=strip.writeThread)throw std::runtime_error("Blocked GUI prevented output, or engine OS/device setup ran on another thread");
            if(replied)throw std::runtime_error("GUI callback ran without the GUI event loop");
            QCoreApplication::processEvents();if(!replied)throw std::runtime_error("Typed command reply did not reach GUI after it resumed");
            runtime.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED);
            api.devices.clear();runtime.resourcesUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE);
            auto gone=new QObject;auto drop=runtime.dispatch(gone);delete gone;drop({{"command","snapshot"}},[](const office::Json&){throw std::runtime_error("Callback reached destroyed UI");});
        }
        std::cout<<"Independent output with stalled GUI, worker affinity, synchronous removal and graceful worker exit passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
