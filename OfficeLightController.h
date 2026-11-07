#pragma once
#include "OfficePanel.h"
#include "OfficeStyle.h"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPointer>
#include <QGridLayout>
#include <QToolButton>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QHideEvent>
#include <QTimer>
#include <QStyle>
#include <algorithm>
#include <map>

namespace officeui {
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
    QWidget* body=new QWidget;
    QWidget* deviceBody=new QWidget;
    QVBoxLayout* deviceRows=new QVBoxLayout(deviceBody);
    QScrollArea* devices=new QScrollArea;
    QLabel* state=new QLabel;
    QLabel* error=new QLabel;
    QToolButton *pin=new QToolButton,*collapse=new QToolButton;
    std::function<void()> openPanel;
    Row makeRow(const std::string& key,const QString& title,const QString& glyph,QVBoxLayout* parent,bool master=false){
        auto box=new QWidget;box->setObjectName(QString::fromStdString("lightRow-"+key));box->setMinimumHeight(master?72:76);
        auto grid=new QGridLayout(box);grid->setContentsMargins(16,4,16,4);grid->setHorizontalSpacing(12);grid->setVerticalSpacing(0);
        auto name=new QLabel(title);name->setObjectName(QString::fromStdString("lightName-"+key));auto labelFont=QFont("Segoe UI");labelFont.setPixelSize(14);name->setFont(labelFont);name->setMinimumWidth(0);name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);name->setToolTip(title);
        auto mute=new QToolButton;mute->setObjectName(QString::fromStdString("lightMute-"+key));mute->setIcon(icon(glyph,QColor("#ededed")));mute->setIconSize({28,28});mute->setFixedSize(44,44);mute->setCheckable(true);mute->setAccessibleName("Mute "+title);mute->setToolTip("Mute / restore "+title);
        auto slider=new IntensitySlider(box);slider->setObjectName(QString::fromStdString("lightSlider-"+key));slider->setAccessibleName(title+" intensity");slider->setToolTip(title+" intensity");
        auto value=new QLabel;value->setObjectName(QString::fromStdString("lightValue-"+key));value->setFixedWidth(42);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);auto f=QFont("Segoe UI");f.setPixelSize(master?24:20);value->setFont(f);
        grid->addWidget(mute,0,0,2,1);grid->addWidget(name,0,1);grid->addWidget(slider,1,1);grid->addWidget(value,0,2,2,1);grid->setColumnStretch(1,1);parent->addWidget(box);
        connect(slider,&QSlider::valueChanged,this,[this,key,value,mute](int level){value->setText(QString::number(level));mute->setChecked(level==0);if(level>0)remembered[key]=level;if(!applying){pending[key]=level;flush();}});
        connect(mute,&QToolButton::clicked,this,[this,key,slider]{slider->setValue(slider->value()?0:remembered.contains(key)?remembered.at(key):100);});
        return {box,name,mute,slider,value};
    }
    void flush(){
        if(busy||pending.empty()||!dispatch)return;
        const auto [key,value]=*pending.begin();pending.erase(pending.begin());busy=true;inFlight=key;
        Json request={{"command","intensity"},{"target",key=="master"?"master":key=="fullscreen"?"fullscreen":"device"},{"value",value}};
        if(key!="master"&&key!="fullscreen")request["device"]=key;
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
    void fit(QScreen* screen=nullptr){
        if(!isVisible())return;
        if(!screen)screen=QGuiApplication::screenAt(frameGeometry().center());if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        const auto available=screen->availableGeometry();
        const int content=deviceRows->sizeHint().height();devices->setFixedHeight(content);adjustSize();
        const int overhead=height()-content;devices->setFixedHeight(std::min(content,std::max(76,available.height()-overhead-12)));
        adjustSize();move(std::clamp(x(),available.left(),std::max(available.left(),available.right()-width()+1)),std::clamp(y(),available.top(),std::max(available.top(),available.bottom()-height()+1)));
    }
protected:
    void changeEvent(QEvent* event) override {QFrame::changeEvent(event);if(event->type()==QEvent::ActivationChange&&!isActiveWindow()&&!pinned)hide();}
    void keyPressEvent(QKeyEvent* event) override {if(event->key()==Qt::Key_Escape){hide();event->accept();}else QFrame::keyPressEvent(event);}
public:
    LightController(QWidget* parent,OfficeDispatch send,std::function<void()> open):QFrame(parent,Qt::Tool|Qt::FramelessWindowHint),dispatch(std::move(send)),openPanel(std::move(open)){
        setObjectName("lightingFlyout");setWindowTitle("Lighting intensity");setAccessibleName("Lighting intensity mixer");setFixedWidth(420);setAttribute(Qt::WA_TranslucentBackground);setFont(QFont("Segoe UI",10));setEnabled(false);
        setStyleSheet(R"(
            QFrame#lightingFlyout{background:#202020;border:1px solid #626262;border-radius:12px;}
            QWidget{color:#f4f4f4;background:transparent;}
            QWidget#lightHeader{background:#384b64;border-top-left-radius:11px;border-top-right-radius:11px;}
            QLabel{border:0;background:transparent;}
            QToolButton,QPushButton{background:transparent;color:#f4f4f4;border:0;border-radius:4px;padding:0;}
            QToolButton:hover,QPushButton:hover{background:#ffffff20;}
            QToolButton:checked{background:#ffffff18;}
            QToolButton:focus,QPushButton:focus,QSlider:focus{border:1px solid #dedede;}
            QSlider::groove:horizontal{height:5px;background:#4d4d4d;border-radius:2px;}
            QSlider::sub-page:horizontal{background:#0089d8;border-radius:2px;}
            QSlider::handle:horizontal{background:#0089d8;width:18px;margin:-7px 0;border-radius:9px;}
            QScrollArea{border:0;background:transparent;}
            QLabel#lightError{color:#ffd194;padding:8px;}
        )");
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(1,1,1,10);outer->setSpacing(0);
        auto header=new QWidget;header->setObjectName("lightHeader");auto head=new QVBoxLayout(header);head->setContentsMargins(0,8,0,4);head->setSpacing(0);
        auto title=new QHBoxLayout;title->setContentsMargins(16,0,10,0);auto name=new QLabel("Lighting");auto font=QFont("Segoe UI");font.setPixelSize(18);name->setFont(font);title->addWidget(name,1);
        pin->setText(QString(QChar(0xE718)));pin->setFont(QFont("Segoe MDL2 Assets",11));pin->setObjectName("lightPin");pin->setFixedSize(36,36);pin->setCheckable(true);pin->setAccessibleName("Pin lighting mixer");pin->setToolTip("Keep open");
        collapse->setText(QString(QChar(0xE70E)));collapse->setFont(QFont("Segoe MDL2 Assets",10));collapse->setObjectName("lightCollapse");collapse->setFixedSize(36,36);collapse->setAccessibleName("Collapse devices");collapse->setToolTip("Collapse / expand devices");title->addWidget(pin);title->addWidget(collapse);head->addLayout(title);
        rows.emplace("master",makeRow("master","All lights","settings_brightness",head,true));outer->addWidget(header);
        auto content=new QVBoxLayout(body);content->setContentsMargins(0,0,0,0);content->setSpacing(0);
        rows.emplace("fullscreen",makeRow("fullscreen","Fullscreen intensity","fullscreen",content));state->setObjectName("lightFullscreenState");state->setContentsMargins(72,0,16,8);state->setFont(QFont("Segoe UI",9));content->addWidget(state);
        deviceRows->setContentsMargins(0,0,0,0);deviceRows->setSpacing(0);devices->setObjectName("lightDevices");devices->setWidget(deviceBody);devices->setWidgetResizable(true);devices->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);content->addWidget(devices);outer->addWidget(body);
        error->setObjectName("lightError");error->setWordWrap(true);error->hide();outer->addWidget(error);
        auto openButton=new QPushButton("Open Office Ambilight");openButton->setObjectName("lightOpenPanel");openButton->setMinimumHeight(36);openButton->setAccessibleName("Open Office Ambilight");outer->addWidget(openButton);
        connect(openButton,&QPushButton::clicked,this,[this]{hide();if(openPanel)openPanel();});
        connect(pin,&QToolButton::toggled,this,[this](bool on){pinned=on;setWindowFlag(Qt::WindowStaysOnTopHint,on);show();raise();activateWindow();});
        connect(collapse,&QToolButton::clicked,this,[this]{expanded=!expanded;body->setVisible(expanded);collapse->setText(QString(QChar(expanded?0xE70E:0xE70D)));collapse->setAccessibleName(expanded?"Collapse devices":"Expand devices");adjustSize();fit();});
    }
    void setState(const Json& value){
        if(!value.is_object()||!value.contains("devices"))return;
        accepted=value;applying=true;setEnabled(true);
        setLevel("master",value.at("master"));setLevel("fullscreen",value.at("fullscreen"));
        state->setText(value.value("fullscreen_active",false)?"Active on the main display":"Applies to fullscreen apps on the main display");
        for(auto& device:value.at("devices")){
            const auto key=device.at("id").get<std::string>();auto name=QString::fromStdString(device.at("name").get<std::string>());
            if(!rows.contains(key))rows.emplace(key,makeRow(key,name,key=="Keychron"?"keyboard":key=="H6609"?"linear_scale":key=="Secretlab"?"hub":key=="Legion"?"memory":"device_unknown",deviceRows));
            auto& row=rows.at(key);row.name->setText(name+(device.at("online").get<bool>()?"":" · Offline"));row.name->setToolTip(row.name->text());row.box->setProperty("online",device.at("online").get<bool>());setLevel(key,device.at("value"));
        }
        applying=false;fit();
    }
    void showAt(const QRect& anchor){
        auto screen=QGuiApplication::screenAt(anchor.center());if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        setState(accepted);show();fit(screen);
        const auto available=screen->availableGeometry();
        int x=anchor.isEmpty()?available.right()-width()-12:anchor.right()-width();
        int y=anchor.isEmpty()?available.bottom()-height()-12:anchor.top()-height()-8;
        move(std::clamp(x,available.left(),std::max(available.left(),available.right()-width()+1)),std::clamp(y,available.top(),std::max(available.top(),available.bottom()-height()+1)));raise();activateWindow();
    }
};
}
