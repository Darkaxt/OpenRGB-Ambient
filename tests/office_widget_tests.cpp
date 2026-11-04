// Drive production widgets with Qt events; no host desktop input or RGB writes.
#define OFFICE_WIDGET_TEST
#include "../OfficeControl.cpp"
#include <iostream>
#include <QFontInfo>
#include <QRawFont>
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void mouse(QWidget& widget,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,point,button,buttons,Qt::NoModifier);QApplication::sendEvent(&widget,&event);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        officeui::loadFonts();
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
        std::cout<<"PASS: production mouse handles, anchors, LED identity, bounds, undo/redo, cancel and vector steppers\n";return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
