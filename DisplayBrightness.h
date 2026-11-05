#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace office {
using GammaRamp=std::array<std::array<uint16_t,256>,3>;

// A scalar dimmer has linear channel ramps. Colour-temperature attenuation may
// differ between channels: take the strongest channel to avoid changing hue.
// Do not mistake an arbitrary ICC/gamma curve for a brightness percentage.
inline std::optional<double> gammaBrightness(const GammaRamp& ramp) {
    double strongest=0;
    for(const auto& channel:ramp) {
        const double slope=channel[254]/254.;
        for(size_t i=0;i<channel.size();++i)
            // LightBulb perturbs the white endpoint by up to four WORD units
            // to force driver refresh. Quantisation must not cause oscillation.
            if(std::abs(channel[i]-slope*i)>8) return {};
        strongest=std::max(strongest,slope*255/65535.);
    }
    return std::clamp(strongest,0.,1.);
}
struct DisplayBrightness {
    std::string display,error;
    double factor=1;
    bool valid=false;
    void observe(const std::string& name,const std::optional<GammaRamp>& ramp,const std::string& failure={}) {
        if(display!=name) {display=name;factor=1;valid=false;}
        auto gain=ramp?gammaBrightness(*ramp):std::nullopt;
        if(gain) {factor=*gain;valid=true;error.clear();}
        else error=ramp?"Display gamma is not a linear brightness ramp":failure;
    }
    double applied(bool follow) const {return follow?factor:1.;}
};
inline unsigned char dimChannel(unsigned char channel,double factor) {
    return static_cast<unsigned char>(std::clamp(std::lround(channel*factor),0l,255l));
}
}
