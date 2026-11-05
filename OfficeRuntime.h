#pragma once
#include "OfficePanel.h"
#include "OfficeAmbience.h"
#include <QThread>
#include <mutex>
class OpenRGBPluginAPIInterface;

// Lifetime belongs to the plugin/GUI thread. The engine is constructed and
// destroyed on the worker, so COM, HWND notifications and timers stay together.
class OfficeRuntime final {
    QThread worker;
    QObject* context=nullptr;
    std::unique_ptr<OfficeAmbience> engine;
    std::mutex lifetime;
public:
    OfficeRuntime(OpenRGBPluginAPIInterface* api,const std::filesystem::path& config);
    ~OfficeRuntime();
    OfficeDispatch dispatch(QObject* recipient);
    void resourcesUpdated(unsigned reason);
};
