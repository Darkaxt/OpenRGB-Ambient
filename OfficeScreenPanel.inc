// Members of the real Office Panel; all actions use its existing typed/IPC owner.
Json screenState,screenDraft;
uint64_t screenRevision=0;
bool screenDirty=false,screenUpdating=false;
QLabel* screenStatus=new QLabel;
officeui::Sundial* screenSundial=new officeui::Sundial;
QLabel *screenSunrise=new QLabel,*screenSunset=new QLabel;
QLabel* screenMessage=new QLabel;
QCheckBox* screenManaged=new officeui::Switch("Manage screen adjustments");
QPushButton* screenSave=new QPushButton("Save screen settings");
QPushButton* screenPreview=new QPushButton("Preview full day");
QTabWidget* screenTabs=new QTabWidget;
QMenu *trayMenu=nullptr,*lightingPauseMenu=nullptr,*screenPauseMenu=nullptr;
QAction *lightingEnable=nullptr,*screenEnable=nullptr;
std::map<std::string,QDoubleSpinBox*> screenNumbers;
std::map<std::string,QCheckBox*> screenChecks;
std::map<std::string,QTimeEdit*> screenTimes;
std::map<std::string,QKeySequenceEdit*> screenKeys;
QPlainTextEdit* screenExclusions=new QPlainTextEdit;

void screenChanged(){if(screenUpdating)return;screenDirty=true;screenSave->setEnabled(client.ready());screenMessage->setText("Unsaved screen settings");}
void screenRequest(Json request){
    request["command"]="screen";client.send(request,[this](const Json& reply){
        if(!reply.value("ok",false)){screenMessage->setText(text(reply.at("error")));screenMessage->setStyleSheet("color:#ffb54a");return;}
        screenMessage->setStyleSheet("");loadScreen(reply.at("result"));poll();
    });
}
void loadScreen(const Json& value){
    if(!value.is_object()||!value.contains("settings"))return;screenState=value;
    bool lightingOff=value.value("lighting_disabled",false),screenOff=value.value("screen_disabled",false);
    if(lightingEnable){lightingEnable->setVisible(lightingOff);lightingPauseMenu->menuAction()->setVisible(!lightingOff);screenEnable->setVisible(screenOff);screenPauseMenu->menuAction()->setVisible(!screenOff);}
    screenUpdating=true;screenManaged->setChecked(value.value("managed",false));screenSundial->setSnapshot(value);
    screenSunrise->setText(QString("SUNRISE\n%1").arg(text(value.value("sunrise",std::string("—")))));
    screenSunset->setText(QString("SUNSET\n%1").arg(text(value.value("sunset",std::string("—")))));
    refresh.setInterval(isVisible()&&pages->currentIndex()==7&&value.value("preview",false)?100:(pages->currentIndex()==1||pages->currentIndex()==4)?200:1000);
    auto reason=value.value("automatic_pause",std::string());
    screenStatus->setText(QString("%1")
        .arg(!value.value("managed",false)?"Ready to replace standalone LightBulb":screenOff?"Screen adjustments disabled":!reason.empty()?QString("Paused: ")+text(reason):value.value("gamma_valid",false)?QString("Verified on %1 displays").arg(value.value("display_count",0)):"Waiting for verified display gamma"));
    screenPreview->setText(value.value("preview",false)?"Stop preview":"Preview full day");
    screenPreview->setEnabled(value.value("managed",false));
    if(!screenDirty){
        screenRevision=value.value("revision",uint64_t(0));screenDraft=value.at("settings");
        for(auto& [key,widget]:screenNumbers){double n=0;
            if(key=="latitude"||key=="longitude"){auto location=screenDraft.value("Location",Json());if(location.is_object())n=location.value(key=="latitude"?"Latitude":"Longitude",0.);}
            else if(key=="transition_minutes"||key=="smoothing_seconds"){auto v=text(screenDraft.at(key=="transition_minutes"?"ConfigurationTransitionDuration":"ConfigurationSmoothingMaxDuration"));auto t=QTime::fromString(v,"HH:mm:ss");n=QTime(0,0).secsTo(t)/(key=="transition_minutes"?60.:1.);}
            else if(key=="transition_offset")n=screenDraft.at("ConfigurationTransitionOffset").get<double>()*100;
            else{auto slash=key.find('/');n=screenDraft.at(key.substr(0,slash)).at(key.substr(slash+1)).get<double>();if(key.ends_with("Brightness"))n*=100;}
            widget->setValue(n);
        }
        for(auto& [key,widget]:screenChecks)widget->setChecked(screenDraft.value(key,false));
        for(auto& [key,widget]:screenTimes)widget->setTime(QTime::fromString(text(screenDraft.at(key)),"HH:mm:ss"));
        for(auto& [key,widget]:screenKeys){auto v=screenDraft.value(key,Json(""));widget->setKeySequence(v.is_string()?QKeySequence(text(v),QKeySequence::PortableText):QKeySequence());widget->setToolTip(v.is_object()&&!v.value("Key",Json(0)).is_number_integer()?"Imported shortcut is retained until edited":"");}
        QStringList paths;if(screenDraft.contains("WhitelistedApplications")&&screenDraft["WhitelistedApplications"].is_array())for(auto& app:screenDraft["WhitelistedApplications"])paths<<text(app.at("ExecutableFilePath"));screenExclusions->setPlainText(paths.join('\n'));
        screenSave->setEnabled(false);
    }
    auto problem=value.value("error",std::string()),keys=value.value("hotkey_error",std::string());
    if(!keys.empty())problem+=(problem.empty()?"":"\n")+keys;
    if(!problem.empty()){screenMessage->setText(text(problem));screenMessage->setStyleSheet("color:#ffb54a");}
    else if(!screenDirty){screenMessage->setStyleSheet("");screenMessage->setText(value.value("imported",false)?"Imported LightBulb settings · original file preserved":"Screen settings saved separately from RGB topology");}
    if(tray)tray->setToolTip(QString("Office Ambilight\nLighting: %1\nScreen: %2 K · %3% · %4").arg(lightingOff?"disabled":"enabled").arg(value.value("temperature",6600.),0,'f',0).arg(value.value("brightness",1.)*100,0,'f',0).arg(screenOff?"disabled":reason.empty()?"enabled":"paused"));
    screenUpdating=false;
}
void saveScreen(){
    auto next=screenDraft;
    for(auto& [key,widget]:screenNumbers){auto n=widget->value();
        if(key=="latitude"||key=="longitude"){if(!next["Location"].is_object())next["Location"]=Json::object();next["Location"][key=="latitude"?"Latitude":"Longitude"]=n;}
        else if(key=="transition_minutes"||key=="smoothing_seconds")next[key=="transition_minutes"?"ConfigurationTransitionDuration":"ConfigurationSmoothingMaxDuration"]=QTime(0,0).addSecs(qRound(n*(key=="transition_minutes"?60:1))).toString("HH:mm:ss").toStdString();
        else if(key=="transition_offset")next["ConfigurationTransitionOffset"]=n/100;
        else{auto slash=key.find('/');next[key.substr(0,slash)][key.substr(slash+1)]=key.ends_with("Brightness")?n/100:n;}
    }
    for(auto& [key,widget]:screenChecks)next[key]=widget->isChecked();
    for(auto& [key,widget]:screenTimes)next[key]=widget->time().toString("HH:mm:ss").toStdString();
    // Preserve untouched imported shortcut representations, including numeric
    // Avalonia keys. Only an actual edit replaces one with Qt portable text.
    for(auto& [key,widget]:screenKeys)if(widget->property("screenEdited").toBool())next[key]=widget->keySequence().toString(QKeySequence::PortableText).toStdString();
    auto apps=Json::array();for(auto line:screenExclusions->toPlainText().split('\n',Qt::SkipEmptyParts))apps.push_back({{"ExecutableFilePath",QDir::toNativeSeparators(line.trimmed()).toStdString()}});next["WhitelistedApplications"]=apps;
    client.send({{"command","screen"},{"action","save"},{"revision",screenRevision},{"settings",next}},[this](const Json& reply){
        if(!reply.value("ok",false)){screenMessage->setText(text(reply.at("error")));screenMessage->setStyleSheet("color:#ffb54a");return;}
        screenDirty=false;for(auto& [key,widget]:screenKeys)widget->setProperty("screenEdited",false);loadScreen(reply.at("result"));
    });
}
QWidget* buildScreenPage(){
    using namespace officeui;
    auto divider=[](){auto line=new QFrame;line->setFrameShape(QFrame::HLine);line->setStyleSheet("color:#1a303d;max-height:1px;");return line;};
    auto page=new QWidget;page->setObjectName("screenPage");auto outer=new QVBoxLayout(page);outer->setContentsMargins(24,16,24,16);outer->setSpacing(14);
    auto stateCard=new Card;auto stateRow=new QHBoxLayout;stateRow->setSpacing(12);
    for(auto l:{screenSunset,screenSunrise}){l->setFont(body(15));l->setAlignment(Qt::AlignCenter);l->setMinimumWidth(74);}
    stateRow->addWidget(screenSunset);stateRow->addWidget(screenSundial);stateRow->addWidget(screenSunrise);stateRow->addSpacing(12);
    auto controls=new QVBoxLayout;controls->addWidget(label("Screen adjustment","eyebrow"));screenStatus->setObjectName("screenStatus");screenStatus->setFont(body(15));screenStatus->setWordWrap(true);controls->addWidget(screenStatus);screenManaged->setObjectName("screenManaged");controls->addWidget(screenManaged);screenPreview->setObjectName("screenCyclePreview");controls->addWidget(screenPreview);stateRow->addLayout(controls,1);stateCard->content->addLayout(stateRow);outer->addWidget(stateCard);
    screenTabs->setObjectName("screenTabs");outer->addWidget(screenTabs,1);
    auto tab=[&](const QString& title){auto body=new QWidget;auto column=new QVBoxLayout(body);column->setContentsMargins(16,16,16,16);column->setSpacing(14);auto scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(body);screenTabs->addTab(scroll,title);return column;};
    auto number=[&](QVBoxLayout* parent,const QString& caption,const std::string& key,double minimum,double maximum,double step,const QString& suffix){
        auto row=new QWidget;auto l=new QHBoxLayout(row);l->setContentsMargins(0,0,0,0);auto captionLabel=label(caption);l->addWidget(captionLabel,1);auto spin=new Stepper<QDoubleSpinBox>;spin->setRange(minimum,maximum);spin->setDecimals(step<.01?6:step<1?2:0);spin->setSingleStep(step);spin->setSuffix(suffix);spin->setMinimumWidth(160);spin->setAccessibleName(caption);spin->setObjectName(QString::fromStdString("screen-"+key));screenNumbers[key]=spin;
        if(key.starts_with("DayConfiguration/")||key.starts_with("NightConfiguration/")){
            auto slider=new Slider;slider->setObjectName(QString::fromStdString("screen-slider-"+key));slider->setAccessibleName(caption);slider->setRange(int(minimum),int(maximum));slider->setSingleStep(int(step));slider->setPageStep(int(step*5));slider->setMinimumWidth(140);l->addWidget(slider,2);
            connect(slider,&QSlider::valueChanged,spin,[spin](int n){spin->setValue(n);});connect(spin,&QDoubleSpinBox::valueChanged,slider,[slider](double n){QSignalBlocker blocked(slider);slider->setValue(qRound(n));});
        }
        l->addWidget(spin);parent->addWidget(row);connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{screenChanged();});
    };
    auto check=[&](QVBoxLayout* parent,const QString& caption,const char* key){auto c=new Switch(caption);c->setObjectName(QString::fromLatin1(key));screenChecks[key]=c;parent->addWidget(c);connect(c,&QCheckBox::toggled,this,[this]{screenChanged();});};
    auto general=tab("Day and night");
    number(general,"Day temperature","DayConfiguration/Temperature",500,20000,50," K");number(general,"Day brightness","DayConfiguration/Brightness",10,100,1,"%");general->addWidget(divider());number(general,"Night temperature","NightConfiguration/Temperature",500,20000,50," K");number(general,"Night brightness","NightConfiguration/Brightness",10,100,1,"%");
    auto offsets=new QHBoxLayout;for(auto item:{std::pair{"Cooler",Json{{"action","offset"},{"name","temperature_offset"},{"delta",100}}},std::pair{"Warmer",Json{{"action","offset"},{"name","temperature_offset"},{"delta",-100}}},std::pair{"Brighter",Json{{"action","offset"},{"name","brightness_offset"},{"delta",.05}}},std::pair{"Dimmer",Json{{"action","offset"},{"name","brightness_offset"},{"delta",-.05}}},std::pair{"Reset offsets",Json{{"action","offset_reset"}}}}){auto b=button(item.first);offsets->addWidget(b);connect(b,&QPushButton::clicked,this,[this,request=item.second]{screenRequest(request);});}general->addLayout(offsets);general->addWidget(label("Screen adjustments and RGB lighting have independent disable controls. RGB brightness following uses the applied display factor once.","hint"));general->addStretch();
    auto schedule=tab("Schedule");check(schedule,"Use manual sunrise and sunset","IsManualSunriseSunsetEnabled");
    for(auto item:{std::pair{"Sunrise","ManualSunriseTime"},std::pair{"Sunset","ManualSunsetTime"}}){auto row=new QHBoxLayout;row->addWidget(label(item.first),1);auto t=new QTimeEdit;t->setDisplayFormat("HH:mm");t->setObjectName(item.second);t->setAccessibleName(item.first);t->setMinimumWidth(160);screenTimes[item.second]=t;row->addWidget(t);schedule->addLayout(row);connect(t,&QTimeEdit::timeChanged,this,[this]{screenChanged();});}
    number(schedule,"Latitude","latitude",-90,90,.000001,{});number(schedule,"Longitude","longitude",-180,180,.000001,{});number(schedule,"Transition duration","transition_minutes",0,180,1," min");number(schedule,"Transition offset","transition_offset",0,100,1,"%");schedule->addWidget(label("Automatic solar times use these coordinates and Windows' current timezone. Until sunrise uses the same schedule for either pause menu.","hint"));schedule->addStretch();
    auto behavior=tab("Behavior");check(behavior,"Use day configuration while disabled","IsDefaultToDayConfigurationEnabled");check(behavior,"Pause screen adjustments for fullscreen applications","IsPauseWhenFullScreenEnabled");check(behavior,"Smooth display adjustments","IsConfigurationSmoothingEnabled");number(behavior,"Smoothing duration","smoothing_seconds",0,60,1," s");check(behavior,"Periodically reapply gamma","IsGammaPollingEnabled");check(behavior,"Pause for excluded applications","IsApplicationWhitelistEnabled");behavior->addWidget(label("Excluded executable paths · one per line","hint"));screenExclusions->setObjectName("screenExclusions");screenExclusions->setAccessibleName("Excluded screen-adjustment applications");screenExclusions->setMinimumHeight(100);behavior->addWidget(screenExclusions);connect(screenExclusions,&QPlainTextEdit::textChanged,this,[this]{screenChanged();});behavior->addWidget(label("Fullscreen and application exclusions affect screen adjustments only; RGB capture continues. Display changes and resume use native lifecycle events.","hint"));behavior->addStretch();
    auto shortcuts=tab("Hotkeys");for(auto item:{std::pair{"Toggle screen adjustments","ToggleHotKey"},std::pair{"Open Ambilight","FocusWindowHotKey"},std::pair{"Increase temperature","IncreaseTemperatureOffsetHotKey"},std::pair{"Decrease temperature","DecreaseTemperatureOffsetHotKey"},std::pair{"Increase brightness","IncreaseBrightnessOffsetHotKey"},std::pair{"Decrease brightness","DecreaseBrightnessOffsetHotKey"},std::pair{"Reset offsets","ResetConfigurationOffsetHotKey"}}){auto row=new QHBoxLayout;row->addWidget(label(item.first),1);auto edit=new QKeySequenceEdit;edit->setMaximumSequenceLength(1);edit->setAccessibleName(item.first);edit->setObjectName(item.second);edit->setMinimumWidth(210);screenKeys[item.second]=edit;row->addWidget(edit);shortcuts->addLayout(row);connect(edit,&QKeySequenceEdit::keySequenceChanged,this,[this,edit]{if(!screenUpdating)edit->setProperty("screenEdited",true);screenChanged();});}shortcuts->addWidget(label("Use Ctrl, Alt, Shift or Win with a letter, digit, arrow, Space or function key. Conflicting registrations are reported here.","hint"));shortcuts->addStretch();
    screenMessage->setObjectName("screenMessage");screenMessage->setWordWrap(true);outer->addWidget(screenMessage);auto actions=new QHBoxLayout;auto reload=button("Reload screen settings","refresh");screenSave->setObjectName("screenSave");screenSave->setEnabled(false);actions->addWidget(reload);actions->addStretch();actions->addWidget(screenSave);outer->addLayout(actions);
    connect(screenSave,&QPushButton::clicked,this,[this]{saveScreen();});connect(reload,&QPushButton::clicked,this,[this]{screenDirty=false;screenRevision=0;poll();});connect(screenManaged,&QCheckBox::toggled,this,[this](bool on){if(!screenUpdating)screenRequest({{"action","managed"},{"enabled",on}});});connect(screenPreview,&QPushButton::clicked,this,[this]{screenRequest({{"action","preview"}});});
    return page;
}
void buildTrayMenu(){
    trayMenu=new QMenu(this);trayMenu->setObjectName("officeTrayMenu");
    trayMenu->addAction("Open",this,[this]{showNormal();raise();activateWindow();});
    auto pauseMenu=[this](const QString& title,const char* target,QAction*& enable){
        enable=trayMenu->addAction(QString("Enable ")+(QString(target)=="lighting"?"lighting":"screen adjustments"),this,[this,target]{screenRequest({{"action","pause"},{"target",target},{"mode","enable"}});});enable->setVisible(false);
        auto menu=trayMenu->addMenu(title);menu->setObjectName(QString(target)+"PauseMenu");
        menu->addAction("Until enabled again",this,[this,target]{screenRequest({{"action","pause"},{"target",target},{"mode","forever"}});});menu->addAction("Until sunrise",this,[this,target]{screenRequest({{"action","pause"},{"target",target},{"mode","sunrise"}});});
        for(int minutes:{1,5,15,30,60,180,360,720,1440}){auto label=minutes<60?QString("For %1 minute%2").arg(minutes).arg(minutes==1?"":"s"):minutes<1440?QString("For %1 hour%2").arg(minutes/60).arg(minutes==60?"":"s"):QString("For 1 day");auto action=menu->addAction(label,this,[this,target,minutes]{screenRequest({{"action","pause"},{"target",target},{"mode","duration"},{"minutes",minutes}});});action->setData(minutes);}
        return menu;
    };
    lightingPauseMenu=pauseMenu("Disable lighting","lighting",lightingEnable);screenPauseMenu=pauseMenu("Disable screen adjustments","screen",screenEnable);trayMenu->addSeparator();trayMenu->addAction("Exit",this,[this]{quitRequested=true;close();});
}
