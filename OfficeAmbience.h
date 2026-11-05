#pragma once
#include <memory>
#include <filesystem>
#include <QObject>
#include "OfficeLayout.h"
class OpenRGBPluginAPIInterface;

// The office's existing plugin host and fixed six-device layout. No window is
// constructed: the capture API, output timer and lifetime belong to this object.
class OfficeAmbience final : public QObject {
public:
    OfficeAmbience(OpenRGBPluginAPIInterface* api, const std::filesystem::path& config);
    ~OfficeAmbience() override;
    void stop();
    void resourcesUpdated(unsigned reason);
    office::Json command(const office::Json& request);
private:
    friend struct OfficeLifecycleTest;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
