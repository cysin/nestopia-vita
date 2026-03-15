#ifndef NESTOPIA_VITA_MAIN_H
#define NESTOPIA_VITA_MAIN_H

#include "runtime/runtime.h"
#include "nestopia_vita_ui_emu.h"
#include "nestopia_vita_config.h"
#include "nestopia_vita_io.h"

#define PEMUIo NestopiaVitaIo
#define PEMUConfig NestopiaVitaConfig
#define PEMUSkin pemu::Skin
#define PEMUUiMain pemu::UiMain
#define PEMUUiEmu NestopiaVitaUiEmu

#endif
