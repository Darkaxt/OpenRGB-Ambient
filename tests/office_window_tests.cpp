#include "../OfficeWindow.h"
#include "../OfficeLightController.h"
#include <QApplication>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QPaintEvent>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

// Observe normal show/paint dispatch. Do not render/grab/repaint the QWidget:
// those calls can repair a partial first presentation and mask this regression.
void firstFlyoutFrame(QApplication& app){
    using office::Json;
    class FirstShow final:public QObject{
    public:
        QRect shown;int shows=0;QRegion painted;
        bool eventFilter(QObject* object,QEvent* event) override{
            if(event->type()==QEvent::Show){shown=static_cast<QWidget*>(object)->frameGeometry();++shows;}
            if(event->type()==QEvent::Paint)painted+=static_cast<QPaintEvent*>(event)->region();
            return false;
        }
    } observed;
    QWidget backdrop;backdrop.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint);
    backdrop.setAutoFillBackground(true);auto palette=backdrop.palette();palette.setColor(QPalette::Window,QColor("#263a45"));backdrop.setPalette(palette);
    backdrop.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());backdrop.show();app.processEvents();
    QWidget owner;owner.resize(1000,700); // actual mixer has a hidden main-window owner
    officeui::LightController mixer(&owner,{},{});mixer.installEventFilter(&observed);
    Json devices=Json::array();
    for(auto id:{"Logitech","Keychron","Legion","H6609","H8048","Secretlab"})devices.push_back({{"id",id},{"name",id},{"online",true},{"value",100}});
    mixer.setState({{"master",100},{"devices",Json::array({devices[0]})}});
    app.processEvents(); // hidden inventory refresh, not a visible warm-up
    mixer.setState({{"master",100},{"devices",devices}});
    auto screen=QGuiApplication::primaryScreen();const auto area=screen->availableGeometry();
    const QRect anchor(area.right()-40,area.bottom()-12,30,10);
    const auto output=qEnvironmentVariable("OFFICE_UI_EVIDENCE");
    Json evidence=Json::array();
    for(int cycle=0;cycle<2;++cycle){
        observed.painted={};mixer.showAt(anchor);const auto prepared=mixer.frameGeometry();
        require(observed.shown==prepared,"Flyout was shown before its final geometry/anchor was prepared");
        app.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::UpdateRequest);app.processEvents();
        require(mixer.frameGeometry()==prepared,"First queued paint changed flyout placement");
        require(observed.painted.contains(QPoint(mixer.width()-25,25))&&observed.painted.contains(QPoint(mixer.width()-25,mixer.height()-20)),"First queued paint omitted the right or bottom surface");
        auto master=mixer.findChild<QSlider*>("lightSlider-master");auto footer=mixer.findChild<QPushButton*>("lightOpenPanel");
        require(master->isVisible()&&footer->isVisible()&&mixer.rect().contains(footer->mapTo(&mixer,footer->rect().center())),"First show lost master or footer layout");
        auto hwnd=reinterpret_cast<HWND>(mixer.winId());RECT client{};GetClientRect(hwnd,&client);
        require(std::abs(client.right-qRound(mixer.width()*mixer.devicePixelRatioF()))<=1,"Native surface does not match logical widget DPI");
        Json row={{"cycle",cycle},{"shows",observed.shows},{"dpr",mixer.devicePixelRatioF()},{"bounds",{prepared.x(),prepared.y(),prepared.width(),prepared.height()}},{"native_client",{client.right,client.bottom}},{"paint_right_bottom",true},{"shown_at_final_geometry",true}};
        if(qEnvironmentVariable("GITHUB_ACTIONS")=="true"){
            require(SUCCEEDED(DwmFlush()),"First frame compositor flush failed");
            const auto native=mixer.mapToGlobal(mixer.rect().center())*mixer.devicePixelRatioF();DWORD pid{};
            auto visible=GetAncestor(WindowFromPoint(POINT{native.x(),native.y()}),GA_ROOT);GetWindowThreadProcessId(visible,&pid);
            require(visible==hwnd&&pid==GetCurrentProcessId(),"First-frame evidence is not the owned fixture");
            auto image=screen->grabWindow(0,prepared.x(),prepared.y(),prepared.width(),prepared.height()).toImage();
            require(!image.isNull(),"First-frame compositor image unavailable");
            const auto point=master->mapTo(&mixer,QPoint(master->width()-11,master->height()/2));
            const double scale=image.width()/double(mixer.width());int green=0;
            for(int y=qRound(point.y()*scale)-5;y<=qRound(point.y()*scale)+5;++y)for(int x=qRound(point.x()*scale)-5;x<=qRound(point.x()*scale)+5;++x){auto pixel=image.pixelColor(x,y);if(pixel.green()>pixel.red()+50&&pixel.green()>pixel.blue()+15)++green;}
            row["right_master_green_pixels"]=green;
            const auto footerRect=QRect(footer->mapTo(&mixer,QPoint()),footer->size());int text=0;
            for(int y=qRound(footerRect.top()*scale);y<qRound(footerRect.bottom()*scale);++y)for(int x=qRound(footerRect.left()*scale);x<qRound(footerRect.right()*scale);++x){auto pixel=image.pixelColor(x,y);if(pixel.red()>150&&pixel.green()>160&&pixel.blue()>150)++text;}
            row["footer_text_pixels"]=text;
            if(!output.isEmpty()){QDir().mkpath(output);image.save(output+QString("/first-show-%1.png").arg(cycle));}
            require(green>10,"First composed frame is missing the master slider at the right edge");
            require(text>50,"First composed frame is missing the footer below the old surface bounds");
        }
        evidence.push_back(row);mixer.hide();app.processEvents();
    }
    if(!output.isEmpty()){QFile file(output+"/first-show.json");require(file.open(QIODevice::WriteOnly),"Cannot retain first-show geometry");file.write(QByteArray::fromStdString(evidence.dump(2)));}
}

// Native composition evidence is limited to the generated fixture on the hosted
// CI desktop. Local execution never captures the user's display.
void acrylicFlyout(QApplication& app){
    using office::Json;
    class Stripes final:public QWidget {
    public:
        QRect target;
        void paintEvent(QPaintEvent*) override {QPainter p(this);for(int x=0;x<width();++x){auto color=x<target.center().x()?QColor(220,40,40):QColor(40,40,220);p.fillRect(x,0,1,height(),x%2?color.lighter(130):color.darker(130));}}
    } background;
    background.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint);background.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());background.show();background.repaint();app.processEvents();
    officeui::LightController mixer(nullptr,{},{});
    Json devices=Json::array();for(auto id:{"H6609","H8048","Secretlab","Keychron","Legion","Logitech"})devices.push_back({{"id",id},{"name",id},{"online",true},{"value",100}});
    mixer.setState({{"master",100},{"devices",devices}});
    auto screen=QGuiApplication::primaryScreen();const auto area=screen->availableGeometry();
    mixer.showAt(QRect(area.right()-40,area.bottom()-35,30,30));mixer.findChild<QToolButton*>("lightCollapse")->click();app.processEvents();mixer.repaint();
    background.target=mixer.frameGeometry().translated(-background.pos());background.repaint();app.processEvents();
    auto hwnd=reinterpret_cast<HWND>(mixer.winId());
    require(mixer.materialStatus().at("active").get<bool>(),mixer.materialStatus().dump().c_str());
    require((GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_LAYERED)!=0,"Acrylic must preserve Qt's layered raster window");
    require(mixer.materialStatus().at("native_set_accepted")==true&&mixer.materialStatus().at("last_accepted_accent")==4,"Native compositor rejected acrylic");
    require(!mixer.mask().contains({0,0})&&mixer.mask().contains(mixer.rect().center()),"Acrylic is not clipped to the rounded surface");
    QImage veil(mixer.size(),QImage::Format_ARGB32_Premultiplied);veil.fill(Qt::transparent);mixer.render(&veil);
    require(veil.pixelColor(30,mixer.height()-18).alpha()>0&&veil.pixelColor(30,mixer.height()-18).alpha()<255,"Opaque UI paint hides native blur, or empty alpha loses input hit testing");
    Json evidence={{"accepted_accent",4},{"requested_tint_abgr",0xb2181b08},{"layered_window",true},{"rounded_mask",true},{"body_alpha",veil.pixelColor(30,mixer.height()-18).alpha()},{"compositor_pixels_checked",false}};
    const auto output=qEnvironmentVariable("OFFICE_UI_EVIDENCE");
    auto render=[&](const char* state){if(output.isEmpty())return;require(QDir().mkpath(output),"Cannot create native material render directory");for(double scale:{1.,1.5,2.}){QImage image(QSize(qRound(mixer.width()*scale),qRound(mixer.height()*scale)),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(scale);image.fill(Qt::transparent);mixer.render(&image);require(image.save(output+QString("/material-veil-%1-%2.png").arg(state).arg(qRound(scale*100))),"Cannot retain native material paint plane");}};
    render("collapsed");
    if(qEnvironmentVariable("GITHUB_ACTIONS")=="true"){
        auto own=[&](QPoint point){auto root=GetAncestor(WindowFromPoint(POINT{qRound(point.x()*mixer.devicePixelRatioF()),qRound(point.y()*mixer.devicePixelRatioF())}),GA_ROOT);DWORD process{};GetWindowThreadProcessId(root,&process);return root==hwnd&&process==GetCurrentProcessId();};
        auto grab=[&]{app.processEvents();mixer.repaint();require(SUCCEEDED(DwmFlush()),"Native compositor did not present fixture");auto r=mixer.frameGeometry();require(own(mixer.mapToGlobal(QPoint(40,mixer.height()-18))),"Compositor evidence target is not the owned lighting widget");auto image=screen->grabWindow(0,r.x(),r.y(),r.width(),r.height()).toImage();require(!image.isNull(),"CI fixture compositor capture unavailable");return image;};
        auto blurred=grab();require(officeui::flyoutMaterial::set(hwnd,false),"Cannot establish clear native fixture reference");auto clear=grab();
        auto contrast=[&](const QImage& image){const auto scale=image.width()/double(mixer.width());const int y=qRound((mixer.height()-18)*scale);double sum=0;int count=0;for(int x=qRound(24*scale);x<qRound(90*scale)-1;++x){sum+=std::abs(image.pixelColor(x,y).lightness()-image.pixelColor(x+1,y).lightness());++count;}return sum/count;};
        double baseline=contrast(clear),frosted=contrast(blurred);evidence["clear_edge_contrast"]=baseline;evidence["blurred_edge_contrast"]=frosted;
        auto scale=blurred.width()/double(mixer.width());auto left=blurred.pixelColor(qRound(40*scale),qRound((mixer.height()-18)*scale)),right=blurred.pixelColor(qRound((mixer.width()-40)*scale),qRound((mixer.height()-18)*scale));
        evidence["left_rgb"]={left.red(),left.green(),left.blue()};evidence["right_rgb"]={right.red(),right.green(),right.blue()};
        if(!output.isEmpty()){require(QDir().mkpath(output),"Cannot create compositor evidence directory");require(blurred.save(output+"/native-acrylic.png")&&clear.save(output+"/native-clear-reference.png"),"Cannot retain owned compositor fixture");QFile file(output+"/native-material.json");require(file.open(QIODevice::WriteOnly),"Cannot retain compositor metrics");file.write(QByteArray::fromStdString(evidence.dump(2)));}
        require(baseline>30,"CI display did not expose the generated clear stripe fixture");
        require(frosted<baseline*.12,"Native material did not blur the generated background");
        require(left.red()-left.blue()>15&&right.blue()-right.red()>15,"Acrylic became opaque instead of preserving blurred background colours");
        evidence["compositor_pixels_checked"]=true;
        require(officeui::flyoutMaterial::set(hwnd,true),"Cannot restore fixture acrylic");
    }
    auto edge=mixer.frameGeometry().bottomRight();auto pin=mixer.findChild<QToolButton*>("lightPin");pin->click();app.processEvents();
    hwnd=reinterpret_cast<HWND>(mixer.winId());require(mixer.materialStatus().at("native_set_accepted")==true&&mixer.materialStatus().at("last_accepted_accent")==4,"Pinning lost native acrylic");
    require(mixer.frameGeometry().bottomRight()==edge,"Pinning acrylic lost tray anchor");
    mixer.findChild<QToolButton*>("lightCollapse")->click();app.processEvents();require(mixer.frameGeometry().bottomRight()==edge&&!mixer.mask().contains({0,0}),"Expanding acrylic lost anchor or rounded clipping");render("expanded");
    mixer.hide();app.processEvents();
    require(!mixer.materialStatus().at("active").get<bool>()&&mixer.materialStatus().at("native_set_accepted")==true&&mixer.materialStatus().at("last_accepted_accent")==0,"Hidden widget retained native acrylic");
    mixer.showAt(QRect(area.right()-40,area.bottom()-35,30,30));app.processEvents();require(mixer.materialStatus().at("active").get<bool>(),"Reopen failed to restore native acrylic");
    mixer.hide();background.hide();
    if(!output.isEmpty()){QFile file(output+"/native-material.json");require(file.open(QIODevice::WriteOnly),"Cannot retain final compositor metrics");file.write(QByteArray::fromStdString(evidence.dump(2)));}
}

int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        if(app.arguments().contains("--flyout-only")){officeui::loadFonts();firstFlyoutFrame(app);return 0;}
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
        firstFlyoutFrame(app);acrylicFlyout(app);
        std::cout<<"Native caption and acrylic policy/compositor/lifecycle checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
