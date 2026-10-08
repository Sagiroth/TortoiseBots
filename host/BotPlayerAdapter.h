#pragma once

#include "ScriptObjects.h"

namespace TortoiseBots {

// Player lifecycle adapter. It observes generic player events and asks the
// module's BotManager to attach/detach AI only for its own Headless records.
class BotPlayerAdapter final : public PlayerScript
{
public:
    BotPlayerAdapter();
    bool IsManagedBot(Player* who) override;
    uint8 GetBotRoles(Player* who) override;
    void OnLogin(Player* player) override;
    void OnMapChanged(Player* player) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
    void OnBeforeLogout(Player* player) override;
    void OnLogout(Player* player) override;
};

// Unit lifecycle adapter to capture lethal damage events (accurate killer attribution).
// The damage hook remembers the last non-self damager so a Spirit of
// Redemption expiry (core self-kill, spell 27965) still logs the mob that
// dealt the fatal blow instead of "Environment".
class BotUnitAdapter final : public UnitScript
{
public:
    BotUnitAdapter();

    void OnUnitDeath(Unit* unit, Unit* killer) override;
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override;
};

} // namespace TortoiseBots

