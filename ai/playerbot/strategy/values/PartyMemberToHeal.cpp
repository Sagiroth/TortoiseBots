
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "PartyMemberToHeal.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "ObjectAccessor.h"

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

bool compareByMissingHealth(const Unit* u1, const Unit* u2, bool incomingDamage = false)
{
    uint32 hp1 = u1->GetHealth();
    if (incomingDamage)
    {
        uint32 damage1 = getIncomingdamage(u1);
        hp1 = damage1 >= hp1 ? 0 : hp1 - damage1;
    }
    uint32 hpmax1 = u1->GetMaxHealth();
    uint32 hp2 = u2->GetHealth();
    if (incomingDamage)
    {
        uint32 damage2 = getIncomingdamage(u2);
        hp2 = damage2 >= hp2 ? 0 : hp2 - damage2;
    }
    uint32 hpmax2 = u2->GetMaxHealth();
    return (hpmax1 - hp1) > (hpmax2 - hp2);
}

// Urgency for the LOS tie-break below: missing health falls back to health
// percent when max health is unreachable (should not happen for live units,
// but the sort must stay a strict ordering either way).
static uint32 missingHealthForLosTieBreak(const Unit* u)
{
    uint32 cur = u->GetHealth();
    uint32 max = u->GetMaxHealth();
    if (!max || cur >= max)
        return 0;
    return max - cur;
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
            target->GetHealthPercent() < 100 &&
            Check(target))
        {
            needHeals.push_back(target);
        }
    }

    if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target"))
    {
        Unit* target = rpgTarget.GetCreature(bot->GetInstanceId());
        if (target && sServerFacade.IsFriendlyTo(bot, target) && target->GetHealthPercent() < 100)
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
            if (isTank || (health < sPlayerbotAIConfig.almostFullHealth && !IsTargetOfSpellCast(player, predicate)))
            {
                needHeals.push_back(player);
            }

            Pet* pet = player->GetPet();
            if (pet && CanHealPet(pet))
            {
                health = pet->GetHealthPercent();
                if (health < sPlayerbotAIConfig.almostFullHealth || !IsTargetOfSpellCast(player, predicate))
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

    if (needHeals.empty() && !tankTargets.empty())
    {
        needHeals = tankTargets;
    }

    bool preHealing = ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT);
    sort(needHeals.begin(), needHeals.end(), [preHealing](const Unit* u1, const Unit* u2) { return compareByMissingHealth(u1, u2, preHealing); });

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
        // mediumHealth 70 -> 30% of the top target's max health.
        uint32 topMissing = missingHealthForLosTieBreak(mostUrgent);
        uint32 smallGap = mostUrgent->GetMaxHealth()
            ? mostUrgent->GetMaxHealth() * (100 - sPlayerbotAIConfig.mediumHealth) / 100
            : 0;
        for (Unit* candidate : needHeals)
        {
            if (candidate == mostUrgent)
                continue;
            if (topMissing > missingHealthForLosTieBreak(candidate) + smallGap)
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
    if (chosen && chosen->GetHealthPercent() >= sPlayerbotAIConfig.almostFullHealth)
        chosen = needHeals[0];

    return chosen;
}

bool PartyMemberToHeal::CanHealPet(Pet* pet)
{
    return MINI_PET != pet->getPetType();
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

Unit* PartyMemberMainTankValue::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group)
        return PlayerbotAI::IsTank(bot) ? bot : NULL;

    // Explicit raid main-tank flag first (donor GetMainTankGuid; core owns
    // the flag via Group::GetMainTankGuid, raid-only by design).
    if (ObjectGuid mainTankGuid = group->GetMainTankGuid())
    {
        if (Player* mainTank = ObjectAccessor::FindPlayer(mainTankGuid))
            if (mainTank->IsAlive() && ai->IsSafe(mainTank))
                return mainTank;
    }

    // Else the first live tank in slot order (donor fallback).
    for (Player* member : LiveGroupMembers(group))
    {
        if (member && member->IsAlive() && ai->IsSafe(member) && PlayerbotAI::IsTank(member))
            return member;
    }

    return NULL;
}
