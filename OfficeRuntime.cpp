#include "OfficeRuntime.h"
#include <QPointer>
#include <exception>

OfficeRuntime::OfficeRuntime(OpenRGBPluginAPIInterface* api,const std::filesystem::path& config) {
    context=new QObject;
    context->moveToThread(&worker);
    worker.setObjectName("Office capture and output");
    QObject::connect(&worker,&QThread::finished,context,&QObject::deleteLater);
    worker.start();
    std::exception_ptr failure;
    QMetaObject::invokeMethod(context,[&] {
        try {std::lock_guard lock(lifetime);engine=std::make_unique<OfficeAmbience>(api,config);}
        catch(...) {failure=std::current_exception();}
    },Qt::BlockingQueuedConnection);
    if(failure) {worker.quit();worker.wait();context=nullptr;std::rethrow_exception(failure);}
}
OfficeRuntime::~OfficeRuntime() {
    // Unpublish under the synchronous removal barrier before destroying OS/GPU
    // resources. No callback can obtain a controller pointer during teardown.
    QMetaObject::invokeMethod(context,[this] {
        std::unique_ptr<OfficeAmbience> retired;
        {std::lock_guard lock(lifetime);retired=std::move(engine);}
        retired.reset();
    },Qt::BlockingQueuedConnection);
    worker.quit();worker.wait();
}
OfficeDispatch OfficeRuntime::dispatch(QObject* recipient) {
    return [this,recipient=QPointer<QObject>(recipient)](office::Json request,OfficeReply reply) {
        QMetaObject::invokeMethod(context,[this,recipient,request=std::move(request),reply=std::move(reply)] {
            office::Json result;
            try {result={{"ok",true},{"result",engine->command(request)}};}
            catch(const std::exception& e) {result={{"ok",false},{"error",e.what()}};}
            if(recipient) QMetaObject::invokeMethod(recipient,[recipient,result=std::move(result),reply] {if(recipient)reply(result);},Qt::QueuedConnection);
        },Qt::QueuedConnection);
    };
}
void OfficeRuntime::resourcesUpdated(unsigned reason) {
    // Intentionally synchronous: native OpenRGB may delete a controller as soon
    // as this callback returns. Queuing this notification would use freed memory.
    std::lock_guard lock(lifetime);
    if(engine) engine->resourcesUpdated(reason);
}
