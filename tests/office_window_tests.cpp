#include "../OfficeWindow.h"
#include "../OfficeLightController.h"
#include <QApplication>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScreen>

// Native composition evidence is limited to the generated fixture on the hosted
// CI desktop. Local execution never captures the user's display.
void acrylicFlyout(QApplication& app){
    using office::Json;
    class Stripes final:public QWidget {
        void paintEvent(QPaintEvent*) override {QPainter p(this);for(int x=0;x<width();++x)p.fillRect(x,0,1,height(),x%2?Qt::white:Qt::black);}
    } background;
    background.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint);background.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());background.show();background.repaint();app.processEvents();
    officeui::LightController mixer(nullptr,{},{});
    Json devices=Json::array();for(auto id:{"H6609","H8048","Secretlab","Keychron","Legion","Logitech"})devices.push_back({{"id",id},{"name",id},{"online",true},{"value",100}});
    mixer.setState({{"master",100},{"devices",devices}});
    auto screen=QGuiApplication::primaryScreen();const auto area=screen->availableGeometry();
    mixer.showAt(QRect(area.right()-40,area.bottom()-35,30,30));mixer.findChild<QToolButton*>("lightCollapse")->click();app.processEvents();mixer.repaint();
    auto hwnd=reinterpret_cast<HWND>(mixer.winId());
    require(mixer.materialStatus().at("active").get<bool>(),mixer.materialStatus().dump().c_str());
    require((GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_LAYERED)!=0,"Acrylic must preserve Qt's layered raster window");
    officeui::flyoutMaterial::AccentPolicy policy;
    require(officeui::flyoutMaterial::read(hwnd,policy)&&policy.state==4&&policy.color==0xb2181b08,"Native acrylic policy/tint readback differs");
    require(!mixer.mask().contains({0,0})&&mixer.mask().contains(mixer.rect().center()),"Acrylic is not clipped to the rounded surface");
    QImage veil(mixer.size(),QImage::Format_ARGB32_Premultiplied);veil.fill(Qt::transparent);mixer.render(&veil);
    require(veil.pixelColor(30,mixer.height()-18).alpha()>0&&veil.pixelColor(30,mixer.height()-18).alpha()<255,"Opaque UI paint hides native blur, or empty alpha loses input hit testing");
    Json evidence={{"native_policy",policy.state},{"tint_abgr",policy.color},{"layered_window",true},{"rounded_mask",true},{"body_alpha",veil.pixelColor(30,mixer.height()-18).alpha()},{"compositor_pixels_checked",false}};
    const auto output=qEnvironmentVariable("OFFICE_UI_EVIDENCE");
    auto render=[&](const char* state){if(output.isEmpty())return;require(QDir().mkpath(output),"Cannot create native material render directory");for(double scale:{1.,1.5,2.}){QImage image(QSize(qRound(mixer.width()*scale),qRound(mixer.height()*scale)),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(scale);image.fill(Qt::transparent);mixer.render(&image);require(image.save(output+QString("/material-veil-%1-%2.png").arg(state).arg(qRound(scale*100))),"Cannot retain native material paint plane");}};
    render("collapsed");
    if(qEnvironmentVariable("GITHUB_ACTIONS")=="true"){
        auto own=[&](QPoint point){auto root=GetAncestor(WindowFromPoint(POINT{qRound(point.x()*mixer.devicePixelRatioF()),qRound(point.y()*mixer.devicePixelRatioF())}),GA_ROOT);DWORD process{};GetWindowThreadProcessId(root,&process);return root==hwnd&&process==GetCurrentProcessId();};
        auto grab=[&]{app.processEvents();mixer.repaint();require(SUCCEEDED(DwmFlush()),"Native compositor did not present fixture");auto r=mixer.frameGeometry();require(own(mixer.mapToGlobal(QPoint(40,mixer.height()-18))),"Compositor evidence target is not the owned lighting widget");auto image=screen->grabWindow(0,r.x(),r.y(),r.width(),r.height()).toImage();require(!image.isNull(),"CI fixture compositor capture unavailable");return image;};
        auto blurred=grab();require(officeui::flyoutMaterial::set(hwnd,false),"Cannot establish clear native fixture reference");auto clear=grab();
        auto contrast=[&](const QImage& image){const auto scale=image.width()/double(mixer.width());const int y=qRound((mixer.height()-18)*scale);double sum=0;int count=0;for(int x=qRound(24*scale);x<qRound(90*scale)-1;++x){sum+=std::abs(image.pixelColor(x,y).lightness()-image.pixelColor(x+1,y).lightness());++count;}return sum/count;};
        double baseline=contrast(clear),frosted=contrast(blurred);evidence["clear_edge_contrast"]=baseline;evidence["blurred_edge_contrast"]=frosted;
        if(!output.isEmpty()){require(QDir().mkpath(output),"Cannot create compositor evidence directory");require(blurred.save(output+"/native-acrylic.png")&&clear.save(output+"/native-clear-reference.png"),"Cannot retain owned compositor fixture");QFile file(output+"/native-material.json");require(file.open(QIODevice::WriteOnly),"Cannot retain compositor metrics");file.write(QByteArray::fromStdString(evidence.dump(2)));}
        require(baseline>30,"CI display did not expose the generated clear stripe fixture");
        require(frosted<baseline*.3,"Native material did not blur the generated background");
        evidence["compositor_pixels_checked"]=true;
        require(officeui::flyoutMaterial::set(hwnd,true),"Cannot restore fixture acrylic");
    }
    auto edge=mixer.frameGeometry().bottomRight();auto pin=mixer.findChild<QToolButton*>("lightPin");pin->click();app.processEvents();
    hwnd=reinterpret_cast<HWND>(mixer.winId());require(officeui::flyoutMaterial::read(hwnd,policy)&&policy.state==4,"Pinning lost native acrylic");
    require(mixer.frameGeometry().bottomRight()==edge,"Pinning acrylic lost tray anchor");
    mixer.findChild<QToolButton*>("lightCollapse")->click();app.processEvents();require(mixer.frameGeometry().bottomRight()==edge&&!mixer.mask().contains({0,0}),"Expanding acrylic lost anchor or rounded clipping");render("expanded");
    mixer.hide();app.processEvents();
    require(!mixer.materialStatus().at("active").get<bool>()&&officeui::flyoutMaterial::read(hwnd,policy)&&policy.state==0,"Hidden widget retained native acrylic");
    mixer.showAt(QRect(area.right()-40,area.bottom()-35,30,30));app.processEvents();require(mixer.materialStatus().at("active").get<bool>(),"Reopen failed to restore native acrylic");
    mixer.hide();background.hide();
    if(!output.isEmpty()){QFile file(output+"/native-material.json");require(file.open(QIODevice::WriteOnly),"Cannot retain final compositor metrics");file.write(QByteArray::fromStdString(evidence.dump(2)));}
}

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
        acrylicFlyout(app);
        std::cout<<"Native caption and acrylic policy/compositor/lifecycle checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
