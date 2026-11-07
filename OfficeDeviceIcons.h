#pragma once
#include <QIcon>
#include <QImageReader>
#include <QPixmap>
#include <array>
#include <string_view>

namespace officeui {
// Physical identity, rather than the editable display name or connection state.
inline constexpr std::array<std::pair<std::string_view,const char*>,6> deviceIconFiles{{
    {"H6609",":/office/devices/h6609.png"},
    {"H8048",":/office/devices/h8048.png"},
    {"Secretlab",":/office/devices/secretlab.png"},
    {"Keychron",":/office/devices/keychron.png"},
    {"Legion",":/office/devices/legion.png"},
    {"Logitech",":/office/devices/logitech.png"}
}};
inline QIcon deviceIcon(std::string_view id){
    // Keep bounded CPU images in the cache, not GUI pixmaps whose destruction
    // would outlive QApplication. 192px covers the 40px control at high DPI.
    static const auto images=[] {
        std::array<QImage,deviceIconFiles.size()> result;
        for(size_t i=0;i<result.size();++i){
            QImageReader reader(QString::fromLatin1(deviceIconFiles[i].second));
            reader.setScaledSize({192,192});result[i]=reader.read();
        }
        return result;
    }();
    for(size_t i=0;i<deviceIconFiles.size();++i)
        if(deviceIconFiles[i].first==id)return QIcon(QPixmap::fromImage(images[i]));
    return {};
}
}
