// Native control panel for the office plugin. Recovered EDAmbilight layout and
// interaction contract: EDAMBILIGHT-GUI-REVIEW.md; no second RGB/capture writer.
#include "OfficeLayout.h"
#include "OfficeProcessing.h"
#include "OfficeStyle.h"
#include "OfficePanel.h"
#include "OfficeScreenPolicy.h"
#include "OfficeSundial.h"
#include "OfficeWindow.h"
#include "OfficeLightController.h"
#include <QTimeEdit>
#include <QDateTimeEdit>
#include <QActionGroup>
#include <QKeySequenceEdit>
#include <QPointer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QColorDialog>
#include <RGBControllerInterface.h>
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
#include <QLocalServer>
#include <QCryptographicHash>
#include <QLockFile>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
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
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QThread>
#include <QMenu>
#include <QStyle>
#include <QProgressBar>
#include <QGraphicsOpacityEffect>
#include <QFileDialog>
#include <functional>
#include <map>
#include <array>
#include <set>

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
    OfficeDispatch direct;
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
        if(direct) {if(connected) connected();return;}
        if(trace) trace("opening-endpoint-record");
        QFile file(state+"/office-control.json");
        if(!file.open(QIODevice::ReadOnly)) {if(error) error("Start the Office Ambience owner before opening the panel.");return;}
        auto bytes=file.readAll();auto info=Json::parse(bytes.constData(),bytes.constData()+bytes.size(),nullptr,false);
        if(!info.is_object() || info.value("protocol",0)!=1) {if(error) error("The installed control endpoint is incompatible.");return;}
        if(!info.contains("token") || !info["token"].is_string() || !info.contains("endpoint") || !info["endpoint"].is_string()) {if(error) error("The private endpoint record is invalid.");return;}
        token=info.at("token").get<std::string>();pending.clear();socket.connectToServer(text(info.at("endpoint")));
    }
    bool ready() const {return bool(direct) || socket.state()==QLocalSocket::ConnectedState;}
    void reconnect(const QString& directory) {socket.abort();callbacks.clear();pending.clear();open(directory);}
    void send(Json request,std::function<void(const Json&)> callback) {
        if(direct) {QPointer<Client> alive(this);direct(std::move(request),[alive,callback=std::move(callback)](const Json& reply){if(alive)callback(reply);});return;}
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
    enum class View {SelectedDevices,SelectedZones,AllDevices};
    View view=View::SelectedDevices;
    // Device/group pairs; group -1 denotes every zone of a device. Empty keeps
    // the original single-zone API for existing editor commands.
    std::vector<std::pair<int,int>> zones;
    bool showGrid=true,snap=false;
    bool showCenter=false;
    QPointF effectCenter{.5,.5};
    std::function<void(QPointF)> centerEdited;
    double grid=.01;
    float blur=0;
    std::function<void()> edited;
    std::function<void(int,int,Qt::KeyboardModifiers)> picked;
    std::function<void(QString)> error;
    MapCanvas() {setMinimumSize(280,210);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setAccessibleName("Main display sampling map");}
    QRectF imageGeometry() const {return imageRect();}
    QRectF selectionGeometry() const {return screenBounds();}
    int orientation() const {auto ids=targets();if(ids.empty())return 0;const auto& d=layout->at(ids.front().first);if(!d.contains("rotation_quarters"))return 0;return d["rotation_quarters"].at(ids.front().second).get<int>();}
    QPointF topNormal() const {switch(orientation()){case 1:return {1,0};case 2:return {0,1};case 3:return {-1,0};default:return {0,-1};}}
    QPointF topAnchor() const {auto b=screenBounds();auto n=topNormal();return b.center()+QPointF(n.x()*b.width()/2,n.y()*b.height()/2);}
    QPointF rotationHandle() const {auto p=topAnchor()+topNormal()*38;return {std::clamp(p.x(),17.,width()-17.),std::clamp(p.y(),17.,height()-17.)};}
    void rotateSelection() {
        auto values=selected();if(values.empty())return;auto from=office::bounds(values);
        float w=(from.bottom-from.top)*2160.f/5120.f,h=(from.right-from.left)*5120.f/2160.f;
        if(w>1 || h>1)throw std::runtime_error("Resize this zone before rotating it inside the main display image.");
        float cx=(from.left+from.right)/2,cy=(from.top+from.bottom)/2;
        float x=std::clamp(cx-w/2,0.f,1-w),y=std::clamp(cy-h/2,0.f,1-h);
        auto next=office::transform(values,from,{x,y,x+w,y+h},90,false);
        checkpoint();for(auto [d,id]:targets()) {
            auto& turns=(*layout)[d]["rotation_quarters"];
            if(turns.is_null())turns=std::vector<int>((*layout)[d]["regions"].size(),0);
            turns[id]=(turns[id].get<int>()+1)%4;
        }
        setSelected(next);
    }
    void resetHistory() {undoHistory.clear();redoHistory.clear();}
    void replaceLayout(const Json& value) {checkpoint();*layout=value;update();if(edited) edited();}
    void undo() {if(undoHistory.empty()) return;redoHistory.push_back(*layout);*layout=undoHistory.back();undoHistory.pop_back();update();if(edited) edited();}
    void redo() {if(redoHistory.empty()) return;undoHistory.push_back(*layout);*layout=redoHistory.back();redoHistory.pop_back();update();if(edited) edited();}
    void fit() {zoom=1;pan={};update();}
    std::vector<std::pair<int,unsigned>> targets() const {
        std::set<std::pair<int,unsigned>> ids;
        if(!layout || layout->empty())return {};
        auto include=[&](int d,int g) {
            if(d<0 || d>=static_cast<int>(layout->size()))return;
            const auto& groups=layout->at(d).at("groups");
            for(size_t n=0;n<groups.size();++n)if(g<0 || static_cast<int>(n)==g) {
                unsigned first=groups[n].at("first"),count=groups[n].at("count");
                for(unsigned i=first;i<first+count;++i)ids.insert({d,i});
            }
        };
        if(zones.empty())include(device,group);else for(auto [d,g]:zones)include(d,g);
        if(led>=0 && zones.size()<=1) {ids.clear();ids.insert({device,static_cast<unsigned>(led)});}
        return {ids.begin(),ids.end()};
    }
    bool multipleZones() const {
        return zones.size()>1 || (zones.size()==1 && zones.front().second<0);
    }
    bool visible(int d,unsigned i) const {
        if(view==View::AllDevices)return true;
        auto matches=[&](int target,int g) {
            if(d!=target)return false;
            if(view==View::SelectedDevices || g<0)return true;
            if(led>=0 && zones.size()<=1)return i==static_cast<unsigned>(led);
            const auto& zone=layout->at(d).at("groups").at(g);
            return i>=zone.at("first").get<unsigned>() && i<zone.at("first").get<unsigned>()+zone.at("count").get<unsigned>();
        };
        if(zones.empty())return matches(device,group);
        return std::any_of(zones.begin(),zones.end(),[&](auto z){return matches(z.first,z.second);});
    }
    std::vector<Region> selected() const {
        std::vector<Region> out;if(!layout) return out;
        for(auto [d,i]:targets()) {auto r=layout->at(d).at("regions").at(i);out.push_back({r[0],r[1],r[2],r[3]});}return out;
    }
    void setSelected(const std::vector<Region>& values,bool remember=false) {
        if(remember) checkpoint();
        auto ids=targets();if(values.size()!=ids.size())throw std::runtime_error("Selected LED count changed during editing");
        for(size_t i=0;i<ids.size();++i) (*layout)[ids[i].first]["regions"][ids[i].second]=office::encode(values[i]);
        update();if(edited) edited();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto image=imageRect();
        p.fillRect(rect(),QColor("#020a10"));
        if(!preview.isNull()) p.drawImage(image,preview);
        else {p.fillRect(image,QColor("#102333"));p.setPen(QColor("#cdd6e0"));p.drawText(image,Qt::AlignCenter,"Waiting for a live main display preview");}
        p.save();p.setClipRect(image.intersected(rect()));
        if(showGrid) {
            // White difference composition inverts the preview only. Fill the
            // union once: separate strokes would invert intersections twice.
            QPainterPath lines;lines.setFillRule(Qt::WindingFill);
            const double scale=p.deviceTransform().m11(),pixel=1./scale;
            for(int line=1;line*grid<1; ++line) {
                const double at=line*grid;
                double x=std::round((image.left()+image.width()*at)*scale)/scale;
                double y=std::round((image.top()+image.height()*at)*scale)/scale;
                lines.addRect(QRectF(x,image.top(),pixel,image.height()));
                lines.addRect(QRectF(image.left(),y,image.width(),pixel));
            }
            p.save();p.setRenderHint(QPainter::Antialiasing,false);
            p.setCompositionMode(QPainter::CompositionMode_Difference);
            p.fillPath(lines,Qt::white);p.restore();
        }
        if(!layout || layout->empty()) {p.restore();return;}
        auto selectedIds=targets();
        // Context first, selection last: an overlapping unselected device must
        // not cover the selected device's cyan LED outlines.
        for(bool selectionPass:{false,true}) for(size_t d=0;d<layout->size();++d) {
            const auto& regions=(*layout)[d]["regions"];
            for(size_t i=0;i<regions.size();++i) {
                if(!visible(static_cast<int>(d),static_cast<unsigned>(i)))continue;
                auto r=regions[i];QRectF box(image.left()+r[0].get<double>()*image.width(),image.top()+r[1].get<double>()*image.height(),
                    (r[2].get<double>()-r[0].get<double>())*image.width(),(r[3].get<double>()-r[1].get<double>())*image.height());
                bool active=std::find(selectedIds.begin(),selectedIds.end(),std::pair<int,unsigned>{static_cast<int>(d),static_cast<unsigned>(i)})!=selectedIds.end();
                if(active!=selectionPass)continue;
                if(active && d<effective.size() && i<effective[d].size()) {
                    auto actual=effective[d][i];QRectF sample(image.left()+actual[0].get<double>()*image.width(),image.top()+actual[1].get<double>()*image.height(),(actual[2].get<double>()-actual[0].get<double>())*image.width(),(actual[3].get<double>()-actual[1].get<double>())*image.height());
                    p.setPen(QPen(QColor("#ffd26e"),1,Qt::DashLine));p.drawRect(sample);
                }
                QColor outline=active?cyan:QColor("#adb8c2"),fill=outline;fill.setAlpha(active?50:12);p.fillRect(box,fill);
                p.setPen(QPen(outline,1.7));p.drawRect(box);
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
            auto values=selected();if(values.size()>1 && !multipleZones()) {
                auto point=[&](Region r){return QPointF(image.left()+(r.left+r.right)*.5*image.width(),image.top()+(r.top+r.bottom)*.5*image.height());};
                auto a=point(values.front()),z=point(values.back());QLineF line(a,z);
                // Local order cue instead of a diagonal across a keyboard.
                auto key=(*layout)[device].value("id",std::string());bool linear=key!="Keychron" && !(key=="Legion" && group==0);
                if(linear && line.length()>16) {double angle=std::atan2(z.y()-a.y(),z.x()-a.x());QPointF u(std::cos(angle),std::sin(angle)),v(-u.y(),u.x());z=a+u*std::min(36.,line.length());p.setPen(QPen(Qt::white,1.5));p.drawLine(a,z);p.drawLine(z,z-u*7+v*4);p.drawLine(z,z-u*7-v*4);}
                p.setPen(Qt::white);p.drawText(QRectF(a.x()+5,a.y()-20,80,18),Qt::AlignLeft,QString("LED %1").arg(selectedIds.front().second));
                auto end=point(values.back());p.setBrush(QColor("#ffd26e"));p.setPen(QPen(QColor("#061b27"),1));p.drawEllipse(a,4,4);p.drawEllipse(end,4,4);p.setPen(Qt::white);p.drawText(QRectF(end.x()+5,end.y()+3,90,18),Qt::AlignLeft,QString("LAST %1").arg(selectedIds.back().second));
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
        bool extend=e->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier);
        if(!handle && (extend || !QRectF(QPointF(box.left,box.top),QPointF(box.right,box.bottom)).contains(normalized))) {
            bool hit=false;
            for(int d=static_cast<int>(layout->size())-1;d>=0 && !hit;--d) {
                const auto& regions=(*layout)[d]["regions"];
                for(size_t i=0;i<regions.size();++i) {if(!visible(d,static_cast<unsigned>(i)))continue;auto r=regions[i];if(normalized.x()>=r[0].get<double>() && normalized.x()<=r[2].get<double>() && normalized.y()>=r[1].get<double>() && normalized.y()<=r[3].get<double>()) {if(picked) picked(d,static_cast<int>(i),e->modifiers());hit=true;break;}}
            }if(!hit) return;
            if(extend){setFocus();e->accept();return;}
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
        if(!image.isNull()) p.drawImage(target,image);else {p.setPen(Qt::white);p.drawText(target,Qt::AlignCenter,"Waiting for the main display source");}
        if(overlay) for(size_t d=0;d<layout.size() && d<colours.size();++d) for(size_t i=0;i<layout[d]["regions"].size() && i<colours[d].size();++i) {
            auto r=layout[d]["regions"][i],c=colours[d][i];QColor color(c[0].get<int>(),c[1].get<int>(),c[2].get<int>(),185);
            QRectF box(target.left()+r[0].get<double>()*target.width(),target.top()+r[1].get<double>()*target.height(),(r[2].get<double>()-r[0].get<double>())*target.width(),(r[3].get<double>()-r[1].get<double>())*target.height());
            p.fillRect(box,color);p.setPen(QPen(color.lighter(160),.6));p.drawRect(box);
        }
    }
};

class Panel : public officeui::OfficeWindow {
    std::string decodedPreview;
    bool wantsPreview() const {return !exportPath.isEmpty() || (isVisible() && !isMinimized() && (pages->currentIndex()==1 || pages->currentIndex()==4));}
    Client client;
    QTimer refresh;
    QString state,exportPath;
    Json settings,layout,lastSnapshot;
    uint64_t revision=0;
    bool dirty=false,updating=false,pending=false,exported=false;
    bool exerciseEditor=false;
    bool allowUnavailable=false;
    int editorPhase=0;
    Json editorBaseline,editorGroupDraft;
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
    QCheckBox* followBrightness=new officeui::Switch("Follow main display brightness");
    QLabel* brightnessStatus=new QLabel;
    QLabel* fullscreenIntensityState=new QLabel;
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
    officeui::LightController* lightMixer=nullptr;
    bool quitRequested=false;
    Json panelOptions;
    Json hardwareData=Json::object();
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
    #include "OfficeHardwarePanel.inc"
    #include "OfficeScreenPanel.inc"
    #include "OfficeAwakePanel.inc"
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
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,key](double n){if(key=="blur") {map->blur=n/100;map->update();}changed();if(key=="blur" && !updating) poll();});
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
        for(auto& [key,box]:numbers)if(key!="fullscreen_intensity"||name=="Reset")box->setValue(c.at(key).get<double>());
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
    void loadPreview(const Json& snapshot) {
        auto generation=snapshot.value("preview_generation",std::string());
        auto png=QByteArray::fromBase64(QByteArray::fromStdString(snapshot.value("preview_png",std::string())));
        bool matchingBlur=std::abs(snapshot.value("preview_blur",numbers.at("blur")->value())-numbers.at("blur")->value())<.00001;
        if(matchingBlur && !png.isEmpty() && (generation.empty() || generation!=decodedPreview)) {
            QImage next;if(next.loadFromData(png,"PNG")) {map->preview=std::move(next);decodedPreview=generation;}
        }
        map->update();
        livePreview->image=map->preview;livePreview->layout=layout;livePreview->colours=map->colours;livePreview->update();
        trace(QString("preview %1 bytes decoded=%2").arg(png.size()).arg(!map->preview.isNull()));
    }
    void load(const Json& snapshot,bool force=false) {
        trace("snapshot-accepted");
        lastSnapshot=snapshot;
        if(lightMixer&&snapshot.contains("intensity"))lightMixer->setState(snapshot.at("intensity"));
        fullscreenIntensityState->setText(snapshot.at("status").value("fullscreen_active",false)?"Active on the main display":"Applies to fullscreen apps on the main display");
        if(snapshot.contains("screen"))loadScreen(snapshot.at("screen"));
        if(snapshot.at("status").contains("awake"))loadAwake(snapshot.at("status").at("awake"));
        bool newHardware=false;
        if(snapshot.contains("hardware")) {
            auto before=hardwareData.value("devices",Json::array()),after=snapshot.at("hardware").at("devices");
            newHardware=before.size()!=after.size();
            if(!newHardware)for(size_t i=0;i<after.size();++i)if(before[i].at("identity")!=after[i].at("identity") || before[i].at("owner")!=after[i].at("owner")){newHardware=true;break;}
            hardwareData=snapshot.at("hardware");
        }
        if(force || !revision || (!dirty && snapshot.at("revision").get<uint64_t>()!=revision)) {
            int selectedDevice=map->device,selectedGroup=map->group;auto selectedZones=map->zones;
            updating=true;revision=snapshot.at("revision");settings=snapshot.at("settings");layout=snapshot.at("layout");
            enabled->setChecked(settings.at("enabled"));fps->setValue(settings.at("fps"));smooth->setValue(static_cast<int>(settings.at("smoothing_ms").get<double>()));
            for(auto& [key,box]:numbers) box->setValue(settings.value(key,office::processingDefaults().at(key)).get<double>());
            for(auto& [key,combo]:choices) combo->setCurrentIndex(combo->findData(text(settings.value(key,office::processingDefaults().at(key)))));
            blackBars->setChecked(settings.value("black_bar_detect",false));
            followBrightness->setChecked(settings.value("follow_display_brightness",true));
            effectCenter[0]->setValue(settings.value("effect_center_x",.5)*100);effectCenter[1]->setValue(settings.value("effect_center_y",.5)*100);map->effectCenter={effectCenter[0]->value()/100,effectCenter[1]->value()/100};
            map->blur=settings.value("blur",0.f)/100;map->resetHistory();
            map->layout=&layout;map->device=map->group=0;map->led=-1;buildTree();buildDevices();
            dirty=false;dirtyLabel->setText("Saved");save->setEnabled(false);updating=false;
            map->zones=selectedZones;restoreTreeSelection(selectedDevice,selectedGroup);
        }
        auto s=snapshot.at("status");
        if(newHardware && revision)buildDevices();
        brightnessStatus->setText(QString("Applied display factor: %1% · %2").arg(s.value("display_brightness_factor",1.)*100,0,'f',1).arg(!snapshot.at("settings").value("follow_display_brightness",true)?"off":s.value("display_brightness_error",std::string()).empty()?(s.value("display_brightness_valid",false)?"following":"waiting for active display"):s.value("display_brightness_valid",false)?"read unavailable; retaining last factor":"read unavailable; using baseline"));
        brightnessStatus->setToolTip(text(s.value("display_brightness_error",std::string())));
        deviceSummary->setText(QString("%1 / %2 devices online  ·  %3 mapped LEDs").arg(s.at("online_devices").get<int>()).arg(layout.size()).arg(s.at("leds").get<int>()));
        inventorySummary->setText(deviceSummary->text());
        totalDevices->setText(QString::number(hardwareData.value("devices",Json::array()).size()));mappedDevices->setText(QString::number(layout.size()));
        if(auto scan=findChild<QPushButton*>("hardwareRescan")){bool busy=hardwareData.value("detecting",false);scan->setEnabled(!busy);scan->setText(busy?QString("Discovering · %1%").arg(hardwareData.value("detection_percent",0)):"Rescan devices");}
        bool resting=!s.value("rest_reason",std::string()).empty();
        status->setText(resting?"RESTING":s.value("output_paused",false)?"PAUSED":s.value("standby",false)?"STANDBY":settings.value("enabled",true)?"ACTIVE":"INACTIVE");
        status->setToolTip(text(s.at("mode"))+" · "+text(s.at("error")));liveRate->setText(QString("%1 Hz output clock\n%2 device updates").arg(s.at("output_fps").get<double>(),0,'f',1).arg(s.value("device_submissions",uint64_t(0))));
        testState->setText(s.value("rest_resume",Json::object()).value("held",false)?"Lights resting · waiting for activity":resting?"Lights resting · resumes automatically":snapshot.at("test_mode")=="none"?"Ambience running":"Temporary test active");
        diagnostics->setPlainText(QString::fromStdString(s.dump(2)));
        map->colours=snapshot.at("colors");
        map->effective=snapshot.value("effective_regions",Json::array());
        if(snapshot.contains("online")) for(size_t d=0;d<onlineLabels.size();++d) {
            bool online=snapshot.at("online").at(d);onlineLabels[d]->setText(online?"Connected":"Offline · map retained");
            onlineLabels[d]->setStyleSheet(online?"color:#22d3a5":"color:#ff9f1c");
            identifyButtons[d]->setEnabled(online);
        }
        loadPreview(snapshot);
        if(!snapshot.value("preview_error",std::string()).empty()) {
            failure(text(snapshot.at("preview_error")));
            if(!exportPath.isEmpty()) QCoreApplication::exit(1);
        }
        stopTest->setEnabled(snapshot.at("test_mode")!="none");
        if(!exportPath.isEmpty() && !exported && (!map->preview.isNull() || allowUnavailable)) {
            if(exerciseEditor && editorPhase<6) exerciseSave();else exportViews();
        }
    }
    Json previewRequest() const {
        Json request={{"command","snapshot"},{"preview",wantsPreview()},{"preview_generation",decodedPreview}};
        if(pages->currentIndex()==2 || !exportPath.isEmpty())request["hardware"]=true;
        if(wantsPreview()) request["preview_blur"]=numbers.at("blur")->value();
        return request;
    }
    void poll() {
        if(pending || !client.ready()) return;
        if(exportPath.isEmpty() && revision && (!isVisible() || isMinimized())) return;
        pending=true;client.send(previewRequest(),[this](const Json& r){response(r,[this](const Json& s){load(s);});});
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
            row->addWidget(online);row->addWidget(check);auto edit=officeui::button("Topology","hub");row->addWidget(edit);
            auto hardwareButton=officeui::button("Hardware","memory");hardwareButton->setAccessibleName("Hardware settings "+text(layout[d]["name"]));row->addWidget(hardwareButton);row->addWidget(identify);
            connect(hardwareButton,&QPushButton::clicked,this,[this,key]{openHardware(key);});
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
        if(hardwareData.contains("devices"))for(const auto& item:hardwareData.at("devices"))if(!item.value("mapped",false)) {
            auto card=new officeui::Card(text(item.at("name")),"memory");card->content->addWidget(officeui::label("Native OpenRGB hardware · not part of the saved office sampling map","hint"));auto edit=officeui::button("Hardware settings","tune");card->content->addWidget(edit);auto key=item.at("id").get<std::string>();connect(edit,&QPushButton::clicked,this,[this,key]{openHardware(key);});deviceList->addWidget(card);
        }
        if(layout.empty()){deviceList->addStretch();auto empty=new officeui::Card("No devices available","device_hub");empty->content->addWidget(officeui::label("Offline maps stay available. Reconnect the native owner or return the KVM devices to this PC."));auto reconnect=officeui::button("Reconnect","refresh");empty->content->addWidget(reconnect);connect(reconnect,&QPushButton::clicked,this,[this]{client.reconnect(state);});deviceList->addWidget(empty);}
        deviceList->addStretch();deviceList->activate();devicePage->setMinimumHeight(deviceList->minimumSize().height());devicePage->updateGeometry();
        totalDevices->setText(QString::number(layout.size()));mappedDevices->setText(QString::number(layout.size()));
        if(layout.empty())inventorySummary->setText("No device maps loaded · reconnect to refresh inventory");
    }
    void syncTree() {
        int d=map->device,g=map->group;{QSignalBlocker blocker(tree);buildTree();}restoreTreeSelection(d,g);
    }
    void restoreTreeSelection(int device,int group) {
        if(layout.empty())return;
        {QSignalBlocker blocker(tree);tree->clearSelection();
            for(auto [d,g]:map->zones)if(d>=0 && d<tree->topLevelItemCount()) {
                auto parent=tree->topLevelItem(d);auto item=g<0?parent:parent->child(g);if(item)item->setSelected(true);
            }
            device=std::clamp(device,0,tree->topLevelItemCount()-1);auto parent=tree->topLevelItem(device);
            auto current=parent->child(std::clamp(group,0,parent->childCount()-1));
            if(tree->selectedItems().empty())current->setSelected(true);
            tree->setCurrentItem(current,0,QItemSelectionModel::NoUpdate);
        }selectionFromTree();
    }
    void selectionFromTree() {
        if(updating || layout.empty())return;
        auto items=tree->selectedItems();if(items.empty()){restoreTreeSelection(map->device,map->group);return;}
        auto primary=tree->currentItem();if(!primary || !primary->isSelected())primary=items.front();
        int d=primary->data(0,Qt::UserRole).toInt(),g=primary->data(0,Qt::UserRole+1).toInt();
        updating=true;map->device=d;map->group=std::max(0,g);map->led=-1;map->zones.clear();
        for(auto item:items)map->zones.emplace_back(item->data(0,Qt::UserRole).toInt(),item->data(0,Qt::UserRole+1).toInt());
        std::sort(map->zones.begin(),map->zones.end());
        led->clear();led->addItem(map->multipleZones()?"All selected LEDs":"Entire zone",-1);
        if(!map->multipleZones()) {
            auto& zone=layout[d]["groups"][map->group];
            for(unsigned i=zone["first"].get<unsigned>();i<zone["first"].get<unsigned>()+zone["count"].get<unsigned>();++i)
                led->addItem(QString("LED %1 · %2").arg(i).arg(text(layout[d]["led_names"][i])),static_cast<int>(i));
        }
        led->setEnabled(!map->multipleZones());zoneName->setReadOnly(map->multipleZones());
        for(auto name:{"reverseSelection","mapToEdge"})if(auto control=findChild<QWidget*>(name))control->setEnabled(!map->multipleZones());
        updating=false;showGeometry();map->update();
    }
    void select(int device,int group) {
        if(layout.empty())return;map->zones={{device,group}};restoreTreeSelection(device,group);
    }
    void showGeometry() {
        if(layout.empty()) return;bool before=updating;updating=true;
        auto b=office::bounds(map->selected());std::array<double,4> values={b.left*100,b.top*100,(b.right-b.left)*100,(b.bottom-b.top)*100};
        for(size_t i=0;i<4;++i) geometry[i]->setValue(values[i]);updating=before;
        auto& g=layout[map->device]["groups"][map->group];
        if(map->multipleZones()) {
            std::set<std::pair<int,int>> groups;std::set<int> devices;
            for(auto [d,n]:map->zones) {devices.insert(d);if(n<0)for(int z=0;z<static_cast<int>(layout[d]["groups"].size());++z)groups.insert({d,z});else groups.insert({d,n});}
            zoneName->setText(QString("%1 zones selected").arg(groups.size()));
            zoneSummary->setText(QString("%1 zones · %2 devices\n%3 LEDs · move together").arg(groups.size()).arg(devices.size()).arg(map->targets().size()));
        } else {zoneName->setText(text(g["name"]));zoneSummary->setText(QString("%1\n%2 LEDs · physical indices %3–%4").arg(text(g["name"])).arg(g["count"].get<int>()).arg(g["first"].get<int>()).arg(g["first"].get<int>()+g["count"].get<int>()-1));}
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
        settings["follow_display_brightness"]=followBrightness->isChecked();
        settings["effect_center_x"]=effectCenter[0]->value()/100;settings["effect_center_y"]=effectCenter[1]->value()/100;
        pending=true;save->setEnabled(false);
        client.send({{"command","save"},{"revision",revision},{"settings",settings},{"layout",layout}},[this](const Json& r){
            response(r,[this](const Json& s){load(s,true);});if(dirty) save->setEnabled(true);
        });
    }
    void exportViews() {
        exported=true;QDir().mkpath(exportPath);
        Json result={{"native_revision",revision},{"devices",layout.size()},{"editor_save_roundtrip",exerciseEditor && editorPhase==6},{"multi_selection_save_roundtrip",exerciseEditor && editorPhase==6},
            {"actual_preview",!map->preview.isNull()},{"source_error",lastSnapshot.at("status").value("error",std::string())},
            {"font_family",font().family().toStdString()},{"views",Json::array()},{"design","EDAmbilight recovered native assets"},
            {"application_model",client.direct?"unified":"diagnostic-client"},{"ui_thread",quintptr(QThread::currentThreadId())},
            {"output_thread",lastSnapshot.at("status").value("output_thread",0u)},{"process",QCoreApplication::applicationPid()},
            {"application_icon",!windowIcon().isNull()}};
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
        resize(1440,900);result["states"]=Json::array();result["hardware_views"]=Json::array();
        result["screen_views"]=Json::array();pages->setCurrentIndex(7);
        for(int tab=0;tab<screenTabs->count();++tab){screenTabs->setCurrentIndex(tab);QApplication::sendPostedEvents();centralWidget()->layout()->activate();auto file=exportPath+"/screen-tab-"+QString::number(tab)+".png";if(!grab().save(file))throw std::runtime_error("Cannot export Screen tab");result["screen_views"].push_back({{"tab",screenTabs->tabText(tab).toStdString()},{"path",file.toStdString()}});}screenTabs->setCurrentIndex(0);
        result["screen_cycle_views"]=Json::array();auto savedScreen=screenState;
        for(auto state:{"day","night","transition","paused","disabled"}){
            auto fixture=savedScreen;fixture["cycle_state"]=state;fixture["cycle_seconds"]=QString(state)=="night"?0.:QString(state)=="transition"?64800.:43200.;fixture["preview"]=false;loadScreen(fixture);
            auto file=exportPath+"/screen-cycle-"+state+".png";if(!grab().save(file))throw std::runtime_error("Cannot export sundial state");
            result["screen_cycle_views"].push_back({{"state",state},{"path",file.toStdString()},{"fixture",true},{"marker_seconds",screenSundial->markerSeconds()}});
        }loadScreen(savedScreen);
        result["caption_controls"]=Json::array();for(auto name:{"windowMinimize","windowMaximize","windowClose"}){
            auto button=findChild<QToolButton*>(name);auto origin=button->mapTo(this,QPoint());
            result["caption_controls"].push_back({{"name",name},{"accessible_name",button->accessibleName().toStdString()},{"x",origin.x()},{"y",origin.y()},{"width",button->width()},{"height",button->height()},{"hit",captionHit(origin+button->rect().center())}});
        }
        result["tray"]=Json::array();
        for(auto action:trayMenu->actions())if(action->isVisible()){
            Json item={{"text",action->text().toStdString()},{"separator",action->isSeparator()}};
            if(action->menu()){item["children"]=Json::array();for(auto child:action->menu()->actions())item["children"].push_back(child->text().toStdString());}result["tray"].push_back(item);
        }
        for(auto menu:{trayMenu,lightingPauseMenu,screenPauseMenu,awakeMenu,awakeIntervalMenu}){menu->ensurePolished();menu->resize(menu->sizeHint());QImage image(menu->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);menu->render(&image);if(!image.save(exportPath+"/"+menu->objectName()+".png"))throw std::runtime_error("Cannot export real tray menu");}
        showAwakeExpiration();QApplication::sendPostedEvents();auto expiry=findChild<QDialog*>("awakeExpirationDialog");expiry->layout()->activate();if(!expiry->grab().save(exportPath+"/awake-expiration.png"))throw std::runtime_error("Cannot export awake expiration picker");expiry->reject();
        if(hardwareData.contains("devices"))for(const auto& device:hardwareData.at("devices")) {
            hardwareDialog(device);auto dialog=findChild<QDialog*>("hardwareDialog");auto tabs=dialog->findChild<QTabWidget*>("hardwareTabs");
            for(int tab=0;tab<tabs->count();++tab){tabs->setCurrentIndex(tab);QApplication::sendPostedEvents();dialog->layout()->activate();
                auto file=exportPath+"/hardware-"+text(device.at("id"))+"-"+QString::number(tab)+".png";
                if(!dialog->grab().save(file))throw std::runtime_error("Could not export native hardware controls");
                result["hardware_views"].push_back({{"device",device.at("id")},{"tab",tabs->tabText(tab).toStdString()},{"path",file.toStdString()}});
            }
            dialog->setObjectName("retiredHardwareDialog");dialog->close();
        }
        auto renderState=[this,&result](const char* name){QApplication::sendPostedEvents();centralWidget()->layout()->activate();auto file=exportPath+"/state-"+name+".png";if(!grab().save(file))throw std::runtime_error("Could not export UI state");result["states"].push_back({{"name",name},{"path",file.toStdString()},{"fixture",true}});};
        pages->setCurrentIndex(0);auto disclosure=findChild<QPushButton*>("advancedOptions");disclosure->setChecked(true);renderState("advanced");disclosure->setChecked(false);
        pages->setCurrentIndex(1);select(0,0);tree->setCurrentItem(tree->topLevelItem(0)->child(0));auto beforeRotation=layout;map->rotateSelection();showGeometry();renderState("rotation-quarter-turn");layout=beforeRotation;map->resetHistory();map->update();
        for(size_t d=0;d<layout.size();++d)if(layout[d].value("id",std::string())=="Legion") {
            tree->setCurrentItem(tree->topLevelItem(d));renderState("selected-device");
            auto parent=tree->topLevelItem(d);tree->setCurrentItem(parent->child(0));
            tree->setCurrentItem(parent->child(1),0,QItemSelectionModel::Select);
            findChild<QComboBox*>("topologyVisibility")->setCurrentIndex(1);renderState("selected-zones");
            findChild<QComboBox*>("topologyVisibility")->setCurrentIndex(0);break;
        }
        findChild<QComboBox*>("topologyVisibility")->setCurrentIndex(2);renderState("all-devices");
        findChild<QComboBox*>("topologyVisibility")->setCurrentIndex(0);
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
        } else if(editorPhase==2) {
            if(layout!=editorBaseline || revision!=editorRevision+2) throw std::runtime_error("Editor failed to restore the original map");
            int laptop=-1;for(size_t d=0;d<layout.size();++d)if(layout[d].value("id",std::string())=="Legion")laptop=static_cast<int>(d);
            if(laptop<0)throw std::runtime_error("Laptop map missing for group Save acceptance");
            tree->setCurrentItem(tree->topLevelItem(laptop));auto bounds=office::bounds(map->selected());
            int direction=bounds.left>.001f?Qt::Key_Left:bounds.right<.999f?Qt::Key_Right:bounds.top>.001f?Qt::Key_Up:Qt::Key_Down;
            QKeyEvent move(QEvent::KeyPress,direction,Qt::ShiftModifier);QApplication::sendEvent(map,&move);
            editorGroupDraft=layout;
            if(editorGroupDraft==editorBaseline || map->targets().size()!=layout[laptop]["regions"].size())throw std::runtime_error("Device row failed to produce a moved group draft");
            editorPhase=3;save->click();
        } else if(editorPhase==3) {
            if(layout!=editorGroupDraft || revision!=editorRevision+3 || !map->multipleZones())throw std::runtime_error("Real Save lost group geometry or selection");
            map->replaceLayout(editorBaseline);editorPhase=4;save->click();
        } else {
            if(layout!=editorBaseline || revision!=editorRevision+4)throw std::runtime_error("Group Save check failed to restore the original map");
            editorPhase=6;trace("editor-group-save-roundtrip-restored");exportViews();
        }
    }
public:
#ifdef OFFICE_WIDGET_TEST
    void fixtureHardware(const Json& value) {refresh.stop();client.connected={};hardwareDialog(value);}
    void fixtureScreen(const Json& value) {refresh.stop();client.connected={};loadScreen(value);shellPages->setCurrentIndex(0);pages->setCurrentIndex(7);}
    void fixtureAwake(const Json& value) {refresh.stop();client.connected={};loadAwake(value);}
    bool fixtureWantsPreview() const {return wantsPreview();}
    Json fixturePreviewRequest() const {return previewRequest();}
    void fixturePreviewReply(const Json& reply) {loadPreview(reply);}
    void fixtureLayout(const Json& value) {
        refresh.stop();client.error={};updating=true;layout=value;map->layout=&layout;map->zones.clear();buildTree();buildDevices();updating=false;select(0,0);shellPages->setCurrentIndex(0);pages->setCurrentIndex(1);
    }
#endif
    Panel(QString stateDirectory,QString exportDirectory,bool exercise=false,bool unavailable=false,OfficeDispatch direct={}):state(std::move(stateDirectory)),exportPath(std::move(exportDirectory)),exerciseEditor(exercise),allowUnavailable(unavailable) {
        client.direct=std::move(direct);
        clock.start();client.trace=[this](QString event){trace(event);};trace("panel-constructing");
        QFile preferences(state+"/office-panel.json");if(preferences.open(QIODevice::ReadOnly)) {auto data=preferences.readAll();panelOptions=Json::parse(data.constData(),data.constData()+data.size(),nullptr,false);}if(!panelOptions.is_object()) panelOptions=Json::object();
        setWindowTitle("Office Ambilight · OpenRGB");resize(1440,900);setMinimumSize(1000,700);
        qApp->setProperty("officeReducedMotion",panelOptions.value("reduced_motion",false));buildViews();
        map->edited=[this]{changed();showGeometry();};
        map->error=[this](QString message){failure(message);};
        map->picked=[this](int d,int index,Qt::KeyboardModifiers modifiers){for(size_t g=0;g<layout[d]["groups"].size();++g) {const auto& group=layout[d]["groups"][g];if(index>=group["first"].get<int>() && index<group["first"].get<int>()+group["count"].get<int>()) {
            auto item=tree->topLevelItem(d)->child(g);auto flags=modifiers&Qt::ControlModifier?QItemSelectionModel::Toggle:modifiers&Qt::ShiftModifier?QItemSelectionModel::Select:QItemSelectionModel::ClearAndSelect;
            tree->setCurrentItem(item,0,flags);break;
        }}};
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
        if(tray && !quitRequested && panelOptions.value("close_to_tray",true)) {hide();event->ignore();return;}
        if((dirty || screenDirty) && QMessageBox::question(this,"Unsaved settings","Close without saving your changes?")!=QMessageBox::Yes) {event->ignore();return;}
        action({{"command","stop_test"}});event->accept();
        if(client.direct) QCoreApplication::quit();
    }
    void resizeEvent(QResizeEvent* e) override {officeui::OfficeWindow::resizeEvent(e);arrangeDashboard();pageSubtitle->setVisible(width()>=1200);}
};
}

namespace {
#include "OfficePanelViews.inc"
}

QWidget* createOfficePanel(const QString& state,OfficeDispatch dispatch) {
    officeui::loadFonts();
    auto destination=qEnvironmentVariable("OPENRGB_OFFICE_EXPORT");
    auto panel=new Panel(state,destination,qEnvironmentVariableIsSet("OPENRGB_OFFICE_EXERCISE_EDITOR"),qEnvironmentVariableIsSet("OPENRGB_OFFICE_ALLOW_UNAVAILABLE"),std::move(dispatch));
    if(!destination.isEmpty())panel->show();
    return panel;
}

#if !defined(OFFICE_WIDGET_TEST) && !defined(OFFICE_EMBEDDED_PANEL)
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("Office Ambilight");
    officeui::loadFonts();
    QCommandLineParser parser;parser.addHelpOption();parser.addOption({"state","Private OpenRGB state directory","directory","D:/Tools/OpenRGB/state-integration"});
    parser.addOption({"export-views","Export all real connected panel views, then exit","directory"});
    parser.addOption({"allow-unavailable-source","Export the real waiting/error state if the main display source is unavailable; not live capture acceptance"});
    parser.addOption({"exercise-editor","Verify the real Save button and restore the original map before exporting"});parser.process(app);
    if(parser.isSet("exercise-editor") && !parser.isSet("export-views")) parser.showHelp(1);
    // One interactive panel per private state. Diagnostic exports are isolated,
    // short-lived consumers and do not compete for the user's interactive lock.
    auto state=QDir(parser.value("state")).absolutePath();QDir().mkpath(state);
    QLockFile lock(state+"/office-panel.lock");lock.setStaleLockTime(0);
    QLocalServer activation;auto name="OfficeAmbilightPanel-"+QString(QCryptographicHash::hash((state.toLower()+qEnvironmentVariable("USERNAME")).toUtf8(),QCryptographicHash::Sha256).toHex());
    if(!parser.isSet("export-views")) {
        if(!lock.tryLock(0)) {
            QLocalSocket existing;existing.connectToServer(name);
            if(!existing.waitForConnected(-1)) {qWarning()<<"The Office panel is already starting or running; use its tray icon.";return 1;}
            existing.waitForDisconnected(-1);return 0;
        }
        activation.setSocketOptions(QLocalServer::UserAccessOption);
        if(!activation.listen(name)) {qWarning()<<activation.errorString();return 1;}
    }
    Panel panel(state,parser.value("export-views"),parser.isSet("exercise-editor"),parser.isSet("allow-unavailable-source"));
    QObject::connect(&activation,&QLocalServer::newConnection,&panel,[&]{
        while(auto socket=activation.nextPendingConnection()) {socket->disconnectFromServer();socket->deleteLater();}
        panel.showNormal();panel.raise();panel.activateWindow();
    });
    panel.show();return app.exec();
}
#endif
