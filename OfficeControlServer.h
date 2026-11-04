#pragma once
#include "OfficeLayout.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QCoreApplication>
#include <QSaveFile>
#include <QFile>
#include <QUuid>
#include <QVariant>
#include <functional>

// One per-user local endpoint owned by the existing native lighting process.
// It accepts only authenticated, bounded JSON commands; no shell/code execution.
class OfficeControlServer final : public QObject {
    QLocalServer server;
    QString endpoint,token,path;
    std::function<office::Json(const office::Json&)> handler;
public:
    OfficeControlServer(const QString& directory,std::function<office::Json(const office::Json&)> call):handler(std::move(call)) {
        token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        endpoint=QString("OpenRGBOffice-%1-%2").arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
        path=directory+"/office-control.json";
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if(!server.listen(endpoint)) throw std::runtime_error("Cannot create the private control endpoint");
        QSaveFile record(path);
        if(!record.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot publish the private control endpoint");
        record.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
        auto data=office::Json{{"endpoint",endpoint.toStdString()},{"token",token.toStdString()},
                               {"pid",QCoreApplication::applicationPid()},{"protocol",1}}.dump();
        if(record.write(data.data(),data.size())!=static_cast<qint64>(data.size()) || !record.commit())
            throw std::runtime_error("Cannot commit the private control endpoint");
        connect(&server,&QLocalServer::newConnection,this,[this] {
            while(auto* socket=server.nextPendingConnection()) {
                socket->setReadBufferSize(262145);
                connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QLocalSocket::disconnected,this,[this,socket] {
                    if(socket->property("authenticated").toBool()) try {handler({{"command","stop_test"}});} catch(std::exception const&) {}
                });
                connect(socket,&QLocalSocket::readyRead,this,[this,socket] {
                    auto pending=socket->property("pending").toByteArray()+socket->readAll();
                    if(pending.size()>262144) { socket->abort();return; }
                    while(pending.contains('\n')) {
                        auto at=pending.indexOf('\n');auto line=pending.left(at);pending.remove(0,at+1);
                        office::Json request,response;
                        try {
                            request=office::Json::parse(line.constData(),line.constData()+line.size());
                            if(request.value("token",std::string())!=token.toStdString()) { socket->abort();return; }
                            socket->setProperty("authenticated",true);
                            response={{"id",request.at("id")},{"ok",true},{"result",handler(request)}};
                        } catch(std::exception const& e) {
                            response={{"id",request.is_object()?request.value("id",office::Json(nullptr)):office::Json(nullptr)},
                                      {"ok",false},{"error",e.what()}};
                        }
                        socket->write(QByteArray::fromStdString(response.dump())+'\n');
                    }
                    socket->setProperty("pending",pending);
                });
            }
        });
    }
    ~OfficeControlServer() override {
        // Socket children are destroyed with the server member, after handler.
        // Disconnect callbacks first so destruction cannot call an expired handler.
        for(auto* socket:server.findChildren<QLocalSocket*>()) QObject::disconnect(socket,nullptr,this,nullptr);
        server.close();
        QFile record(path);
        if(record.open(QIODevice::ReadOnly)) {
            auto bytes=record.readAll();record.close();
            auto data=office::Json::parse(bytes.constData(),bytes.constData()+bytes.size(),nullptr,false);
            if(data.is_object() && data.value("endpoint",std::string())==endpoint.toStdString()) record.remove();
        }
    }
};
