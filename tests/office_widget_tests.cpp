// Drive production widgets with Qt events; no host desktop input or RGB writes.
#define OFFICE_WIDGET_TEST
#include "../OfficeControl.cpp"
#include <iostream>
#include <QFontInfo>
#include <QRawFont>
#include <QTest>
#include <QBuffer>
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
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
    require(draftRequest.size()==4 && !draftRequest.contains("settings") && !draftRequest.contains("layout"),
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
    auto speed=dialog->findChild<QSpinBox*>("hardwareSpeed"),brightness=dialog->findChild<QSpinBox*>("hardwareBrightness");auto choice=dialog->findChild<QComboBox*>("hardwareMode");
    require(!speed->isVisible() && !brightness->isVisible(),"Direct mode displayed unsupported speed/brightness controls");
    choice->setCurrentIndex(1);app.processEvents();require(speed->isVisible() && speed->minimum()==1 && speed->maximum()==5 && brightness->isVisible(),"Mode capabilities did not replace unsupported controls with driver ranges");
    require(!dialog->findChild<QPushButton*>("hardwareSave")->isEnabled(),"Unapplied native selection can overwrite hardware memory");
    speed->setValue(4);brightness->setValue(60);dialog->findChild<QPushButton*>("hardwareApply")->click();
    require(sent.at("command")=="hardware" && sent.at("action")=="mode" && sent.at("identity")=="identity-1" && sent.at("mode").at("speed")==4 && sent.at("mode").at("brightness")==60,"Native mode widgets did not send the identity-bound edited parameters");
    dialog->findChild<QPushButton*>("hardwareAmbience")->click();require(sent.at("action")=="ambience","Return to ambience not wired");
    dialog->close();app.processEvents();
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        officeui::loadFonts();
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
        std::cout<<"PASS: production mouse handles, anchors, LED identity, bounds, undo/redo, cancel and vector steppers\n";return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
