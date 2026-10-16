#ifndef _WMANAGER_H
#define _WMANAGER_H

#include <mutex>
#include "drivers/storage/storage.h"

// Held while Settings or pending settings are changed across tasks/cores.
extern std::mutex g_settingsMutex;

void init_WifiManager();
void wifiManagerProcess();
void reset_configuration();
void saveSettingsToFlash(TSettings *settings = nullptr);

#endif // _WMANAGER_H

