#pragma once
// Native adaptation of LightBulb DashboardView.axaml / Arc.cs / ArcPoint.cs,
// f74285784be7bc191872c06b1b5e644eb209b98b. See LICENSE-LightBulb-MIT.txt.
#include "OfficeStyle.h"
#include "OfficeScreenPolicy.h"
#include <QTime>
#include <QHideEvent>

namespace officeui {
class Sundial : public QWidget {
    office::Json snapshot;
    double seconds=0;
    QVariantAnimation motion;
    static double wrapped(double v){return office::screen::wrap(v,86400.);}
    void arc(QPainter& p,const QRectF& r,double start,double end,QColor color) {
        double span=wrapped(end-start);if(span<.00001)return;
        p.setPen(QPen(color,13,Qt::SolidLine,Qt::FlatCap));
        p.drawArc(r,qRound((90-start/240)*16),qRound(-span/240*16));
    }
    void stateIcon(QPainter& p,QPointF c,const QString& state) {
        p.save();p.translate(c);p.setPen(QPen(ink,1.8,Qt::SolidLine,Qt::RoundCap));p.setBrush(Qt::NoBrush);
        if(state=="day") {p.drawEllipse(QPointF(0,0),8,8);for(int i=0;i<8;++i){p.drawLine(QPointF(0,-13),QPointF(0,-17));p.rotate(45);}}
        else if(state=="night") {QPainterPath moon;moon.moveTo(6,-14);moon.cubicTo(-17,-13,-17,15,6,14);moon.cubicTo(-5,9,-5,-8,6,-14);p.drawPath(moon);p.drawLine(12,-8,12,-2);p.drawLine(9,-5,15,-5);}
        else if(state=="paused") {p.drawEllipse(QPointF(),16,16);p.drawLine(-4,-6,-4,6);p.drawLine(4,-6,4,6);}
        else if(state=="disabled") {p.drawEllipse(QPointF(),16,16);p.drawLine(-11,11,11,-11);}
        else {p.drawArc(QRectF(-14,-14,28,28),20*16,140*16);p.drawArc(QRectF(-14,-14,28,28),200*16,140*16);p.drawLine(-14,-5,-13,4);p.drawLine(-13,4,-5,0);p.drawLine(14,5,13,-4);p.drawLine(13,-4,5,0);}
        p.restore();
    }
protected:
    void hideEvent(QHideEvent* e) override {motion.stop();QWidget::hideEvent(e);}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        auto center=QPointF(width()/2.,height()/2.);QRectF ring(center.x()-83,center.y()-83,166,166);
        const auto times=snapshot.value("cycle_times",office::Json::object());
        const double riseStart=times.value("sunrise_start",21600.),riseEnd=times.value("sunrise_end",25200.),setStart=times.value("sunset_start",64800.),setEnd=times.value("sunset_end",68400.);
        arc(p,ring,setEnd,riseStart,QColor("#B4ECC17E"));arc(p,ring,riseEnd,setStart,QColor("#B48FBDCD"));
        arc(p,ring,riseStart,riseEnd,QColor("#B4FAEDD6"));arc(p,ring,setStart,setEnd,QColor("#B4FAEDD6"));
        double angle=seconds/86400*2*office::screen::pi;auto marker=center+QPointF(std::sin(angle)*83,-std::cos(angle)*83);
        p.setPen(QPen(QColor("#b1704e"),1));p.setBrush(QColor("#e38755"));p.drawEllipse(marker,7,7);
        QString state=QString::fromStdString(snapshot.value("cycle_state",std::string("disabled")));
        stateIcon(p,center+QPointF(-47,-5),state);
        p.setPen(ink);p.setFont(body(18,QFont::DemiBold));p.drawText(QRectF(center.x()-21,center.y()-30,90,52),Qt::AlignVCenter,
            QString("%1 K\n%2%").arg(snapshot.value("temperature",6600.),0,'f',0).arg(snapshot.value("brightness",1.)*100,0,'f',0));
        p.setFont(body(12));p.setPen(muted);p.drawText(QRectF(center.x()-64,center.y()+24,128,20),Qt::AlignCenter,state.toUpper());
        p.setFont(body(14));p.drawText(QRectF(center.x()-55,center.y()+44,110,20),Qt::AlignCenter,QTime(0,0).addSecs(int(wrapped(seconds))).toString("HH:mm"));
    }
public:
    explicit Sundial(QWidget* parent=nullptr):QWidget(parent),motion(this) {
        setObjectName("screenSundial");setFixedSize(194,194);setAccessibleName("Screen adjustment sundial");
        motion.setDuration(100);motion.setEasingCurve(QEasingCurve::Linear);
        connect(&motion,&QVariantAnimation::valueChanged,this,[this](const QVariant& value){seconds=value.toDouble();update();});
    }
    void setSnapshot(const office::Json& value) {
        snapshot=value;const double target=value.value("cycle_seconds",0.);motion.stop();
        if(isVisible()&&value.value("preview",false)&&!qApp->property("officeReducedMotion").toBool()){
            double delta=wrapped(target-wrapped(seconds));
            // A preview start/stop or wall-clock change jumps directly to the
            // owner's time. Only consecutive preview samples interpolate.
            if(delta<1800){motion.setStartValue(seconds);motion.setEndValue(seconds+delta);motion.start();}
            else seconds=target;
        }else seconds=target;
        setAccessibleDescription(QString("%1, %2 K, %3 percent, %4")
            .arg(QString::fromStdString(value.value("cycle_state",std::string("disabled"))))
            .arg(value.value("temperature",6600.),0,'f',0).arg(value.value("brightness",1.)*100,0,'f',0)
            .arg(QTime(0,0).addSecs(int(wrapped(target))).toString("HH:mm")));
        setToolTip(accessibleDescription());update();
    }
    double markerSeconds() const {return wrapped(seconds);}
    bool animating() const {return motion.state()==QAbstractAnimation::Running;}
};
}
