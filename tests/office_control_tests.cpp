#include "../OfficeControlServer.h"
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
using office::Json;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir directory;
        unsigned calls=0;
        OfficeControlServer server(directory.path(),[&](const Json& request)->Json {
            auto command=request.at("command").get<std::string>();
            if(command=="stop_test") return {{"restored",true}};
            ++calls;
            if(command!="echo") throw std::runtime_error("Unknown control command");
            return request.at("payload");
        });
        QFile record(directory.path()+"/office-control.json");record.open(QIODevice::ReadOnly);
        auto data=record.readAll();auto endpoint=Json::parse(data.constData(),data.constData()+data.size());record.close();
        auto exchange=[&](Json request,bool badToken=false,bool split=false) {
            QLocalSocket client;QEventLoop loop;QByteArray response;
            request["id"]=123;request["token"]=badToken?"wrong":endpoint.at("token").get<std::string>();
            auto bytes=QByteArray::fromStdString(request.dump())+'\n';bool tail=false;
            QObject::connect(&client,&QLocalSocket::connected,&loop,[&] {if(split) client.write(bytes.left(5));else client.write(bytes);});
            QObject::connect(&client,&QLocalSocket::bytesWritten,&loop,[&](qint64){if(split && !tail) {tail=true;client.write(bytes.mid(5));}});
            QObject::connect(&client,&QLocalSocket::readyRead,&loop,[&] {response+=client.readAll();if(response.contains('\n')) loop.quit();});
            QObject::connect(&client,&QLocalSocket::disconnected,&loop,&QEventLoop::quit);
            QObject::connect(&client,&QLocalSocket::errorOccurred,&loop,[&](auto){loop.quit();});
            client.connectToServer(QString::fromStdString(endpoint.at("endpoint").get<std::string>()));loop.exec();
            client.abort();
            return response.isEmpty()?Json(nullptr):Json::parse(response.constData(),response.constData()+response.size());
        };
        auto answer=exchange({{"command","echo"},{"payload",{{"led",313},{"rgb",Json::array({10,20,30})}}}},false,true);
        if(!answer.at("ok").get<bool>() || answer.at("id")!=123 || answer.at("result").at("led")!=313) throw std::runtime_error("Authenticated split-frame reply failed");
        unsigned before=calls;
        if(!exchange({{"command","echo"},{"payload",1}},true).is_null() || before!=calls) throw std::runtime_error("Unauthenticated request reached the handler");
        answer=exchange({{"command","unknown"}});
        if(answer.at("ok").get<bool>() || !answer.contains("error")) throw std::runtime_error("Handler failure was reported as success");
        if(!exchange({{"command","echo"},{"payload",std::string(262145,'x')}}).is_null()) throw std::runtime_error("Oversized request accepted");
        std::cout<<"PASS: private authenticated IPC, split messages, response identity, errors and size boundary\n";return 0;
    } catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
