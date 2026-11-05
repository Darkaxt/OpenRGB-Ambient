#pragma once
// Native rendering of the recovered EDAmbilight design system. Fonts and
// symbols originate in the shipped v1.2.0 release; see assets/office/provenance.
#include "OfficeSymbols.h"
#include <QApplication>
#include <QCheckBox>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QShowEvent>
#include <stdexcept>

namespace officeui {
inline const QColor accent("#14f5ff"), ink("#cdd6e0"), muted("#93a4b5");
inline QFont body(int pixels=14,QFont::Weight weight=QFont::Medium) {QFont f("Office Body");f.setPixelSize(pixels);f.setWeight(weight);return f;}
inline QFont display(int pixels=16) {QFont f("Office Display");f.setPixelSize(pixels);f.setWeight(QFont::Bold);f.setLetterSpacing(QFont::AbsoluteSpacing,2);return f;}
class DotLabel : public QLabel {
public:
    DotLabel(const QString& text={},QWidget* parent=nullptr):QLabel(text,parent) {setContentsMargins(14,0,0,0);}
    void paintEvent(QPaintEvent* e) override {QLabel::paintEvent(e);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(palette().color(QPalette::WindowText));p.drawEllipse(QPointF(4,height()/2.),3,3);}
};
inline void loadFonts() {
    for(auto file:{"Orbitron-400.ttf","Orbitron-700.ttf","Rajdhani-400.ttf","Rajdhani-500.ttf","Rajdhani-600.ttf","Rajdhani-700.ttf","Symbols-Outline.ttf","Symbols-Active.ttf"})
        if(QFontDatabase::addApplicationFont(QString(":/office/")+file)<0) throw std::runtime_error("Could not load an embedded reference font");
    qApp->setFont(body());
}
inline void symbol(QPainter& p,const QRectF& rect,const QString& name,QColor color=accent,bool active=false,int size=24) {
    auto it=symbolCodes.find(name);if(it==symbolCodes.end()) return;
    p.save();QFont font(active?"Office Symbols Active":"Office Symbols Outline");font.setPixelSize(size);p.setFont(font);p.setPen(color);p.drawText(rect,Qt::AlignCenter,QString(QChar(*it)));p.restore();
}
inline QIcon icon(const QString& name,QColor color=accent) {
    QPixmap pix(64,64);pix.fill(Qt::transparent);QPainter p(&pix);symbol(p,pix.rect(),name,color,false,44);return QIcon(pix);
}
inline QLabel* label(const QString& text,const char* role="body") {
    auto out=new QLabel(text);out->setProperty("role",role);out->setFont(body());out->setWordWrap(true);
    if(QString(role)=="eyebrow") {auto f=body(12,QFont::Bold);f.setLetterSpacing(QFont::AbsoluteSpacing,1.2);out->setFont(f);out->setText(text.toUpper());}
    if(QString(role)=="metric") out->setFont(display(28));
    if(QString(role)=="hint") out->setFont(body(13));
    return out;
}
inline QPushButton* button(const QString& text,const QString& glyph={},bool primary=false) {
    auto b=new QPushButton(text);b->setProperty("primary",primary);b->setCursor(Qt::PointingHandCursor);b->setMinimumHeight(34);
    if(!glyph.isEmpty()) {b->setIcon(icon(glyph));b->setIconSize({16,16});}return b;
}

class Card : public QFrame {
public:
    QVBoxLayout* content;
    Card(const QString& title={},const QString& glyph={},QWidget* parent=nullptr):QFrame(parent) {
        setObjectName("referenceCard");setAttribute(Qt::WA_StyledBackground,false);
        content=new QVBoxLayout(this);content->setSizeConstraint(QLayout::SetMinimumSize);content->setContentsMargins(20,18,20,18);content->setSpacing(12);
        if(!title.isEmpty()) {
            auto row=new QHBoxLayout;row->setSpacing(10);
            if(!glyph.isEmpty()) {auto badge=new QLabel;badge->setPixmap(icon(glyph).pixmap(18,18));badge->setObjectName("sectionBadge");badge->setFixedSize(32,32);badge->setAlignment(Qt::AlignCenter);row->addWidget(badge);}
            row->addWidget(label(title,"eyebrow"),1);content->addLayout(row);
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QRectF box=QRectF(rect()).adjusted(1.5,1.5,-1.5,-1.5);
        QLinearGradient surface(box.topLeft(),box.bottomRight());surface.setColorAt(0,QColor("#0a151f"));surface.setColorAt(.35,QColor("#050e17"));surface.setColorAt(1,QColor("#030b12"));
        p.setBrush(surface);p.setPen(QPen(QColor(165,190,205,35),1));p.drawRoundedRect(box,20,20);
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(190,215,230,15),2));p.drawRoundedRect(box.adjusted(3,3,-3,-3),17,17);
        p.setPen(QPen(QColor(20,245,255,95),.8));QPainterPath corner;corner.moveTo(7,29);corner.lineTo(7,19);corner.quadTo(7,7,20,7);corner.lineTo(30,7);p.drawPath(corner);
    }
};
class Backdrop : public QWidget {
public:
    using QWidget::QWidget;
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),QColor("#040c14"));
        QRadialGradient g(QPointF(width()*.25,height()*.48),width()*.7);g.setColorAt(0,QColor(0,92,105,18));g.setColorAt(1,QColor(0,0,0,0));p.fillRect(rect(),g);
        p.setPen(QColor(130,190,210,3));for(int y=0;y<height();y+=4)p.drawLine(0,y,width(),y);
    }
};
class Switch : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
    QSize sizeHint() const override {return {46+(text().isEmpty()?0:10+fontMetrics().horizontalAdvance(text())),32};}
    bool hitButton(const QPoint& p) const override {return rect().contains(p);}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setOpacity(isEnabled()?1:.4);
        QRectF track(1,height()/2.-11,43,22);p.setPen(QPen(isChecked()?accent:QColor("#34434e"),1));
        p.setBrush(isChecked()?QColor("#008795"):QColor("#101b25"));p.drawRoundedRect(track,11,11);
        if(isChecked()) {p.setPen(QPen(QColor(20,245,255,28),5));p.setBrush(Qt::NoBrush);p.drawRoundedRect(track,11,11);}
        p.setPen(Qt::NoPen);p.setBrush(isChecked()?accent:QColor("#c5cbd1"));p.drawEllipse(QPointF(isChecked()?33:12,height()/2.),8,8);
        p.setPen(ink);p.setFont(font());p.drawText(QRect(54,0,width()-54,height()),Qt::AlignVCenter|Qt::AlignLeft,text());
        if(hasFocus()) {p.setPen(QPen(accent,1,Qt::DotLine));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(rect()).adjusted(0,0,-1,-1),5,5);}
    }
};
class Slider : public QSlider {
    void valueAt(QPointF p) {setValue(QStyle::sliderValueFromPosition(minimum(),maximum(),qRound(p.x()-10),std::max(1,width()-20),invertedAppearance()));}
public:
    Slider(QWidget* p=nullptr):QSlider(Qt::Horizontal,p) {setMinimumHeight(28);setCursor(Qt::PointingHandCursor);}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setOpacity(isEnabled()?1:.35);
        double y=height()/2.,x=10+(width()-20)*double(value()-minimum())/std::max(1,maximum()-minimum());
        p.setPen(QPen(QColor("#24313d"),3,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(10,y),QPointF(width()-10,y));
        p.setPen(QPen(accent,3,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(10,y),QPointF(x,y));
        QRadialGradient glow(QPointF(x,y),12);glow.setColorAt(0,QColor(20,245,255,100));glow.setColorAt(1,Qt::transparent);p.setBrush(glow);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(x,y),12,12);
        p.setBrush(accent);p.setPen(QPen(QColor("#003340"),2));p.drawEllipse(QPointF(x,y),6,6);
        if(hasFocus()) {p.setPen(QPen(ink,1,Qt::DotLine));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),4,4);}
    }
    void mousePressEvent(QMouseEvent* e) override {if(e->button()==Qt::LeftButton){setFocus();setSliderDown(true);valueAt(e->position());e->accept();}else QSlider::mousePressEvent(e);}
    void mouseMoveEvent(QMouseEvent* e) override {if(e->buttons()&Qt::LeftButton){valueAt(e->position());e->accept();}else QSlider::mouseMoveEvent(e);}
    void mouseReleaseEvent(QMouseEvent* e) override {setSliderDown(false);QSlider::mouseReleaseEvent(e);}
};
class NavButton : public QPushButton {
    QString glyph;
public:
    NavButton(const QString& title,const QString& name,QWidget* parent=nullptr):QPushButton(title,parent),glyph(name) {setCheckable(true);setFixedHeight(70);setCursor(Qt::PointingHandCursor);setAccessibleName(title);}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        if(isChecked() || underMouse()) {QLinearGradient g(0,0,width(),0);g.setColorAt(0,QColor(20,245,255,isChecked()?25:12));g.setColorAt(1,QColor(20,245,255,3));p.fillRect(rect(),g);}
        if(isChecked()) {p.fillRect(QRectF(0,8,2.5,height()-16),accent);p.fillRect(QRectF(3,8,3,height()-16),QColor(20,245,255,20));}
        symbol(p,QRectF(0,12,width(),28),glyph,isChecked()?accent:QColor("#71818f"),isChecked(),26);
        auto f=body(11,QFont::Bold);f.setLetterSpacing(QFont::AbsoluteSpacing,.7);p.setFont(f);p.setPen(isChecked()?accent:QColor("#8b99a7"));p.drawText(QRectF(0,43,width(),18),Qt::AlignCenter,text().toUpper());
        if(hasFocus()) {p.setPen(QPen(ink,1,Qt::DotLine));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(rect()).adjusted(7,4,-7,-4),4,4);}
    }
};
class Logo : public QWidget {
    QVariantAnimation sonar;
    bool emblem=false;
public:
    Logo(QWidget* p=nullptr,bool startup=false):QWidget(p),emblem(startup) {
        setFixedHeight(startup?140:84);setAccessibleName("Office Ambilight");
        sonar.setStartValue(0.);sonar.setEndValue(1.);sonar.setDuration(3000);sonar.setLoopCount(-1);
        connect(&sonar,&QVariantAnimation::valueChanged,this,[this]{update();});
    }
    void showEvent(QShowEvent* e) override {QWidget::showEvent(e);if(!qApp->property("officeReducedMotion").toBool())sonar.start();}
    void hideEvent(QHideEvent* e) override {sonar.stop();QWidget::hideEvent(e);}
    void motionChanged() {sonar.stop();if(isVisible() && !qApp->property("officeReducedMotion").toBool())sonar.start();update();}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPointF c(width()/2.,emblem?height()/2.:41);
        double core=emblem?60:18,outer=emblem?92:39;
        QRadialGradient glow(c,outer);glow.setColorAt(0,QColor(20,245,255,25));glow.setColorAt(1,Qt::transparent);p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(c,outer,outer);
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(20,245,255,30),1));p.drawEllipse(c,outer,outer);p.setPen(QPen(QColor(20,245,255,80),1));p.drawEllipse(c,core+3,core+3);p.setPen(QPen(accent,1));p.drawEllipse(c,core,core);
        if(sonar.state()==QAbstractAnimation::Running){double phase=sonar.currentValue().toDouble();double radius=core+4+phase*(outer-core-4);p.setPen(QPen(QColor(20,245,255,qRound(70*(1-phase))),1));p.drawEllipse(c,radius,radius);}
        if(emblem) {
            // Exact geometry from the recovered loading SVG, in its 56-unit viewBox.
            p.save();p.translate(c-QPointF(28,28));p.setPen(Qt::NoPen);
            for(int i=0;i<3;++i){p.setBrush(QColor(20,245,255,i==0?255:i==1?158:77));p.drawRoundedRect(QRectF(9,20+i*7,38,3.5),1.75,1.75);}
            p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(20,245,255,217),1.5));p.drawEllipse(QPointF(28,13),5.5,5.5);p.setPen(Qt::NoPen);p.setBrush(QColor(20,245,255,140));p.drawEllipse(QPointF(28,13),2,2);p.restore();
        } else symbol(p,QRectF(c.x()-10,c.y()-10,20,20),"settings_brightness",accent,false,18);
        if(!emblem){p.setPen(QColor(255,255,255,13));p.drawLine(0,height()-1,width(),height()-1);}
    }
};
inline QString stylesheet() {return R"(
QWidget{color:#cdd6e0;background:transparent;}
QMenu{background:#071522;color:#cdd6e0;border:1px solid #23525b;padding:5px;}
QMenu::item{background:#071522;padding:7px 22px;}
QMenu::item:selected{background:#10333e;color:#14f5ff;}
QMenu::item:disabled{color:#637486;}
QMenu::separator{height:1px;background:#23525b;margin:4px 6px;}
QMainWindow,QDialog{background:#040c14;} QLabel[role='eyebrow']{color:#93a4b5;font-weight:700;}
QLabel[role='hint']{color:#93a4b5;} QLabel[role='metric']{color:#14f5ff;}
QLabel#sectionBadge{background:rgba(20,245,255,15);border:1px solid rgba(20,245,255,35);border-radius:8px;}
QPushButton{background:#0a1b25;border:1px solid #23525b;border-radius:9px;padding:5px 12px;font-weight:600;}
QPushButton[primary='true']{background:#07323a;border-color:#008d9c;color:#14f5ff;}
QPushButton:hover{background:#10333e;border-color:#14f5ff;} QPushButton:pressed{background:#0b4650;}
QPushButton:focus{outline:1px dotted #cdd6e0;} QPushButton:disabled{color:#637486;border-color:#25323d;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox{background:#020b12;border:1px solid #16414a;border-radius:9px;padding:7px 10px;min-height:22px;selection-background-color:#106878;}
QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus{border-color:#14f5ff;}
QAbstractSpinBox[readout='true']{font-family:'Office Display';font-weight:700;font-size:17px;color:#14f5ff;background:transparent;border:0;padding:0;}
QComboBox{padding-right:26px;} QComboBox::drop-down{width:25px;border:0;}
QComboBox QAbstractItemView{background:#071522;border:1px solid #23525b;selection-background-color:#10404b;}
QTreeWidget,QPlainTextEdit{background:transparent;border:0;outline:0;selection-background-color:#123c48;}
QTreeWidget::item{padding:7px 4px;} QTreeWidget::item:selected{background:#0c303d;color:#14f5ff;}
QCheckBox{spacing:8px;} QCheckBox::indicator{width:16px;height:16px;border:1px solid #5a6b78;border-radius:4px;background:#0b1925;}
QCheckBox::indicator:checked{background:#00b6c5;border-color:#14f5ff;}
QScrollArea{border:0;background:transparent;} QScrollBar:vertical{background:transparent;width:6px;margin:0;}
QScrollBar::handle:vertical{background:#29424f;min-height:28px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;border:0;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
QScrollBar:horizontal{background:transparent;height:6px;margin:0;} QScrollBar::handle:horizontal{background:#29424f;min-width:28px;border-radius:3px;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;border:0;} QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}
QSplitter::handle{background:transparent;width:8px;} QToolButton{border:0;background:transparent;}
QToolTip{background:#0a1b25;color:#cdd6e0;border:1px solid #23525b;}
QTabWidget::pane{border:1px solid #1c4550;border-radius:10px;background:#06111a;}
QTabBar::tab{background:#081923;color:#92a4b3;padding:10px 16px;border-bottom:2px solid transparent;}
QTabBar::tab:selected{color:#14f5ff;border-bottom-color:#14f5ff;background:#10313a;}
)";}
}
