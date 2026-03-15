#ifndef NESTOPIA_VITA_IO_H
#define NESTOPIA_VITA_IO_H

#include "cross2d/c2d.h"

extern void NestopiaPathsInit(c2d::C2DIo *io);

namespace c2d {
    class NestopiaVitaIo : public c2d::C2DIo {
    public:
        NestopiaVitaIo() : C2DIo() {
            C2DIo::create(NestopiaVitaIo::getDataPath());
            C2DIo::create(NestopiaVitaIo::getDataPath() + "configs");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "saves");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "roms");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "states");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "cheats");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "screenshots");
            C2DIo::create(NestopiaVitaIo::getDataPath() + "samples");
            NestopiaPathsInit(this);
        }

        ~NestopiaVitaIo() override {
            printf("~NestopiaVitaIo()\n");
        }

        std::string getDataPath() override {
            return "ux0:/data/nestopia-vita/";
        }
    };
}

#endif
