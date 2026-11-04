// Drive production widgets with Qt events; no host desktop input or RGB writes.
#define OFFICE_WIDGET_TEST
#include "../OfficeControl.cpp"
#include <iostream>
#include <QFontInfo>
#include <QRawFont>
#include <QTest>
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
    Json layout=Json::array({{{"regions",office::encode(std::vector<Region>{{.25f,.25f,.55f,.65f}})},
        {"groups",Json::array({{{"first",0},{"count",1}}})}}});
    canvas.layout=&layout;auto saved=layout;
    canvas.showGrid=false;auto original=render(1.);canvas.showGrid=true;auto grid=render(1.);
    auto bounds=canvas.selectionGeometry();
    for(auto point:{bounds.topLeft(),bounds.bottomRight(),canvas.rotationHandle()})
        require(grid.pixelColor(point.toPoint())==original.pixelColor(point.toPoint()),"Grid inverted a selection or rotation handle");
    require(layout==saved,"Rendering the grid changed the saved map");
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
    auto tree=panel.findChild<QTreeWidget*>("topologyTree");auto view=panel.findChild<QComboBox*>("topologyVisibility");
    // MapCanvas has no Q_OBJECT: resolve the actual QWidget by its stable name.
    auto canvas=static_cast<MapCanvas*>(panel.findChild<QWidget*>("topologyCanvas"));
    require(tree && canvas && view,"Production topology controls missing");
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
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        officeui::loadFonts();
        invertedGrid(app);
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
