#pragma once
// Client-drawn caption integrated with the application header. Windows still
// owns movement, resizing, snapping, system menu and maximize work-area policy.
#include "OfficeStyle.h"
#include <QMainWindow>
#include <QToolButton>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <OfficeWindowActions.h>
#endif

namespace officeui {
class CaptionButton : public QToolButton {
    int kind;
public:
    explicit CaptionButton(int action,QWidget* parent):QToolButton(parent),kind(action) {
        setFixedSize(46,48);setFocusPolicy(Qt::StrongFocus);
        setObjectName(action==0?"windowMinimize":action==1?"windowMaximize":"windowClose");
        setAccessibleName(action==0?"Minimize":action==1?"Maximize":"Close");setToolTip(accessibleName());
    }
    void sync(){if(kind==1){setAccessibleName(window()->isMaximized()?"Restore":"Maximize");setToolTip(accessibleName());}update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        if(underMouse()||property("nativeHover").toBool()||isDown())p.fillRect(rect(),kind==2?QColor("#c42b1c"):QColor("#19303d"));
        p.setPen(QPen(ink,1.2));p.setBrush(Qt::NoBrush);auto c=QPointF(width()/2.,height()/2.);
        if(kind==0)p.drawLine(c+QPointF(-5,0),c+QPointF(5,0));
        else if(kind==2){p.drawLine(c+QPointF(-5,-5),c+QPointF(5,5));p.drawLine(c+QPointF(-5,5),c+QPointF(5,-5));}
        else if(window()->isMaximized()){p.drawRect(QRectF(c.x()-3,c.y()-5,8,8));p.fillRect(QRectF(c.x()-5,c.y()-3,8,8),QColor("#040c14"));p.drawRect(QRectF(c.x()-5,c.y()-3,8,8));}
        else p.drawRect(QRectF(c.x()-5,c.y()-5,10,10));
        if(hasFocus()){p.setPen(QPen(accent,1,Qt::DotLine));p.drawRect(rect().adjusted(5,5,-6,-6));}
    }
};

class OfficeWindow : public QMainWindow {
    QWidget* caption=nullptr;
    CaptionButton* maximize=nullptr;
protected:
    void changeEvent(QEvent* e) override {QMainWindow::changeEvent(e);if(e->type()==QEvent::WindowStateChange&&maximize)maximize->sync();}
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type,void* message,qintptr* result) override {
        auto m=static_cast<MSG*>(message);
        if(caption&&m->message==WM_NCCALCSIZE&&m->wParam){
            auto r=&reinterpret_cast<NCCALCSIZE_PARAMS*>(m->lParam)->rgrc[0];
            if(IsZoomed(m->hwnd)){MONITORINFO info{sizeof(info)};if(GetMonitorInfoW(MonitorFromWindow(m->hwnd,MONITOR_DEFAULTTONEAREST),&info))*r=info.rcWork;}
            *result=0;return true;
        }
        if(caption&&m->message==WM_NCHITTEST){
            RECT r{};GetWindowRect(m->hwnd,&r);POINT pos{GET_X_LPARAM(m->lParam),GET_Y_LPARAM(m->lParam)};
            const auto dpi=GetDpiForWindow(m->hwnd);int border=GetSystemMetricsForDpi(SM_CXSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
            if(!IsZoomed(m->hwnd)){
                bool left=pos.x<r.left+border,right=pos.x>=r.right-border,top=pos.y<r.top+border,bottom=pos.y>=r.bottom-border;
                if(left||right||top||bottom){*result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;return true;}
            }
            ScreenToClient(m->hwnd,&pos);QPoint point(qRound(pos.x/devicePixelRatioF()),qRound(pos.y/devicePixelRatioF()));
            *result=captionHit(point);return true;
        }
        if(maximize&&(m->message==WM_NCMOUSEMOVE||m->message==WM_NCMOUSELEAVE)){
            maximize->setProperty("nativeHover",m->message==WM_NCMOUSEMOVE&&m->wParam==HTMAXBUTTON);maximize->update();
            if(m->message==WM_NCMOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_NONCLIENT|TME_LEAVE,m->hwnd,0};TrackMouseEvent(&track);}
        }
        return QMainWindow::nativeEvent(type,message,result);
    }
#endif
public:
    // Qt 6.8's frameless setWindowState_sys uses MoveWindow rather than
    // restoring WS_MAXIMIZE. Mixing that emulation with native snap/system-menu
    // commands leaves two state owners. All visible caption/tray actions use
    // the same Windows system-command path; Qt observes the resulting WM_SIZE.
    void showNormal(){
#ifdef Q_OS_WIN
        if(caption&&QGuiApplication::platformName()=="windows"){office::showWindow(this,SC_RESTORE);return;}
#endif
        QMainWindow::showNormal();
    }
    void showMaximized(){
#ifdef Q_OS_WIN
        if(caption&&QGuiApplication::platformName()=="windows"){office::showWindow(this,SC_MAXIMIZE);return;}
#endif
        QMainWindow::showMaximized();
    }
    void showMinimized(){
#ifdef Q_OS_WIN
        if(caption&&QGuiApplication::platformName()=="windows"){office::showWindow(this,SC_MINIMIZE);return;}
#endif
        QMainWindow::showMinimized();
    }
    void integrateCaption(QWidget* header,QHBoxLayout* row){
        caption=header;caption->setObjectName("windowHeader");
        for(int i=0;i<3;++i){auto b=new CaptionButton(i,header);row->addWidget(b);if(i==1)maximize=b;
            connect(b,&QToolButton::clicked,this,[this,i]{if(i==0)showMinimized();else if(i==1){if(isMaximized())showNormal();else showMaximized();}else close();});}
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()=="windows"){
            setWindowFlag(Qt::FramelessWindowHint);
            auto hwnd=reinterpret_cast<HWND>(winId());auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
            SetWindowLongPtrW(hwnd,GWL_STYLE,style|WS_THICKFRAME|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX);
            MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(hwnd,&margins);
            SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
#endif
    }
    int captionHit(QPoint point) const {
#ifdef Q_OS_WIN
        if(!caption||!caption->rect().contains(caption->mapFrom(this,point)))return HTCLIENT;
        auto local=caption->mapFrom(this,point);auto child=caption->childAt(local);
        if(child==maximize)return HTMAXBUTTON;
        for(auto w=child;w&&w!=caption;w=w->parentWidget())if(qobject_cast<QAbstractButton*>(w))return HTCLIENT;
        return HTCAPTION;
#else
        Q_UNUSED(point);return 0;
#endif
    }
};
}
