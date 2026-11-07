#pragma once
#include <shobjidl.h>
#include <QImageReader>
#include <QPainter>
#include <stdexcept>
#include <cmath>
#include <string>
#include <winrt/base.h>

namespace office {
inline QRect wallpaperRect(RECT r) {return {r.left,r.top,r.right-r.left,r.bottom-r.top};}

// Render only the main monitor. In span/tile mode the image origin belongs to
// the virtual desktop, including displays to the left/above the main display.
inline QImage positionWallpaper(const QImage& image,QRect monitor,QRect desktop,
                                DESKTOP_WALLPAPER_POSITION position,COLORREF background) {
    if(image.isNull()||monitor.isEmpty()||desktop.isEmpty())throw std::runtime_error("Windows wallpaper image is unavailable");
    QImage result(monitor.size(),QImage::Format_ARGB32);
    if(result.isNull())throw std::runtime_error("Windows wallpaper image allocation failed");
    result.fill(QColor(GetRValue(background),GetGValue(background),GetBValue(background)));
    QPainter painter(&result);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(-monitor.left(),-monitor.top());
    const QRect area=(position==DWPOS_SPAN||position==DWPOS_TILE)?desktop:monitor;
    if(position==DWPOS_TILE) {
        int x=area.left()+int(std::floor(double(monitor.left()-area.left())/image.width()))*image.width();
        int y=area.top()+int(std::floor(double(monitor.top()-area.top())/image.height()))*image.height();
        for(int top=y;top<=monitor.bottom();top+=image.height())
            for(int left=x;left<=monitor.right();left+=image.width())painter.drawImage(left,top,image);
    } else {
        QSizeF size=image.size();
        if(position==DWPOS_STRETCH)size=area.size();
        else if(position!=DWPOS_CENTER)size.scale(area.size(),position==DWPOS_FIT?Qt::KeepAspectRatio:Qt::KeepAspectRatioByExpanding);
        QRectF target(area.left()+(area.width()-size.width())/2,area.top()+(area.height()-size.height())/2,size.width(),size.height());
        painter.drawImage(target,image);
    }
    return result;
}

inline QImage desktopWallpaperImage(RECT target) {
    winrt::com_ptr<IDesktopWallpaper> wallpaper;
    winrt::check_hresult(CoCreateInstance(__uuidof(DesktopWallpaper),nullptr,CLSCTX_ALL,__uuidof(IDesktopWallpaper),wallpaper.put_void()));
    UINT count{};winrt::check_hresult(wallpaper->GetMonitorDevicePathCount(&count));
    struct ShellString {PWSTR value=nullptr;~ShellString(){CoTaskMemFree(value);}};
    QRect desktop;std::wstring selected;
    for(UINT i=0;i<count;++i) {
        ShellString id;winrt::check_hresult(wallpaper->GetMonitorDevicePathAt(i,&id.value));
        RECT bounds{};winrt::check_hresult(wallpaper->GetMonitorRECT(id.value,&bounds));
        if(IsRectEmpty(&bounds))continue;
        desktop=desktop.united(wallpaperRect(bounds));
        if(EqualRect(&bounds,&target))selected=id.value;
    }
    if(selected.empty())throw std::runtime_error("Main display has no Windows wallpaper association");
    ShellString path;winrt::check_hresult(wallpaper->GetWallpaper(selected.c_str(),&path.value));
    if(!path.value||!*path.value)throw std::runtime_error("Windows has no desktop wallpaper image");
    QImageReader reader(QString::fromWCharArray(path.value));reader.setAutoTransform(true);
    auto image=reader.read();
    if(image.isNull())throw std::runtime_error("Windows desktop wallpaper image could not be decoded");
    DESKTOP_WALLPAPER_POSITION position{};COLORREF color{};
    winrt::check_hresult(wallpaper->GetPosition(&position));winrt::check_hresult(wallpaper->GetBackgroundColor(&color));
    return positionWallpaper(image,wallpaperRect(target),desktop,position,color);
}
}
