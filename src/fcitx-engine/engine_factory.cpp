#include "fcitx-engine/engine.h"
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
fcitx::AddonInstance *modernime::ModernEngineFactory::create(fcitx::AddonManager *manager) { return new ModernEngine(manager->instance()); }
FCITX_ADDON_FACTORY(modernime::ModernEngineFactory)
