// Native control panel for the office plugin. Recovered EDAmbilight layout and
// interaction contract: EDAMBILIGHT-GUI-REVIEW.md; no second RGB/capture writer.
#include "OfficeLayout.h"
#include "OfficeProcessing.h"
#include "OfficeStyle.h"
#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLocalSocket>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QCloseEvent>
#include <QScrollArea>
#include <QCommandLineParser>
#include <QSaveFile>
#include <QDebug>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QButtonGroup>
#include <QToolButton>
#include <QSlider>
#include <QGridLayout>
#include <QWheelEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QShortcut>
#include <QProcess>
#include <QSignalBlocker>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QStyle>
#include <QProgressBar>
#include <QGraphicsOpacityEffect>
#include <QFileDialog>
#include <functional>
#include <map>
#include <array>

using office::Json;
using office::Region;
namespace {
QString text(const Json& v) {return QString::fromStdString(v.get<std::string>());}
QColor cyan("#14f5ff");

class ChevronButton : public QToolButton {
    bool up;
public:
    ChevronButton(bool direction,QWidget* parent):QToolButton(parent),up(direction) {
        setAutoRepeat(true);setFocusPolicy(Qt::NoFocus);setAccessibleName(up?"Increase value":"Decrease value");
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);if(underMouse()) p.fillRect(rect(),QColor("#10333e"));
        p.setPen(QPen(isEnabled()?cyan:QColor("#677783"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        auto c=rect().center();QPolygonF arrow;arrow<<QPointF(c.x()-4,c.y()+(up?2:-2))<<QPointF(c.x(),c.y()+(up?-2:2))<<QPointF(c.x()+4,c.y()+(up?2:-2));p.drawPolyline(arrow);
    }
};
class ChoiceBox : public QComboBox {
protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(width()-26,2,24,height()-4,QColor("#020b12"));
        p.setPen(QPen(isEnabled()?cyan:QColor("#677783"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        auto c=QPointF(width()-14,height()/2.);QPolygonF arrow;arrow<<c+QPointF(-5,-2)<<c+QPointF(0,3)<<c+QPointF(5,-2);p.drawPolyline(arrow);
    }
};
template<class Base> class Stepper : public Base {
    ChevronButton *up=new ChevronButton(true,this),*down=new ChevronButton(false,this);
public:
    Stepper() {
        this->setButtonSymbols(QAbstractSpinBox::NoButtons);this->setMinimumHeight(40);
        this->setStyleSheet("padding-right:32px;");
        QObject::connect(up,&QToolButton::clicked,this,[this]{this->stepUp();});
        QObject::connect(down,&QToolButton::clicked,this,[this]{this->stepDown();});
    }
protected:
    void resizeEvent(QResizeEvent* e) override {Base::resizeEvent(e);int half=this->height()/2;up->setGeometry(this->width()-30,1,28,half-1);down->setGeometry(this->width()-30,half,28,this->height()-half-1);}
};

class Client : public QObject {
    QLocalSocket socket;
    QByteArray pending;
    std::string token;
    uint64_t next=0;
    std::map<uint64_t,std::function<void(const Json&)>> callbacks;
public:
    std::function<void()> connected;
    std::function<void(QString)> error;
    std::function<void(QString)> trace;
    Client() {
        connect(&socket,&QLocalSocket::connected,this,[this]{if(trace) trace("pipe-connected");if(connected) connected();});
        connect(&socket,&QLocalSocket::disconnected,this,[this]{callbacks.clear();if(error) error("OpenRGB disconnected. Your unsaved map is retained.");});
        connect(&socket,&QLocalSocket::errorOccurred,this,[this](auto){if(error) error(socket.errorString());});
        connect(&socket,&QLocalSocket::readyRead,this,[this] {
            pending+=socket.readAll();
            if(trace) trace(QString("reply-buffer %1 bytes").arg(pending.size()));
            if(pending.size()>2*1024*1024) {socket.abort();return;}
            while(pending.contains('\n')) {
                auto at=pending.indexOf('\n');auto line=pending.left(at);pending.remove(0,at+1);
                auto value=Json::parse(line.constData(),line.constData()+line.size(),nullptr,false);
                if(!value.is_object() || !value.contains("id") || !value["id"].is_number_unsigned()) {socket.abort();return;}
                auto found=callbacks.find(value["id"].get<uint64_t>());
                if(trace) trace(QString("reply %1 matched=%2").arg(value["id"].get<uint64_t>()).arg(found!=callbacks.end()));
                if(found==callbacks.end()) continue;
                auto callback=std::move(found->second);callbacks.erase(found);callback(value);
            }
        });
    }
    ~Client() {
        // QLocalSocket can emit disconnected during destruction. Disconnect
        // before the std::function members (and the panel they capture) die.
        QObject::disconnect(&socket,nullptr,this,nullptr);
        callbacks.clear();socket.abort();
    }
    void open(const QString& state) {
        if(trace) trace("opening-endpoint-record");
        QFile file(state+"/office-control.json");
        if(!file.open(QIODevice::ReadOnly)) {if(error) error("Start the Office Ambience owner before opening the panel.");return;}
        auto bytes=file.readAll();auto info=Json::parse(bytes.constData(),bytes.constData()+bytes.size(),nullptr,false);
        if(!info.is_object() || info.value("protocol",0)!=1) {if(error) error("The installed control endpoint is incompatible.");return;}
        if(!info.contains("token") || !info["token"].is_string() || !info.contains("endpoint") || !info["endpoint"].is_string()) {if(error) error("The private endpoint record is invalid.");return;}
        token=info.at("token").get<std::string>();pending.clear();socket.connectToServer(text(info.at("endpoint")));
    }
    bool ready() const {return socket.state()==QLocalSocket::ConnectedState;}
    void reconnect(const QString& directory) {socket.abort();callbacks.clear();pending.clear();open(directory);}
    void send(Json request,std::function<void(const Json&)> callback) {
        if(!ready()) {if(error) error("OpenRGB is not connected. Reconnect before saving.");return;}
        request["id"]=++next;request["token"]=token;callbacks[next]=std::move(callback);
        if(trace) trace(QString("request %1 %2").arg(next).arg(text(request.at("command"))));
        socket.write(QByteArray::fromStdString(request.dump())+'\n');
    }
};

class MapCanvas : public QWidget {
    QPointF dragStart;
    QPointF panStart,pan;
    double zoom=1;
    unsigned handle=0;
    bool panning=false;
    bool draggingCenter=false;
    std::vector<Region> dragRegions;
    std::vector<Json> undoHistory,redoHistory;
    void checkpoint() {undoHistory.push_back(*layout);redoHistory.clear();}
    QRectF screenBounds() const {
        auto b=office::bounds(selected());auto image=imageRect();
        return {image.left()+b.left*image.width(),image.top()+b.top*image.height(),(b.right-b.left)*image.width(),(b.bottom-b.top)*image.height()};
    }
    unsigned hitHandle(QPointF point) const {
        if(selected().empty()) return 0;auto b=screenBounds();
        if(!b.adjusted(-8,-8,8,8).contains(point)) return 0;
        unsigned h=0;if(std::abs(point.x()-b.left())<=8) h|=1;else if(std::abs(point.x()-b.right())<=8) h|=2;
        if(std::abs(point.y()-b.top())<=8) h|=4;else if(std::abs(point.y()-b.bottom())<=8) h|=8;return h;
    }
    void pointerCursor(QPointF p) {
        if(QLineF(p,rotationHandle()).length()<=16){setCursor(Qt::PointingHandCursor);setToolTip("Rotate 90° clockwise · attached to the shape's Top edge");return;}setToolTip({});
        auto h=hitHandle(p);setCursor(h==5 || h==10?Qt::SizeFDiagCursor:h==6 || h==9?Qt::SizeBDiagCursor:h&3?Qt::SizeHorCursor:h&12?Qt::SizeVerCursor:screenBounds().contains(p)?Qt::SizeAllCursor:Qt::ArrowCursor);
    }
    QRectF imageRect() const {
        auto area=rect().adjusted(12,12,-12,-12);double width=area.width(),height=width*2160/5120;
        if(height>area.height()) {height=area.height();width=height*5120/2160;}
        width*=zoom;height*=zoom;
        return {area.center().x()-width/2+pan.x(),area.center().y()-height/2+pan.y(),width,height};
    }
public:
    Json* layout=nullptr;
    Json colours,effective;
    QImage preview;
    int device=0,group=0,led=-1;
    bool showAll=false,showGrid=true,snap=false;
    bool showCenter=false;
    QPointF effectCenter{.5,.5};
    std::function<void(QPointF)> centerEdited;
    float grid=.01f,blur=0;
    std::function<void()> edited;
    std::function<void(int,int)> picked;
    std::function<void(QString)> error;
    MapCanvas() {setMinimumSize(280,210);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setAccessibleName("Actual LG sampling map");}
    QRectF imageGeometry() const {return imageRect();}
    QRectF selectionGeometry() const {return screenBounds();}
    int orientation() const {auto ids=indices();if(ids.empty())return 0;const auto& d=layout->at(device);if(!d.contains("rotation_quarters"))return 0;return d["rotation_quarters"].at(ids.front()).get<int>();}
    QPointF topNormal() const {switch(orientation()){case 1:return {1,0};case 2:return {0,1};case 3:return {-1,0};default:return {0,-1};}}
    QPointF topAnchor() const {auto b=screenBounds();auto n=topNormal();return b.center()+QPointF(n.x()*b.width()/2,n.y()*b.height()/2);}
    QPointF rotationHandle() const {auto p=topAnchor()+topNormal()*38;return {std::clamp(p.x(),17.,width()-17.),std::clamp(p.y(),17.,height()-17.)};}
    void rotateSelection() {
        auto values=selected();if(values.empty())return;auto from=office::bounds(values);
        float w=(from.bottom-from.top)*2160.f/5120.f,h=(from.right-from.left)*5120.f/2160.f;
        if(w>1 || h>1)throw std::runtime_error("Resize this zone before rotating it inside the LG image.");
        float cx=(from.left+from.right)/2,cy=(from.top+from.bottom)/2;
        float x=std::clamp(cx-w/2,0.f,1-w),y=std::clamp(cy-h/2,0.f,1-h);
        auto next=office::transform(values,from,{x,y,x+w,y+h},90,false);
        checkpoint();auto& turns=(*layout)[device]["rotation_quarters"];
        if(turns.is_null())turns=std::vector<int>((*layout)[device]["regions"].size(),0);
        for(auto id:indices())turns[id]=(turns[id].get<int>()+1)%4;
        setSelected(next);
    }
    void resetHistory() {undoHistory.clear();redoHistory.clear();}
    void replaceLayout(const Json& value) {checkpoint();*layout=value;update();if(edited) edited();}
    void undo() {if(undoHistory.empty()) return;redoHistory.push_back(*layout);*layout=undoHistory.back();undoHistory.pop_back();update();if(edited) edited();}
    void redo() {if(redoHistory.empty()) return;undoHistory.push_back(*layout);*layout=redoHistory.back();redoHistory.pop_back();update();if(edited) edited();}
    void fit() {zoom=1;pan={};update();}
    std::vector<unsigned> indices() const {
        if(!layout || layout->empty()) return {};
        const auto& g=layout->at(device).at("groups").at(group);
        if(led>=0) return {static_cast<unsigned>(led)};
        std::vector<unsigned> out;for(unsigned i=g.at("first").get<unsigned>();i<g.at("first").get<unsigned>()+g.at("count").get<unsigned>();++i) out.push_back(i);
        return out;
    }
    std::vector<Region> selected() const {
        std::vector<Region> out;if(!layout) return out;
        for(auto i:indices()) {auto r=layout->at(device).at("regions").at(i);out.push_back({r[0],r[1],r[2],r[3]});}return out;
    }
    void setSelected(const std::vector<Region>& values,bool remember=false) {
        if(remember) checkpoint();
        auto ids=indices();for(size_t i=0;i<ids.size();++i) (*layout)[device]["regions"][ids[i]]=office::encode(values[i]);
        update();if(edited) edited();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto image=imageRect();
        p.fillRect(rect(),QColor("#020a10"));
        if(!preview.isNull()) p.drawImage(image,preview);
        else {p.fillRect(image,QColor("#102333"));p.setPen(QColor("#cdd6e0"));p.drawText(image,Qt::AlignCenter,"Waiting for a live LG preview");}
        p.save();p.setClipRect(image.intersected(rect()));
        p.setPen(QPen(QColor(120,155,175,24),1));
        float visualGrid=std::max(grid,static_cast<float>(20./std::max(1.,image.width())));
        if(showGrid) for(float at=visualGrid;at<1;at+=visualGrid) {p.drawLine(QPointF(image.left()+image.width()*at,image.top()),QPointF(image.left()+image.width()*at,image.bottom()));p.drawLine(QPointF(image.left(),image.top()+image.height()*at),QPointF(image.right(),image.top()+image.height()*at));}
        if(!layout || layout->empty()) {p.restore();return;}
        auto selectedIds=indices();
        for(size_t d=0;d<layout->size();++d) {
            if(!showAll && static_cast<int>(d)!=device) continue;
            const auto& regions=(*layout)[d]["regions"];
            for(size_t i=0;i<regions.size();++i) {
                auto r=regions[i];QRectF box(image.left()+r[0].get<double>()*image.width(),image.top()+r[1].get<double>()*image.height(),
                    (r[2].get<double>()-r[0].get<double>())*image.width(),(r[3].get<double>()-r[1].get<double>())*image.height());
                bool active=static_cast<int>(d)==device && std::find(selectedIds.begin(),selectedIds.end(),i)!=selectedIds.end();
                if(active && d<effective.size() && i<effective[d].size()) {
                    auto actual=effective[d][i];QRectF sample(image.left()+actual[0].get<double>()*image.width(),image.top()+actual[1].get<double>()*image.height(),(actual[2].get<double>()-actual[0].get<double>())*image.width(),(actual[3].get<double>()-actual[1].get<double>())*image.height());
                    p.setPen(QPen(QColor("#ffd26e"),1,Qt::DashLine));p.drawRect(sample);
                }
                QColor fill=cyan;fill.setAlpha(active?50:12);p.fillRect(box,fill);
                p.setPen(QPen(active?cyan:QColor(200,210,225,70),active?1.7:0.5));p.drawRect(box);
                if(d<colours.size() && i<colours[d].size()) {auto c=colours[d][i];p.fillRect(QRectF(box.left()+1,box.top()+1,std::min(7.,box.width()),std::min(7.,box.height())),QColor(c[0].get<int>(),c[1].get<int>(),c[2].get<int>()));}
                if(active && (led>=0 || selectedIds.size()<=10) && box.width()>18 && box.height()>16) {p.setPen(Qt::white);p.drawText(box,Qt::AlignCenter,QString::number(i));}
            }
        }
        p.restore();p.setPen(cyan);p.drawRect(image);
        if(showCenter) {QPointF c(image.left()+effectCenter.x()*image.width(),image.top()+effectCenter.y()*image.height());p.setPen(QPen(QColor("#ffdc70"),2));p.drawLine(c-QPointF(12,0),c+QPointF(12,0));p.drawLine(c-QPointF(0,12),c+QPointF(0,12));p.setBrush(Qt::NoBrush);p.drawEllipse(c,5,5);}
        if(!selectedIds.empty()) {
            auto b=screenBounds();p.setPen(QPen(cyan,2));p.setBrush(Qt::NoBrush);p.drawRect(b);
            if(blur>0) {p.setPen(QPen(QColor(255,210,110,190),1,Qt::DashLine));p.drawRect(b.adjusted(-blur*image.height(),-blur*image.height(),blur*image.height(),blur*image.height()).intersected(image));}
            p.setPen(QPen(QColor("#00141c"),1));p.setBrush(cyan);
            for(auto x:{b.left(),b.center().x(),b.right()}) for(auto y:{b.top(),b.center().y(),b.bottom()})
                if(x!=b.center().x() || y!=b.center().y()) p.drawEllipse(QPointF(x,y),4,4);
            auto anchor=topAnchor(),normal=topNormal(),rotate=rotationHandle();
            QPointF tangent(-normal.y(),normal.x());
            p.setPen(QPen(cyan,3));p.drawLine(anchor-tangent*std::min(18.,b.width()/3),anchor+tangent*std::min(18.,b.width()/3));
            p.setPen(QPen(cyan,1));p.drawLine(anchor+normal*5,rotate-normal*15);
            p.setBrush(QColor("#061b27"));p.setPen(QPen(cyan,1.5));p.drawEllipse(rotate,15,15);
            p.save();p.translate(rotate);p.rotate(orientation()*90);officeui::symbol(p,{-11,-11,22,22},"restart_alt",cyan,false,21);p.restore();
            p.setPen(Qt::white);p.setBrush(QColor("#061b27"));auto topText=QRectF(anchor+normal*14-QPointF(16,8),QSizeF(32,16));p.drawRoundedRect(topText,3,3);p.drawText(topText,Qt::AlignCenter,"TOP");
            auto values=selected();if(values.size()>1) {
                auto point=[&](Region r){return QPointF(image.left()+(r.left+r.right)*.5*image.width(),image.top()+(r.top+r.bottom)*.5*image.height());};
                auto a=point(values.front()),z=point(values.back());QLineF line(a,z);
                // Local order cue instead of a diagonal across a keyboard.
                auto key=(*layout)[device].value("id",std::string());bool linear=key!="Keychron" && !(key=="Legion" && group==0);
                if(linear && line.length()>16) {double angle=std::atan2(z.y()-a.y(),z.x()-a.x());QPointF u(std::cos(angle),std::sin(angle)),v(-u.y(),u.x());z=a+u*std::min(36.,line.length());p.setPen(QPen(Qt::white,1.5));p.drawLine(a,z);p.drawLine(z,z-u*7+v*4);p.drawLine(z,z-u*7-v*4);}
                p.setPen(Qt::white);p.drawText(QRectF(a.x()+5,a.y()-20,80,18),Qt::AlignLeft,QString("LED %1").arg(selectedIds.front()));
                auto end=point(values.back());p.setBrush(QColor("#ffd26e"));p.setPen(QPen(QColor("#061b27"),1));p.drawEllipse(a,4,4);p.drawEllipse(end,4,4);p.setPen(Qt::white);p.drawText(QRectF(end.x()+5,end.y()+3,90,18),Qt::AlignLeft,QString("LAST %1").arg(selectedIds.back()));
            }
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()==Qt::MiddleButton) {panning=true;panStart=e->position()-pan;setCursor(Qt::ClosedHandCursor);return;}
        if(e->button()!=Qt::LeftButton || !layout || layout->empty()) return;
        if(QLineF(e->position(),rotationHandle()).length()<=16){setFocus();try{rotateSelection();}catch(const std::exception& ex){if(error)error(ex.what());}e->accept();return;}
        auto area=imageRect();auto point=e->position();handle=hitHandle(point);if(!handle && !area.contains(point)) return;
        if(showCenter && QLineF(point,QPointF(area.left()+effectCenter.x()*area.width(),area.top()+effectCenter.y()*area.height())).length()<=10) {draggingCenter=true;setCursor(Qt::CrossCursor);return;}
        QPointF normalized((point.x()-area.left())/area.width(),(point.y()-area.top())/area.height());
        auto selectedValues=selected();auto box=office::bounds(selectedValues);
        if(!handle && !QRectF(QPointF(box.left,box.top),QPointF(box.right,box.bottom)).contains(normalized)) {
            bool hit=false;
            for(int d=static_cast<int>(layout->size())-1;d>=0 && !hit;--d) {
                if(!showAll && d!=device) continue;const auto& regions=(*layout)[d]["regions"];
                for(size_t i=0;i<regions.size();++i) {auto r=regions[i];if(normalized.x()>=r[0].get<double>() && normalized.x()<=r[2].get<double>() && normalized.y()>=r[1].get<double>() && normalized.y()<=r[3].get<double>()) {if(picked) picked(d,static_cast<int>(i));hit=true;break;}}
            }if(!hit) return;
        }
        dragStart=normalized;dragRegions=selected();checkpoint();setFocus();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if(panning) {pan=e->position()-panStart;update();return;}
        if(draggingCenter) {auto area=imageRect();effectCenter={std::clamp((e->position().x()-area.left())/area.width(),0.,1.),std::clamp((e->position().y()-area.top())/area.height(),0.,1.)};if(centerEdited) centerEdited(effectCenter);update();return;}
        if(!(e->buttons()&Qt::LeftButton) || dragRegions.empty()) {if(layout && !layout->empty()) pointerCursor(e->position());return;}
        auto area=imageRect();double dx=(e->position().x()-area.left())/area.width()-dragStart.x(),dy=(e->position().y()-area.top())/area.height()-dragStart.y();
        auto b=office::bounds(dragRegions);
        if(handle) {auto to=office::resizeBounds(b,handle,static_cast<float>(dx),static_cast<float>(dy),.001f,snap?grid:0);setSelected(office::transform(dragRegions,b,to,0,false));return;}
        if(snap) {dx=std::round((b.left+dx)/grid)*grid-b.left;dy=std::round((b.top+dy)/grid)*grid-b.top;}
        dx=std::clamp(dx,-double(b.left),1.-b.right);dy=std::clamp(dy,-double(b.top),1.-b.bottom);
        auto values=dragRegions;for(auto& r:values) {r.left+=static_cast<float>(dx);r.right+=static_cast<float>(dx);r.top+=static_cast<float>(dy);r.bottom+=static_cast<float>(dy);}setSelected(values);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {panning=false;draggingCenter=false;dragRegions.clear();handle=0;if(layout && !layout->empty()) pointerCursor(e->position());}
    void wheelEvent(QWheelEvent* e) override {
        double next=std::clamp(zoom*std::pow(1.2,e->angleDelta().y()/120.0),1.,8.);
        auto anchor=e->position()-QPointF(rect().center());pan=anchor-(anchor-pan)*(next/zoom);zoom=next;update();e->accept();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if(!layout || layout->empty()) return;
        if(e->matches(QKeySequence::Undo)) {undo();return;}if(e->matches(QKeySequence::Redo)) {redo();return;}
        if(e->key()==Qt::Key_R && e->modifiers()==Qt::NoModifier){try{rotateSelection();}catch(const std::exception& ex){if(error)error(ex.what());}return;}
        if(e->key()==Qt::Key_Escape && !dragRegions.empty()) {
            *layout=undoHistory.back();undoHistory.pop_back();dragRegions.clear();handle=0;update();if(edited) edited();return;
        }
        float step=e->modifiers()&Qt::ShiftModifier?.001f:.005f,dx=0,dy=0;
        if(e->key()==Qt::Key_Left) dx=-step;else if(e->key()==Qt::Key_Right) dx=step;
        else if(e->key()==Qt::Key_Up) dy=-step;else if(e->key()==Qt::Key_Down) dy=step;else {QWidget::keyPressEvent(e);return;}
        auto values=selected();auto b=office::bounds(values);dx=std::clamp(dx,-b.left,1-b.right);dy=std::clamp(dy,-b.top,1-b.bottom);
        for(auto& r:values) {r.left+=dx;r.right+=dx;r.top+=dy;r.bottom+=dy;}setSelected(values,true);
    }
};

class OutputPreview : public QWidget {
public:
    QImage image;Json layout,colours;bool overlay=true;
    OutputPreview() {setMinimumSize(300,220);setAccessibleName("Live capture and actual LED colours");}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#020a10"));
        QRectF target(rect().adjusted(16,16,-16,-16));double h=target.width()*2160/5120;
        if(h>target.height()) {double w=target.height()*5120/2160;target.setLeft(target.center().x()-w/2);target.setWidth(w);} else {target.setTop(target.center().y()-h/2);target.setHeight(h);}
        if(!image.isNull()) p.drawImage(target,image);else {p.setPen(Qt::white);p.drawText(target,Qt::AlignCenter,"Waiting for the LG source");}
        if(overlay) for(size_t d=0;d<layout.size() && d<colours.size();++d) for(size_t i=0;i<layout[d]["regions"].size() && i<colours[d].size();++i) {
            auto r=layout[d]["regions"][i],c=colours[d][i];QColor color(c[0].get<int>(),c[1].get<int>(),c[2].get<int>(),185);
            QRectF box(target.left()+r[0].get<double>()*target.width(),target.top()+r[1].get<double>()*target.height(),(r[2].get<double>()-r[0].get<double>())*target.width(),(r[3].get<double>()-r[1].get<double>())*target.height());
            p.fillRect(box,color);p.setPen(QPen(color.lighter(160),.6));p.drawRect(box);
        }
    }
};

class Panel : public QMainWindow {
    Client client;
    QTimer refresh;
    QString state,exportPath;
    Json settings,layout,lastSnapshot;
    uint64_t revision=0;
    bool dirty=false,updating=false,pending=false,exported=false;
    bool exerciseEditor=false;
    bool allowUnavailable=false;
    int editorPhase=0;
    Json editorBaseline;
    uint64_t editorRevision=0;
    std::vector<QLabel*> onlineLabels;
    std::vector<QPushButton*> identifyButtons;
    QElapsedTimer clock;
    QStackedWidget* pages=new QStackedWidget;
    QLabel* connection=new officeui::DotLabel("Connecting to OpenRGB…");
    QLabel* status=new officeui::DotLabel;
    QLabel* dirtyLabel=new QLabel;
    QPushButton* save=new QPushButton("Save changes");
    QCheckBox* enabled=new officeui::Switch("Enable");
    QSpinBox* fps=new Stepper<QSpinBox>;
    QSpinBox* smooth=new Stepper<QSpinBox>;
    std::map<std::string,QDoubleSpinBox*> numbers;
    std::map<std::string,QComboBox*> choices;
    QCheckBox* blackBars=new officeui::Switch("Black bar detect");
    QLabel* deviceSummary=new QLabel;
    QLabel* inventorySummary=new QLabel;
    QLabel* pageTitle=new QLabel;
    QLabel* pageSubtitle=new QLabel;
    QLabel* liveRate=new QLabel;
    QLabel* totalDevices=new QLabel;
    QLabel* mappedDevices=new QLabel;
    QLabel* mapStatus=new QLabel;
    QLabel* testState=new QLabel;
    QLabel* connectionDetail=new officeui::DotLabel;
    QLabel* zoneSummary=new QLabel;
    QLabel* loadingMessage=new QLabel("Connecting to the local lighting owner…");
    QLabel* loadingTitle=nullptr;
    QProgressBar* loadingProgress=nullptr;
    QWidget* footer=nullptr;
    QWidget* dashboardDevices=new QWidget;
    QVBoxLayout* dashboardList=new QVBoxLayout(dashboardDevices);
    QWidget* connectionPage=nullptr;
    QStackedWidget* shellPages=new QStackedWidget;
    QGridLayout* dashboardGrid=nullptr;
    QWidget* dashboardStatus=nullptr;
    std::array<QWidget*,7> dashboardCards{};
    OutputPreview* livePreview=new OutputPreview;
    QSystemTrayIcon* tray=nullptr;
    bool quitRequested=false;
    Json panelOptions;
    bool lifecycleBusy=false;
    QTreeWidget* tree=new QTreeWidget;
    MapCanvas* map=new MapCanvas;
    QLineEdit* zoneName=new QLineEdit;
    QComboBox* led=new ChoiceBox;
    std::array<QDoubleSpinBox*,4> geometry;
    std::array<QDoubleSpinBox*,2> effectCenter;
    QPlainTextEdit* diagnostics=new QPlainTextEdit;
    QWidget* devicePage=new QWidget;
    QVBoxLayout* deviceList=new QVBoxLayout(devicePage);
    QPushButton* stopTest=new QPushButton("Stop test / return to ambience");

    void changed() {if(updating) return;dirty=true;dirtyLabel->setText("Unsaved changes");save->setEnabled(client.ready());}
    void writePanelOption(const char* key,bool value) {
        auto next=panelOptions;next[key]=value;QSaveFile file(state+"/office-panel.json");auto data=QByteArray::fromStdString(next.dump(2));
        if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) {failure("Could not save panel preferences");return;}panelOptions=next;
    }
    void lifecycle(const QString& script) {
        if(lifecycleBusy) return;
        if(dirty) {failure("Save or reload your draft before restarting/stopping lighting.");return;}
        auto runtime=QStandardPaths::findExecutable("pwsh.exe");if(runtime.isEmpty()) {failure("PowerShell 7 is required for the verified lighting launcher.");return;}
        auto path=QCoreApplication::applicationDirPath()+"/office/"+script;
        if(!QFile::exists(path)) {failure("The packaged lighting lifecycle script is missing.");return;}
        auto process=new QProcess(this);process->setProgram(runtime);QStringList arguments{"-NoProfile","-NonInteractive","-File",path,"-StateRoot",state};
        if(script=="Restart-Office.ps1") arguments<<"-InstallRoot"<<QCoreApplication::applicationDirPath();
        process->setArguments(arguments);process->setWorkingDirectory(state);
        auto environment=QProcessEnvironment::systemEnvironment();environment.insert("TEMP",state+"/runtime-temp");environment.insert("TMP",state+"/runtime-temp");process->setProcessEnvironment(environment);
#ifdef Q_OS_WIN
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=0x08000000;}); // CREATE_NO_WINDOW
#endif
        lifecycleBusy=true;refresh.stop();connection->setText(script=="Restart-Office.ps1"?"Restarting lighting…":"Stopping lighting…");
        connect(process,&QProcess::errorOccurred,this,[this,process](auto){lifecycleBusy=false;failure(process->errorString());refresh.start();process->deleteLater();});
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,script](int code,auto state){
            lifecycleBusy=false;if(code) failure("Lighting lifecycle failed: "+QString::fromUtf8(process->readAllStandardError()));
            else if(script=="Restart-Office.ps1") client.reconnect(this->state);else connection->setText("Lighting stopped");
            refresh.start();process->deleteLater();});process->start();
    }
    void buildViews();
    void arrangeDashboard();
    void mapToEdge(int edge) {
        if(!edge || layout.empty())return;auto values=map->selected();size_t count=values.size();if(!count)return;
        float depth=numbers.at("depth")->value()/100;std::vector<Region> result;
        for(size_t i=0;i<count;++i){float a=float(i)/count,b=float(i+1)/count;
            result.push_back(edge==1?Region{a,0,b,depth}:edge==2?Region{1-depth,a,1,b}:edge==3?Region{1-b,1-depth,1-a,1}:edge==4?Region{0,1-b,depth,1-a}:Region{a,0,b,1});}
        map->setSelected(result,true);
    }
    void autoArrange() {
        if(layout.empty())return;auto next=layout;
        // Deterministic strips stacked in the image, while matrix keys retain
        // their internal arrangement and every vector index remains hardware ID.
        size_t groups=0;for(auto& d:next)groups+=d["groups"].size();size_t row=0;
        for(auto& d:next)for(auto& g:d["groups"]){std::vector<Region> regions;unsigned first=g["first"],count=g["count"];
            for(unsigned i=first;i<first+count;++i){auto r=d["regions"][i];regions.push_back({r[0],r[1],r[2],r[3]});}
            float y=float(row++)/groups,h=std::min(.08f,1.f/groups);auto transformed=office::transform(regions,office::bounds(regions),{.05f,y,.95f,y+h},0,false);
            for(unsigned i=0;i<count;++i)d["regions"][first+i]=office::encode(transformed[i]);}
        map->replaceLayout(next);showGeometry();
    }
    QWidget* groupBox(const QString& title,QLayout* inside) {auto group=new officeui::Card(title);group->content->addLayout(inside);return group;}
    QWidget* number(const std::string& key,double minimum,double maximum,double step,const QString& suffix={}) {
        auto row=new QWidget;auto line=new QVBoxLayout(row);line->setContentsMargins(0,0,0,0);line->setSpacing(0);
        auto slider=new officeui::Slider;auto spin=new Stepper<QDoubleSpinBox>;
        spin->setRange(minimum,maximum);spin->setSingleStep(step);spin->setDecimals(step>=1?0:2);spin->setSuffix(suffix);spin->setFixedWidth(100);spin->setProperty("readout",true);spin->setMinimumHeight(30);spin->setMaximumHeight(32);
        spin->setAccessibleName(QString::fromStdString(key));slider->setAccessibleName(QString::fromStdString(key)+" slider");
        slider->setRange(0,qRound((maximum-minimum)/step));
        connect(spin,&QDoubleSpinBox::valueChanged,slider,[=](double n){slider->setValue(qRound((n-minimum)/step));});
        connect(slider,&QSlider::valueChanged,spin,[=](int n){spin->setValue(minimum+n*step);});
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,key](double n){if(key=="blur") {map->blur=n/100;map->update();}changed();});
        numbers[key]=spin;line->addWidget(spin,0,Qt::AlignRight);line->addWidget(slider);return row;
    }
    QComboBox* choice(const std::string& key,std::initializer_list<std::pair<const char*,const char*>> entries) {
        auto combo=new ChoiceBox;for(auto [label,value]:entries) combo->addItem(label,value);
        combo->setAccessibleName(QString::fromStdString(key));choices[key]=combo;
        connect(combo,&QComboBox::currentIndexChanged,this,[this]{changed();});return combo;
    }
    void preset(const QString& name) {
        Json c=office::processingDefaults();c["fps"]=15;c["smoothing_ms"]=140;
        if(name!="Reset") {c["strategy"]="adaptive";c["smoothing_mode"]="ema";c["scene_cut"]=60;c["black_bar_detect"]=true;c["saturation"]=1.4;
            if(name=="Cinema") {c["ema_alpha"]=.2;c["scene_cut"]=80;c["blur"]=2;}
            if(name=="Gaming") {c["fps"]=30;c["ema_alpha"]=.7;c["scene_cut"]=40;}
            if(name=="Calm") {c["ema_alpha"]=.1;c["scene_cut"]=255;c["blur"]=4;c["brightness"]=.6;}}
        for(auto& [key,box]:numbers) box->setValue(c.at(key).get<double>());
        for(auto& [key,combo]:choices) combo->setCurrentIndex(combo->findData(text(c.at(key))));
        blackBars->setChecked(c.at("black_bar_detect"));fps->setValue(c.at("fps"));smooth->setValue(c.at("smoothing_ms"));changed();
    }
    void trace(const QString& message) {
        if(exportPath.isEmpty()) return;
        QFile log(exportPath+".events.jsonl");
        if(log.open(QIODevice::WriteOnly|QIODevice::Append)) log.write(QByteArray::fromStdString(Json{{"elapsed_ms",clock.elapsed()},{"event",message.toStdString()}}.dump())+'\n');
    }
    void failure(const QString& message) {trace("error: "+message);qWarning().noquote()<<message;connection->setText("Connection needs attention");connection->setToolTip(message);connection->setStyleSheet("color:#ff9f1c");loadingTitle->setText("Connection unavailable");loadingTitle->setStyleSheet("color:#ffb54a");loadingProgress->hide();loadingMessage->setText(message);connectionDetail->setText(message);if(!client.ready())shellPages->setCurrentIndex(1);}
    void response(const Json& value,std::function<void(const Json&)> then={}) {
        pending=false;
        if(!value.value("ok",false)) {failure(text(value.at("error")));return;}
        connection->setText("Connected");connection->setStyleSheet("color:#22d3a5");connectionDetail->setText("Connected");connection->setToolTip("Native OpenRGB owner");shellPages->setCurrentIndex(0);
        if(then) then(value.at("result"));
    }
    void load(const Json& snapshot,bool force=false) {
        trace("snapshot-accepted");
        lastSnapshot=snapshot;
        if(force || !revision || (!dirty && snapshot.at("revision").get<uint64_t>()!=revision)) {
            int selectedDevice=map->device,selectedGroup=map->group;
            updating=true;revision=snapshot.at("revision");settings=snapshot.at("settings");layout=snapshot.at("layout");
            enabled->setChecked(settings.at("enabled"));fps->setValue(settings.at("fps"));smooth->setValue(static_cast<int>(settings.at("smoothing_ms").get<double>()));
            for(auto& [key,box]:numbers) box->setValue(settings.value(key,office::processingDefaults().at(key)).get<double>());
            for(auto& [key,combo]:choices) combo->setCurrentIndex(combo->findData(text(settings.value(key,office::processingDefaults().at(key)))));
            blackBars->setChecked(settings.value("black_bar_detect",false));
            effectCenter[0]->setValue(settings.value("effect_center_x",.5)*100);effectCenter[1]->setValue(settings.value("effect_center_y",.5)*100);map->effectCenter={effectCenter[0]->value()/100,effectCenter[1]->value()/100};
            map->blur=settings.value("blur",0.f)/100;map->resetHistory();
            map->layout=&layout;map->device=map->group=0;map->led=-1;buildTree();buildDevices();
            dirty=false;dirtyLabel->setText("Saved");save->setEnabled(false);updating=false;select(selectedDevice,selectedGroup);
        }
        auto s=snapshot.at("status");
        deviceSummary->setText(QString("%1 / %2 devices online  ·  %3 mapped LEDs").arg(s.at("online_devices").get<int>()).arg(layout.size()).arg(s.at("leds").get<int>()));
        inventorySummary->setText(deviceSummary->text());
        totalDevices->setText(QString::number(layout.size()));mappedDevices->setText(QString::number(layout.size()));
        status->setText(s.value("standby",false)?"STANDBY":settings.value("enabled",true)?"ACTIVE":"INACTIVE");
        status->setToolTip(text(s.at("mode"))+" · "+text(s.at("error")));liveRate->setText(QString("%1 fps output").arg(s.at("output_fps").get<double>(),0,'f',1));
        testState->setText(snapshot.at("test_mode")=="none"?"Ambience running":"Temporary test active");
        diagnostics->setPlainText(QString::fromStdString(s.dump(2)));
        map->colours=snapshot.at("colors");
        map->effective=snapshot.value("effective_regions",Json::array());
        if(snapshot.contains("online")) for(size_t d=0;d<onlineLabels.size();++d) {
            bool online=snapshot.at("online").at(d);onlineLabels[d]->setText(online?"Connected":"Offline · map retained");
            onlineLabels[d]->setStyleSheet(online?"color:#22d3a5":"color:#ff9f1c");
            identifyButtons[d]->setEnabled(online);
        }
        auto png=QByteArray::fromBase64(QByteArray::fromStdString(snapshot.value("preview_png",std::string())));
        if(!png.isEmpty()) map->preview.loadFromData(png,"PNG");map->update();
        livePreview->image=map->preview;livePreview->layout=layout;livePreview->colours=map->colours;livePreview->update();
        trace(QString("preview %1 bytes decoded=%2").arg(png.size()).arg(!map->preview.isNull()));
        if(!snapshot.value("preview_error",std::string()).empty()) {
            failure(text(snapshot.at("preview_error")));
            if(!exportPath.isEmpty()) QCoreApplication::exit(1);
        }
        stopTest->setEnabled(snapshot.at("test_mode")!="none");
        if(!exportPath.isEmpty() && !exported && (!map->preview.isNull() || allowUnavailable)) {
            if(exerciseEditor && editorPhase<3) exerciseSave();else exportViews();
        }
    }
    void poll() {
        if(pending || !client.ready()) return;
        pending=true;client.send({{"command","snapshot"},{"preview",pages->currentIndex()==1 || pages->currentIndex()==4 || !exportPath.isEmpty()}},[this](const Json& r){response(r,[this](const Json& s){load(s);});});
    }
    void action(const Json& request) {client.send(request,[this](const Json& r){response(r);poll();});}
    void buildTree() {
        tree->clear();
        for(size_t d=0;d<layout.size();++d) {
            auto* item=new QTreeWidgetItem(tree,{text(layout[d]["name"])});item->setData(0,Qt::UserRole,static_cast<int>(d));item->setData(0,Qt::UserRole+1,-1);
            item->setCheckState(0,layout[d]["enabled"].get<bool>()?Qt::Checked:Qt::Unchecked);
            for(size_t g=0;g<layout[d]["groups"].size();++g) {
                const auto& group=layout[d]["groups"][g];
                auto* child=new QTreeWidgetItem(item,{QString("%1 · %2 LEDs").arg(text(group["name"])).arg(group["count"].get<unsigned>())});
                child->setData(0,Qt::UserRole,static_cast<int>(d));child->setData(0,Qt::UserRole+1,static_cast<int>(g));
                child->setCheckState(0,group.value("enabled",true)?Qt::Checked:Qt::Unchecked);
            }item->setExpanded(true);
        }
        tree->setColumnWidth(0,210);
    }
    void buildDevices() {
        onlineLabels.clear();identifyButtons.clear();
        while(auto* item=deviceList->takeAt(0)) {if(item->widget()) {item->widget()->hide();item->widget()->deleteLater();}delete item;}
        while(auto* item=dashboardList->takeAt(0)) {if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}
        auto editZone=[this](size_t d,size_t g){select(d,g);tree->setCurrentItem(tree->topLevelItem(d)->child(g));pages->setCurrentIndex(1);};
        for(size_t d=0;d<layout.size();++d) {
            auto key=layout[d].value("id",std::string());bool matrix=key=="Keychron" || key=="Legion";
            QString glyph=matrix?"keyboard":key=="Logitech"?"fiber_manual_record":"linear_scale";
            auto card=new officeui::Card;card->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);auto row=new QHBoxLayout;row->setSpacing(12);
            auto mark=new QLabel;mark->setPixmap(officeui::icon(glyph,matrix?QColor("#a78bfa"):officeui::accent).pixmap(26,26));mark->setFixedSize(38,38);row->addWidget(mark);
            auto detail=new QVBoxLayout;auto name=officeui::label(text(layout[d]["name"]));name->setFont(officeui::body(16,QFont::Bold));detail->addWidget(name);
            auto metadata=officeui::label(QString("%1  ·  %2 LEDs  ·  %3 zones").arg(matrix?"MATRIX":"LINEAR").arg(layout[d]["regions"].size()).arg(layout[d]["groups"].size()),"hint");detail->addWidget(metadata);row->addLayout(detail,1);
            auto check=new officeui::Switch;check->setAccessibleName("Include "+text(layout[d]["name"])+" in lighting");check->setChecked(layout[d]["enabled"]);
            auto identify=officeui::button("Flash","bolt");identify->setAccessibleName("Identify "+text(layout[d]["name"]));
            identifyButtons.push_back(identify);
            bool connected=lastSnapshot.contains("online") && lastSnapshot["online"].at(d).get<bool>();
            auto online=new QLabel(connected?"Connected":"Offline · map retained");online->setStyleSheet(connected?"color:#22d3a5":"color:#ff9f1c");onlineLabels.push_back(online);identify->setEnabled(connected);
            row->addWidget(online);row->addWidget(check);auto edit=officeui::button("Edit topology","hub");row->addWidget(edit);row->addWidget(identify);
            connect(edit,&QPushButton::clicked,this,[editZone,d]{editZone(d,0);});
            connect(check,&QCheckBox::toggled,this,[this,d](bool on){layout[d]["enabled"]=on;changed();syncTree();buildDevices();});
            connect(identify,&QPushButton::clicked,this,[this,d]{action({{"command","identify"},{"device",layout[d]["id"]}});});
            card->content->addLayout(row);
            for(size_t g=0;g<layout[d]["groups"].size();++g) {
                auto groupRow=new QHBoxLayout;groupRow->setContentsMargins(50,0,0,0);auto on=new officeui::Switch(text(layout[d]["groups"][g]["name"]));on->setChecked(layout[d]["groups"][g].value("enabled",true));
                auto zoneEdit=officeui::button("Map zone","hub");groupRow->addWidget(on,1);groupRow->addWidget(zoneEdit);card->content->addLayout(groupRow);
                connect(on,&QCheckBox::toggled,this,[this,d,g](bool enabled){layout[d]["groups"][g]["enabled"]=enabled;changed();syncTree();buildDevices();});
                connect(zoneEdit,&QPushButton::clicked,this,[editZone,d,g]{editZone(d,g);});
            }
            card->content->activate();deviceList->addWidget(card);
            auto fade=new QGraphicsOpacityEffect(card);fade->setOpacity(layout[d].value("enabled",true)?1.:.48);card->setGraphicsEffect(fade);
            auto dashRow=new QHBoxLayout;auto dashIcon=new QLabel;dashIcon->setPixmap(officeui::icon(glyph).pixmap(20,20));dashRow->addWidget(dashIcon);
            auto dashName=new officeui::Switch(text(layout[d]["name"]));dashName->setChecked(layout[d]["enabled"]);dashRow->addWidget(dashName,1);
            dashRow->addWidget(officeui::label(QString("%1 LEDs · %2 zones").arg(layout[d]["regions"].size()).arg(layout[d]["groups"].size()),"hint"));
            auto dashEdit=officeui::button("Topology","hub");dashRow->addWidget(dashEdit);auto expand=officeui::button("Zones","hub");expand->setCheckable(true);dashRow->addWidget(expand);auto dashWidget=new QWidget;auto dashLayout=new QVBoxLayout(dashWidget);dashLayout->addLayout(dashRow);auto zones=new QWidget;auto zoneRows=new QVBoxLayout(zones);zoneRows->setContentsMargins(42,0,0,0);for(size_t g=0;g<layout[d]["groups"].size();++g){auto zone=officeui::button(text(layout[d]["groups"][g]["name"]),"hub");zoneRows->addWidget(zone);connect(zone,&QPushButton::clicked,this,[editZone,d,g]{editZone(d,g);});}zones->hide();dashLayout->addWidget(zones);connect(expand,&QPushButton::toggled,zones,&QWidget::setVisible);dashboardList->addWidget(dashWidget);
            connect(dashName,&QCheckBox::toggled,this,[this,d](bool on){layout[d]["enabled"]=on;changed();syncTree();buildDevices();});connect(dashEdit,&QPushButton::clicked,this,[editZone,d]{editZone(d,0);});
        }dashboardList->addStretch();
        if(layout.empty()){deviceList->addStretch();auto empty=new officeui::Card("No devices available","device_hub");empty->content->addWidget(officeui::label("Offline maps stay available. Reconnect the native owner or return the KVM devices to this PC."));auto reconnect=officeui::button("Reconnect","refresh");empty->content->addWidget(reconnect);connect(reconnect,&QPushButton::clicked,this,[this]{client.reconnect(state);});deviceList->addWidget(empty);}
        deviceList->addStretch();deviceList->activate();devicePage->setMinimumHeight(deviceList->minimumSize().height());devicePage->updateGeometry();
        totalDevices->setText(QString::number(layout.size()));mappedDevices->setText(QString::number(layout.size()));
        if(layout.empty())inventorySummary->setText("No device maps loaded · reconnect to refresh inventory");
    }
    void syncTree() {
        QSignalBlocker blocker(tree);int d=map->device,g=map->group;buildTree();
        if(d>=0 && d<tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(d)->child(g));
    }
    void select(int device,int group) {
        if(layout.empty()) return;updating=true;map->device=device;map->group=group;map->led=-1;
        auto& g=layout[device]["groups"][group];zoneName->setText(text(g["name"]));
        led->clear();led->addItem("Entire zone",-1);
        for(unsigned i=g["first"].get<unsigned>();i<g["first"].get<unsigned>()+g["count"].get<unsigned>();++i)
            led->addItem(QString("LED %1 · %2").arg(i).arg(text(layout[device]["led_names"][i])),static_cast<int>(i));
        updating=false;showGeometry();map->update();
    }
    void showGeometry() {
        if(layout.empty()) return;bool before=updating;updating=true;
        auto b=office::bounds(map->selected());std::array<double,4> values={b.left*100,b.top*100,(b.right-b.left)*100,(b.bottom-b.top)*100};
        for(size_t i=0;i<4;++i) geometry[i]->setValue(values[i]);updating=before;
        auto& g=layout[map->device]["groups"][map->group];zoneSummary->setText(QString("%1\n%2 LEDs · physical indices %3–%4").arg(text(g["name"])).arg(g["count"].get<int>()).arg(g["first"].get<int>()).arg(g["first"].get<int>()+g["count"].get<int>()-1));
        mapStatus->setText(QString("Drag to move · corners resize · middle-drag pans · scroll zooms\nX %1%   Y %2%   W %3%   H %4% · cyan draft / amber effective sampling").arg(values[0],0,'f',1).arg(values[1],0,'f',1).arg(values[2],0,'f',1).arg(values[3],0,'f',1));
    }
    void applyGeometry(int rotation=0,bool reverse=false) {
        if(updating || layout.empty()) return;
        if(rotation){try{map->rotateSelection();}catch(const std::exception& e){failure(e.what());}return;}
        auto values=map->selected();auto from=office::bounds(values);
        Region to{static_cast<float>(geometry[0]->value()/100),static_cast<float>(geometry[1]->value()/100),0,0};
        to.right=to.left+static_cast<float>(geometry[2]->value()/100);to.bottom=to.top+static_cast<float>(geometry[3]->value()/100);
        try {map->setSelected(office::transform(values,from,to,rotation,reverse),true);}
        catch(std::exception const& e) {failure(e.what());showGeometry();}
    }
    void saveChanges() {
        if(pending || !revision) return;
        settings["enabled"]=enabled->isChecked();settings["fps"]=fps->value();settings["smoothing_ms"]=smooth->value();
        for(auto& [key,box]:numbers) settings[key]=box->value();
        for(auto& [key,combo]:choices) settings[key]=combo->currentData().toString().toStdString();
        settings["black_bar_detect"]=blackBars->isChecked();
        settings["effect_center_x"]=effectCenter[0]->value()/100;settings["effect_center_y"]=effectCenter[1]->value()/100;
        pending=true;save->setEnabled(false);
        client.send({{"command","save"},{"revision",revision},{"settings",settings},{"layout",layout}},[this](const Json& r){
            response(r,[this](const Json& s){load(s,true);});if(dirty) save->setEnabled(true);
        });
    }
    void exportViews() {
        exported=true;QDir().mkpath(exportPath);
        Json result={{"native_revision",revision},{"devices",layout.size()},{"editor_save_roundtrip",exerciseEditor && editorPhase==3},
            {"actual_preview",!map->preview.isNull()},{"source_error",lastSnapshot.at("status").value("error",std::string())},
            {"font_family",font().family().toStdString()},{"views",Json::array()},{"design","EDAmbilight recovered native assets"}};
        for(auto size:{QSize(1000,700),QSize(1440,900),QSize(1920,1080)}) {
            resize(size);
            int device=size.width()==1000?0:size.width()==1440?2:1;
            select(device,0);tree->setCurrentItem(tree->topLevelItem(device)->child(0));
            for(int page=0;page<pages->count();++page) {
                pages->setCurrentIndex(page);QApplication::sendPostedEvents();centralWidget()->layout()->activate();
                auto file=QString("%1/%2x%3-page%4.png").arg(exportPath).arg(size.width()).arg(size.height()).arg(page);
                if(!grab().save(file)) throw std::runtime_error("Could not export the native panel image");
                auto settingsColumn=findChild<QWidget*>("settingsColumn");
                result["views"].push_back({{"path",file.toStdString()},{"width",width()},{"height",height()},{"page",page},
                    {"page_title",pageTitle->text().toStdString()},{"rail_width",findChild<QWidget*>("navigation")->width()},
                    {"settings_column_width",settingsColumn->width()},{"map_save_visible",footer->isVisible()}});
            }
        }
        // Supplemental presentations are local UI fixtures, never saved to the
        // hardware owner. They expose states absent from the live gallery.
        resize(1440,900);result["states"]=Json::array();
        auto renderState=[this,&result](const char* name){QApplication::sendPostedEvents();centralWidget()->layout()->activate();auto file=exportPath+"/state-"+name+".png";if(!grab().save(file))throw std::runtime_error("Could not export UI state");result["states"].push_back({{"name",name},{"path",file.toStdString()},{"fixture",true}});};
        pages->setCurrentIndex(0);auto disclosure=findChild<QPushButton*>("advancedOptions");disclosure->setChecked(true);renderState("advanced");disclosure->setChecked(false);
        pages->setCurrentIndex(1);select(0,0);tree->setCurrentItem(tree->topLevelItem(0)->child(0));auto beforeRotation=layout;map->rotateSelection();showGeometry();renderState("rotation-quarter-turn");layout=beforeRotation;map->resetHistory();map->update();
        auto retainedLayout=layout,retainedSnapshot=lastSnapshot;
        lastSnapshot["online"]=std::vector<bool>(layout.size(),false);buildDevices();pages->setCurrentIndex(2);renderState("offline");
        layout=Json::array();buildDevices();renderState("empty");layout=retainedLayout;lastSnapshot=retainedSnapshot;buildDevices();
        shellPages->setCurrentIndex(1);loadingTitle->setText("Connection unavailable");loadingTitle->setStyleSheet("color:#ffb54a");loadingProgress->hide();loadingMessage->setText("The local lighting owner is unavailable. Reconnect, or continue to Settings to inspect its connection.");renderState("connection-unavailable");shellPages->setCurrentIndex(0);
        QFile output(exportPath+"/panel-export.json");output.open(QIODevice::WriteOnly);output.write(QByteArray::fromStdString(result.dump(2)));
        QCoreApplication::quit();
    }
    void exerciseSave() {
        // Explicit diagnostic mode drives the same widgets/signals as the user.
        // It restores the complete map before any rendered acceptance evidence.
        if(editorPhase==0) {
            editorBaseline=layout;editorRevision=revision;select(0,0);
            map->rotateSelection();
            zoneName->setText(text(editorBaseline[0]["groups"][0]["name"])+" [UI save check]");
            QMetaObject::invokeMethod(zoneName,"editingFinished",Qt::DirectConnection);
            if(!dirty || !save->isEnabled()) throw std::runtime_error("Editor widget did not produce a saveable draft");
            editorPhase=1;save->click();
        } else if(editorPhase==1) {
            if(revision!=editorRevision+1 || layout[0]["groups"][0]["name"]==editorBaseline[0]["groups"][0]["name"])
                throw std::runtime_error("Real Save button did not persist the edited name");
            int previous=editorBaseline[0].contains("rotation_quarters")?editorBaseline[0]["rotation_quarters"][0].get<int>():0;
            if(layout[0]["rotation_quarters"][0]!=(previous+1)%4)throw std::runtime_error("Rotation metadata was not persisted by the real Save action");
            map->replaceLayout(editorBaseline);select(0,0);zoneName->setText(text(editorBaseline[0]["groups"][0]["name"]));
            QMetaObject::invokeMethod(zoneName,"editingFinished",Qt::DirectConnection);
            editorPhase=2;save->click();
        } else {
            if(layout!=editorBaseline || revision!=editorRevision+2) throw std::runtime_error("Editor failed to restore the original map");
            editorPhase=3;trace("editor-save-roundtrip-restored");exportViews();
        }
    }
public:
    Panel(QString stateDirectory,QString exportDirectory,bool exercise=false,bool unavailable=false):state(std::move(stateDirectory)),exportPath(std::move(exportDirectory)),exerciseEditor(exercise),allowUnavailable(unavailable) {
        clock.start();client.trace=[this](QString event){trace(event);};trace("panel-constructing");
        QFile preferences(state+"/office-panel.json");if(preferences.open(QIODevice::ReadOnly)) {auto data=preferences.readAll();panelOptions=Json::parse(data.constData(),data.constData()+data.size(),nullptr,false);}if(!panelOptions.is_object()) panelOptions=Json::object();
        setWindowTitle("Office Ambilight · OpenRGB");resize(1440,900);setMinimumSize(1000,700);
        qApp->setProperty("officeReducedMotion",panelOptions.value("reduced_motion",false));buildViews();
        map->edited=[this]{changed();showGeometry();if(!layout.empty()) {bool prior=updating;updating=true;zoneName->setText(text(layout[map->device]["groups"][map->group]["name"]));updating=prior;}};
        map->error=[this](QString message){failure(message);};
        map->picked=[this](int d,int index){for(size_t g=0;g<layout[d]["groups"].size();++g) {const auto& group=layout[d]["groups"][g];if(index>=group["first"].get<int>() && index<group["first"].get<int>()+group["count"].get<int>()) {select(d,g);tree->setCurrentItem(tree->topLevelItem(d)->child(g));break;}}};
        client.error=[this](QString message){pending=false;save->setEnabled(false);failure(message);if(!this->exportPath.isEmpty()) QCoreApplication::exit(2);};client.connected=[this]{poll();};
        connect(&refresh,&QTimer::timeout,this,[this]{poll();});refresh.start(1000);
        // Opening after construction also lets a real connection failure exit
        // diagnostic mode through the running application event loop.
        QMetaObject::invokeMethod(this,[this]{client.open(state);},Qt::QueuedConnection);
    }
    void closeEvent(QCloseEvent* event) override {
        // Export fixtures edit only their private in-memory presentation after
        // the live Save check has restored the owner's exact original map.
        // Do not open an invisible unsaved-draft dialog on diagnostic shutdown.
        if(exported && !exportPath.isEmpty()){event->accept();return;}
        if(lifecycleBusy) {failure("Wait for the lighting lifecycle operation to finish.");event->ignore();return;}
        if(tray && !quitRequested && panelOptions.value("close_to_tray",true)) {hide();event->ignore();return;}
        if(dirty && QMessageBox::question(this,"Unsaved map","Close without saving your changes?")!=QMessageBox::Yes) {event->ignore();return;}
        action({{"command","stop_test"}});event->accept();
    }
    void resizeEvent(QResizeEvent* e) override {QMainWindow::resizeEvent(e);arrangeDashboard();pageSubtitle->setVisible(width()>=1200);}
};
}

namespace {
#include "OfficePanelViews.inc"
}

#ifndef OFFICE_WIDGET_TEST
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("Office Ambilight");
    officeui::loadFonts();
    QCommandLineParser parser;parser.addHelpOption();parser.addOption({"state","Private OpenRGB state directory","directory","D:/Tools/OpenRGB/state-integration"});
    parser.addOption({"export-views","Export all real connected panel views, then exit","directory"});
    parser.addOption({"allow-unavailable-source","Export the real waiting/error state if the LG source is unavailable; not live capture acceptance"});
    parser.addOption({"exercise-editor","Verify the real Save button and restore the original map before exporting"});parser.process(app);
    if(parser.isSet("exercise-editor") && !parser.isSet("export-views")) parser.showHelp(1);
    Panel panel(parser.value("state"),parser.value("export-views"),parser.isSet("exercise-editor"),parser.isSet("allow-unavailable-source"));panel.show();return app.exec();
}
#endif
