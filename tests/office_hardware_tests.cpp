#include "../OfficeHardware.h"
#include "interface_stubs.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
struct Hardware : StubRGBControllerInterface {
    unsigned updates=0,custom=0,speed=2,active=0;RGBColor color=0;
    std::string GetName() override {return "Hardware";}
    unsigned GetLEDCount() override {return 2;}
    unsigned GetModeCount() override {return 2;}
    std::string GetModeName(unsigned m) override {return m?"Pulse":"Direct";}
    unsigned GetModeFlags(unsigned m) override {return m?MODE_FLAG_HAS_SPEED|MODE_FLAG_HAS_MODE_SPECIFIC_COLOR:MODE_FLAG_HAS_PER_LED_COLOR;}
    unsigned GetModeSpeed(unsigned) override {return speed;}
    unsigned GetModeSpeedMin(unsigned) override {return 1;}
    unsigned GetModeSpeedMax(unsigned) override {return 5;}
    unsigned GetModeColorsMin(unsigned) override {return 1;}
    unsigned GetModeColorsMax(unsigned) override {return 1;}
    unsigned GetModeColorsCount(unsigned) override {return 1;}
    RGBColor GetModeColor(unsigned,unsigned) override {return color;}
    unsigned GetModeColorMode(unsigned m) override {return m?MODE_COLORS_MODE_SPECIFIC:MODE_COLORS_PER_LED;}
    int GetActiveMode() override {return active;}
    void SetActiveMode(int m) override {active=m;}
    void SetModeSpeed(unsigned,unsigned v) override {speed=v;}
    void SetModeColor(unsigned,unsigned,RGBColor c) override {color=c;}
    void UpdateMode() override {++updates;}
    void SetCustomMode() override {++custom;}
};
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir dir;Hardware device;office::HardwareState state(dir.path()+"/hardware.json");
        auto info=state.describe("test",&device,true);
        auto identity=info.at("identity");
        auto mode=info.at("modes").at(1);mode["speed"]=4;mode["colors"]=office::Json::array({ToRGBColor(10,20,30)});
        state.setMode("test",&device,identity,mode);
        require(device.active==1 && device.speed==4 && device.color==ToRGBColor(10,20,30),"Native mode parameters reach actual controller API");
        require(state.hardware("test"),"Native ownership excludes ambience");
        auto before=device.updates;state.output("test",&device,false,false);state.output("test",&device,false,false);
        require(device.updates==before,"Settled hardware effect must not restart every frame");
        state.output("test",&device,true,false);state.output("test",&device,false,false);
        require(device.custom==1 && device.updates==before+1,"Rest/test switches to direct once and restores hardware effect once");
        auto bad=mode;bad["speed"]=99;bool rejected=false;
        try{state.setMode("test",&device,identity,bad);}catch(const std::exception&){rejected=true;}
        require(rejected && device.speed==4,"Invalid parameter rejected before hardware mutation");
        rejected=false;try{state.setMode("test",&device,"stale",mode);}catch(const std::exception&){rejected=true;}
        require(rejected,"Stale device identity rejected");
        office::HardwareState restarted(dir.path()+"/hardware.json");restarted.reconnected("test");restarted.output("test",&device,false,false);
        require(restarted.hardware("test") && device.active==1,"Persist selected owner and mode across restart/reconnect");
        state.ambience("test",&device);require(!state.hardware("test"),"Explicit return to ambience releases native ownership");
        std::cout<<"Hardware mode ownership, validation, persistence and rest restoration passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
