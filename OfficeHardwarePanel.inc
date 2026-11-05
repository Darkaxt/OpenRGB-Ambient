// Panel members: native hardware operations live on Devices; screen sampling
// remains in Topology. Commands always use the identity read for this editor.
void hardwareCall(Json request,QWidget* recipient,std::function<void(const Json&)> done={}) {
    request["command"]="hardware";QPointer<QWidget> alive(recipient);
    client.send(request,[this,alive,done](const Json& reply){if(!alive)return;response(reply,[done](const Json& result){if(done)done(result);});});
}
void openHardware(const std::string& key) {
    client.send({{"command","snapshot"},{"hardware",true}},[this,key](const Json& reply){response(reply,[this,key](const Json& snapshot){
        hardwareData=snapshot.at("hardware");
        for(const auto& device:hardwareData.at("devices"))if(device.at("id")==key){hardwareDialog(device);return;}
        failure("Device is offline. Its saved topology is retained; rescan after reconnecting it.");
    });});
}
void hardwareDialog(Json device) {
    using namespace officeui;
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(text(device.at("name"))+" · OpenRGB hardware");dialog->resize(850,650);dialog->setMinimumSize(650,480);dialog->setObjectName("hardwareDialog");
    auto outer=new QVBoxLayout(dialog);auto heading=label(text(device.at("name")));heading->setFont(display(20));outer->addWidget(heading);
    auto ownership=label(device.at("owner")=="hardware"?"Hardware effect owns this device":"Ambience owns this device","hint");ownership->setObjectName("hardwareOwner");outer->addWidget(ownership);
    auto tabs=new QTabWidget;tabs->setObjectName("hardwareTabs");outer->addWidget(tabs,1);
    auto request=[device](const char* action){return Json{{"action",action},{"device",device.at("id")},{"identity",device.at("identity")}};};
    auto overview=new QWidget;auto info=new QVBoxLayout(overview);auto identityCard=new Card("Connection and identity","memory");
    for(auto key:{"vendor","native_name","description","version","serial","location"}) {auto value=QString::fromStdString(device.value(key,std::string()));if(!value.isEmpty()){identityCard->content->addWidget(label(QString::fromUtf8(key).replace('_',' '),"eyebrow"));auto line=label(value);line->setTextInteractionFlags(Qt::TextSelectableByMouse);identityCard->content->addWidget(line);}}
    identityCard->content->addWidget(label(QString("%1 LEDs · %2 hardware zones").arg(device.at("leds").size()).arg(device.at("zones").size()),"hint"));info->addWidget(identityCard);info->addStretch();tabs->addTab(scroll(overview),"Device");

    auto lighting=new QWidget;auto lightingLayout=new QVBoxLayout(lighting);auto ownershipRow=new QHBoxLayout;ownershipRow->addWidget(label("Apply a native effect to hand this device to OpenRGB hardware mode.","hint"),1);
    auto returnToAmbience=button("Use ambience","auto_awesome");returnToAmbience->setObjectName("hardwareAmbience");returnToAmbience->setEnabled(device.value("mapped",false));ownershipRow->addWidget(returnToAmbience);lightingLayout->addLayout(ownershipRow);
    connect(returnToAmbience,&QPushButton::clicked,dialog,[this,dialog,request,ownership]{hardwareCall(request("ambience"),dialog,[ownership](const Json&){ownership->setText("Ambience owns this device");});});
    auto modeCard=new Card("Native lighting mode","light_mode");auto target=new ChoiceBox;target->setObjectName("hardwareModeTarget");target->addItem("Entire device",-1);
    for(const auto& zone:device.at("zones"))if(!zone.at("modes").empty())target->addItem(text(zone.at("name")),zone.at("index").get<int>());
    modeCard->content->addWidget(label("Hardware target","hint"));modeCard->content->addWidget(target);
    auto modeChoice=new ChoiceBox;modeChoice->setObjectName("hardwareMode");modeCard->content->addWidget(modeChoice);
    auto controls=new QWidget;auto form=new QFormLayout(controls);modeCard->content->addWidget(controls);
    auto speed=new Stepper<QSpinBox>,brightness=new Stepper<QSpinBox>;speed->setObjectName("hardwareSpeed");brightness->setObjectName("hardwareBrightness");
    auto direction=new ChoiceBox,source=new ChoiceBox;direction->setObjectName("hardwareDirection");source->setObjectName("hardwareColorSource");
    auto palette=new QWidget;auto paletteLayout=new QVBoxLayout(palette);paletteLayout->setContentsMargins(0,0,0,0);
    auto colorCount=new Stepper<QSpinBox>;colorCount->setObjectName("hardwareColorCount");auto colorIndex=new ChoiceBox;auto color=button("Choose colour","palette");color->setObjectName("hardwareColor");
    form->addRow("Speed",speed);form->addRow("Brightness",brightness);form->addRow("Direction",direction);form->addRow("Colours",source);form->addRow("Palette size",colorCount);form->addRow("LED / palette entry",colorIndex);form->addRow("",color);
    auto active=std::make_shared<Json>(Json::object());auto rendering=std::make_shared<bool>(false);
    auto updateColor=std::make_shared<std::function<void()>>();*updateColor=[active,source,colorIndex,color] {
        if(active->empty() || colorIndex->currentIndex()<0)return;
        auto key=source->currentData().toInt()==MODE_COLORS_PER_LED?"led_colors":"colors";auto i=colorIndex->currentIndex();
        if(!active->contains(key) || size_t(i)>=active->at(key).size())return;unsigned c=active->at(key).at(i);
        color->setStyleSheet(QString("border-left:12px solid rgb(%1,%2,%3);").arg(RGBGetRValue(c)).arg(RGBGetGValue(c)).arg(RGBGetBValue(c)));
    };
    auto rebuildColors=std::make_shared<std::function<void()>>();*rebuildColors=[device,active,source,colorCount,colorIndex,color,form,updateColor] {
        if(active->empty())return;auto cm=source->currentData().toInt();(*active)["color_mode"]=cm;
        form->setRowVisible(colorCount,cm==MODE_COLORS_MODE_SPECIFIC);form->setRowVisible(colorIndex,cm==MODE_COLORS_MODE_SPECIFIC || cm==MODE_COLORS_PER_LED);form->setRowVisible(color,cm==MODE_COLORS_MODE_SPECIFIC || cm==MODE_COLORS_PER_LED);
        colorIndex->clear();
        if(cm==MODE_COLORS_PER_LED){int z=active->at("zone"),first=0,count=int(device.at("leds").size());if(z>=0){first=device["zones"][z]["first"];count=device["zones"][z]["count"];}
            if(!active->contains("led_colors")){(*active)["led_colors"]=Json::array();for(int i=0;i<count;++i)(*active)["led_colors"].push_back(device["leds"][first+i]["color"]);}
            for(int i=0;i<count;++i)colorIndex->addItem(QString("%1 · %2").arg(first+i).arg(text(device["leds"][first+i]["name"])));
        }else if(cm==MODE_COLORS_MODE_SPECIFIC){auto& values=(*active)["colors"];while(values.size()<size_t(colorCount->value()))values.push_back(0xffffff);while(values.size()>size_t(colorCount->value()))values.erase(values.end()-1);for(size_t i=0;i<values.size();++i)colorIndex->addItem(QString("Colour %1").arg(i+1));}
        (*updateColor)();
    };
    auto refreshMode=std::make_shared<std::function<void()>>();*refreshMode=[device,target,modeChoice,active,rendering,speed,brightness,direction,source,colorCount,form,rebuildColors] {
        *rendering=true;int z=target->currentData().toInt(),index=modeChoice->currentIndex();auto modes=z<0?device.at("modes"):device.at("zones").at(z).at("modes");
        if(index<0 || size_t(index)>=modes.size()){*active=Json::object();*rendering=false;return;}*active=modes.at(index);unsigned flags=active->at("flags");
        speed->setRange(active->at("speed_min"),active->at("speed_max"));speed->setValue(active->at("speed"));form->setRowVisible(speed,flags&MODE_FLAG_HAS_SPEED);
        brightness->setRange(active->at("brightness_min"),active->at("brightness_max"));brightness->setValue(active->at("brightness"));form->setRowVisible(brightness,flags&MODE_FLAG_HAS_BRIGHTNESS);
        direction->clear();QStringList names={"Left","Right","Up","Down","Horizontal","Vertical","Up left","Up right","Down left","Down right"};for(int i=0;i<10;++i){unsigned needed=i<2?MODE_FLAG_HAS_DIRECTION_LR:i<4?MODE_FLAG_HAS_DIRECTION_UD:i<6?MODE_FLAG_HAS_DIRECTION_HV:MODE_FLAG_HAS_DIRECTION_DIAG;if(flags&needed)direction->addItem(names[i],i);}direction->setCurrentIndex(direction->findData(active->at("direction").get<int>()));form->setRowVisible(direction,direction->count()>0);
        source->clear();if(flags&MODE_FLAG_HAS_PER_LED_COLOR)source->addItem("Per LED",MODE_COLORS_PER_LED);if(flags&MODE_FLAG_HAS_MODE_SPECIFIC_COLOR)source->addItem("Mode palette",MODE_COLORS_MODE_SPECIFIC);if(flags&MODE_FLAG_HAS_RANDOM_COLOR)source->addItem("Random",MODE_COLORS_RANDOM);source->setCurrentIndex(source->findData(active->at("color_mode").get<int>()));form->setRowVisible(source,source->count()>0);
        colorCount->setRange(active->at("colors_min"),active->at("colors_max"));colorCount->setValue(int(active->at("colors").size()));(*rebuildColors)();*rendering=false;
    };
    auto populate=std::make_shared<std::function<void()>>();*populate=[device,target,modeChoice,refreshMode] {
        QSignalBlocker block(modeChoice);modeChoice->clear();int z=target->currentData().toInt();auto modes=z<0?device.at("modes"):device["zones"][z]["modes"];
        for(const auto& m:modes)modeChoice->addItem(text(m.at("name")));int active=z<0?device.at("active_mode").get<int>():device["zones"][z]["active_mode"].get<int>();modeChoice->setCurrentIndex(active>=0 && active<modeChoice->count()?active:0);(*refreshMode)();
    };
    connect(target,&QComboBox::currentIndexChanged,dialog,[populate]{(*populate)();});connect(modeChoice,&QComboBox::currentIndexChanged,dialog,[refreshMode]{(*refreshMode)();});
    connect(source,&QComboBox::currentIndexChanged,dialog,[rendering,rebuildColors]{if(!*rendering)(*rebuildColors)();});connect(colorCount,&QSpinBox::valueChanged,dialog,[rendering,rebuildColors]{if(!*rendering)(*rebuildColors)();});connect(colorIndex,&QComboBox::currentIndexChanged,dialog,[updateColor]{(*updateColor)();});
    connect(color,&QPushButton::clicked,dialog,[dialog,active,source,colorIndex,updateColor]{auto key=source->currentData().toInt()==MODE_COLORS_PER_LED?"led_colors":"colors";int i=colorIndex->currentIndex();if(i<0 || !active->contains(key) || size_t(i)>=active->at(key).size())return;unsigned old=active->at(key).at(i);auto selected=QColorDialog::getColor(QColor(RGBGetRValue(old),RGBGetGValue(old),RGBGetBValue(old)),dialog,"Hardware colour");if(selected.isValid()){(*active)[key][i]=ToRGBColor(selected.red(),selected.green(),selected.blue());(*updateColor)();}});
    auto actions=new QHBoxLayout;auto apply=button("Apply hardware mode","check",true),saveHardware=button("Save to device","save");apply->setObjectName("hardwareApply");saveHardware->setObjectName("hardwareSave");actions->addWidget(apply);actions->addWidget(saveHardware);modeCard->content->addLayout(actions);
    connect(apply,&QPushButton::clicked,dialog,[this,dialog,request,active,speed,brightness,direction,source,ownership]{if(active->empty())return;auto value=*active;value["speed"]=speed->value();value["brightness"]=brightness->value();value["direction"]=direction->currentData().isValid()?direction->currentData().toInt():0;if(source->currentIndex()>=0)value["color_mode"]=source->currentData().toInt();auto call=request("mode");call["mode"]=value;hardwareCall(call,dialog,[ownership](const Json&){ownership->setText("Hardware effect owns this device · rest and temporary tests still take priority");});});
    auto canSave=[active,target,saveHardware]{saveHardware->setEnabled(!active->empty() && target->currentData().toInt()<0 && (active->at("flags").get<unsigned>()&MODE_FLAG_MANUAL_SAVE));saveHardware->setToolTip("Available only when this driver mode supports manual nonvolatile save");};connect(modeChoice,&QComboBox::currentIndexChanged,dialog,canSave);connect(target,&QComboBox::currentIndexChanged,dialog,canSave);connect(saveHardware,&QPushButton::clicked,dialog,[this,dialog,request]{hardwareCall(request("save_mode"),dialog);});
    (*populate)();canSave();apply->setEnabled(!active->empty());lightingLayout->addWidget(modeCard);lightingLayout->addWidget(label("Speed, brightness, direction and colour controls appear only when the selected driver mode supports them. Applying a mode saves this application's selection; Save to device writes device memory only when supported.","hint"));lightingLayout->addStretch();tabs->addTab(scroll(lighting),"Lighting");

    auto zonesPage=new QWidget;auto zoneLayout=new QVBoxLayout(zonesPage);auto zoneTree=new QTreeWidget;zoneTree->setHeaderLabels({"Hardware zone / segment","First LED","LEDs","Matrix"});
    for(const auto& z:device.at("zones")){auto item=new QTreeWidgetItem(zoneTree,{text(z.at("name")),QString::number(z.at("first").get<int>()),QString::number(z.at("count").get<int>()),QString("%1 × %2").arg(z.at("matrix_width").get<int>()).arg(z.at("matrix_height").get<int>())});for(const auto& s:z.at("segments"))new QTreeWidgetItem(item,{text(s.at("name")),QString::number(s.at("first").get<int>()),QString::number(s.at("count").get<int>())});item->setData(0,Qt::UserRole,z.at("index").get<int>());item->setExpanded(true);}zoneLayout->addWidget(zoneTree,1);
    auto zoneAction=new QHBoxLayout;auto size=new Stepper<QSpinBox>;auto resize=button("Resize hardware zone","straighten");size->setObjectName("hardwareZoneSize");resize->setObjectName("hardwareResize");zoneAction->addWidget(size);zoneAction->addWidget(resize);zoneLayout->addLayout(zoneAction);
    auto zoneHint=label(device.value("mapped",false)?"Hardware geometry is bound to the saved ambience map. Resize sampling regions in Topology; changing LED counts here would invalidate their identities.":"Select a driver-resizable zone. This changes native hardware geometry, not a screen sampling region.","hint");zoneLayout->addWidget(zoneHint);
    connect(zoneTree,&QTreeWidget::currentItemChanged,dialog,[device,size,resize](QTreeWidgetItem* item){bool valid=item && !item->parent();int z=valid?item->data(0,Qt::UserRole).toInt():-1;bool supported=valid && !device.value("mapped",false) && (device["zones"][z]["flags"].get<unsigned>()&(ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE|ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY));size->setEnabled(supported);resize->setEnabled(supported);if(valid){size->setRange(device["zones"][z]["min"],device["zones"][z]["max"]);size->setValue(device["zones"][z]["count"]);}});size->setEnabled(false);resize->setEnabled(false);
    connect(resize,&QPushButton::clicked,dialog,[this,dialog,request,zoneTree,size]{auto item=zoneTree->currentItem();if(!item)return;auto call=request("resize");call["zone"]=item->data(0,Qt::UserRole).toInt();call["count"]=size->value();hardwareCall(call,dialog,[dialog](const Json&){dialog->close();});});tabs->addTab(zonesPage,"Hardware zones");
    auto advanced=new QWidget;auto advancedLayout=new QVBoxLayout(advanced);auto schema=device.at("schema");
    advancedLayout->addWidget(label("Driver configuration","eyebrow"));
    if(schema.is_null() || schema.empty())advancedLayout->addWidget(label("This driver exposes no additional configuration. Hardware modes and zone capabilities are shown on their own tabs.","hint"));
    else if(device.value("mapped",false))advancedLayout->addWidget(label("Driver configuration is retained by OpenRGB. This device's LED geometry is bound to your saved map, so geometry-affecting settings are protected. Screen regions remain editable in Topology.","hint"));
    else {auto values=std::make_shared<Json>(device.at("configuration"));auto form=new QFormLayout;advancedLayout->addLayout(form);schemaFields(schema,values,"",form);auto applyConfig=button("Apply driver settings","save",true);advancedLayout->addWidget(applyConfig);connect(applyConfig,&QPushButton::clicked,dialog,[this,dialog,request,values]{auto call=request("configuration");call["configuration"]=*values;hardwareCall(call,dialog,[dialog](const Json&){dialog->close();});});}
    advancedLayout->addStretch();tabs->addTab(scroll(advanced),"Driver");
    auto close=new QDialogButtonBox(QDialogButtonBox::Close);connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::close);outer->addWidget(close);dialog->show();
}
void schemaFields(const Json& schema,std::shared_ptr<Json> values,const std::string& prefix,QFormLayout* form) {
    auto properties=schema.value("properties",Json::object());for(const auto& [key,s]:properties.items()) {
        auto name=QString::fromStdString(s.value("title",key));auto escaped=QString::fromStdString(key).replace("~","~0").replace("/","~1").toStdString();auto pointer=Json::json_pointer(prefix+"/"+escaped);
        if(!values->contains(pointer))(*values)[pointer]=s.value("default",Json());auto v=values->at(pointer);
        if(s.contains("enum")){auto box=new ChoiceBox;for(const auto& option:s.at("enum"))box->addItem(option.is_string()?text(option):QString::fromStdString(option.dump()));auto entries=s.at("enum");auto found=std::find(entries.begin(),entries.end(),v);box->setCurrentIndex(found==entries.end()?0:int(found-entries.begin()));form->addRow(name,box);connect(box,&QComboBox::currentIndexChanged,box,[values,pointer,entries](int i){if(i>=0)(*values)[pointer]=entries.at(i);});}
        else if(s.value("type",std::string())=="boolean"){auto box=new officeui::Switch;box->setChecked(v.is_boolean() && v.get<bool>());form->addRow(name,box);connect(box,&QCheckBox::toggled,box,[values,pointer](bool on){(*values)[pointer]=on;});}
        else if(s.value("type",std::string())=="integer"){auto box=new Stepper<QSpinBox>;box->setRange(s.value("minimum",0),s.value("maximum",1000000));box->setValue(v.is_number_integer()?v.get<int>():box->minimum());form->addRow(name,box);connect(box,&QSpinBox::valueChanged,box,[values,pointer](int n){(*values)[pointer]=n;});}
        else if(s.value("type",std::string())=="number"){auto box=new Stepper<QDoubleSpinBox>;box->setRange(s.value("minimum",0.),s.value("maximum",1000000.));box->setValue(v.is_number()?v.get<double>():box->minimum());form->addRow(name,box);connect(box,&QDoubleSpinBox::valueChanged,box,[values,pointer](double n){(*values)[pointer]=n;});}
        else if(s.value("type",std::string())=="string"){auto box=new QLineEdit(v.is_string()?text(v):QString());if(s.value("format",std::string())=="password" || key.find("token")!=std::string::npos || key.find("password")!=std::string::npos)box->setEchoMode(QLineEdit::Password);form->addRow(name,box);connect(box,&QLineEdit::textChanged,box,[values,pointer](QString v){(*values)[pointer]=v.toStdString();});}
        else if(s.value("type",std::string())=="object")schemaFields(s,values,pointer.to_string(),form);
        else form->addRow(name,officeui::label("This structured option requires the native driver editor.","hint"));
    }
}
void profilesDialog() {
    client.send({{"command","snapshot"},{"hardware",true}},[this](const Json& reply){response(reply,[this](const Json& snapshot){
        auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("OpenRGB hardware profiles");dialog->resize(580,300);auto outer=new QVBoxLayout(dialog);auto card=new officeui::Card("Hardware profiles","save");card->content->addWidget(officeui::label("Native profiles save hardware colours and modes. They do not replace your screen-sampling topology or ambience presets.","hint"));auto profiles=new ChoiceBox;profiles->setEditable(true);for(const auto& name:snapshot.at("hardware").at("profiles"))profiles->addItem(text(name));card->content->addWidget(profiles);auto actions=new QHBoxLayout;auto save=officeui::button("Save hardware profile","save",true),load=officeui::button("Load hardware profile","folder_open");actions->addWidget(save);actions->addWidget(load);card->content->addLayout(actions);outer->addWidget(card);for(auto pair:{std::make_pair(save,"profile_save"),std::make_pair(load,"profile_load")})connect(pair.first,&QPushButton::clicked,dialog,[this,dialog,profiles,action=pair.second]{hardwareCall({{"action",action},{"name",profiles->currentText().toStdString()}},dialog,[dialog](const Json&){dialog->accept();});});dialog->show();
    });});
}
