#pragma once
#include "OfficePanel.h"
#include "OfficeStyle.h"
#include "OfficeDeviceIcons.h"
#include "OfficeFlyoutMaterial.h"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPointer>
#include <QGridLayout>
#include <QToolButton>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QStyle>
#include <QMenu>
#include <algorithm>
#include <map>

namespace officeui {
inline void styleLightingMenu(QMenu* menu){
    menu->setAttribute(Qt::WA_TranslucentBackground);menu->setFont(body(16));
    menu->setStyleSheet(R"(
        QMenu{background:#081b18;color:#d4e6df;border:1px solid #28584e;border-radius:11px;padding:6px;}
        QMenu::item{background:transparent;padding:10px 26px 10px 14px;border-radius:6px;}
        QMenu::item:selected{background:#164538;color:#78efc9;}
        QMenu::item:disabled{color:#82988f;}
        QMenu::separator{height:1px;background:#28584e;margin:6px 10px;}
        QMenu::icon{padding-left:6px;}
    )");
    for(auto action:menu->actions())if(action->menu())styleLightingMenu(action->menu());
}
class MixerHeaderButton final : public QToolButton {
    bool pin;
public:
    MixerHeaderButton(bool isPin):pin(isPin){}
protected:
    void paintEvent(QPaintEvent* event) override {
        QToolButton::paintEvent(event);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.translate(rect().center());
        painter.setPen(QPen(QColor("#d4e6df"),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(Qt::NoBrush);
        if(pin){painter.rotate(isChecked()?0:35);QPolygonF shape;shape<<QPointF(-4,-7)<<QPointF(4,-7)<<QPointF(3,-2)<<QPointF(6,2)<<QPointF(-6,2)<<QPointF(-3,-2)<<QPointF(-4,-7);painter.drawPolyline(shape);painter.drawLine(QPointF(0,2),QPointF(0,9));}
        else {const double direction=property("expanded").toBool()?1:-1;QPolygonF shape;shape<<QPointF(-5,2*direction)<<QPointF(0,-3*direction)<<QPointF(5,2*direction);painter.drawPolyline(shape);}
    }
};
// BetterTrumpet's master/header and per-session interaction, adapted to stable
// physical light IDs. All writes use the existing native owner's command path.
class IntensitySlider final : public QSlider {
public:
    IntensitySlider(QWidget* parent):QSlider(Qt::Horizontal,parent){setRange(0,100);setSingleStep(1);setPageStep(10);setMinimumHeight(44);}
protected:
    void wheelEvent(QWheelEvent* event) override {if(!event->angleDelta().y()){event->ignore();return;}setValue(value()+(event->angleDelta().y()>0?2:-2));event->accept();}
    void mousePressEvent(QMouseEvent* event) override {if(event->button()!=Qt::LeftButton){QSlider::mousePressEvent(event);return;}setFocus(Qt::MouseFocusReason);setSliderDown(true);setValue(QStyle::sliderValueFromPosition(0,100,qRound(event->position().x())-9,std::max(1,width()-18)));event->accept();}
    void mouseMoveEvent(QMouseEvent* event) override {if(isSliderDown()){setValue(QStyle::sliderValueFromPosition(0,100,qRound(event->position().x())-9,std::max(1,width()-18)));event->accept();}else QSlider::mouseMoveEvent(event);}
    void mouseReleaseEvent(QMouseEvent* event) override {if(event->button()==Qt::LeftButton&&isSliderDown()){setSliderDown(false);event->accept();}else QSlider::mouseReleaseEvent(event);}
};
class LightController final : public QFrame {
    using Json=office::Json;
    struct Row {QWidget* box;QLabel* name;QToolButton* mute;IntensitySlider* slider;QLabel* value;};
    std::map<std::string,Row> rows;
    std::map<std::string,int> remembered,pending;
    std::string inFlight;
    OfficeDispatch dispatch;
    Json accepted;
    bool applying=false,busy=false,pinned=false,expanded=true;
    bool acrylic=false;
    bool nativeSetAccepted=false;
    int acceptedAccent=0;
    QString materialReason="hidden";
    QRect placementAnchor;
    QWidget* body=new QWidget;
    QWidget* deviceBody=new QWidget;
    QVBoxLayout* deviceRows=new QVBoxLayout(deviceBody);
    QScrollArea* devices=new QScrollArea;
    QLabel* error=new QLabel;
    QWidget* header=new QWidget;
    QToolButton *pin=new MixerHeaderButton(true),*collapse=new MixerHeaderButton(false);
    std::function<void()> openPanel;
    Row makeRow(const std::string& key,const QString& title,QVBoxLayout* parent,bool master=false){
        auto box=new QWidget;box->setObjectName(QString::fromStdString("lightRow-"+key));box->setMinimumHeight(master?72:76);
        auto grid=new QGridLayout(box);grid->setContentsMargins(16,4,16,4);grid->setHorizontalSpacing(12);grid->setVerticalSpacing(0);
        auto name=new QLabel(title);name->setObjectName(QString::fromStdString("lightName-"+key));name->setFont(officeui::body(15));name->setMinimumWidth(0);name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);name->setToolTip(title);
        auto mute=new QToolButton;mute->setObjectName(QString::fromStdString("lightMute-"+key));mute->setIcon(master?icon("settings_brightness",QColor("#45ddb0")):deviceIcon(key));mute->setIconSize(master?QSize(28,28):QSize(40,40));mute->setFixedSize(44,44);mute->setCheckable(true);mute->setAccessibleName("Mute "+title);mute->setToolTip("Mute / restore "+title);
        auto slider=new IntensitySlider(box);slider->setObjectName(QString::fromStdString("lightSlider-"+key));slider->setAccessibleName(title+(key=="fullscreen"?"":" intensity"));slider->setToolTip(slider->accessibleName());
        auto value=new QLabel;value->setObjectName(QString::fromStdString("lightValue-"+key));value->setFixedWidth(42);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);value->setFont(officeui::body(master?26:22));
        grid->addWidget(mute,0,0,2,1);grid->addWidget(name,0,1);grid->addWidget(slider,1,1);grid->addWidget(value,0,2,2,1);grid->setColumnStretch(1,1);parent->addWidget(box);
        connect(slider,&QSlider::valueChanged,this,[this,key,value,mute](int level){value->setText(QString::number(level));mute->setChecked(level==0);if(level>0)remembered[key]=level;if(!applying){pending[key]=level;flush();}});
        connect(mute,&QToolButton::clicked,this,[this,key,slider]{slider->setValue(slider->value()?0:remembered.contains(key)?remembered.at(key):100);});
        return {box,name,mute,slider,value};
    }
    void flush(){
        if(busy||pending.empty()||!dispatch)return;
        const auto [key,value]=*pending.begin();pending.erase(pending.begin());busy=true;inFlight=key;
        Json request={{"command","intensity"},{"target",key=="master"?"master":"device"},{"value",value}};
        if(key!="master")request["device"]=key;
        QPointer<LightController> alive(this);
        dispatch(std::move(request),[alive](const Json& reply){
            if(!alive)return;alive->busy=false;
            if(!reply.value("ok",false)){alive->pending.clear();alive->setState(alive->accepted);alive->error->setText(QString::fromStdString(reply.value("error",std::string("Could not save intensity"))));alive->error->show();return;}
            alive->error->hide();alive->setState(reply.at("result"));alive->flush();
        });
    }
    void setLevel(const std::string& key,double value){
        auto& row=rows.at(key);if(pending.contains(key)||(busy&&inFlight==key)||row.slider->isSliderDown())return;
        row.slider->setValue(qRound(value));row.value->setText(QString::number(qRound(value)));row.mute->setChecked(value==0);
    }
    static void measureLayouts(QWidget* widget){
        // Hidden QWidget parents defer geometry propagation. Activate descendants
        // first, then invalidate their parent's cached item hints/constraints.
        for(auto child:widget->children())if(auto w=qobject_cast<QWidget*>(child);w&&!w->isWindow())measureLayouts(w);
        if(auto layout=widget->layout()){layout->invalidate();layout->activate();}
    }
    void fit(QScreen* screen=nullptr){
        if(!screen)screen=QGuiApplication::screenAt(placementAnchor.isEmpty()?frameGeometry().center():placementAnchor.center());if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        const auto available=screen->availableGeometry();
        measureLayouts(this);
        const int content=deviceRows->sizeHint().height();devices->setFixedHeight(content);measureLayouts(this);
        // QWidget::adjustSize caps an unshown top-level window to 2/3 of its
        // screen, before the queued layout establishes the real minimum size.
        // Derive chrome and viewport from the layout instead of that capped size.
        const int overhead=sizeHint().height()-(expanded?content:0);
        devices->setFixedHeight(std::min(content,std::max(76,available.height()-overhead-12)));
        measureLayouts(this);resize(width(),sizeHint().height());layout()->activate();
        // Size changes grow/shrink away from the tray, preserving the opening
        // anchor rather than retaining the expanded panel's old top edge.
        const int x=placementAnchor.isEmpty()?available.right()-width()-12:placementAnchor.right()-width();
        const int y=placementAnchor.isEmpty()?available.bottom()-height()-12:placementAnchor.top()-height()-8;
        move(std::clamp(x,available.left(),std::max(available.left(),available.right()-width()+1)),std::clamp(y,available.top(),std::max(available.top(),available.bottom()-height()+1)));
    }
    void roundMaterial(){
        if(!acrylic){if(!mask().isEmpty())clearMask();return;}
        QPainterPath shape;shape.addRoundedRect(QRectF(rect()),12,12);
        setMask(QRegion(shape.toFillPolygon().toPolygon()));
    }
    void updateMaterial(bool visible){
        bool active=false;materialReason=visible?"unsupported-platform":"hidden";
#ifdef Q_OS_WIN
        if(visible){
            materialReason=flyoutMaterial::unavailableReason();
            if(materialReason.isEmpty()){
                active=flyoutMaterial::set(reinterpret_cast<HWND>(winId()),true);
                nativeSetAccepted=active;if(active)acceptedAccent=4;
                materialReason=active?"native-acrylic":"native-acrylic-rejected";
            }
        }
        if(!active&&acrylic){nativeSetAccepted=flyoutMaterial::set(reinterpret_cast<HWND>(internalWinId()),false);if(nativeSetAccepted)acceptedAccent=0;}
#endif
        if(active!=acrylic){acrylic=active;roundMaterial();update();}
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor("#28584e"),1));
        painter.setBrush(acrylic?QColor(8,27,24,20):QColor("#081b18"));painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),12,12);
        QPainterPath outline;outline.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),11,11);painter.setClipPath(outline);
        painter.fillRect(header->geometry(),acrylic?QColor(16,51,44,90):QColor("#10332c"));
    }
    void showEvent(QShowEvent* event) override {QFrame::showEvent(event);updateMaterial(true);}
    void hideEvent(QHideEvent* event) override {updateMaterial(false);QFrame::hideEvent(event);}
    void resizeEvent(QResizeEvent* event) override {QFrame::resizeEvent(event);roundMaterial();}
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type,void* message,qintptr* result) override {
        auto native=static_cast<MSG*>(message);
        if(isVisible()&&(native->message==WM_SETTINGCHANGE||native->message==WM_THEMECHANGED||native->message==WM_DWMCOMPOSITIONCHANGED))updateMaterial(true);
        return QFrame::nativeEvent(type,message,result);
    }
#endif
    void changeEvent(QEvent* event) override {QFrame::changeEvent(event);if(event->type()==QEvent::ActivationChange&&!isActiveWindow()&&!pinned)hide();}
    void keyPressEvent(QKeyEvent* event) override {if(event->key()==Qt::Key_Escape){hide();event->accept();}else QFrame::keyPressEvent(event);}
public:
    LightController(QWidget* parent,OfficeDispatch send,std::function<void()> open):QFrame(parent,Qt::Tool|Qt::FramelessWindowHint),dispatch(std::move(send)),openPanel(std::move(open)){
        setObjectName("lightingFlyout");setWindowTitle("Lighting intensity");setAccessibleName("Lighting intensity mixer");setFixedWidth(420);setAttribute(Qt::WA_TranslucentBackground);setFont(officeui::body(15));setEnabled(false);
        setStyleSheet(R"(
            QFrame#lightingFlyout{background:transparent;border:0;}
            QWidget{color:#d4e6df;background:transparent;}
            QWidget#lightHeader{background:transparent;}
            QLabel{border:0;background:transparent;}
            QToolButton,QPushButton{background:transparent;color:#d4e6df;border:0;border-radius:6px;padding:0;}
            QToolButton:hover,QPushButton:hover{background:rgba(255,255,255,20);}
            QToolButton:checked{background:rgba(255,255,255,24);}
            QToolButton:focus,QPushButton:focus,QSlider:focus{border:1px solid #78efc9;}
            QSlider::groove:horizontal{height:5px;background:#34544b;border-radius:2px;}
            QSlider::sub-page:horizontal{background:#45ddb0;border-radius:2px;}
            QSlider::handle:horizontal{background:#45ddb0;width:18px;margin:-7px 0;border-radius:9px;}
            QScrollArea{border:0;background:transparent;}
            QLabel#lightError{color:#ffd194;padding:8px;}
        )");
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(1,1,1,10);outer->setSpacing(0);
        header->setObjectName("lightHeader");auto head=new QVBoxLayout(header);head->setContentsMargins(0,8,0,4);head->setSpacing(0);
        auto title=new QHBoxLayout;title->setContentsMargins(16,0,10,0);auto name=new QLabel("Lighting");name->setFont(officeui::body(20));title->addWidget(name,1);
        pin->setObjectName("lightPin");pin->setFixedSize(36,36);pin->setCheckable(true);pin->setAccessibleName("Pin lighting mixer");pin->setToolTip("Keep open");
        collapse->setProperty("expanded",true);collapse->setObjectName("lightCollapse");collapse->setFixedSize(36,36);collapse->setAccessibleName("Collapse devices");collapse->setToolTip("Collapse / expand devices");title->addWidget(pin);title->addWidget(collapse);head->addLayout(title);
        rows.emplace("master",makeRow("master","All lights",head,true));outer->addWidget(header);
        auto content=new QVBoxLayout(body);content->setContentsMargins(0,0,0,0);content->setSpacing(0);
        deviceRows->setContentsMargins(0,0,0,0);deviceRows->setSpacing(0);devices->setObjectName("lightDevices");devices->setWidget(deviceBody);devices->setWidgetResizable(true);devices->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);content->addWidget(devices);outer->addWidget(body);
        error->setObjectName("lightError");error->setWordWrap(true);error->hide();outer->addWidget(error);
        auto openButton=new QPushButton("Open Office Ambilight");openButton->setObjectName("lightOpenPanel");openButton->setMinimumHeight(36);openButton->setAccessibleName("Open Office Ambilight");outer->addWidget(openButton);
        connect(openButton,&QPushButton::clicked,this,[this]{hide();if(openPanel)openPanel();});
        connect(pin,&QToolButton::toggled,this,[this](bool on){pinned=on;setWindowFlag(Qt::WindowStaysOnTopHint,on);show();fit();raise();activateWindow();});
        connect(collapse,&QToolButton::clicked,this,[this]{expanded=!expanded;body->setVisible(expanded);collapse->setProperty("expanded",expanded);collapse->setAccessibleName(expanded?"Collapse devices":"Expand devices");collapse->update();fit();});
    }
    Json materialStatus() const {return {{"active",acrylic},{"reason",materialReason.toStdString()},{"visible",isVisible()},{"native_set_accepted",nativeSetAccepted},{"last_accepted_accent",acceptedAccent}};}
    void setState(const Json& value){
        if(!value.is_object()||!value.contains("devices"))return;
        accepted=value;applying=true;setEnabled(true);
        setLevel("master",value.at("master"));
        for(auto& device:value.at("devices")){
            const auto key=device.at("id").get<std::string>();auto name=QString::fromStdString(device.at("name").get<std::string>());
            if(!rows.contains(key))rows.emplace(key,makeRow(key,name,deviceRows));
            auto& row=rows.at(key);row.name->setText(name+(device.at("online").get<bool>()?"":" · Offline"));row.name->setToolTip(row.name->text());row.box->setProperty("online",device.at("online").get<bool>());setLevel(key,device.at("value"));
        }
        applying=false;if(isVisible())fit();
    }
    void showAt(const QRect& anchor){
        auto screen=QGuiApplication::screenAt(anchor.center());if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        placementAnchor=anchor;
        setState(accepted);ensurePolished();
        // Bind native DPI and finish layout/placement while hidden. showEvent
        // applies acrylic and its clip to this final surface; showing first
        // published the previous surface before fit resized/moved it.
        winId();windowHandle()->setScreen(screen);deviceRows->activate();layout()->activate();fit(screen);
        show();
        raise();activateWindow();
    }
};
}
