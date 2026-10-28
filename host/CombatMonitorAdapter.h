#pragma once
// The combat monitor's core seams: one UnitScript (damage, heal, enter
// combat, death) and one PlayerScript (spell cast), both on the map threads.
#include "ScriptObjects.h"

namespace TortoiseBots {

class CombatMonitorUnitAdapter final : public UnitScript
{
public:
    CombatMonitorUnitAdapter();

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override;
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override;
    void OnUnitEnterCombat(Unit* unit, Unit* enemy) override;
    void OnUnitDeath(Unit* unit, Unit* killer) override;
};

class CombatMonitorCastAdapter final : public PlayerScript
{
public:
    CombatMonitorCastAdapter();

    void OnSpellCast(Player* player, Spell* spell, bool skipCheck) override;
};

} // namespace TortoiseBots
