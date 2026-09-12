#pragma once

#include "ScriptObjects.h"

namespace TortoiseBots {

// Player lifecycle adapter. It observes generic player events and asks the
// module's BotManager to attach/detach AI only for its own Headless records.
class BotPlayerAdapter final : public PlayerScript
{
public:
    BotPlayerAdapter();

    void OnLogin(Player* player) override;
    void OnMapChanged(Player* player) override;
    void OnBeforeLogout(Player* player) override;
    void OnLogout(Player* player) override;
};

// Unit lifecycle adapter to capture lethal damage events (accurate killer attribution).
class BotUnitAdapter final : public UnitScript
{
public:
    BotUnitAdapter();

    void OnUnitDeath(Unit* unit, Unit* killer) override;
};

} // namespace TortoiseBots

