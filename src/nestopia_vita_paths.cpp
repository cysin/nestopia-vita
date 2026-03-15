#include "cross2d/c2d.h"

#define MAX_PATH 512

char szNestopiaHomePath[MAX_PATH];
char szNestopiaRomPath[MAX_PATH];
char szNestopiaSavePath[MAX_PATH];
char szNestopiaConfigPath[MAX_PATH];
char szNestopiaStatePath[MAX_PATH];
char szNestopiaCheatPath[MAX_PATH];

// Alias for RGUI files that reference szAppRomPath / szAppConfigPath
char szAppRomPath[MAX_PATH];
char szAppConfigPath[MAX_PATH];

void NestopiaPathsInit(c2d::C2DIo *io) {
    printf("NestopiaPathsInit: dataPath = %s\n", io->getDataPath().c_str());

    snprintf(szNestopiaHomePath, MAX_PATH - 1, "%s", io->getDataPath().c_str());
    io->create(szNestopiaHomePath);

    snprintf(szNestopiaRomPath, MAX_PATH - 1, "%sroms/", szNestopiaHomePath);
    io->create(szNestopiaRomPath);
    snprintf(szAppRomPath, MAX_PATH - 1, "%s", szNestopiaRomPath);

    snprintf(szNestopiaSavePath, MAX_PATH - 1, "%ssaves/", szNestopiaHomePath);
    io->create(szNestopiaSavePath);

    snprintf(szNestopiaConfigPath, MAX_PATH - 1, "%sconfigs/", szNestopiaHomePath);
    io->create(szNestopiaConfigPath);
    snprintf(szAppConfigPath, MAX_PATH - 1, "%s", szNestopiaConfigPath);

    snprintf(szNestopiaStatePath, MAX_PATH - 1, "%sstates/", szNestopiaHomePath);
    io->create(szNestopiaStatePath);

    snprintf(szNestopiaCheatPath, MAX_PATH - 1, "%scheats/", szNestopiaHomePath);
    io->create(szNestopiaCheatPath);
}
