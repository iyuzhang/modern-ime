#include <fcitx/addoninstance.h>
#include <fcitx/addonfactory.h>

namespace modernime {
// This deliberately contains no renderer: the engine sends immutable snapshots to
// the independent Qt Quick process. Loading or losing it never changes key input.
class UiBridge final : public fcitx::AddonInstance {};
class UiBridgeFactory final : public fcitx::AddonFactory { public: fcitx::AddonInstance *create(fcitx::AddonManager *) override { return new UiBridge; } };
}
FCITX_ADDON_FACTORY(modernime::UiBridgeFactory)
