#ifndef NESTOPIA_VITA_UI_EMU_H
#define NESTOPIA_VITA_UI_EMU_H

#include <string>

#include "runtime/ui_emu.h"

class NestopiaVitaUiEmu : public pemu::UiEmu {
public:
    explicit NestopiaVitaUiEmu(pemu::UiMain *ui);

    int load(const pemu::Game &game) override;

    void stop() override;

    void nestopia_config_init();

    int nestopia_core_init(const char *rom_path);

private:
    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    unsigned int m_turbo_counter = 0;
};

#endif // NESTOPIA_VITA_UI_EMU_H
