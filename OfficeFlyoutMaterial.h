#pragma once
#include <QGuiApplication>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace officeui {
#ifdef Q_OS_WIN
namespace flyoutMaterial {
// The same layered-window acrylic contract used by BetterTrumpet. Qt's raster
// translucent windows use UpdateLayeredWindow; non-layered DWM backdrops cannot
// replace this policy without changing that window's composition model.
struct AccentPolicy {int state=0,flags=0;DWORD color=0;DWORD animation=0;};
struct AttributeData {int attribute=19;void* data=nullptr;UINT size=0;};
using SetAttribute=BOOL(WINAPI*)(HWND,const AttributeData*);
using GetAttribute=BOOL(WINAPI*)(HWND,AttributeData*);
inline SetAttribute setter(){static const auto f=reinterpret_cast<SetAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));return f;}
inline GetAttribute getter(){static const auto f=reinterpret_cast<GetAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowCompositionAttribute"));return f;}
inline bool set(HWND window,bool on){
    auto f=setter();if(!f||!IsWindow(window))return false;
    AccentPolicy policy;if(on){policy.state=4;policy.flags=0x1e0;policy.color=0xb2181b08;}
    AttributeData data;data.data=&policy;data.size=sizeof(policy);return f(window,&data)!=FALSE;
}
inline bool read(HWND window,AccentPolicy& policy){
    auto f=getter();if(!f||!IsWindow(window))return false;
    AttributeData data;data.data=&policy;data.size=sizeof(policy);return f(window,&data)!=FALSE;
}
inline QString unavailableReason(){
    if(QGuiApplication::platformName()!="windows")return "non-native-renderer";
    HIGHCONTRASTW high{};high.cbSize=sizeof(high);
    if(!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(high),&high,0))return "high-contrast-query-failed";
    if(high.dwFlags&HCF_HIGHCONTRASTON)return "high-contrast";
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",QSettings::NativeFormat);
    if(!settings.value("EnableTransparency",1).toBool())return "transparency-disabled";
    BOOL enabled=FALSE;if(FAILED(DwmIsCompositionEnabled(&enabled))||!enabled)return "composition-unavailable";
    return setter()?QString():QString("acrylic-unavailable");
}
}
#endif
}
