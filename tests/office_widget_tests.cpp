// Drive production widgets with Qt events; no host desktop input or RGB writes.
#define OFFICE_WIDGET_TEST
#include "../OfficeControl.cpp"
#include <iostream>
#include <QFontInfo>
#include <QRawFont>
#include <QTest>
#include <QBuffer>
#include <QSignalSpy>
#include <QEventLoop>
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void lightingMixer(QApplication& app){
    Json state={{"revision",1},{"master",100},{"fullscreen",50},{"fullscreen_active",false},{"devices",Json::array()}};
    for(auto [id,name]:std::vector<std::pair<const char*,const char*>>{{"H6609","Govee strip"},{"H8048","Govee light bars"},{"Secretlab","Secretlab desk"},{"Keychron","Keychron Q6 HE"},{"Legion","Legion keyboard"},{"Logitech","G502 X PLUS"}})
        state["devices"].push_back({{"id",id},{"name",name},{"online",std::string(id)!="Keychron"},{"value",100}});
    std::vector<Json> requests;std::vector<OfficeReply> replies;bool opened=false;
    officeui::LightController mixer(nullptr,[&](Json request,OfficeReply reply){requests.push_back(std::move(request));replies.push_back(std::move(reply));},[&]{opened=true;});
    auto respond=[&](size_t index){auto reply=std::move(replies.at(index));reply({{"ok",true},{"result",state}});};
    mixer.setState(state);mixer.showAt({780,730,30,30});app.processEvents();
    auto slider=[&](const char* key){auto value=mixer.findChild<QSlider*>(QString("lightSlider-")+key);require(value!=nullptr,"Lighting slider missing");require(!value->accessibleName().isEmpty()&&value->height()>=44,"Lighting slider lacks accessible name/hit target");return value;};
    auto master=slider("master");require(master->value()==100&&slider("fullscreen")->value()==50,"Lighting percentages not rendered");
    master->setValue(50);master->setValue(20);master->setValue(30);require(requests.size()==1&&requests[0].at("target")=="master","Rapid drag did not serialize native writes");
    state["master"]=50;respond(0);
    require(master->value()==30&&requests.size()==2&&requests[1].at("value")==30,"Old reply overwrote latest slider intent or intermediate value was replayed");
    state["master"]=30;respond(1);
    auto device=slider("H6609");device->setValue(40);require(requests.back().at("target")=="device"&&requests.back().at("device")=="H6609","Device row is not bound to stable physical identity");
    state["devices"][0]["value"]=40;respond(replies.size()-1);
    auto mute=mixer.findChild<QToolButton*>("lightMute-H6609");mute->click();require(requests.back().at("value")==0,"Device mute did not reach native command");
    state["devices"][0]["value"]=0;respond(replies.size()-1);mute->click();require(requests.back().at("value")==40,"Unmute did not restore device level");
    state["devices"][0]["value"]=40;respond(replies.size()-1);
    auto offline=mixer.findChild<QLabel*>("lightName-Keychron");require(offline&&offline->text().contains("Offline")&&slider("Keychron")->isEnabled(),"Offline device is missing or cannot retain a future level");
    state["fullscreen_active"]=true;mixer.setState(state);require(mixer.findChild<QLabel*>("lightFullscreenState")->text().contains("Active"),"Actual fullscreen state missing");
    const auto output=qEnvironmentVariable("OFFICE_UI_EVIDENCE");Json metrics=Json::array();
    auto render=[&](const char* name){
        app.processEvents();
        for(double scale:{1.,1.5,2.}){
            QImage image(QSize(qRound(mixer.width()*scale),qRound(mixer.height()*scale)),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(scale);image.fill(Qt::transparent);mixer.render(&image);
            require(mixer.width()==420&&image.width()==qRound(420*scale),"Flyout width does not follow reference/DPI contract");
            require(image.pixelColor(qRound(10*scale),qRound((mixer.height()-15)*scale))==QColor("#202020"),"Flyout body must be opaque charcoal, not transparent desktop");
            if(!output.isEmpty()){QDir().mkpath(output);require(image.save(output+QString("/%1-%2.png").arg(name).arg(qRound(scale*100))),"Could not retain app-owned mixer rendering");}
        }
        for(auto widget:mixer.findChildren<QSlider*>()){
            const auto bounds=QRect(widget->mapTo(&mixer,QPoint()),widget->size());
            require(widget->width()>=180&&widget->height()>=44,"Mixer slider clipped below usable size");
            metrics.push_back({{"state",name},{"id",widget->objectName().toStdString()},{"accessible_name",widget->accessibleName().toStdString()},{"bounds",{bounds.x(),bounds.y(),bounds.width(),bounds.height()}},{"visible",widget->isVisible()},{"value",widget->value()}});
        }
    };
    render("expanded");auto height=mixer.height();mixer.findChild<QToolButton*>("lightCollapse")->click();app.processEvents();require(mixer.height()<height&&!mixer.findChild<QScrollArea*>("lightDevices")->isVisible(),"Collapse did not compact the flyout");render("collapsed");mixer.findChild<QToolButton*>("lightCollapse")->click();
    auto pin=mixer.findChild<QToolButton*>("lightPin");pin->click();app.processEvents();QApplication::setActiveWindow(&mixer);QApplication::setActiveWindow(nullptr);require(mixer.isVisible(),"Pinned flyout dismissed on deactivation");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&mixer,&escape);require(!mixer.isVisible(),"Escape must dismiss even when pinned");
    mixer.showAt({780,730,30,30});pin->click();app.processEvents();QApplication::setActiveWindow(&mixer);QApplication::setActiveWindow(nullptr);require(!mixer.isVisible(),"Unpinned flyout did not dismiss on click-away");
    mixer.showAt({780,730,30,30});mixer.findChild<QPushButton*>("lightOpenPanel")->click();require(opened&&!mixer.isVisible(),"Open panel action failed");
    if(!output.isEmpty()){QFile file(output+"/metrics.json");require(file.open(QIODevice::WriteOnly),"Cannot save mixer metrics");file.write(QByteArray::fromStdString(metrics.dump(2)));}
}
void mouse(QWidget& widget,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent event(type,point,point,button,buttons,modifiers);QApplication::sendEvent(&widget,&event);
}
void invertedGrid(QApplication& app) {
    MapCanvas canvas;canvas.resize(1048,600);canvas.grid=.25f;
    canvas.preview=QImage(512,216,QImage::Format_RGB32);
    canvas.show();app.processEvents();
    auto render=[&](double scale) {
        QImage output(QSize(qRound(canvas.width()*scale),qRound(canvas.height()*scale)),QImage::Format_ARGB32_Premultiplied);
        output.setDevicePixelRatio(scale);output.fill(Qt::transparent);canvas.render(&output);return output;
    };
    for(double scale:{1.,1.5,2.})for(auto colour:{QColor(12,35,68),QColor(244,217,183)}) {
        canvas.preview.fill(colour);canvas.showGrid=false;auto original=render(scale);
        canvas.showGrid=true;auto grid=render(scale);auto image=canvas.imageGeometry();
        int x=qRound((image.left()+image.width()*.25)*scale),y=qRound((image.top()+image.height()*.25)*scale);
        for(auto point:{QPoint(x,y+8),QPoint(x+8,y),QPoint(x,y)}) {
            auto before=original.pixelColor(point),after=grid.pixelColor(point);
            require(after.red()==255-before.red() && after.green()==255-before.green() && after.blue()==255-before.blue(),"Grid lines and crossings must invert the underlying pixel exactly once");
        }
        require(grid.pixelColor(x+8,y+8)==original.pixelColor(x+8,y+8),"Grid changed pixels between its lines");
        canvas.showGrid=false;require(render(scale)==original,"Disabling grid did not restore the exact preview");
    }
    canvas.preview.fill(QColor(12,35,68));
    for(auto size:{QSize(1048,600),QSize(380,280)})for(double scale:{1.,1.5,2.})for(double step:{.01,.05,.1}) {
        canvas.resize(size);canvas.grid=step;canvas.showGrid=true;auto output=render(scale);auto image=canvas.imageGeometry();
        const QColor inverse(243,220,187);
        int x=qRound((image.left()+image.width()*.003)*scale),y=qRound((image.top()+image.height()*.003)*scale),vertical=0,horizontal=0;
        for(int column=qCeil(image.left()*scale)+1;column<qFloor(image.right()*scale);++column)if(output.pixelColor(column,y)==inverse)++vertical;
        for(int row=qCeil(image.top()*scale)+1;row<qFloor(image.bottom()*scale);++row)if(output.pixelColor(x,row)==inverse)++horizontal;
        require(vertical==qRound(1./step)-1 && horizontal==vertical,"Grid percentage must match the actual divisions at every viewport size and DPI");
    }
    canvas.resize(1048,600);canvas.grid=.01;
    Json layout=Json::array({{{"regions",office::encode(std::vector<Region>{{.25f,.25f,.55f,.65f}})},
        {"groups",Json::array({{{"first",0},{"count",1}}})}}});
    canvas.layout=&layout;auto saved=layout;
    canvas.showGrid=false;auto original=render(1.);canvas.showGrid=true;auto grid=render(1.);
    auto bounds=canvas.selectionGeometry();
    for(auto point:{bounds.topLeft(),bounds.bottomRight(),canvas.rotationHandle()})
        require(grid.pixelColor(point.toPoint())==original.pixelColor(point.toPoint()),"Grid inverted a selection or rotation handle");
    require(layout==saved,"Rendering the grid changed the saved map");
    canvas.snap=true;auto p=canvas.selectionGeometry().center();
    mouse(canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);
    mouse(canvas,QEvent::MouseMove,p+QPointF(14,11),Qt::NoButton,Qt::LeftButton);
    mouse(canvas,QEvent::MouseButtonRelease,p+QPointF(14,11),Qt::LeftButton,Qt::NoButton);
    require(std::abs(layout[0]["regions"][0][0].get<double>()-.26)<.00001 && std::abs(layout[0]["regions"][0][1].get<double>()-.28)<.00001,"Snapping does not match the visible one-percent grid");
}
void multiSelection(QApplication& app) {
    Json original=Json::array();
    for(int d=0;d<2;++d) {
        Json regions=Json::array(),groups=Json::array(),names=Json::array();
        for(int g=0;g<3;++g) {
            float x=.12f+d*.4f,y=.15f+g*.19f;
            regions.push_back(office::encode(Region{x,y,x+.14f,y+.09f}));
            groups.push_back({{"first",g},{"count",1},{"name","Zone "+std::to_string(g)},{"enabled",true}});names.push_back("LED "+std::to_string(g));
        }
        original.push_back({{"id",d==0?"Legion":"Secretlab"},{"name",d==0?"Laptop":"Desk"},{"enabled",true},{"regions",regions},{"groups",groups},{"led_names",names}});
    }
    Panel panel("E:/Temp/openrgb-multiselect-widget-unavailable",{});panel.fixtureLayout(original);panel.show();app.processEvents();
    auto follow=panel.findChild<QCheckBox*>("followDisplayBrightness");
    auto brightnessReadback=panel.findChild<QLabel*>("displayBrightnessStatus");
    require(follow && brightnessReadback && follow->text()=="Follow main display brightness","Display dimming control/readback missing from production panel");
    auto tree=panel.findChild<QTreeWidget*>("topologyTree");auto view=panel.findChild<QComboBox*>("topologyVisibility");
    // MapCanvas has no Q_OBJECT: resolve the actual QWidget by its stable name.
    auto canvas=static_cast<MapCanvas*>(panel.findChild<QWidget*>("topologyCanvas"));
    require(tree && canvas && view,"Production topology controls missing");
    auto previewBlur=panel.findChild<QDoubleSpinBox*>("previewBlur"),topologyBlur=panel.findChild<QDoubleSpinBox*>("topologyBlur");
    require(previewBlur && topologyBlur,"Preview/Topology blur controls are missing");
    previewBlur->setValue(2.5);require(topologyBlur->value()==2.5 && std::abs(canvas->blur-.025)<.00001,"Preview blur does not update the shared topology draft");
    topologyBlur->setValue(1.5);require(previewBlur->value()==1.5,"Topology blur does not update Preview");
    auto previewPage=panel.findChild<QWidget*>("previewPage");auto save=panel.findChild<QPushButton*>("saveChanges");
    auto pageStack=qobject_cast<QStackedWidget*>(previewPage->parentWidget());require(pageStack && save,"Preview apply action missing");
    pageStack->setCurrentWidget(previewPage);app.processEvents();require(save->isVisible() && save->text()=="Apply settings","Preview blur requires leaving the page to apply");
    require(panel.fixtureWantsPreview(),"Visible Preview must request current images");
    auto draftRequest=panel.fixturePreviewRequest();
    require(draftRequest.at("command")=="snapshot" && draftRequest.at("preview")==true && draftRequest.at("preview_blur")==1.5,
        "Unsaved blur must reach the read-only preview request instead of waiting for Apply");
    require(!draftRequest.contains("settings") && !draftRequest.contains("layout") && !draftRequest.value("hardware",false),
        "Preview request must not apply or save the draft");
    auto imageReply=[](double blur,const char* generation,QColor colour) {
        QImage image(32,16,QImage::Format_RGB32);image.fill(colour);QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
        return Json{{"preview_blur",blur},{"preview_generation",generation},{"preview_png",bytes.toBase64().toStdString()}};
    };
    panel.fixturePreviewReply(imageReply(1.5,"first",Qt::red));require(canvas->preview.pixelColor(0,0)==Qt::red,"Matching draft preview did not reach the image widget");
    previewBlur->setValue(10);
    panel.fixturePreviewReply(imageReply(1.5,"stale",Qt::green));require(canvas->preview.pixelColor(0,0)==Qt::red,"An old slider reply replaced the newer draft preview");
    panel.fixturePreviewReply(imageReply(10,"second",Qt::blue));require(canvas->preview.pixelColor(0,0)==Qt::blue,"New draft preview did not update the rendered source");
    require(*canvas->layout==original,"Previewing blur rewrote topology");
    panel.hide();app.processEvents();require(!panel.fixtureWantsPreview(),"Hidden panel continues requesting images");
    require(!panel.fixturePreviewRequest().contains("preview_blur"),"Hidden panel requests draft blur processing");
    panel.show();pageStack->setCurrentIndex(0);app.processEvents();require(!panel.fixtureWantsPreview(),"Control page unnecessarily requests images");
    pageStack->setCurrentIndex(1);previewBlur->setValue(0);
    auto click=[&](QTreeWidgetItem* row,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        tree->scrollToItem(row);app.processEvents();auto box=tree->visualItemRect(row);auto p=QPointF(box.left()+75,box.center().y());
        // QTreeView currentChanged also consults QGuiApplication's modifier
        // state. QPA injection updates it; a direct sendEvent does not, and
        // would falsely reset the Shift range anchor to the clicked row.
        QTest::mouseClick(panel.windowHandle(),Qt::LeftButton,modifiers,tree->viewport()->mapTo(&panel,p.toPoint()));
    };
    auto laptop=tree->topLevelItem(0),desk=tree->topLevelItem(1);
    click(laptop);require(canvas->targets().size()==3,"Device row did not include all laptop zones");
    require(canvas->visible(0,2) && !canvas->visible(1,0),"Selected-device view includes another device");
    click(laptop->child(0));click(laptop->child(2),Qt::ControlModifier);
    require(canvas->targets().size()==2,"Ctrl-click did not retain both zones");
    view->setCurrentIndex(1);require(canvas->visible(0,0) && !canvas->visible(0,1) && canvas->visible(0,2),"Selected-zone view ignored the selection");
    view->setCurrentIndex(2);require(canvas->visible(1,0),"All-device view missing");view->setCurrentIndex(0);
    click(laptop->child(0));click(laptop->child(2),Qt::ShiftModifier);require(canvas->targets().size()==3,"Shift range did not select all laptop zones");
    click(desk->child(0),Qt::ControlModifier);require(canvas->targets().size()==4,"Cross-device selection lost a zone");
    auto before=*canvas->layout;auto box=canvas->selectionGeometry();auto p=box.center();auto image=canvas->imageGeometry();
    mouse(*canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);
    mouse(*canvas,QEvent::MouseMove,p+QPointF(image.width()*.07,image.height()*.04),Qt::NoButton,Qt::LeftButton);
    mouse(*canvas,QEvent::MouseButtonRelease,p+QPointF(image.width()*.07,image.height()*.04),Qt::LeftButton,Qt::NoButton);
    auto moved=*canvas->layout;
    for(auto [d,i]:canvas->targets())for(int c=0;c<4;++c)require(std::abs(moved[d]["regions"][i][c].get<float>()-before[d]["regions"][i][c].get<float>()-(c%2?.04f:.07f))<.00001,"Group drag distorted spacing or dropped a zone");
    require(moved[1]["regions"][1]==before[1]["regions"][1] && moved[1]["groups"]==before[1]["groups"],"Group drag changed unselected LEDs or hardware identity");
    canvas->undo();require(*canvas->layout==before,"One undo did not restore the whole group");canvas->redo();require(*canvas->layout==moved,"Group redo failed");canvas->undo();
    box=canvas->selectionGeometry();p=box.center();mouse(*canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(*canvas,QEvent::MouseMove,p+QPointF(10000,10000),Qt::NoButton,Qt::LeftButton);
    auto bounds=office::bounds(canvas->selected());require(bounds.right<=1.000001 && bounds.bottom<=1.000001,"Group escaped monitor bounds");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&escape);require(*canvas->layout==before,"Escape did not cancel the group gesture");
    QKeyEvent right(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(canvas,&right);
    for(auto [d,i]:canvas->targets())require(std::abs((*canvas->layout)[d]["regions"][i][0].get<float>()-before[d]["regions"][i][0].get<float>()-.005f)<.00001,"Arrow key did not move every selected LED");canvas->undo();
    click(laptop);click(laptop->child(0),Qt::ControlModifier);require(canvas->targets().size()==3,"Parent and child selected the same LED twice");
    panel.fixtureLayout(original); // Leave no unsaved fixture or active gesture.
}
void contextOutlines(QApplication& app) {
    Json layout=Json::array({
        {{"regions",office::encode(std::vector<Region>{{.2f,.3f,.3f,.7f},{.3f,.3f,.4f,.7f}})},{"groups",Json::array({{{"first",0},{"count",2}}})}},
        {{"regions",office::encode(std::vector<Region>{{.6f,.3f,.8f,.7f},{.3f,.35f,.5f,.55f}})},{"groups",Json::array({{{"first",0},{"count",2}}})}}
    });
    auto original=layout;MapCanvas canvas;canvas.layout=&layout;canvas.view=MapCanvas::View::AllDevices;canvas.showGrid=false;canvas.resize(1048,600);
    canvas.preview=QImage(512,216,QImage::Format_RGB32);canvas.show();app.processEvents();
    for(auto background:{QColor(12,35,68),QColor(244,217,183)})for(double scale:{1.,1.5,2.}) {
        canvas.preview.fill(background);QImage out(QSize(qRound(canvas.width()*scale),qRound(canvas.height()*scale)),QImage::Format_ARGB32_Premultiplied);out.setDevicePixelRatio(scale);canvas.render(&out);
        auto image=canvas.imageGeometry();auto pixel=[&](double x,double y){return out.pixelColor(qRound((image.left()+image.width()*x)*scale),qRound((image.top()+image.height()*y)*scale));};
        auto grey=pixel(.6,.45);require(grey.red()>150 && grey.green()>150 && grey.blue()>150 && std::abs(grey.red()-grey.blue())<25,"Unselected device outline is faint or tinted instead of visible neutral grey");
        auto active=pixel(.3,.45);require(active.red()<90 && active.green()>180 && active.blue()>180,"Grey context painted over a selected cyan LED outline");
    }
    require(layout==original,"Context rendering changed the saved topology");
}
void trayMenuBackground() {
    QWidget parent;parent.setStyleSheet(officeui::stylesheet());QMenu menu(&parent);
    auto first=menu.addAction("Open Office Ambilight");menu.addSeparator();menu.addAction("Exit panel");
    menu.ensurePolished();menu.resize(menu.sizeHint());menu.setActiveAction(first);
    for(double scale:{1.,1.5,2.}) {
        QImage image(QSize(qRound(menu.width()*scale),qRound(menu.height()*scale)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(scale);image.fill(Qt::transparent);menu.render(&image);
        auto background=image.pixelColor(qRound(5*scale),qRound((menu.height()-5)*scale));
        require(background.alpha()==255 && background.lightness()<40,"Tray menu must paint an opaque dark background despite the transparent QWidget theme");
        auto row=menu.actionGeometry(first);auto highlight=image.pixelColor(qRound((row.left()+4)*scale),qRound(row.center().y()*scale));
        require(highlight.alpha()==255 && highlight!=background,"Tray menu selection must remain visible on its own background");
    }
}
void hardwareEditor(QApplication& app) {
    Json sent;Panel panel("unused-test-state",{},false,false,[&](Json request,OfficeReply reply){sent=request;if(request.at("command")=="snapshot")reply({{"ok",false},{"error","No capture source in the hardware editor fixture"}});else reply({{"ok",true},{"result",Json::object()}});});
    Json plain={{"index",0},{"zone",-1},{"name","Direct"},{"flags",MODE_FLAG_HAS_PER_LED_COLOR},{"speed",0},{"speed_min",0},{"speed_max",0},{"brightness",0},{"brightness_min",0},{"brightness_max",0},{"direction",0},{"color_mode",MODE_COLORS_PER_LED},{"colors_min",0},{"colors_max",0},{"colors",Json::array()}};
    Json pulse=plain;pulse["index"]=1;pulse["name"]="Pulse";pulse["flags"]=MODE_FLAG_HAS_SPEED|MODE_FLAG_HAS_BRIGHTNESS|MODE_FLAG_HAS_MODE_SPECIFIC_COLOR;pulse["speed_min"]=1;pulse["speed_max"]=5;pulse["speed"]=2;pulse["brightness_max"]=100;pulse["brightness"]=80;pulse["color_mode"]=MODE_COLORS_MODE_SPECIFIC;pulse["colors_min"]=1;pulse["colors_max"]=3;pulse["colors"]=Json::array({0xffffff});
    Json device={{"id","test"},{"identity","identity-1"},{"name","Test keyboard"},{"owner","ambience"},{"mapped",true},{"active_mode",0},{"zones",Json::array()},{"schema",Json::object()},{"configuration",Json::object()},
        {"modes",Json::array({plain,pulse})},{"leds",Json::array({{{"name","Key A"},{"color",255}},{{"name","Key B"},{"color",65535}}})}};
    panel.fixtureHardware(device);app.processEvents();auto dialog=panel.findChild<QDialog*>("hardwareDialog");require(dialog,"Native hardware editor did not open");
    require(!panel.windowIcon().isNull(),"OpenRGB application icon resource missing");
    auto tabs=dialog->findChild<QTabWidget*>("hardwareTabs");require(tabs && tabs->count()==4,"Native hardware controls lost their contextual tab placement");tabs->setCurrentIndex(1);app.processEvents();
    QImage background(dialog->size(),QImage::Format_ARGB32_Premultiplied);background.fill(Qt::transparent);dialog->render(&background);
    require(background.pixelColor(3,3)==QColor("#040c14"),"Hardware dialog must paint its opaque themed surface, not leak a black or transparent window");
    auto speed=dialog->findChild<QSpinBox*>("hardwareSpeed"),brightness=dialog->findChild<QSpinBox*>("hardwareBrightness");auto choice=dialog->findChild<QComboBox*>("hardwareMode");
    require(!speed->isVisible() && !brightness->isVisible(),"Direct mode displayed unsupported speed/brightness controls");
    choice->setCurrentIndex(1);app.processEvents();require(speed->isVisible() && speed->minimum()==1 && speed->maximum()==5 && brightness->isVisible(),"Mode capabilities did not replace unsupported controls with driver ranges");
    require(!dialog->findChild<QPushButton*>("hardwareSave")->isEnabled(),"Unapplied native selection can overwrite hardware memory");
    speed->setValue(4);brightness->setValue(60);dialog->findChild<QPushButton*>("hardwareApply")->click();
    require(sent.at("command")=="hardware" && sent.at("action")=="mode" && sent.at("identity")=="identity-1" && sent.at("mode").at("speed")==4 && sent.at("mode").at("brightness")==60,"Native mode widgets did not send the identity-bound edited parameters");
    dialog->findChild<QPushButton*>("hardwareAmbience")->click();require(sent.at("action")=="ambience","Return to ambience not wired");
    dialog->close();app.processEvents();
}
void screenEditor(QApplication& app){
    Json sent;Panel panel("unused-screen-fixture",{},false,false,[&](Json request,OfficeReply reply){if(request.at("command")=="snapshot")reply({{"ok",false},{"error","No capture in Screen widget fixture"}});else{sent=request;reply({{"ok",true},{"result",Json::object()}});}});
    Json settings={{"DayConfiguration",{{"Temperature",6600},{"Brightness",1.}}},{"NightConfiguration",{{"Temperature",6600},{"Brightness",.85}}},{"ConfigurationTransitionDuration","03:00:00"},{"ConfigurationSmoothingMaxDuration","00:00:05"},{"ConfigurationTransitionOffset",0.},{"Location",{{"Latitude",50.},{"Longitude",30.}}},{"ManualSunriseTime","07:20:00"},{"ManualSunsetTime","16:30:00"},{"WhitelistedApplications",Json::array()}};
    Json value={{"settings",settings},{"revision",3},{"managed",true},{"temperature",6600},{"brightness",.85},{"gamma_valid",true},{"display_count",2},{"sunrise","07:00"},{"sunset","17:00"},{"cycle_seconds",43200.},{"cycle_state","day"},{"cycle_times",{{"sunrise_start",14400.},{"sunrise_end",25200.},{"sunset_start",61200.},{"sunset_end",72000.}}}};
    panel.fixtureScreen(value);panel.show();app.processEvents();auto menu=panel.findChild<QMenu*>("officeTrayMenu");require(menu,"Real tray menu missing");
    QStringList entries;for(auto action:menu->actions())if(action->isVisible()&&!action->isSeparator())entries<<action->text();
    require(entries==QStringList{"Open","Disable lighting","Disable screen adjustments","Awake","Exit"},"Tray menu differs from agreed root structure with Awake addition");
    auto lighting=panel.findChild<QMenu*>("lightingPauseMenu"),screen=panel.findChild<QMenu*>("screenPauseMenu");require(lighting&&screen&&lighting->actions().size()==11&&screen->actions().size()==11,"Pause choices missing");
    lighting->actions().at(2)->trigger();require(sent.at("target")=="lighting"&&sent.at("mode")=="duration"&&sent.at("minutes")==1,"Real RGB pause action dispatch failed");
    screen->actions().at(1)->trigger();require(sent.at("target")=="screen"&&sent.at("mode")=="sunrise","Real Screen pause action dispatch failed");
    auto brightness=panel.findChild<QDoubleSpinBox*>("screen-NightConfiguration/Brightness");require(brightness&&brightness->value()==85,"Imported brightness not rendered");brightness->setValue(80);panel.findChild<QPushButton*>("screenSave")->click();
    require(sent.at("action")=="save"&&sent.at("revision")==3&&sent.at("settings").at("NightConfiguration").at("Brightness")==.8,"Real Screen save did not send edited settings");
    value["screen_disabled"]=true;value["lighting_disabled"]=false;panel.fixtureScreen(value);entries.clear();for(auto action:menu->actions())if(action->isVisible()&&!action->isSeparator())entries<<action->text();require(entries.contains("Enable screen adjustments")&&entries.contains("Disable lighting"),"Independent enable menu state failed");
    require(panel.findChild<QTabWidget*>("screenTabs")->count()==4,"Screen feature tabs incomplete");
    auto slider=panel.findChild<QSlider*>("screen-slider-NightConfiguration/Brightness");require(slider&&slider->value()==85,"Screen slider not synchronized on reload");
    slider->setValue(72);require(brightness->value()==72,"Screen slider did not update exact field");brightness->setValue(81);require(slider->value()==81,"Exact field did not update slider");
    panel.findChild<QPushButton*>("screenSave")->click();require(sent.at("settings").at("NightConfiguration").at("Brightness")==.81,"Slider companion changed save semantics");
    auto dial=dynamic_cast<officeui::Sundial*>(panel.findChild<QWidget*>("screenSundial"));require(dial&&dial->markerSeconds()==43200,"Sundial is disconnected from owner's effective clock");
    value["preview"]=true;value["cycle_seconds"]=43800.;panel.fixtureScreen(value);require(dial->animating(),"Visible owner preview samples do not animate");
    QSignalSpy frames(dial->findChild<QVariantAnimation*>(),&QVariantAnimation::valueChanged);
    QEventLoop loop;QObject::connect(dial->findChild<QVariantAnimation*>(),&QVariantAnimation::finished,&loop,&QEventLoop::quit);loop.exec();
    require(frames.count()>1,"Sundial did not render intermediate owner-clock positions");
    require(std::abs(dial->markerSeconds()-43800)<.001,"Sundial preview did not reach owner's sample");
    auto first=dial->grab().toImage();qApp->setProperty("officeReducedMotion",true);value["cycle_seconds"]=44400.;panel.fixtureScreen(value);require(!dial->animating()&&dial->markerSeconds()==44400,"Reduced motion did not bypass interpolation");
    value["cycle_seconds"]=0.;value["cycle_state"]="night";panel.fixtureScreen(value);require(first!=dial->grab().toImage(),"Day/night sundial did not change its real rendered state");
    qApp->setProperty("officeReducedMotion",false);value["cycle_seconds"]=600.;panel.fixtureScreen(value);require(dial->animating(),"Preview motion failed after midnight wrap");panel.hide();app.processEvents();require(!dial->animating(),"Hidden Screen page retained animation");
    auto minimize=panel.findChild<QToolButton*>("windowMinimize"),maximize=panel.findChild<QToolButton*>("windowMaximize"),close=panel.findChild<QToolButton*>("windowClose");require(minimize&&maximize&&close,"Integrated caption controls missing");
    require(panel.captionHit(maximize->mapTo(&panel,maximize->rect().center()))==HTMAXBUTTON,"Maximize does not expose Windows snap hit target");
    require(panel.captionHit(close->mapTo(&panel,close->rect().center()))==HTCLIENT,"Close button became a drag region");
}
void awakeEditor(QApplication& app){
    Json sent;Panel panel("unused-awake-fixture",{},false,false,[&](Json request,OfficeReply reply){if(request.at("command")=="snapshot")reply({{"ok",false},{"error","No capture in awake widget fixture"}});else{sent=request;reply({{"ok",true},{"result",Json::object()}});}});
    Json value={{"mode","automatic"},{"keep_screen_on",false},{"until_ms",0},{"intervals",Json::array({{{"label","30 minutes"},{"seconds",1800}},{{"label","1 hour"},{"seconds",3600}},{{"label","2 hours"},{"seconds",7200}}})}};
    panel.fixtureAwake(value);auto automatic=panel.findChild<QAction*>("awakeAutomatic"),indefinite=panel.findChild<QAction*>("awakeIndefinite"),off=panel.findChild<QAction*>("awakeOff"),display=panel.findChild<QAction*>("awakeKeepScreenOn");
    require(automatic&&automatic->isChecked()&&display->isChecked()&&!display->isEnabled(),"Automatic mode must visibly require display awake");
    auto intervals=panel.findChild<QMenu*>("awakeIntervalMenu");require(intervals&&intervals->actions().size()==3,"PowerToys default intervals missing");intervals->actions()[1]->trigger();require(sent.at("command")=="awake"&&sent.at("mode")=="interval"&&sent.at("seconds")==3600,"Interval action did not dispatch to real owner path");
    indefinite->trigger();require(sent.at("mode")=="indefinite","Indefinite action failed");
    value["mode"]="indefinite";panel.fixtureAwake(value);require(display->isEnabled()&&!display->isChecked(),"Manual Keep screen on preference lost");display->trigger();require(sent.at("action")=="display"&&sent.at("enabled")==true,"Keep screen on action failed");
    off->trigger();require(sent.at("mode")=="off","Off action failed");value["mode"]="off";panel.fixtureAwake(value);require(off->isChecked()&&!display->isEnabled(),"Off did not disable display preference control");
    panel.findChild<QAction*>("awakeExpiration")->trigger();app.processEvents();auto dialog=panel.findChild<QDialog*>("awakeExpirationDialog");require(dialog,"Expiration chooser missing");auto time=dialog->findChild<QDateTimeEdit*>("awakeExpirationTime");auto target=QDateTime::currentDateTime().addSecs(7200);time->setDateTime(target);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    require(sent.at("mode")=="expiration"&&std::abs(sent.at("until_ms").get<int64_t>()-target.toMSecsSinceEpoch())<1000,"Expiration chooser did not submit its selected native date/time");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        officeui::loadFonts();
        screenEditor(app);
        awakeEditor(app);
        hardwareEditor(app);
        trayMenuBackground();
        invertedGrid(app);
        contextOutlines(app);
        require(QFontInfo(officeui::body()).family()=="Office Body","Native body font fell back instead of using the recovered asset");
        require(QFontInfo(officeui::display()).family()=="Office Display","Native display font fell back");
        for(auto family:{"Office Symbols Outline","Office Symbols Active"}) {
            QFont f(family);f.setPixelSize(26);auto raw=QRawFont::fromFont(f);
            for(auto code:officeui::symbolCodes)require(raw.glyphIndexesForString(QString(QChar(code))).at(0)!=0,"A recovered icon maps to a missing glyph");
        }
        officeui::Switch toggle("Enable");toggle.resize(160,32);toggle.show();app.processEvents();
        mouse(toggle,QEvent::MouseButtonPress,{22,16},Qt::LeftButton,Qt::LeftButton);mouse(toggle,QEvent::MouseButtonRelease,{22,16},Qt::LeftButton,Qt::NoButton);require(toggle.isChecked(),"Reference switch lost its real checkbox behavior");
        officeui::Slider slider;slider.setRange(0,100);slider.resize(220,28);slider.show();app.processEvents();
        mouse(slider,QEvent::MouseButtonPress,{110,14},Qt::LeftButton,Qt::LeftButton);mouse(slider,QEvent::MouseButtonRelease,{110,14},Qt::LeftButton,Qt::NoButton);require(slider.value()==50,"Reference slider pointer does not match its painted track");
        officeui::Card multiZone("Four-zone device");
        for(int i=0;i<5;++i)multiZone.content->addWidget(new officeui::Switch("Zone"));
        multiZone.resize(400,80);multiZone.show();app.processEvents();
        require(multiZone.height()>=multiZone.minimumSizeHint().height(),"Multi-zone card shrank below its controls");
        for(auto widget:multiZone.findChildren<QCheckBox*>())require(widget->height()>=widget->minimumSizeHint().height(),"Multi-zone controls overlap at the available height");
        Json baseline=Json::array({{{"regions",office::encode(std::vector<Region>{{.2f,.3f,.4f,.7f},{.4f,.3f,.6f,.7f}})},
            {"groups",Json::array({{{"first",0},{"count",2}}})}}});
        Json layout=baseline;MapCanvas canvas;canvas.layout=&layout;canvas.resize(900,500);canvas.show();app.processEvents();
        auto originalBox=canvas.selectionGeometry();auto firstHandle=canvas.rotationHandle();
        mouse(canvas,QEvent::MouseButtonPress,firstHandle,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,firstHandle,Qt::LeftButton,Qt::NoButton);
        require(canvas.orientation()==1 && canvas.topNormal()==QPointF(1,0),"Floating rotate action did not follow the shape's own top");
        require(canvas.rotationHandle().x()>canvas.selectionGeometry().center().x(),"Rotated shape handle stayed above the screen instead of its own top");
        auto turned=layout;canvas.undo();require(layout==baseline,"Rotation undo did not restore exact geometry and metadata");canvas.redo();require(layout==turned,"Rotation redo lost orientation");
        for(int i=0;i<3;++i)canvas.rotateSelection();require(canvas.orientation()==0,"Four turns did not restore top orientation");
        for(size_t i=0;i<2;++i)for(size_t c=0;c<4;++c)require(std::abs(layout[0]["regions"][i][c].get<float>()-baseline[0]["regions"][i][c].get<float>())<.00001,"Four turns changed region identity or geometry");
        layout=Json::parse(turned.dump());require(canvas.orientation()==1,"Saved orientation metadata did not survive reload");
        layout=baseline;canvas.resetHistory();
        unsigned edits=0;canvas.edited=[&]{++edits;};
        for(unsigned handle:{1u,2u,4u,8u,5u,6u,9u,10u}) {
            layout=baseline;canvas.resetHistory();auto b=canvas.selectionGeometry();
            QPointF p(handle&1?b.left():handle&2?b.right():b.center().x(),handle&4?b.top():handle&8?b.bottom():b.center().y());
            mouse(canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,p+QPointF(15,12),Qt::NoButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,p+QPointF(15,12),Qt::LeftButton,Qt::NoButton);
            auto changed=canvas.selected();auto bounds=office::bounds(changed);
            require(layout!=baseline,"Resize handle did not change the actual sampling map");
            if(!(handle&1)) require(std::abs(bounds.left-.2f)<.0001,"Opposite left anchor moved");
            if(!(handle&2)) require(std::abs(bounds.right-.6f)<.0001,"Opposite right anchor moved");
            if(!(handle&4)) require(std::abs(bounds.top-.3f)<.0001,"Opposite top anchor moved");
            if(!(handle&8)) require(std::abs(bounds.bottom-.7f)<.0001,"Opposite bottom anchor moved");
            require(changed[0].left<changed[1].left,"Resize renumbered physical LEDs");
            canvas.undo();require(layout==baseline,"Undo did not restore exact map");canvas.redo();require(layout!=baseline,"Redo missing");
        }
        layout=baseline;auto before=canvas.selectionGeometry();auto p=before.bottomRight();
        mouse(canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,QPointF(-10000,-10000),Qt::NoButton,Qt::LeftButton);
        office::validateRegions(layout[0]["regions"],2);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&canvas,&escape);
        require(layout==baseline,"Escape failed to cancel a resize");
        Stepper<QDoubleSpinBox> spin;spin.setRange(0,100);spin.resize(160,48);spin.show();app.processEvents();
        auto buttons=spin.findChildren<QToolButton*>();require(buttons.size()==2,"Visible vector step buttons missing");
        require(buttons[0]->width()>=26 && buttons[0]->height()>=20,"Step arrow hit target too small");buttons[0]->click();require(spin.value()==1,"Increase arrow not wired");buttons[1]->click();require(spin.value()==0,"Decrease arrow not wired");
        require(edits>=8,"No live editor change notifications");
        multiSelection(app);
        lightingMixer(app);
        std::cout<<"PASS: production mouse handles, anchors, LED identity, bounds, undo/redo, cancel and vector steppers\n";return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
