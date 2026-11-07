#pragma once
#include "OfficeFullscreenProfiles.h"
#include "OfficeDeviceIcons.h"
#include "OfficeStyle.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <map>

namespace officeui {
class FullscreenProfilesEditor final : public QDialog {
    using Json=office::Json;
    Json profiles;
    QComboBox* apps=new QComboBox;
    QLineEdit* executable=new QLineEdit;
    QLabel* message=new QLabel;
    QPushButton* remove=new QPushButton("Remove profile");
    struct Row{QCheckBox* use;QSpinBox* level;};
    std::map<std::string,Row> rows;
    int defaultLevel;
    std::string selected() const {return apps->currentData().toString().toStdString();}
    void showSelection(){
        const auto key=selected();bool available=!key.empty();remove->setEnabled(available);
        for(auto& [id,row]:rows){
            QSignalBlocker a(row.use),b(row.level);bool override=available&&profiles.at(key).contains(id);
            row.use->setEnabled(available);row.use->setChecked(override);row.level->setEnabled(override);
            row.level->setValue(override?qRound(profiles.at(key).at(id).get<double>()):defaultLevel);
        }
    }
    void store(const std::string& id){
        auto key=selected();if(key.empty())return;auto& row=rows.at(id);
        if(row.use->isChecked())profiles[key][id]=row.level->value();else profiles[key].erase(id);
        row.level->setEnabled(row.use->isChecked());
    }
    void addExecutable(){
        auto key=office::fullscreenExecutableKey(executable->text());
        if(!office::validFullscreenExecutable(key)){message->setText("Enter an executable name, for example NuvioDV.exe, or browse to the app.");return;}
        int index=apps->findData(QString::fromStdString(key));
        if(index<0){profiles[key]=Json::object();apps->addItem(QString::fromStdString(key),QString::fromStdString(key));index=apps->count()-1;}
        apps->setCurrentIndex(index);executable->clear();message->clear();showSelection();
    }
public:
    FullscreenProfilesEditor(Json value,const Json& layout,int fallback,QWidget* parent=nullptr):QDialog(parent),profiles(std::move(value)),defaultLevel(fallback){
        setObjectName("fullscreenProfilesDialog");setWindowTitle("Fullscreen app profiles");setFont(body(15));setMinimumWidth(560);setModal(true);
        setStyleSheet(R"(
            QDialog{background:#081b18;color:#d4e6df;} QLabel,QCheckBox{color:#d4e6df;background:transparent;}
            QComboBox,QLineEdit,QSpinBox{background:#10332c;color:#d4e6df;border:1px solid #28584e;border-radius:5px;padding:6px;min-height:28px;}
            QPushButton{background:#10332c;color:#d4e6df;border:1px solid #28584e;border-radius:6px;padding:8px 12px;min-height:24px;}
            QPushButton:hover{background:#164538;} QPushButton:focus,QComboBox:focus,QLineEdit:focus,QSpinBox:focus{border:1px solid #78efc9;}
            QPushButton:disabled,QCheckBox:disabled,QSpinBox:disabled{color:#82988f;}
            QCheckBox::indicator{width:18px;height:18px;}
        )");
        auto content=new QVBoxLayout(this);content->setContentsMargins(20,18,20,18);content->setSpacing(12);
        content->addWidget(label("Fullscreen app profiles","eyebrow"));
        content->addWidget(label("Override the fullscreen intensity for selected devices. Normal wallpaper levels return when the app leaves fullscreen on the main display."));
        auto addRow=new QHBoxLayout;executable->setObjectName("profileExecutable");executable->setPlaceholderText("NuvioDV.exe");executable->setAccessibleName("Application executable name");
        auto add=button("Add app");add->setObjectName("profileAdd");auto browse=button("Browse…");browse->setObjectName("profileBrowse");
        addRow->addWidget(executable,1);addRow->addWidget(add);addRow->addWidget(browse);content->addLayout(addRow);
        message->setObjectName("profileMessage");message->setWordWrap(true);content->addWidget(message);
        apps->setObjectName("profileApplications");apps->setAccessibleName("Fullscreen application profile");remove->setObjectName("profileRemove");
        auto choose=new QHBoxLayout;choose->addWidget(apps,1);choose->addWidget(remove);content->addLayout(choose);
        for(auto& device:layout){
            const auto id=device.at("id").get<std::string>();const auto name=QString::fromStdString(device.at("name").get<std::string>());
            auto row=new QHBoxLayout;auto picture=new QLabel;picture->setPixmap(deviceIcon(id).pixmap({32,32}));picture->setFixedSize(36,36);row->addWidget(picture);
            auto use=new QCheckBox(name);use->setObjectName(QString::fromStdString("profileOverride-"+id));use->setMinimumHeight(44);use->setAccessibleName("Override "+name);
            auto level=new QSpinBox;level->setRange(0,100);level->setSuffix(" %");level->setObjectName(QString::fromStdString("profileLevel-"+id));level->setAccessibleName(name+" fullscreen intensity");level->setMinimumHeight(32);
            row->addWidget(use,1);row->addWidget(level);content->addLayout(row);rows.emplace(id,Row{use,level});
            connect(use,&QCheckBox::toggled,this,[this,id]{store(id);});connect(level,&QSpinBox::valueChanged,this,[this,id]{store(id);});
        }
        content->addWidget(label(QString("Unchecked devices use the default fullscreen intensity (%1%). Master and device sliders still apply. Profiles match the executable name across updates.").arg(defaultLevel),"hint"));
        content->addWidget(label("Choose Use profiles, then Apply settings in the main window to save.","hint"));
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->setObjectName("profileButtons");buttons->button(QDialogButtonBox::Save)->setText("Use profiles");content->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(apps,&QComboBox::currentIndexChanged,this,[this]{showSelection();});
        connect(add,&QPushButton::clicked,this,[this]{addExecutable();});connect(executable,&QLineEdit::returnPressed,this,[this]{addExecutable();});
        connect(browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Select application",{},"Windows application (*.exe)");if(!path.isEmpty()){executable->setText(path);addExecutable();}});
        connect(remove,&QPushButton::clicked,this,[this]{auto key=selected();if(key.empty())return;profiles.erase(key);apps->removeItem(apps->currentIndex());showSelection();});
        for(auto& [exe,levels]:profiles.items())apps->addItem(QString::fromStdString(exe),QString::fromStdString(exe));
        showSelection();
    }
    const Json& value() const{return profiles;}
};
}
