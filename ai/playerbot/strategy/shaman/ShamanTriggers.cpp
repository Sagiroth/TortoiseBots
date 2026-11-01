
#include "playerbot/playerbot.h"
#include "ShamanTriggers.h"
#include "ShamanActions.h"
#include "playerbot/policy/class/ShamanFireGatePolicy.h"
#include "playerbot/strategy/values/ThreatValues.h"

using namespace ai;

std::list<std::string> ShamanWeaponTrigger::spells;

bool ShamanWeaponTrigger::IsActive()
{
    if (ai::BuffTriggerMountGated(bot, ai))
        return false;
    // Upkeep must survive every level: before this the strategies queued one
    // fixed imbue (windfury for enhancement, flametongue elsewhere) while this
    // trigger stayed true on any known imbue, so the cast failed every tick
    // below its gate (issue #406: windfury is gated at 30). A queued imbue the
    // bot never trained also has no spell id, so the trigger now requires a
    // trained spell it can actually cast - the newest known rank, since
    // HasSpell resolves the highest rank - and skips unknown imbues.
    if (spells.empty())
    {
        spells.push_back("windfury weapon");
        spells.push_back("flametongue weapon");
        spells.push_back("frostbrand weapon");
        spells.push_back("rockbiter weapon");
    }

    for (std::list<std::string>::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        std::string const& spell = *i;

        if (!ai->HasSpell(spell))
            continue;

        uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
        if (!spellId)
            continue;

        if (AI_VALUE2(Item*, "item for spell", spellId))
            return true;
    }

    return false;
}

bool ShockTrigger::IsActive()
{
    if (!ai->HasSpell("earth shock") && !ai->HasSpell("flame shock") && !ai->HasSpell("frost shock"))
        return false;

    // Flame shock is exempt here: it owns the shared shock cooldown via the
    // higher-priority flame shock upkeep line, so earth shock still spends
    // the cooldown while the flame DoT ticks instead of being locked out.
    return SpellTrigger::IsActive() && !ai->HasAnyAuraOf(GetTarget(), "frost shock", "earth shock", NULL) && !HasMaxDebuffs();
}

namespace
{
    // Creature-rank read shared by the fight decision: anything above normal
    // (elite, rare, rare-elite, world boss) counts wherever the fight happens.
    bool TankFightRankIsElite(Unit* unit)
    {
        if (Creature* creature = dynamic_cast<Creature*>(unit))
        {
            if (CreatureInfo const* info = creature->GetCreatureInfo())
                return TankShamanTargetIsElite(info->rank, CREATURE_ELITE_NORMAL);
        }
        return false;
    }

    // The tank's pack fire rows (Fire Nova, Magma) are automatic rows: they
    // follow the drop mode like FireTotemTrigger, and a manual fire strategy
    // or a `.bot totem fire` order owns the slot, so the pack pick never
    // drops over the player's totem (nor gets replaced by it next tick).
    bool TankFirePackRowAllowed(PlayerbotAI* ai, Player* bot)
    {
        if (!TotemAutoRowActive(ai, bot, bot->IsInCombat()))
            return false;
        for (char const* manual : { "totem fire nova", "totem fire flametongue", "totem fire resistance",
            "totem fire magma", "totem fire searing" })
        {
            if (ai->HasStrategy(manual, BotState::BOT_STATE_COMBAT))
                return false;
        }
        std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", TotemOrderKey(1))->Get();
        return stored.empty() || stored == "auto";
    }
}

bool TankFightTargetTrigger::IsActive()
{
    // The fight decision must hold for the whole fight: reading the current
    // target alone flipped it on every target switch between an elite and its
    // normal-rank adds, and the earth pick followed the flip (Stoneclaw took
    // the slot from Strength of Earth, the fight row took it back on the next
    // switch). Any engaged attacker that is elite/boss keeps it a fight.
    Unit* target = AI_VALUE(Unit*, "current target");
    bool targetElite = TankFightRankIsElite(target);
    bool attackerElite = false;
    if (!targetElite)
    {
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
        {
            if (TankFightRankIsElite(ai->GetUnit(guid)))
            {
                attackerElite = true;
                break;
            }
        }
    }

    Group* group = bot->GetGroup();
    bool inRaid = group && group->isRaidGroup();
    Map* map = bot->GetMap();
    bool inInstance = map && map->IsDungeon();
    return TankShamanEarthShockFreeFire(TankFightTargetIsBossOrElite(targetElite, attackerElite), inInstance, inRaid);
}

bool TankEarthShockFightTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !sServerFacade.IsAlive(target))
        return false;

    if (!ai->CanCastSpell("earth shock", target, true, nullptr, true))
        return false;

    // Reuse the shared shock gate (any shock known, no shock debuff on the
    // target, debuff slots free) so the tank row never double-dips the
    // shock cooldown against the interrupt rows in ShamanStrategy.
    ShockTrigger shock(ai);
    if (!shock.IsActive())
        return false;

    TankFightTargetTrigger fight(ai);
    return fight.IsActive();
}

bool TankStrengthOfEarthTrigger::IsActive()
{
    if (!ai->HasSpell("strength of earth totem"))
        return false;

    if (!TotemAutoRowActive(ai, bot, bot->IsInCombat()))
        return false;

    // Manual .bot totem orders win
    if (ai->HasStrategy("totem earth strength", BotState::BOT_STATE_COMBAT))
        return !AI_VALUE2(bool, "has totem", "strength of earth totem");
    if (ai->HasStrategy("totem earth stoneclaw", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth stoneskin", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth earthbind", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth tremor", BotState::BOT_STATE_COMBAT))
        return false;

    std::string stored = AI_VALUE2(std::string, "manual saved string", TotemOrderKey(0));
    std::string ordered = OrderedTotemForSlot(stored);
    if (!ordered.empty())
    {
        if (ordered != "strength of earth totem")
            return false;
    }
    else if (!stored.empty() && stored != "auto")
        return false;

    bool hasEarth = AI_VALUE2(bool, "has totem", "stoneclaw totem") ||
        AI_VALUE2(bool, "has totem", "stoneskin totem") ||
        AI_VALUE2(bool, "has totem", "earthbind totem") ||
        AI_VALUE2(bool, "has totem", "strength of earth totem") ||
        AI_VALUE2(bool, "has totem", "tremor totem");

    // Our own Stoneclaw is the pick of the trash pack just fought: the
    // fight takes the slot over from it (a pack's totem must not lock a
    // boss out of Strength of Earth). Any other standing earth totem —
    // Tremor, Stoneskin, a manual order, another shaman's — keeps the slot.
    bool standingStoneclaw = AI_VALUE2(bool, "has own totem", "stoneclaw totem");

    TankFightTargetTrigger fight(ai);
    if (!TankStrengthOfEarthShouldDrop(fight.IsActive(), hasEarth, standingStoneclaw))
        return false;

    return ai->CanCastSpell("strength of earth totem", bot, true, nullptr, true);
}

bool TankThreatLeadTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !sServerFacade.IsAlive(target))
        return false;

    if (!ai->CanCastSpell("earth shock", target, true, nullptr, true))
        return false;

    float ownThreat = ThreatValue::GetThreat(bot, target);

    // Another member catching up on our target: their threat at 70%+ of
    // ours (the economical-trash threshold in ShamanTankPolicy.h).
    bool catchingUp = false;
    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || !sServerFacade.IsAlive(member) || !ai->IsSafe(member))
                continue;
            // Manager-direct read (ThreatValues.cpp idiom): works for bot and
            // player members alike, no member AI needed.
            float memberThreat = ThreatValue::GetThreat(member, target);
            if (TankShamanThreatNeedsShock(ownThreat, memberThreat, 70, false))
            {
                catchingUp = true;
                break;
            }
        }
    }

    // A loose pack mob: any attacker whose victim is a live non-tank group
    // member (not us — losing our own target is the taunt row's job).
    bool mobLoose = false;
    std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (ObjectGuid guid : attackers)
    {
        Unit* attacker = ai->GetUnit(guid);
        if (!attacker || attacker == target || !sServerFacade.IsAlive(attacker))
            continue;
        Unit* victim = attacker->GetVictim();
        if (!victim || victim == bot || !victim->IsPlayer())
            continue;
        Player* victimPlayer = (Player*)victim;
        if (!sServerFacade.IsAlive(victimPlayer))
            continue;
        if (ai->IsTank(victimPlayer))
            continue;
        if (group && !group->IsMember(victimPlayer->getObjectGuid()))
            continue;
        mobLoose = true;
        break;
    }

    return catchingUp || mobLoose;
}

bool TankStoneclawTrigger::IsActive()
{
    if (!ai->HasSpell("stoneclaw totem"))
        return false;
    Group* group = bot->GetGroup();
    bool inGroup = group != nullptr;
    bool manual = ai->HasStrategy("totem earth stoneclaw", BotState::BOT_STATE_COMBAT);
    bool other = ai->HasStrategy("totem earth stoneskin", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth earthbind", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth strength", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("totem earth tremor", BotState::BOT_STATE_COMBAT);
    bool hasEarth = AI_VALUE2(bool, "has totem", "stoneclaw totem") ||
        AI_VALUE2(bool, "has totem", "stoneskin totem") ||
        AI_VALUE2(bool, "has totem", "earthbind totem") ||
        AI_VALUE2(bool, "has totem", "strength of earth totem") ||
        AI_VALUE2(bool, "has totem", "tremor totem");
    // Our own Strength of Earth (the fight pick) or Earthbind (the runner
    // snare) is a replaceable standing pick: the pack takes the slot from
    // either instead of coming out of a boss fight with the wrong earth
    // totem for it. Own-scoped, so another shaman's totem is never replaced.
    bool replaceablePick = TankEarthPickReplaceable(
        AI_VALUE2(bool, "has own totem", "strength of earth totem"),
        AI_VALUE2(bool, "has own totem", "earthbind totem"));
    TankFightTargetTrigger fight(ai);
    bool isFight = fight.IsActive();
    TankThreatLeadTrigger lead(ai);
    bool threatNeeded = lead.IsActive();
    uint8 attackers = AI_VALUE(uint8, "attackers count");
    if (!TankStoneclawShouldDrop(inGroup, manual, other, hasEarth, threatNeeded, attackers, isFight, replaceablePick))
        return false;
    return ai->CanCastSpell("stoneclaw totem", bot, true, nullptr, true);
}

bool TankLightningShieldTrigger::IsActive()
{
    // Lightning Shield owns the pull and the top of the mana bar; once
    // Water Shield is trained and mana falls to two thirds, the water row
    // takes over and this one stands down.
    if (TankShamanWantsWaterShield(ai->HasSpell("water shield"), AI_VALUE2(uint8, "mana", "self target")))
        return false;
    return BuffTrigger::IsActive();
}

bool TankWaterShieldTrigger::IsActive()
{
    if (!TankShamanWantsWaterShield(true, AI_VALUE2(uint8, "mana", "self target")))
        return false;
    return BuffTrigger::IsActive();
}

bool TotemicSlamTrigger::IsActive()
{
    if (bot->GetRace() != RACE_TAUREN)
        return false;
    uint32 spellId = AI_VALUE2(uint32, "spell id", "totemic slam");
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || AI_VALUE2(float, "distance", "current target") > ATTACK_DISTANCE)
        return false;
    // Damage-and-slow filler: fire only while both strikes cool down, so it
    // never steals the GCD from Stormstrike or Lightning Strike.
    if (!ai->HasSpell("stormstrike") || !ai->HasSpell("lightning strike"))
        return true;
    uint32 stormstrike = AI_VALUE2(uint32, "spell id", "stormstrike");
    uint32 strike = AI_VALUE2(uint32, "spell id", "lightning strike");
    bool strikesReady = (!stormstrike || sServerFacade.IsSpellReady(bot, stormstrike)) ||
        (!strike || sServerFacade.IsSpellReady(bot, strike));
    return !strikesReady;
}

bool TankMagmaTrigger::IsActive()
{
    if (!ai->HasSpell("magma totem"))
        return false;
    if (AI_VALUE2(bool, "has totem", "magma totem"))
        return false;
    if (!TankFirePackRowAllowed(ai, bot))
        return false;

    // Magma is the fire slot's fallback, never a replacement for a Fire Nova
    // that is still available or still standing: Nova owns the slot while its
    // row above this one can fire, and a standing Nova totem must be left
    // alone to detonate (any fire totem drop unsummons the one in the slot).
    uint32 novaSpellId = AI_VALUE2(uint32, "spell id", "fire nova totem");
    bool novaReady = novaSpellId && sServerFacade.IsSpellReady(bot, novaSpellId);
    bool novaTotemStanding = AI_VALUE2(bool, "has totem", "fire nova totem");

    uint8 attackerCount = AI_VALUE(uint8, "attackers count");
    bool manaForMagma = AI_VALUE2(uint8, "mana", "self target") >= 25;

    if (!TankMagmaShouldFollow(novaReady, novaTotemStanding, attackerCount, manaForMagma))
        return false;

    return ai->CanCastSpell("magma totem", bot, true, nullptr, true);
}

TankFireNovaTrigger::TankFireNovaTrigger(PlayerbotAI* ai) : AoeTrigger(ai, 2, FireNovaDropRange())
{
}

bool TankFireNovaTrigger::IsActive()
{
    // The drop's own readiness/ownership gate: the spell must be trained and
    // off its 15 s category cooldown, and a Nova totem already standing owns
    // the slot until it detonates.
    uint32 spellId = AI_VALUE2(uint32, "spell id", "fire nova totem");
    bool known = spellId != 0;
    bool ready = spellId && sServerFacade.IsSpellReady(bot, spellId);
    bool standing = AI_VALUE2(bool, "has totem", "fire nova totem");
    if (!TankFireNovaShouldDrop(known, ready, standing))
        return false;
    if (!TankFirePackRowAllowed(ai, bot))
        return false;

    // The pack gate: 2+ attackers inside the Nova's own detonation window
    // (10 yd, the blast radius of the pulse the totem drops at our feet),
    // inherited from AoeTrigger with its breakable-CC interlock - a sheep or
    // sap beside the pack still blocks the blast. Wider than the 5 yd melee
    // window, so a pack that stopped just outside melee still counts.
    return AoeTrigger::IsActive();
}
