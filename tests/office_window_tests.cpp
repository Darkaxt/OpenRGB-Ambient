#include "../OfficeWindow.h"
#include <QApplication>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        officeui::loadFonts();officeui::OfficeWindow window;window.resize(1000,700);window.setMinimumSize(1000,700);
        auto root=new QWidget;auto layout=new QVBoxLayout(root);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto header=new QWidget;header->setFixedHeight(48);auto row=new QHBoxLayout(header);row->setContentsMargins(16,0,0,0);row->addWidget(new QLabel("Office Ambilight"),1);
        auto control=new QPushButton("Existing header action");row->addWidget(control);window.integrateCaption(header,row);layout->addWidget(header);layout->addStretch();window.setCentralWidget(root);window.show();app.processEvents();
        auto hwnd=reinterpret_cast<HWND>(window.winId());auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
        require((style&(WS_THICKFRAME|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX))==(WS_THICKFRAME|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX),"Native Windows caption capabilities lost");
        auto hit=[&](QPoint point){auto global=window.mapToGlobal(point);return SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(qRound(global.x()*window.devicePixelRatioF()),qRound(global.y()*window.devicePixelRatioF())));};
        // Tests run at the runner's default DPI. Direct captionHit separately
        // exercises DIP coordinates used at every exported scale.
        require(hit({1,1})==HTTOPLEFT&&hit({window.width()-2,window.height()-2})==HTBOTTOMRIGHT,"Native resize corners missing");
        require(hit({150,24})==HTCAPTION,"Noninteractive header cannot drag/double-click");
        auto max=window.findChild<QToolButton*>("windowMaximize"),min=window.findChild<QToolButton*>("windowMinimize"),close=window.findChild<QToolButton*>("windowClose");
        require(hit(max->mapTo(&window,max->rect().center()))==HTMAXBUTTON,"Native Windows snap hit test missing");
        require(hit(control->mapTo(&window,control->rect().center()))==HTCLIENT&&hit(close->mapTo(&window,close->rect().center()))==HTCLIENT,"Interactive caption action became draggable");
        require(GetSystemMenu(hwnd,FALSE)!=nullptr,"Windows system menu unavailable");
        SendMessageW(hwnd,WM_SYSCOMMAND,SC_MAXIMIZE,0);app.processEvents();require(IsZoomed(hwnd)&&window.isMaximized(),"Native maximize failed");
        RECT client{};GetClientRect(hwnd,&client);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        require(client.right==monitor.rcWork.right-monitor.rcWork.left&&client.bottom==monitor.rcWork.bottom-monitor.rcWork.top,"Maximized client extends beyond work area");
        require(max->accessibleName()=="Restore","Maximized button did not become Restore");max->click();app.processEvents();require(!IsZoomed(hwnd)&&!window.isMaximized(),"Restore button failed");
        max->click();app.processEvents();require(IsZoomed(hwnd)&&window.isMaximized(),"Maximize button failed");window.hide();app.processEvents();window.showNormal();app.processEvents();require(!IsZoomed(hwnd)&&!window.isMaximized()&&window.isVisible(),"Tray-style reopen did not restore the native window");
        max->click();app.processEvents();office::showWindow(static_cast<QWidget*>(&window));app.processEvents();require(!IsZoomed(hwnd)&&!window.isMaximized(),"Real native activation helper diverges from caption state");
        min->click();app.processEvents();require(IsIconic(hwnd),"Minimize button failed");window.showNormal();app.processEvents();
        SendMessageW(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,0);app.processEvents();require(IsZoomed(hwnd),"Caption double-click did not maximize");window.showNormal();app.processEvents();close->click();app.processEvents();require(!window.isVisible(),"Close button did not dispatch normal close lifecycle");
        std::cout<<"Native caption hit regions, snap, maximize/work-area, restore, minimize and close passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
