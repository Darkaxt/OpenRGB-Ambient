#pragma once
#include "OfficeStyle.h"
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QToolButton>
#include <QResizeEvent>

namespace officeui {
// Shared visible arrows used by the main panel and fullscreen profile editor.
class ChevronButton : public QToolButton {
    bool up;
    QColor tone;
public:
    ChevronButton(bool direction,QWidget* parent,QColor color=accent):QToolButton(parent),up(direction),tone(color){
        setAutoRepeat(true);setFocusPolicy(Qt::NoFocus);setAccessibleName(up?"Increase value":"Decrease value");
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);if(underMouse())p.fillRect(rect(),QColor("#10333e"));
        p.setPen(QPen(isEnabled()?tone:QColor("#677783"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        auto c=rect().center();QPolygonF arrow;arrow<<QPointF(c.x()-4,c.y()+(up?2:-2))<<QPointF(c.x(),c.y()+(up?-2:2))<<QPointF(c.x()+4,c.y()+(up?2:-2));p.drawPolyline(arrow);
    }
};
class ChoiceBox : public QComboBox {
    QColor tone;
public:
    ChoiceBox(QColor color=accent):tone(color){}
protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(width()-26,2,24,height()-4,QColor("#020b12"));
        p.setPen(QPen(isEnabled()?tone:QColor("#677783"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        auto c=QPointF(width()-14,height()/2.);QPolygonF arrow;arrow<<c+QPointF(-5,-2)<<c+QPointF(0,3)<<c+QPointF(5,-2);p.drawPolyline(arrow);
    }
};
template<class Base> class Stepper : public Base {
    ChevronButton *up,*down;
public:
    Stepper(QColor color=accent):up(new ChevronButton(true,this,color)),down(new ChevronButton(false,this,color)){
        this->setButtonSymbols(QAbstractSpinBox::NoButtons);this->setMinimumHeight(40);
        this->setStyleSheet("padding-right:32px;");
        QObject::connect(up,&QToolButton::clicked,this,[this]{this->stepUp();});
        QObject::connect(down,&QToolButton::clicked,this,[this]{this->stepDown();});
    }
protected:
    void resizeEvent(QResizeEvent* e) override {Base::resizeEvent(e);int half=this->height()/2;up->setGeometry(this->width()-30,1,28,half-1);down->setGeometry(this->width()-30,half,28,this->height()-half-1);}
};
}
