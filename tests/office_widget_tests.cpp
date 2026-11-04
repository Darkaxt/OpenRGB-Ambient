// Drive production widgets with Qt events; no host desktop input or RGB writes.
#define OFFICE_WIDGET_TEST
#include "../OfficeControl.cpp"
#include <iostream>
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void mouse(QWidget& widget,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,point,button,buttons,Qt::NoModifier);QApplication::sendEvent(&widget,&event);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        Json baseline=Json::array({{{"regions",office::encode(std::vector<Region>{{.2f,.3f,.4f,.7f},{.4f,.3f,.6f,.7f}})},
            {"groups",Json::array({{{"first",0},{"count",2}}})}}});
        Json layout=baseline;MapCanvas canvas;canvas.layout=&layout;canvas.resize(900,500);canvas.show();app.processEvents();
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
