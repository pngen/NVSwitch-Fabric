#pragma once
#include "nvswitch_fabric/backend.h"
namespace nvswitch_fabric {
class NvidiaBackend : public Backend {
public:
    Result<void> initialize() override;
    void shutdown() noexcept override;
    BackendCapabilities capabilities() const override;
    Result<Backend::Scan> scan() override;
private:
    bool inited_{false};
    int deviceCount_{0};
};
}
