// Native control panel for the office plugin. Recovered EDAmbilight layout and
// interaction contract: EDAMBILIGHT-GUI-REVIEW.md; no second RGB/capture writer.
#include "OfficeLayout.h"
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
#include <functional>
#include <map>
#include <array>

using office::Json;
using office::Region;
namespace {
QString text(const Json& v) {return QString::fromStdString(v.get<std::string>());}
QColor cyan("#14f5ff");

class Client : public QObject {
    QLocalSocket socket;
    QByteArray pending;
    std::string token;
    uint64_t next=0;
    std::map<uint64_t,std::function<void(const Json&)>> callbacks;
public:
    std::function<void()> connected;
    std::function<void(QString)> error;
    Client() {
        connect(&socket,&QLocalSocket::connected,this,[this]{if(connected) connected();});
        connect(&socket,&QLocalSocket::disconnected,this,[this]{callbacks.clear();if(error) error("OpenRGB disconnected. Your unsaved map is retained.");});
        connect(&socket,&QLocalSocket::errorOccurred,this,[this](auto){if(error) error(socket.errorString());});
        connect(&socket,&QLocalSocket::readyRead,this,[this] {
            pending+=socket.readAll();
            if(pending.size()>2*1024*1024) {socket.abort();return;}
            while(pending.contains('\n')) {
                auto at=pending.indexOf('\n');auto line=pending.left(at);pending.remove(0,at+1);
                auto value=Json::parse(line.constData(),line.constData()+line.size(),nullptr,false);
                if(!value.is_object() || !value.contains("id") || !value["id"].is_number_unsigned()) {socket.abort();return;}
                auto found=callbacks.find(value["id"].get<uint64_t>());
                if(found==callbacks.end()) continue;
                auto callback=std::move(found->second);callbacks.erase(found);callback(value);
            }
        });
    }
    void open(const QString& state) {
        QFile file(state+"/office-control.json");
        if(!file.open(QIODevice::ReadOnly)) {if(error) error("Start the Office Ambience owner before opening the panel.");return;}
        auto bytes=file.readAll();auto info=Json::parse(bytes.constData(),bytes.constData()+bytes.size(),nullptr,false);
        if(!info.is_object() || info.value("protocol",0)!=1) {if(error) error("The installed control endpoint is incompatible.");return;}
        if(!info.contains("token") || !info["token"].is_string() || !info.contains("endpoint") || !info["endpoint"].is_string()) {if(error) error("The private endpoint record is invalid.");return;}
        token=info.at("token").get<std::string>();pending.clear();socket.connectToServer(text(info.at("endpoint")));
    }
    bool ready() const {return socket.state()==QLocalSocket::ConnectedState;}
    void send(Json request,std::function<void(const Json&)> callback) {
        if(!ready()) {if(error) error("OpenRGB is not connected. Reconnect before saving.");return;}
        request["id"]=++next;request["token"]=token;callbacks[next]=std::move(callback);
        socket.write(QByteArray::fromStdString(request.dump())+'\n');
    }
};

class MapCanvas : public QWidget {
    QPointF dragStart;
    std::vector<Region> dragRegions;
    QRectF imageRect() const {
        auto area=rect().adjusted(12,12,-12,-12);double width=area.width(),height=width*2160/5120;
        if(height>area.height()) {height=area.height();width=height*5120/2160;}
        return {area.center().x()-width/2,area.center().y()-height/2,width,height};
    }
public:
    Json* layout=nullptr;
    Json colours;
    QImage preview;
    int device=0,group=0,led=-1;
    bool showAll=false;
    std::function<void()> edited;
    std::function<void(int)> picked;
    MapCanvas() {setMinimumSize(280,210);setFocusPolicy(Qt::StrongFocus);setAccessibleName("Actual LG sampling map");}
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
    void setSelected(const std::vector<Region>& values) {
        auto ids=indices();for(size_t i=0;i<ids.size();++i) (*layout)[device]["regions"][ids[i]]=office::encode(values[i]);
        update();if(edited) edited();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto image=imageRect();
        p.fillRect(rect(),QColor("#020a10"));
        if(!preview.isNull()) p.drawImage(image,preview);
        else {p.fillRect(image,QColor("#102333"));p.setPen(QColor("#cdd6e0"));p.drawText(image,Qt::AlignCenter,"Waiting for a live LG preview");}
        p.setPen(QPen(QColor(255,255,255,22),1));
        for(int i=1;i<10;++i) {p.drawLine(QPointF(image.left()+image.width()*i/10,image.top()),QPointF(image.left()+image.width()*i/10,image.bottom()));p.drawLine(QPointF(image.left(),image.top()+image.height()*i/10),QPointF(image.right(),image.top()+image.height()*i/10));}
        if(!layout || layout->empty()) return;
        auto selectedIds=indices();
        for(size_t d=0;d<layout->size();++d) {
            if(!showAll && static_cast<int>(d)!=device) continue;
            const auto& regions=(*layout)[d]["regions"];
            for(size_t i=0;i<regions.size();++i) {
                auto r=regions[i];QRectF box(image.left()+r[0].get<double>()*image.width(),image.top()+r[1].get<double>()*image.height(),
                    (r[2].get<double>()-r[0].get<double>())*image.width(),(r[3].get<double>()-r[1].get<double>())*image.height());
                bool active=static_cast<int>(d)==device && std::find(selectedIds.begin(),selectedIds.end(),i)!=selectedIds.end();
                QColor fill=cyan;fill.setAlpha(active?50:12);p.fillRect(box,fill);
                p.setPen(QPen(active?cyan:QColor(200,210,225,70),active?1.7:0.5));p.drawRect(box);
                if(d<colours.size() && i<colours[d].size()) {auto c=colours[d][i];p.fillRect(QRectF(box.left()+1,box.top()+1,std::min(7.,box.width()),std::min(7.,box.height())),QColor(c[0].get<int>(),c[1].get<int>(),c[2].get<int>()));}
                if(active && (led>=0 || selectedIds.size()<=10) && box.width()>18 && box.height()>16) {p.setPen(Qt::white);p.drawText(box,Qt::AlignCenter,QString::number(i));}
            }
        }
        p.setPen(cyan);p.drawRect(image);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()!=Qt::LeftButton || !layout || layout->empty()) return;
        auto area=imageRect();auto point=e->position();if(!area.contains(point)) return;
        QPointF normalized((point.x()-area.left())/area.width(),(point.y()-area.top())/area.height());
        auto selectedValues=selected();auto box=office::bounds(selectedValues);
        if(!QRectF(QPointF(box.left,box.top),QPointF(box.right,box.bottom)).contains(normalized)) {
            const auto& regions=(*layout)[device]["regions"];
            for(size_t i=0;i<regions.size();++i) {auto r=regions[i];if(normalized.x()>=r[0].get<double>() && normalized.x()<=r[2].get<double>() && normalized.y()>=r[1].get<double>() && normalized.y()<=r[3].get<double>()) {if(picked) picked(static_cast<int>(i));break;}}
        }
        dragStart=normalized;dragRegions=selected();setFocus();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if(!(e->buttons()&Qt::LeftButton) || dragRegions.empty()) return;
        auto area=imageRect();double dx=(e->position().x()-area.left())/area.width()-dragStart.x(),dy=(e->position().y()-area.top())/area.height()-dragStart.y();
        auto b=office::bounds(dragRegions);dx=std::clamp(dx,-double(b.left),1.-b.right);dy=std::clamp(dy,-double(b.top),1.-b.bottom);
        auto values=dragRegions;for(auto& r:values) {r.left+=static_cast<float>(dx);r.right+=static_cast<float>(dx);r.top+=static_cast<float>(dy);r.bottom+=static_cast<float>(dy);}setSelected(values);
    }
    void mouseReleaseEvent(QMouseEvent*) override {dragRegions.clear();}
    void keyPressEvent(QKeyEvent* e) override {
        if(!layout || layout->empty()) return;
        float step=e->modifiers()&Qt::ShiftModifier?.001f:.005f,dx=0,dy=0;
        if(e->key()==Qt::Key_Left) dx=-step;else if(e->key()==Qt::Key_Right) dx=step;
        else if(e->key()==Qt::Key_Up) dy=-step;else if(e->key()==Qt::Key_Down) dy=step;else {QWidget::keyPressEvent(e);return;}
        auto values=selected();auto b=office::bounds(values);dx=std::clamp(dx,-b.left,1-b.right);dy=std::clamp(dy,-b.top,1-b.bottom);
        for(auto& r:values) {r.left+=dx;r.right+=dx;r.top+=dy;r.bottom+=dy;}setSelected(values);
    }
};

class Panel : public QMainWindow {
    Client client;
    QTimer refresh;
    QString state,exportPath;
    Json settings,layout,lastSnapshot;
    uint64_t revision=0;
    bool dirty=false,updating=false,pending=false,exported=false;
    QStackedWidget* pages=new QStackedWidget;
    QLabel* connection=new QLabel("Connecting to OpenRGB…");
    QLabel* status=new QLabel;
    QLabel* dirtyLabel=new QLabel;
    QPushButton* save=new QPushButton("Save changes");
    QCheckBox* enabled=new QCheckBox("Ambience enabled");
    QSpinBox* fps=new QSpinBox;
    QSpinBox* smooth=new QSpinBox;
    QTreeWidget* tree=new QTreeWidget;
    MapCanvas* map=new MapCanvas;
    QLineEdit* zoneName=new QLineEdit;
    QComboBox* led=new QComboBox;
    std::array<QDoubleSpinBox*,4> geometry;
    QPlainTextEdit* diagnostics=new QPlainTextEdit;
    QWidget* devicePage=new QWidget;
    QVBoxLayout* deviceList=new QVBoxLayout(devicePage);
    QPushButton* stopTest=new QPushButton("Stop test / return to ambience");

    void changed() {if(updating) return;dirty=true;dirtyLabel->setText("Unsaved changes");save->setEnabled(client.ready());}
    QWidget* groupBox(const QString& title,QLayout* inside) {auto group=new QGroupBox(title);group->setLayout(inside);return group;}
    void failure(const QString& message) {connection->setText(message);connection->setStyleSheet("color:#ff9f1c");}
    void response(const Json& value,std::function<void(const Json&)> then={}) {
        pending=false;
        if(!value.value("ok",false)) {failure(text(value.at("error")));return;}
        connection->setText("Connected · native OpenRGB owner");connection->setStyleSheet("color:#22d3a5");
        if(then) then(value.at("result"));
    }
    void load(const Json& snapshot,bool force=false) {
        lastSnapshot=snapshot;
        if(force || !revision || (!dirty && snapshot.at("revision").get<uint64_t>()!=revision)) {
            int selectedDevice=map->device,selectedGroup=map->group;
            updating=true;revision=snapshot.at("revision");settings=snapshot.at("settings");layout=snapshot.at("layout");
            enabled->setChecked(settings.at("enabled"));fps->setValue(settings.at("fps"));smooth->setValue(static_cast<int>(settings.at("smoothing_ms").get<double>()));
            map->layout=&layout;map->device=map->group=0;map->led=-1;buildTree();buildDevices();
            dirty=false;dirtyLabel->setText("Saved");save->setEnabled(false);updating=false;select(selectedDevice,selectedGroup);
        }
        auto s=snapshot.at("status");
        status->setText(QString("%1\n%2 FPS output · %3 captured frames\nGPU sampling %4 ms · new-frame submission %5 ms\n%6")
            .arg(text(s.at("mode")).toUpper()).arg(s.at("output_fps").get<double>(),0,'f',1)
            .arg(s.at("input_frames").get<uint64_t>()).arg(s.at("gpu_processing_ms").get<double>(),0,'f',2)
            .arg(s.at("callback_to_native_submit_ms").get<double>(),0,'f',2).arg(text(s.at("error"))));
        diagnostics->setPlainText(QString::fromStdString(s.dump(2)));
        map->colours=snapshot.at("colors");
        auto png=QByteArray::fromBase64(QByteArray::fromStdString(snapshot.value("preview_png",std::string())));
        if(!png.isEmpty()) map->preview.loadFromData(png,"PNG");map->update();
        stopTest->setEnabled(snapshot.at("test_mode")!="none");
        if(!exportPath.isEmpty() && !exported && !map->preview.isNull()) exportViews();
    }
    void poll() {
        if(pending || !client.ready()) return;
        pending=true;client.send({{"command","snapshot"},{"preview",true}},[this](const Json& r){response(r,[this](const Json& s){load(s);});});
    }
    void action(const Json& request) {client.send(request,[this](const Json& r){response(r);poll();});}
    void buildTree() {
        tree->clear();
        for(size_t d=0;d<layout.size();++d) {
            auto* item=new QTreeWidgetItem(tree,{text(layout[d]["name"])});item->setData(0,Qt::UserRole,static_cast<int>(d));item->setData(0,Qt::UserRole+1,-1);
            for(size_t g=0;g<layout[d]["groups"].size();++g) {
                const auto& group=layout[d]["groups"][g];
                auto* child=new QTreeWidgetItem(item,{QString("%1 · %2 LEDs").arg(text(group["name"])).arg(group["count"].get<unsigned>())});
                child->setData(0,Qt::UserRole,static_cast<int>(d));child->setData(0,Qt::UserRole+1,static_cast<int>(g));
            }item->setExpanded(true);
        }
        tree->setColumnWidth(0,210);
    }
    void buildDevices() {
        while(auto* item=deviceList->takeAt(0)) {if(item->widget()) item->widget()->deleteLater();delete item;}
        for(size_t d=0;d<layout.size();++d) {
            auto row=new QHBoxLayout;auto check=new QCheckBox(text(layout[d]["name"]));check->setChecked(layout[d]["enabled"]);
            auto identify=new QPushButton("Identify");identify->setAccessibleName("Identify "+text(layout[d]["name"]));
            row->addWidget(check,1);row->addWidget(new QLabel(QString("%1 LEDs").arg(layout[d]["regions"].size())));row->addWidget(identify);
            connect(check,&QCheckBox::toggled,this,[this,d](bool on){layout[d]["enabled"]=on;changed();});
            connect(identify,&QPushButton::clicked,this,[this,d]{action({{"command","identify"},{"device",layout[d]["id"]}});});
            deviceList->addWidget(groupBox(text(layout[d]["id"]),row));
        }deviceList->addStretch();
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
    }
    void applyGeometry(int rotation=0,bool reverse=false) {
        if(updating || layout.empty()) return;
        auto values=map->selected();auto from=office::bounds(values);
        Region to{static_cast<float>(geometry[0]->value()/100),static_cast<float>(geometry[1]->value()/100),0,0};
        to.right=to.left+static_cast<float>(geometry[2]->value()/100);to.bottom=to.top+static_cast<float>(geometry[3]->value()/100);
        if(rotation%180) {
            // Quarter-turn about the centre, preserving dimensions in monitor
            // pixels rather than stretching a horizontal strip inside its old box.
            float cx=(from.left+from.right)/2,cy=(from.top+from.bottom)/2;
            float width=(from.bottom-from.top)*2160.f/5120.f,height=(from.right-from.left)*5120.f/2160.f;
            if(width>1 || height>1) {failure("Resize this zone before rotating it inside the LG image.");return;}
            float left=std::clamp(cx-width/2,0.f,1-width),top=std::clamp(cy-height/2,0.f,1-height);
            to={left,top,left+width,top+height};
        }
        try {map->setSelected(office::transform(values,from,to,rotation,reverse));}
        catch(std::exception const& e) {failure(e.what());showGeometry();}
    }
    void saveChanges() {
        if(pending || !revision) return;
        settings["enabled"]=enabled->isChecked();settings["fps"]=fps->value();settings["smoothing_ms"]=smooth->value();
        pending=true;save->setEnabled(false);
        client.send({{"command","save"},{"revision",revision},{"settings",settings},{"layout",layout}},[this](const Json& r){
            response(r,[this](const Json& s){load(s,true);});if(dirty) save->setEnabled(true);
        });
    }
    void exportViews() {
        exported=true;QDir().mkpath(exportPath);
        Json result={{"native_revision",revision},{"devices",layout.size()},{"views",Json::array()}};
        for(auto size:{QSize(1000,700),QSize(1440,900),QSize(1920,1080)}) {
            resize(size);
            int device=size.width()==1000?0:size.width()==1440?2:1;
            select(device,0);tree->setCurrentItem(tree->topLevelItem(device)->child(0));
            for(int page=0;page<4;++page) {
                pages->setCurrentIndex(page);QApplication::sendPostedEvents();centralWidget()->layout()->activate();
                auto file=QString("%1/%2x%3-page%4.png").arg(exportPath).arg(size.width()).arg(size.height()).arg(page);
                if(!grab().save(file)) throw std::runtime_error("Could not export the native panel image");
                result["views"].push_back({{"path",file.toStdString()},{"width",width()},{"height",height()},{"page",page}});
            }
        }
        QFile output(exportPath+"/panel-export.json");output.open(QIODevice::WriteOnly);output.write(QByteArray::fromStdString(result.dump(2)));
        QCoreApplication::quit();
    }
public:
    Panel(QString state,QString exportPath):state(std::move(state)),exportPath(std::move(exportPath)) {
        setWindowTitle("Office Ambilight · OpenRGB");resize(1440,900);setMinimumSize(1000,700);
        setStyleSheet("QWidget{background:#040c14;color:#cdd6e0;font-family:'Segoe UI';font-size:14px;} QMainWindow{background:#020a10;} QGroupBox{background:#091722;border:1px solid #23424c;border-radius:12px;margin-top:16px;padding:16px;} QGroupBox::title{color:#14f5ff;subcontrol-origin:margin;left:16px;} QPushButton{background:#102732;border:1px solid #31535e;border-radius:6px;padding:10px;} QPushButton:hover{border-color:#14f5ff;} QPushButton:focus{border:2px solid #14f5ff;} QPushButton:disabled{color:#677783;} QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox{background:#10202c;border:1px solid #31535e;border-radius:5px;padding:7px;} QTreeWidget,QPlainTextEdit{background:#020a10;border:1px solid #23424c;border-radius:6px;} QTreeWidget::item{padding:7px;} QTreeWidget::item:selected{background:#153d49;color:#14f5ff;} QLabel#title{font-size:24px;font-weight:600;color:#eef7ff;} QSplitter::handle{background:#18313c;} QCheckBox{spacing:8px;} QCheckBox::indicator{width:20px;height:20px;}");
        auto central=new QWidget;auto outer=new QHBoxLayout(central);outer->setContentsMargins(0,0,12,12);
        auto navigation=new QWidget;navigation->setFixedWidth(80);auto rail=new QVBoxLayout(navigation);rail->setContentsMargins(5,16,5,8);
        auto logo=new QLabel("RGB");logo->setAlignment(Qt::AlignCenter);logo->setStyleSheet("color:#14f5ff;font-weight:600;font-size:18px;padding:16px 0");rail->addWidget(logo);
        int pageIndex=0;for(auto name:{"Control","Topology","Devices","Status"}) {auto button=new QPushButton(name);button->setMinimumHeight(56);rail->addWidget(button);int page=pageIndex++;connect(button,&QPushButton::clicked,this,[this,page]{pages->setCurrentIndex(page);});}rail->addStretch();
        auto main=new QVBoxLayout;main->setContentsMargins(16,16,0,0);auto title=new QLabel("Office Ambilight");title->setObjectName("title");main->addWidget(title);main->addWidget(connection);main->addWidget(pages,1);
        auto footer=new QHBoxLayout;footer->addWidget(dirtyLabel);footer->addStretch();auto reload=new QPushButton("Reload saved map");footer->addWidget(reload);footer->addWidget(save);main->addLayout(footer);
        outer->addWidget(navigation);outer->addLayout(main,1);setCentralWidget(central);

        auto controlPage=new QWidget;auto controls=new QVBoxLayout(controlPage);auto form=new QFormLayout;
        fps->setRange(1,30);fps->setSuffix(" FPS");smooth->setRange(0,2000);smooth->setSingleStep(20);smooth->setSuffix(" ms");
        form->addRow(enabled);form->addRow("Output target",fps);form->addRow("Colour adaptation",smooth);
        controls->addWidget(groupBox("Output",form));auto source=new QLabel("LG wallpaper behind ordinary windows. Maximized/fullscreen LG apps take priority.\nOther monitors do not change the source.");source->setWordWrap(true);controls->addWidget(source);
        auto live=new QVBoxLayout;status->setWordWrap(true);status->setMinimumHeight(130);live->addWidget(status);controls->addWidget(groupBox("Live capture",live));
        auto test=new QHBoxLayout;auto calibrate=new QPushButton("Show spatial calibration");test->addWidget(calibrate);test->addWidget(stopTest);controls->addLayout(test);controls->addStretch();pages->addWidget(controlPage);

        auto topology=new QSplitter;tree->setHeaderLabel("Devices and zones");tree->setMinimumWidth(210);topology->addWidget(tree);
        auto mapPage=new QWidget;auto mapLayout=new QVBoxLayout(mapPage);auto all=new QCheckBox("Show all devices");mapLayout->addWidget(all);mapLayout->addWidget(map,1);
        auto note=new QLabel("Drag a selected region. Arrow keys move 0.5%; Shift moves 0.1%. Coloured marks show live LED output.");note->setWordWrap(true);mapLayout->addWidget(note);topology->addWidget(mapPage);
        auto props=new QWidget;props->setMinimumWidth(215);props->setMaximumWidth(285);auto propLayout=new QVBoxLayout(props);auto fields=new QFormLayout;
        fields->addRow("Zone name",zoneName);fields->addRow("Selection",led);
        size_t i=0;for(auto label:{"X (%)","Y (%)","Width (%)","Height (%)"}) {auto box=new QDoubleSpinBox;box->setDecimals(3);box->setRange(i<2?0:.001,100);box->setSingleStep(.5);geometry[i++]=box;fields->addRow(label,box);connect(box,&QDoubleSpinBox::editingFinished,this,[this]{applyGeometry();});}
        propLayout->addWidget(groupBox("Sampling area",fields));auto rotate=new QPushButton("Rotate 90°");auto reverse=new QPushButton("Reverse LED order");propLayout->addWidget(rotate);propLayout->addWidget(reverse);propLayout->addStretch();topology->addWidget(props);topology->setStretchFactor(1,1);topology->setSizes({230,820,250});pages->addWidget(topology);

        auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(devicePage);pages->addWidget(scroll);
        auto diagPage=new QWidget;auto diag=new QVBoxLayout(diagPage);diagnostics->setReadOnly(true);diag->addWidget(diagnostics,1);auto recover=new QPushButton("Recover capture");diag->addWidget(recover);pages->addWidget(diagPage);
        save->setEnabled(false);
        connect(save,&QPushButton::clicked,this,[this]{saveChanges();});
        connect(reload,&QPushButton::clicked,this,[this]{if(dirty && QMessageBox::question(this,"Reload saved map","Discard unsaved mapping changes?")!=QMessageBox::Yes) return;dirty=false;revision=0;if(!client.ready()) client.open(this->state);else poll();});
        connect(enabled,&QCheckBox::toggled,this,[this]{changed();});connect(fps,&QSpinBox::valueChanged,this,[this]{changed();});connect(smooth,&QSpinBox::valueChanged,this,[this]{changed();});
        connect(tree,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){if(updating || !item) return;int g=item->data(0,Qt::UserRole+1).toInt();select(item->data(0,Qt::UserRole).toInt(),std::max(0,g));});
        connect(led,&QComboBox::currentIndexChanged,this,[this]{if(updating) return;map->led=led->currentData().toInt();showGeometry();map->update();});
        connect(zoneName,&QLineEdit::editingFinished,this,[this]{if(updating || layout.empty()) return;layout[map->device]["groups"][map->group]["name"]=zoneName->text().toStdString();changed();});
        connect(all,&QCheckBox::toggled,this,[this](bool on){map->showAll=on;map->update();});
        connect(rotate,&QPushButton::clicked,this,[this]{applyGeometry(90);});connect(reverse,&QPushButton::clicked,this,[this]{applyGeometry(0,true);});
        connect(calibrate,&QPushButton::clicked,this,[this]{action({{"command","calibrate"}});});connect(stopTest,&QPushButton::clicked,this,[this]{action({{"command","stop_test"}});});
        connect(recover,&QPushButton::clicked,this,[this]{action({{"command","recover"}});});
        map->edited=[this]{changed();showGeometry();};
        map->picked=[this](int index){for(int i=1;i<led->count();++i) if(led->itemData(i).toInt()==index) {led->setCurrentIndex(i);break;}};
        client.error=[this](QString message){pending=false;save->setEnabled(false);failure(message);};client.connected=[this]{poll();};
        connect(&refresh,&QTimer::timeout,this,[this]{poll();});refresh.start(1000);client.open(state);
    }
    void closeEvent(QCloseEvent* event) override {
        if(dirty && QMessageBox::question(this,"Unsaved map","Close without saving your changes?")!=QMessageBox::Yes) {event->ignore();return;}
        action({{"command","stop_test"}});event->accept();
    }
};
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("Office Ambilight");
    QCommandLineParser parser;parser.addHelpOption();parser.addOption({"state","Private OpenRGB state directory","directory","D:/Tools/OpenRGB/state-integration"});
    parser.addOption({"export-views","Export all real connected panel views, then exit","directory"});parser.process(app);
    Panel panel(parser.value("state"),parser.value("export-views"));panel.show();return app.exec();
}
