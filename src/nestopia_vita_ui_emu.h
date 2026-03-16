#ifndef NESTOPIA_VITA_UI_EMU_H
#define NESTOPIA_VITA_UI_EMU_H

#include <memory>
#include <string>

#include "runtime/ui_emu.h"

class NestopiaVitaCoreBridge;

class NestopiaVitaUiEmu : public pemu::UiEmu {
public:
    explicit NestopiaVitaUiEmu(pemu::UiMain *ui);
    ~NestopiaVitaUiEmu() override;

    int load(const pemu::Game &game) override;

    void stop() override;
    void pause() override;
    void resume() override;

    NestopiaVitaCoreBridge *getCore() const { return m_core.get(); }

private:
    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    unsigned int m_turbo_counter = 0;
    std::unique_ptr<NestopiaVitaCoreBridge> m_core;
};

int nestopia_state_load(const char *path);
int nestopia_state_save(const char *path);
void nestopia_apply_cheats();

#endif // NESTOPIA_VITA_UI_EMU_H
