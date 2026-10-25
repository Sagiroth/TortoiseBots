#include "playerbot/strategy/actions/FankrissRaidActions.h"
#include "playerbot/policy/raid/FankrissRaidPolicy.h"

#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/TwinsOpeningActions.h"
#include "PartyMemberToHeal.h"
#include "HealerCoverage.h"
#include "playerbot/HealTargetPolicy.h"
#include "playerbot/HealReachPolicy.h"
#include "playerbot/HealerManaPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/RaidTankRolePolicy.h"
#include "ObjectAccessor.h"
#include <algorithm>
#include "playerbot/HealingTriagePolicy.h"
#include <map>
#include "playerbot/RaidTankHealing.h"

using namespace ai;

class IsTargetOfHealingSpell : public SpellEntryPredicate
{
public:
    virtual bool Check(SpellEntry const* spell)
    {
        return PlayerbotAI::IsHealSpell(spell);
    }
};

uint32 getIncomingdamage(Unit const* pTarget)
{
    uint32 damage = 0;
    for (auto const& pAttacker : pTarget->GetAttackers())
        if (pAttacker->CanReachWithMeleeAutoAttack(pTarget))
            damage += uint32((pAttacker->GetFloatValue(UNIT_FIELD_MINDAMAGE) + pAttacker->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2);

    return damage;
}

bool compareByHealth(const Unit *u1, const Unit *u2)
{
    return u1->GetHealthPercent() < u2->GetHealthPercent();
}

// Direct heals other group members are casting at each target right now
// (HealComm-style): base spell amounts of cast-time heals still in their cast
// bar, keyed by target. Healers that ignored these all started big heals on
// the same tank; Patchwerk measured ~63% overheal on the tanks.
// Runs ONCE per value refresh (1 s TTL) over the bot's own group slots —
// at most 40 members, never the spatial grid — so the dozen heal-action
// checks per tick share one bounded scan. Ungrouped bots return an empty
// map without touching the grid. Only live group casters count; dead
// targets collect nothing (a corpse needs no heal credit).
std::map<ObjectGuid, uint32> IncomingHealsValue::Calculate()
{
    std::map<ObjectGuid, uint32> incoming;
    Group* group = bot->GetGroup();
    if (!group)
        return incoming;
    for (Player* caster : LiveGroupMembers(group))
    {
        if (!caster || caster == bot || !caster->IsAlive())
            continue;
        Spell* spell = caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || spell->getState() != SPELL_STATE_PREPARING || !PlayerbotAI::IsHealSpell(spell->m_spellInfo))
            continue;
        Unit* target = ai->GetUnit(spell->m_targets.getUnitTargetGuid());
        if (!target || !target->IsAlive())
            continue;
        for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
        {
            if (spell->m_spellInfo->Effect[effect] != SPELL_EFFECT_HEAL)
                continue;
            int32 const amount = spell->CalculateDamage(SpellEffectIndex(effect), target);
            if (amount > 0)
                incoming[target->GetObjectGuid()] += uint32(amount);
        }
    }
    return incoming;
}

uint32 ai::GetIncomingHeal(PlayerbotAI* ai, Unit* target)
{
    if (!target)
        return 0;
    std::map<ObjectGuid, uint32> const incoming =
        ai->GetAiObjectContext()->GetValue<std::map<ObjectGuid, uint32>>("incoming heals")->Get();
    auto const it = incoming.find(target->GetObjectGuid());
    return it == incoming.end() ? 0 : it->second;
}

// Effective health percent (incoming melee damage taken off first when
// pre-healing). Ranking by percent, as the donor does, keeps a cloth member at
// 60 % ahead of a tank at 76 %: by missing hit points the tank's larger pool
// always won and the member who was actually dying got nothing.
// Incoming heals inflate only above lowHealth (HealthWithIncoming): a tank
// in danger keeps its real urgency so the sort never parks it below
// scratch DPS while a slow cast is still in flight.
static float effectiveHealthPercent(const Unit* u, bool incomingDamage, std::map<ObjectGuid, uint32> const& incomingHeals)
{
    uint32 hp = u->GetHealth();
    if (incomingDamage)
    {
        uint32 damage = getIncomingdamage(u);
        hp = damage >= hp ? 0 : hp - damage;
    }
    uint32 incoming = 0;
    auto const heal = incomingHeals.find(u->GetObjectGuid());
    if (heal != incomingHeals.end())
        incoming = heal->second;
    uint32 hpmax = u->GetMaxHealth();
    hp = HealthWithIncoming(hp, hpmax, incoming, (std::uint8_t)sPlayerbotAIConfig.lowHealth);
    return hpmax ? hp * 100.0f / hpmax : 100.0f;
}

float ai::HealReachLimit(PlayerbotAI* ai)
{
    return HealReachLimitFor(ai->GetBot()->InBattleGround(), ai->GetRange("heal"), sPlayerbotAIConfig.healDistanceBg,
        sPlayerbotAIConfig.contactDistance);
}

bool ai::AnotherHealerCoversAlly(PlayerbotAI* ai, Unit* ally, float maxDistance)
{
    Player* bot = ai->GetBot();
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group || !ally)
        return false;

    // A critical ally is never "covered": every healer may walk to it.
    if (!CoverApplies(ally->GetHealthPercent(), float(sPlayerbotAIConfig.criticalHealth)))
        return false;

    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || member == ally)
            continue;
        uint8 const cls = member->GetClass();
        bool const classCanHeal = cls == CLASS_PRIEST || cls == CLASS_PALADIN || cls == CLASS_DRUID || cls == CLASS_SHAMAN;
        bool const sameMap = member->GetMap() == bot->GetMap();
        if (HealerCoversAlly(member->IsAlive(), sameMap, sameMap ? member->GetDistance(ally, SizeFactor::CombatReach) : 0.0f,
                maxDistance, classCanHeal,
                [member]()
                {
                    uint32 const maxMana = member->GetMaxPower(POWER_MANA);
                    float const manaPct = maxMana ? member->GetPower(POWER_MANA) * 100.0f / maxMana : 100.0f;
                    return HealerFitToCover(member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL),
                        (member->GetUInt32Value(UNIT_FIELD_FLAGS) & (UNIT_FLAG_SILENCED | UNIT_FLAG_PACIFIED)) != 0, manaPct);
                },
                [member]() { return PlayerbotAI::IsHeal(member); },
                [member, ally]() { return member->IsWithinLOSInMap(ally, true); }))
            return true;
    }
    return false;
}

Unit* PartyMemberToHeal::Calculate()
{
    HealingBands const bands{float(sPlayerbotAIConfig.criticalHealth), float(sPlayerbotAIConfig.lowHealth),
        float(sPlayerbotAIConfig.mediumHealth), float(sPlayerbotAIConfig.almostFullHealth)};
    bool const preHealing = ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT);
    bool const focused = ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT);
    std::map<ObjectGuid, uint32> const incomingHeals =
        ai->GetAiObjectContext()->GetValue<std::map<ObjectGuid, uint32>>("incoming heals")->Get();
    std::size_t healerOrdinal = 0;
    std::size_t assignedHealerOrdinal = 0;

    // One bounded raid-roster pass, including human healers. No nearby-world scan
    // per candidate. Focused healers own their assignment rather than a raid slot.
    for (Player* player : LiveGroupMembers(bot->GetGroup()))
    {
        if (!Check(player))
            continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(player);
        // A cast starting must not renumber all remaining Patchwerk healers.
        if (!focused && peer && ai->IsHeal(player) &&
            !peer->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT) &&
            player->GetObjectGuid() < bot->GetObjectGuid())
            ++assignedHealerOrdinal;
        if (!focused && peer && ai->IsHeal(player) &&
            !peer->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT) &&
            !player->HasUnitState(UNIT_STAT_CAN_NOT_REACT) && !player->IsNonMeleeSpellCasted(true) &&
            player->GetMaxPower(POWER_MANA) &&
            100.0f * player->GetPower(POWER_MANA) / player->GetMaxPower(POWER_MANA) > sPlayerbotAIConfig.lowMana &&
            player->GetObjectGuid() < bot->GetObjectGuid())
            ++healerOrdinal;
    }

    std::vector<HealingCandidate> candidates;
    std::map<uint64, Unit*> targets;
    auto addTarget = [&](Unit* target, bool tank, bool pet)
    {
        if (!Check(target))
            return;
        // Issue #904: in combat an ally this healer cannot reach from where it
        // stands and another healer covers is that healer's to heal. Dropping it
        // here (not just refusing the walk) lets this healer fall back to somebody
        // it can reach, instead of picking the far ally and then standing idle.
        // The covering healer is in reach of the ally, so it keeps the ally in its
        // own list: nobody drops an ally nobody else can heal. Out-of-reach is
        // tested distance first, line of sight only inside the range.
        if (ai->IsStateActive(BotState::BOT_STATE_COMBAT))
        {
            float const reachLimit = HealReachLimit(ai);
            if (DropCoveredCandidate(target->GetHealthPercent(), float(sPlayerbotAIConfig.criticalHealth),
                [&]() { return bot->GetDistance(target, SizeFactor::CombatReach) > reachLimit ||
                    !bot->IsWithinLOSInMap(target, true); },
                [&]() { return AnotherHealerCoversAlly(ai, target, reachLimit); }))
                return;
        }
        bool const assigned = target->IsPlayer() && IsPatchwerkHealingAssignment(static_cast<Player*>(target));
        auto const incoming = incomingHeals.find(target->GetObjectGuid());
        HealingCandidate candidate{target->GetObjectGuid().GetRawValue(), target->GetHealth(),
            target->GetMaxHealth(), preHealing ? getIncomingdamage(target) : 0,
            tank || assigned, pet, false,
            sServerFacade.IsWithinLOSInMap(bot, target), target->IsInCombat(), assigned};
        candidate.incomingHealing = incoming == incomingHeals.end() ? 0 : incoming->second;
        candidate.incomingLowHealth = sPlayerbotAIConfig.lowHealth;
        if (NeedsPartyHealing(candidate, bands) && targets.emplace(candidate.guid, target).second)
            candidates.push_back(candidate);
    };

    // A focus-heal list must not leak into opportunistic NPC, pet or selection healing.
    if (!focused)
    {
        Unit* selected = ai->GetUnit(bot->GetSelectionGuid());
        addTarget(selected, selected && selected->IsPlayer() && ai->IsTank(static_cast<Player*>(selected)),
            selected && selected->IsPet());
        if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target"))
            addTarget(rpgTarget.GetCreature(bot->GetInstanceId()), false, false);
    }

    for (Player* player : GetPartyMembers())
    {
        addTarget(player, ai->IsTank(player), false);
        Pet* pet = player->GetPet();
        if (!focused && sServerFacade.IsAlive(player) && pet && CanHealPet(pet))
            addTarget(pet, false, true);
    }

    // Upstream rationale retained; local assignment and LOS eligibility apply below.
            // do not heal dueling members
    // Prefer an in-LOS member over an out-of-LOS one at similar urgency: an
    // out-of-LOS pick still casts (its reach action walks the healer into
    // LOS), so without this the healer chases a pillar-blocked member while
    // a reachable one waits. LOS is only a tie-break inside a small missing-
    // health window, never a filter: a dying member behind a pillar still
    // outranks a scratched one in the open. Pets share the candidate set and
    // take the same tie-break (no separate path).
    // Never while the top pick is in real danger: below lowHealth the
    // healer goes to it, pillar or not.
        // Window: a candidate less urgent by more than this still loses.
        // mediumHealth 70 -> 30 health-percent points above the top target.
    // Spreading the targets over several healers is only worth doing while there
    // is more than one worth spreading. needHeals holds every tank regardless of
    // health, so with a single injured member the second healer's index lands on
    // somebody at full health - the health triggers then find nothing to do, never
    // fire, and nobody gets healed at all while the healer stands there doing
    // damage. Fall back to whoever is worst off.

    // Entangle can relocate the healer. Keep the existing focus/incoming-heal
    // triage, but use a reachable injured ally before walking back across AQ.
    if (FankrissHealingActive(ai))
    {
        auto reachable = [&](HealingCandidate const& candidate)
            { return FankrissHealReachable(ai, targets.at(candidate.guid)); };
        if (std::any_of(candidates.begin(), candidates.end(), reachable))
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [&](HealingCandidate const& candidate) { return !reachable(candidate); }), candidates.end());
    }

    bool const canReach = ai->CanMove() && !ai->HasStrategy("stay", ai->GetState()) &&
        !bot->IsNonMeleeSpellCasted(true, false, true);
    FilterHealingLineOfSight(candidates, focused, canReach);

    if (candidates.empty())
        return nullptr;
    std::sort(candidates.begin(), candidates.end(), [&](HealingCandidate const& left, HealingCandidate const& right)
        { return HealingPriority(left, right, bands); });
    return targets.at(candidates[HealingAssignment(candidates, bands,
        candidates.front().patchwerkAssigned ? assignedHealerOrdinal : healerOrdinal)].guid);
}

bool PartyMemberToHeal::CanHealPet(Pet* pet)
{
    return HealablePet(MINI_PET == pet->getPetType(), sServerFacade.IsAlive(pet));
}

bool PartyMemberToHeal::Check(Unit* player)
{
    if (!TwinsHealTargetAllowed(ai, player)) return false;
    bool isBg = bot->InBattleGround();

    // The cast action owns the exact range check and its reach prerequisite owns
    // movement. Keep an injured member in the candidate set until that
    // prerequisite has had a chance to close the gap; filtering at cast range
    // here makes the reach action unreachable. The donor uses the same two-times
    // heal envelope for this value.
    float maxDist = isBg ? sPlayerbotAIConfig.healDistanceBg : ai->GetRange("heal");
    maxDist *= 2.0f;
    maxDist = FankrissHealingSearchRange(maxDist, FankrissHealingActive(ai));

    if (!player || player == bot || !player->IsInWorld() || !sServerFacade.IsAlive(player) ||
        player->GetMap() != bot->GetMap() || !sServerFacade.IsFriendlyTo(bot, player))
        return false;

    if (player->IsPlayer())
    {
        Player* member = static_cast<Player*>(player);
        if (member->IsBeingTeleported() || (member->m_duel && member->m_duel->opponent))
            return false;
    }

    // A hidden GM is never a spell target (see PartyMemberValue::Check).
    if (player->IsPlayer() && player->GetVisibility() == VISIBILITY_OFF)
        return false;

    if (sServerFacade.getDistance2d(bot, player) > maxDist)
        return false;

    return true;
}

std::vector<Player*> PartyMemberToHeal::GetPartyMembers()
{
    std::vector<Player*> partyMembers;
    if (ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT))
    {
        Unit* player = nullptr;
        const std::list<ObjectGuid> focusHealTargets = AI_VALUE(std::list<ObjectGuid>, "focus heal targets");
        for(const ObjectGuid& focusHealTarget : focusHealTargets)
        {
            Player* player = (Player*)ai->GetUnit(focusHealTarget);
            if (player && IsInGroup_Helper(player, bot) && ai->IsSafe(player))
            {
                partyMembers.push_back(player);
            }
        }
    }
    else
    {
        Group* group = bot->GetGroup();
        if (group)
        {
            for (Player* player : LiveGroupMembers(group))
            {
                if (player && ai->IsSafe(player))
                {
                    partyMembers.push_back(player);
                }
            }
        }
    }

    return partyMembers;
}

Unit* PartyMemberToProtect::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group)
        return NULL;

    std::vector<Unit*> needProtect;

    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit)
            continue;

        // Penqle's CreatureAI has no ranged-unit marker; use melee spacing.
        bool isRanged = false;

        Unit* pVictim = unit->GetVictim();
        if (!pVictim || !pVictim->IsPlayer())
            continue;

        if (pVictim == bot)
            continue;

        if (sServerFacade.getDistance2d(pVictim, bot) > 30.0f)
            continue;

        float attackDistance = isRanged ? 30.0f : 10.0f;
        if (sServerFacade.getDistance2d(pVictim, unit) > attackDistance)
            continue;

        if (ai->IsTank((Player*)pVictim) && pVictim->GetHealthPercent() > 10)
            continue;
        else if (pVictim->GetHealthPercent() > 30)
            continue;

        if (find(needProtect.begin(), needProtect.end(), pVictim) == needProtect.end())
        needProtect.push_back(pVictim);
    }

    if (needProtect.empty())
        return NULL;

    sort(needProtect.begin(), needProtect.end(), compareByHealth);

    return needProtect[0];
}

Unit* HealerLowMana::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Unit* lowestHealer = nullptr;
    float lowestPct = 100.0f;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !ai->IsSafe(member) || !ai->IsHeal(member))
            continue;
        if (member->GetMapId() != bot->GetMapId() || !sServerFacade.IsAlive(member))
            continue;
        uint32 maxMana = member->GetMaxPower(POWER_MANA);
        if (!maxMana)
            continue;
        float pct = (static_cast<float>(member->GetPower(POWER_MANA)) / maxMana) * 100.0f;
        if (pct < lowestPct)
        {
            lowestPct = pct;
            lowestHealer = member;
        }
    }

    return lowestHealer;
}

Unit* PartyMemberToRemoveRoots::Calculate()
{
    Unit* target = nullptr;
    Group* group = bot->GetGroup();
    if(group)
    {
        for (Player* player : LiveGroupMembers(group))
        {
            if (sServerFacade.IsAlive(player))
            {
                if (player->m_duel && player->m_duel->opponent)
                    continue;

                if (player->HasAuraType(SPELL_AURA_MOD_ROOT) || player->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
                {
                    if (!ai->HasAura("stealth", player) && !ai->HasAura("prowl", player))
                    {
                        target = player;
                        break;
                    }
                }
            }
        }
    }

    return target;
}

RaidTankRoles ai::GetRaidTankRoles(PlayerbotAI* ai)
{
    RaidTankRoles roles;
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
    {
        if (PlayerbotAI::IsTank(bot))
        {
            roles.tanks.push_back(bot);
            roles.botIndex = 0;
        }
        return roles;
    }

    // Explicit raid flags first (donor GetMainTankGuid; the core owns the
    // Main Tank / Main Assist flags on Group, raid-only by design), then
    // promoted assistants among the assist tanks, then live tanks by slot.
    ObjectGuid const mainTankGuid = group->GetMainTankGuid();
    ObjectGuid const mainAssistGuid = group->GetMainAssistantGuid();

    std::vector<Player*> members;
    std::vector<RaidTankCandidate> slots;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member)
            continue;
        bool const usable = member->IsAlive() && ai->IsSafe(member);
        bool const isTank = usable && PlayerbotAI::IsTank(member);
        members.push_back(member);
        slots.push_back({usable,
                         isTank,
                         member->GetObjectGuid() == mainTankGuid,
                         member->GetObjectGuid() == mainAssistGuid,
                         isTank && group->IsAssistant(member->GetObjectGuid())});
    }

    for (std::size_t index : OrderRaidTankRoles(slots))
    {
        if (members[index] == bot)
            roles.botIndex = static_cast<int>(roles.tanks.size());
        roles.tanks.push_back(members[index]);
    }
    return roles;
}

Unit* PartyMemberMainTankValue::Calculate()
{
    return GetRaidTankRoles(ai).MainTank();
}
