#ifndef NESTOPIA_VITA_CONFIG_H
#define NESTOPIA_VITA_CONFIG_H

#include "runtime/pemu_config.h"

class NestopiaVitaConfig final : public pemu::PEMUConfig {
public:
    NestopiaVitaConfig(c2d::Renderer *renderer, int version);

    std::string getCoreVersion() override {
        return "Nestopia 1.52.0";
    }

    std::vector<std::string> getCoreSupportedExt() override {
        return {".zip", ".nes", ".nez", ".unf", ".unif"};
    }
};

#endif // NESTOPIA_VITA_CONFIG_H
