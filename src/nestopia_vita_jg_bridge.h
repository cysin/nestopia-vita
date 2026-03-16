#ifndef NESTOPIA_VITA_JG_BRIDGE_H
#define NESTOPIA_VITA_JG_BRIDGE_H

#include <memory>
#include <string>
#include <vector>

#include "cross2d/c2d.h"

struct NestopiaCheat;
class NestopiaVitaUiEmu;

class NestopiaVitaCoreBridge {
public:
    explicit NestopiaVitaCoreBridge(NestopiaVitaUiEmu *emu);
    ~NestopiaVitaCoreBridge();

    int load(const std::string &full_path);
    void unload();

    void execFrame(c2d::Input::Player *players, unsigned int turbo_counter);

    int saveState(const char *path);
    int loadState(const char *path);
    void applyCheats(const std::vector<NestopiaCheat> &cheats);
    void suspendHotkeysUntilRelease();
    void stopRewind();

    float getTargetFps() const;
    bool isPal() const;
    const std::string &getLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
