// Real tray controls dispatch to the existing worker, sharing its native lease.
QMenu *awakeMenu=nullptr,*awakeIntervalMenu=nullptr;
QAction *awakeAutomatic=nullptr,*awakeExpiration=nullptr,*awakeIndefinite=nullptr,*awakeOff=nullptr,*awakeDisplay=nullptr,*awakeInfo=nullptr;
Json awakeState,awakeIntervals;
void awakeRequest(Json request,std::function<void()> accepted={}){
    request["command"]="awake";client.send(request,[this,accepted](const Json& reply){
        if(!reply.value("ok",false)){loadAwake(awakeState);QMessageBox::warning(this,"Keep awake",text(reply.at("error")));return;}
        loadAwake(reply.at("result"));if(accepted)accepted();poll();
    });
}
void loadAwake(const Json& value){
    if(!awakeMenu||!value.is_object()||!value.contains("mode"))return;awakeState=value;
    auto mode=value.at("mode").get<std::string>();
    awakeAutomatic->setChecked(mode=="automatic");awakeExpiration->setChecked(mode=="expiration");awakeIndefinite->setChecked(mode=="indefinite");awakeOff->setChecked(mode=="off");awakeIntervalMenu->menuAction()->setChecked(mode=="interval");
    awakeDisplay->setChecked(mode=="automatic"||value.value("keep_screen_on",false));awakeDisplay->setEnabled(mode!="off"&&mode!="automatic");
    auto options=value.value("intervals",Json::array());
    if(options!=awakeIntervals){
        awakeIntervalMenu->clear();awakeIntervals=options;
        for(auto& option:options){auto seconds=option.at("seconds").get<int64_t>();auto action=awakeIntervalMenu->addAction(text(option.at("label")));action->setCheckable(true);action->setData(QVariant::fromValue<qlonglong>(seconds));connect(action,&QAction::triggered,this,[this,seconds]{awakeRequest({{"action","mode"},{"mode","interval"},{"seconds",seconds}});});}
    }
    for(auto action:awakeIntervalMenu->actions())action->setChecked(mode=="interval"&&action->data().toLongLong()==value.value("interval_seconds",int64_t(0)));
    auto error=value.value("error",std::string());auto until=value.value("until_ms",int64_t(0));
    awakeInfo->setText(!error.empty()?text(error):until>0?QString("Until %1").arg(QDateTime::fromMSecsSinceEpoch(until).toString("ddd d MMM · HH:mm")):mode=="automatic"?"Automatic: main-display maximized / fullscreen apps":mode=="off"?"Using the selected power plan":value.value("display_required",false)?"System and display kept awake":"System kept awake; display follows power plan");
}
void showAwakeExpiration(){
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("awakeExpirationDialog");dialog->setWindowTitle("Keep awake until");dialog->setMinimumWidth(420);
    auto column=new QVBoxLayout(dialog);column->setContentsMargins(24,24,24,24);column->setSpacing(16);column->addWidget(officeui::label("Keep awake until this local date and time"));
    auto date=new QDateTimeEdit;date->setObjectName("awakeExpirationTime");date->setAccessibleName("Awake expiration date and time");date->setCalendarPopup(true);date->setDisplayFormat("ddd dd MMM yyyy  HH:mm");date->setMinimumDateTime(QDateTime::currentDateTime());
    auto until=awakeState.value("until_ms",int64_t(0));date->setDateTime(until>QDateTime::currentMSecsSinceEpoch()?QDateTime::fromMSecsSinceEpoch(until):QDateTime::currentDateTime().addSecs(3600));column->addWidget(date);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);column->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(dialog,&QDialog::rejected,this,[this]{loadAwake(awakeState);});
    QPointer<QDialog> guard=dialog;connect(buttons,&QDialogButtonBox::accepted,this,[this,guard,date]{awakeRequest({{"action","mode"},{"mode","expiration"},{"until_ms",date->dateTime().toMSecsSinceEpoch()}},[guard]{if(guard)guard->accept();});});dialog->open();
}
void buildAwakeMenu(){
    awakeMenu=trayMenu->addMenu("Awake");awakeMenu->setObjectName("awakeMenu");
    auto modes=new QActionGroup(this);modes->setExclusive(true);
    awakeAutomatic=awakeMenu->addAction("Automatic (maximized / fullscreen apps)");awakeAutomatic->setObjectName("awakeAutomatic");
    awakeIntervalMenu=awakeMenu->addMenu("Keep awake on interval");awakeIntervalMenu->setObjectName("awakeIntervalMenu");
    awakeExpiration=awakeMenu->addAction("Keep awake until expiration date and time");awakeExpiration->setObjectName("awakeExpiration");
    awakeIndefinite=awakeMenu->addAction("Keep awake indefinitely");awakeIndefinite->setObjectName("awakeIndefinite");
    awakeOff=awakeMenu->addAction("Off (keep using the selected power plan)");awakeOff->setObjectName("awakeOff");
    for(auto action:{awakeAutomatic,awakeIntervalMenu->menuAction(),awakeExpiration,awakeIndefinite,awakeOff}){action->setCheckable(true);modes->addAction(action);}
    awakeMenu->addSeparator();awakeDisplay=awakeMenu->addAction("Keep screen on");awakeDisplay->setObjectName("awakeKeepScreenOn");awakeDisplay->setCheckable(true);
    awakeInfo=awakeMenu->addAction("Waiting for the lighting owner");awakeInfo->setEnabled(false);
    connect(awakeAutomatic,&QAction::triggered,this,[this]{awakeRequest({{"action","mode"},{"mode","automatic"}});});
    connect(awakeIndefinite,&QAction::triggered,this,[this]{awakeRequest({{"action","mode"},{"mode","indefinite"}});});
    connect(awakeOff,&QAction::triggered,this,[this]{awakeRequest({{"action","mode"},{"mode","off"}});});
    connect(awakeExpiration,&QAction::triggered,this,[this]{showAwakeExpiration();});
    connect(awakeDisplay,&QAction::triggered,this,[this](bool on){awakeRequest({{"action","display"},{"enabled",on}});});
}
