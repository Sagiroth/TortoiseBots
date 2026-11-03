
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
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
#include <map>

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
    std::vector<Unit*> needHeals;
    std::vector<Unit*> tankTargets;
    if (bot->GetSelectionGuid())
    {
        Unit* target = ai->GetUnit(bot->GetSelectionGuid());
        if (target &&
            target->getObjectGuid() != bot->getObjectGuid() &&
            sServerFacade.IsFriendlyTo(bot, target) &&
            HealableTarget(sServerFacade.IsAlive(target)) &&
            target->GetHealthPercent() < 100 &&
            Check(target))
        {
            needHeals.push_back(target);
        }
    }

    if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target"))
    {
        Unit* target = rpgTarget.GetCreature(bot->GetInstanceId());
        if (target && sServerFacade.IsFriendlyTo(bot, target) &&
            HealableTarget(sServerFacade.IsAlive(target)) && target->GetHealthPercent() < 100)
        {
            needHeals.push_back(target);
        }
    }

    const std::vector<Player*> partyMembers = GetPartyMembers();
    if (partyMembers.empty() && needHeals.empty())
    {
        return nullptr;
    }

    if (!partyMembers.empty() || !needHeals.empty())
    {
        IsTargetOfHealingSpell predicate;
        for (Player* player : partyMembers)
        {
            if (!Check(player) || !sServerFacade.IsAlive(player))
            {
                continue;
            }

            bool isTank = ai->IsTank(player);

            // do not heal dueling members
            if (player->m_duel && player->m_duel->opponent)
            {
                continue;
            }

            uint32 incomingDamage = 0;
            if (ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT))
                incomingDamage = getIncomingdamage(player);

            const uint32 currentHealth = player->GetHealth();
            const uint32 effectiveHealth = incomingDamage >= currentHealth
                ? 0
                : currentHealth - incomingDamage;
            uint8 health = ((effectiveHealth * 100.0f) / player->GetMaxHealth());
            // Above the critical line a member another healer is already casting
            // at is left to that healer; at or below it the member stays a
            // candidate for everybody (MemberNeedsHeal).
            if (isTank || MemberNeedsHeal(float(health), float(sPlayerbotAIConfig.almostFullHealth),
                    float(sPlayerbotAIConfig.criticalHealth),
                    [&]() { return IsTargetOfSpellCast(player, predicate); }))
            {
                needHeals.push_back(player);
            }

            Pet* pet = player->GetPet();
            if (pet && CanHealPet(pet))
            {
                health = pet->GetHealthPercent();
                // Hurt AND not already being healed. The heal cast is looked up on the
                // pet itself: the old test asked about the owner, so a healed owner
                // hid a hurt pet.
                if (PetNeedsHeal(float(health), float(sPlayerbotAIConfig.almostFullHealth),
                        [&]() { return IsTargetOfSpellCast(pet, predicate); }))
                {
                    needHeals.push_back(pet);
                }
            }

            if (isTank && IsInGroup_Helper(bot, player))
            {
                tankTargets.push_back(player);
            }
        }
    }

    if (needHeals.empty() && tankTargets.empty())
    {
        return nullptr;
    }

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
        auto const dropCovered = [this, reachLimit](std::vector<Unit*>& list)
        {
            list.erase(std::remove_if(list.begin(), list.end(), [this, reachLimit](Unit* candidate)
            {
                return DropCoveredCandidate(candidate->GetHealthPercent(), float(sPlayerbotAIConfig.criticalHealth),
                    [&]()
                    {
                        return bot->GetDistance(candidate, SizeFactor::CombatReach) > reachLimit ||
                            !bot->IsWithinLOSInMap(candidate, true);
                    },
                    [&]() { return AnotherHealerCoversAlly(ai, candidate, reachLimit); });
            }), list.end());
        };
        dropCovered(needHeals);
        dropCovered(tankTargets);
        if (needHeals.empty() && tankTargets.empty())
            return nullptr;
    }

    if (needHeals.empty() && !tankTargets.empty())
    {
        needHeals = tankTargets;
    }

    bool preHealing = ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT);
    std::map<ObjectGuid, uint32> const incomingHeals =
        ai->GetAiObjectContext()->GetValue<std::map<ObjectGuid, uint32>>("incoming heals")->Get();
    sort(needHeals.begin(), needHeals.end(), [preHealing, &incomingHeals](const Unit* u1, const Unit* u2) { return effectiveHealthPercent(u1, preHealing, incomingHeals) < effectiveHealthPercent(u2, preHealing, incomingHeals); });

    // Prefer an in-LOS member over an out-of-LOS one at similar urgency: an
    // out-of-LOS pick still casts (its reach action walks the healer into
    // LOS), so without this the healer chases a pillar-blocked member while
    // a reachable one waits. LOS is only a tie-break inside a small missing-
    // health window, never a filter: a dying member behind a pillar still
    // outranks a scratched one in the open. Pets share the candidate set and
    // take the same tie-break (no separate path).
    Unit* mostUrgent = needHeals.front();
    // Never while the top pick is in real danger: below lowHealth the
    // healer goes to it, pillar or not.
    if (mostUrgent->GetHealthPercent() >= sPlayerbotAIConfig.lowHealth &&
        !sServerFacade.IsWithinLOSInMap(bot, mostUrgent))
    {
        // Window: a candidate less urgent by more than this still loses.
        // mediumHealth 70 -> 30 health-percent points above the top target.
        float const topPercent = mostUrgent->GetHealthPercent();
        float const smallGap = float(100 - sPlayerbotAIConfig.mediumHealth);
        for (Unit* candidate : needHeals)
        {
            if (candidate == mostUrgent)
                continue;
            if (candidate->GetHealthPercent() > topPercent + smallGap)
                break;
            if (sServerFacade.IsWithinLOSInMap(bot, candidate))
            {
                std::swap(*std::find(needHeals.begin(), needHeals.end(), candidate), needHeals.front());
                break;
            }
        }
    }

    int healerIndex = 0;
    if (!partyMembers.empty())
    {
        for (Player* player : partyMembers)
        {
            if (!ai->IsSafe(player))
            {
                continue;
            }
            else if (player == bot)
            {
                break;
            }
            else if (ai->IsHeal(player) && PlayerbotAIStorage::Instance().GetAI(player))
            {
                float percent = (float)player->GetPower(POWER_MANA) / (float)player->GetMaxPower(POWER_MANA) * 100.0;
                if (percent > sPlayerbotAIConfig.lowMana)
                {
                    healerIndex++;
                }
            }
        }
    }
    else
    {
        healerIndex = 1;
    }

    healerIndex = healerIndex % needHeals.size();

    // Spreading the targets over several healers is only worth doing while there
    // is more than one worth spreading. needHeals holds every tank regardless of
    // health, so with a single injured member the second healer's index lands on
    // somebody at full health - the health triggers then find nothing to do, never
    // fire, and nobody gets healed at all while the healer stands there doing
    // damage. Fall back to whoever is worst off.
    Unit* chosen = needHeals[healerIndex];
    if (chosen && effectiveHealthPercent(chosen, false, incomingHeals) >= sPlayerbotAIConfig.almostFullHealth)
        chosen = needHeals[0];

    return chosen;
}

bool PartyMemberToHeal::CanHealPet(Pet* pet)
{
    return HealablePet(MINI_PET == pet->getPetType(), sServerFacade.IsAlive(pet));
}

bool PartyMemberToHeal::Check(Unit* player)
{
    bool isBg = bot->InBattleGround();

    // The cast action owns the exact range check and its reach prerequisite owns
    // movement. Keep an injured member in the candidate set until that
    // prerequisite has had a chance to close the gap; filtering at cast range
    // here makes the reach action unreachable. The donor uses the same two-times
    // heal envelope for this value.
    float maxDist = isBg ? sPlayerbotAIConfig.healDistanceBg : ai->GetRange("heal");
    maxDist *= 2.0f;

    if (!player)
        return false;

    if (player->getObjectGuid() == bot->getObjectGuid())
        return false;

    if (player->GetMapId() != bot->GetMapId())
        return false;

    // A hidden GM is never a spell target (see PartyMemberValue::Check).
    if (player->IsPlayer() && player->GetVisibility() == VISIBILITY_OFF)
        return false;

    if (!player->IsInWorld())
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
