// The combat monitor's core seams: no pointer is kept past the call, every
// hook returns at once while the monitor is off.
#include "CombatMonitorAdapter.h"
#include "../runtime/CombatMonitorService.h"
#include "Player.h"
#include "Creature.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellAuraDefines.h"
#include "Maps/Map.h"
#include "Unit.h"

namespace TortoiseBots {

CombatMonitorUnitAdapter::CombatMonitorUnitAdapter()
    : UnitScript("tortoisebots_combat_monitor", { UNITHOOK_ON_DAMAGE, UNITHOOK_ON_HEAL,
        UNITHOOK_ON_UNIT_ENTER_COMBAT, UNITHOOK_ON_UNIT_DEATH })
{
}

CombatMonitorCastAdapter::CombatMonitorCastAdapter()
    : PlayerScript("tortoisebots_combat_monitor_casts", { PLAYERHOOK_ON_SPELL_CAST })
{
}

static uint32_t OwnerLow(Unit* unit)
{
    Player* owner = unit ? unit->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    return owner ? owner->GetGUIDLow() : 0;
}

void CombatMonitorUnitAdapter::OnDamage(Unit* attacker, Unit* victim, uint32& damage)
{
    if (!CombatMonitorService::Instance().Enabled())
        return;
    if (!attacker || !victim || attacker == victim)
        return;
    uint32_t const attackerOwner = OwnerLow(attacker);
    uint32_t const victimOwner = OwnerLow(victim);
    if (!attackerOwner && !victimOwner)
        return;
    uint32_t enemyEntry = 0;
    uint64_t enemyGuid = 0;
    char const* enemyName = "";
    if (attackerOwner && !victimOwner)
    {
        Creature* creature = victim->ToCreature();
        enemyEntry = victim->GetEntry();
        enemyGuid = victim->GetObjectGuid().GetRawValue();
        enemyName = creature ? creature->GetName() : victim->GetName();
    }
    else if (!attackerOwner && victimOwner)
    {
        Creature* creature = attacker->ToCreature();
        enemyEntry = attacker->GetEntry();
        enemyGuid = attacker->GetObjectGuid().GetRawValue();
        enemyName = creature ? creature->GetName() : attacker->GetName();
    }
    else
        return;   // member-on-member damage (charm, Mind Control) is not counted
    CombatMonitorService::Instance().OnDamage(attackerOwner, victimOwner, damage,
        attacker->GetName(), enemyEntry, enemyGuid, enemyName);
}

void CombatMonitorUnitAdapter::OnHeal(Unit* healer, Unit* receiver, uint32& gain)
{
    if (!CombatMonitorService::Instance().Enabled())
        return;
    if (!healer || !receiver || !gain)
        return;
    uint32_t const missing = receiver->GetMaxHealth() > receiver->GetHealth()
        ? receiver->GetMaxHealth() - receiver->GetHealth() : 0;
    uint32_t const overheal = gain > missing ? gain - missing : 0;
    uint32_t const healerOwner = OwnerLow(healer);
    if (!healerOwner)
        return;
    CombatMonitorService::Instance().OnHeal(healerOwner, gain, overheal);
}

void CombatMonitorUnitAdapter::OnUnitEnterCombat(Unit* unit, Unit* /*enemy*/)
{
    if (!CombatMonitorService::Instance().Enabled())
        return;
    if (!unit || !unit->IsPlayer())
        return;
    CombatMonitorService::Instance().OnEnterCombat(unit->GetGUIDLow());
}

void CombatMonitorUnitAdapter::OnUnitDeath(Unit* unit, Unit* killer)
{
    if (!CombatMonitorService::Instance().Enabled())
        return;
    if (!unit)
        return;
    if (unit->IsPlayer())
    {
        char const* killerName = killer ? killer->GetName() : "";
        CombatMonitorService::Instance().OnDeath(unit->GetGUIDLow(), OwnerLow(killer), killerName,
            unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), 0);
        return;
    }
    Creature* creature = unit->ToCreature();
    if (creature)
        CombatMonitorService::Instance().OnDeath(0, OwnerLow(killer), "", 0.0f, 0.0f, 0.0f, unit->GetEntry());
}

void CombatMonitorCastAdapter::OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/)
{
    if (!CombatMonitorService::Instance().Enabled())
        return;
    if (!player || !spell || spell->IsTriggered())
        return;
    SpellEntry const* info = spell->m_spellInfo;
    if (!info)
        return;
    bool heal = false;
    bool taunt = false;
    bool interrupt = false;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (info->Effect[i] == SPELL_EFFECT_HEAL)
            heal = true;
        if (info->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
            heal = true;
        if (info->Effect[i] == SPELL_EFFECT_ATTACK_ME || info->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT)
            taunt = true;
        else if (info->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST)
            interrupt = true;
    }
    char const kind = taunt ? 't' : (interrupt ? 'i' : 0);
    uint64_t target = 0;
    std::string targetName;
    if (kind)
    {
        target = spell->m_targets.getUnitTargetGuid().GetRawValue();
        if (Unit* unit = target ? player->GetMap()->GetUnit(::ObjectGuid(target)) : nullptr)
            targetName = unit->GetName();
    }
    CombatMonitorService::Instance().OnCast(player->GetGUIDLow(), info->SpellName[0].c_str(), heal, kind, target,
                                            targetName.c_str());
}

} // namespace TortoiseBots
