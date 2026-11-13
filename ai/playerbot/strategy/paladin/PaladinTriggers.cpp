
#include "playerbot/playerbot.h"
#include "PaladinTriggers.h"
#include "PaladinActions.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GroupMembers.h"
#include "runtime/policy/class/JudgementUpkeepPolicy.h"

using namespace ai;

// Tortoise Holy Paladin: Daybreak target finder predicate.
// Match a party member who has the Daybreak buff (spell 51322 specifically;
// other "Daybreak" entries 50931 / 51323 are different mechanics — talent
// passive and old vanilla effect respectively) AND HP < 95% (so the heal
// has somewhere to land).
class PlayerWithDaybreakPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PlayerWithDaybreakPredicate(PlayerbotAI* ai)
        : PlayerbotAIAware(ai), FindPlayerPredicate() {}

    virtual bool Check(Unit* unit) override
    {
        if (!unit || !sServerFacade.IsAlive(unit))
            return false;
        // 51322 is the Daybreak buff applied on crit-heals (boost healing
        // taken + max HP). Distinct from 50931 (old) and 51323 (talent).
        if (!ai->HasAura(51322, unit))
            return false;
        // Only return targets that actually need healing — top off allies
        // already at full HP would waste the buff window.
        uint32 hp = unit->GetHealth();
        uint32 maxHp = unit->GetMaxHealth();
        if (maxHp == 0)
            return false;
        return (hp * 100 / maxHp) < 95;
    }
};

Unit* PartyMemberWithDaybreakValue::Calculate()
{
    PlayerWithDaybreakPredicate predicate(ai);
    return FindPartyMember(predicate);
}

bool SealTrigger::IsActive()
{
	Unit* target = GetTarget();
	return !ai->HasAura("seal of justice", target) &&
        !ai->HasAura("seal of command", target) &&
		!ai->HasAura("seal of righteousness", target) &&
		!ai->HasAura("seal of light", target) &&
        !ai->HasAura("seal of wisdom", target) &&
        AI_VALUE2(bool, "combat", "self target");
}

bool SealInMeleeTrigger::IsActive()
{
	if (!SealTrigger::IsActive())
		return false;
	Unit* target = AI_VALUE(Unit*, "current target");
	return target && bot->CanReachWithMeleeAutoAttack(target);
}

namespace
{
// Judgement's range (spell_template rangeIndex 7, the same 10 yd the
// "judgement" action uses).
constexpr float kJudgementRangeYd = 10.0f;

struct JudgementUpkeepState
{
    TortoiseBots::JudgementKind kind = TortoiseBots::JudgementKind::None;
    bool anySealUp = false;
    bool wantedSealUp = false;
    bool needsRefresh = false;
    bool judgementReady = false;
};

// One read of the situation for the three upkeep triggers. Cheap exits first
// (toggle, group, target, spells known); the group walk and the aura read only
// run for a grouped paladin on a boss / elite in judging range. No DB access,
// no world scan: the group list and the target's aura list only.
JudgementUpkeepState ReadJudgementUpkeep(PlayerbotAI* ai, Player* bot, bool wantWisdom, bool wantLight)
{
    using namespace TortoiseBots;
    JudgementUpkeepState state;

    if (!sPlayerbotAIConfig.paladinJudgementUpkeep)
        return state;
    Group* group = bot->GetGroup();
    if (!group || group->GetMembersCount() < 2)
        return state;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || target->IsPlayer() || !sServerFacade.IsAlive(target) || bot->GetDistance(target) > kJudgementRangeYd)
        return state;
    if ((wantWisdom && !ai->HasSpell("seal of wisdom")) || (wantLight && !ai->HasSpell("seal of light")) || !ai->HasSpell("judgement"))
        return state;

    Creature* creature = dynamic_cast<Creature*>(target);
    CreatureInfo const* info = creature ? creature->GetCreatureInfo() : nullptr;
    bool const eliteOrBoss = info && (info->rank == CREATURE_ELITE_ELITE || info->rank == CREATURE_ELITE_RAREELITE ||
        info->rank == CREATURE_ELITE_WORLDBOSS);
    bool const inInstance = bot->GetMap() && bot->GetMap()->IsDungeon();
    if (!eliteOrBoss && !inInstance)
        return state;

    uint8 const mana = AI_VALUE2(uint8, "mana", "self target");
    bool const healer = ai->IsHeal(bot);
    bool partyNeedsHealing = false;
    if (healer)
    {
        Unit* toHeal = AI_VALUE(Unit*, "party member to heal");
        partyNeedsHealing = toHeal && toHeal->GetHealthPercent() < (float)sPlayerbotAIConfig.mediumHealth;
    }
    if (!JudgementUpkeepAffordable(healer, mana, (uint8)sPlayerbotAIConfig.mediumMana, partyNeedsHealing))
        return state;

    // One walk of the group: the paladin bots (owner rule) and, when the target
    // is not an elite, whether a tank is on it.
    JudgementPaladin paladins[kMaxJudgementPaladins];
    std::size_t paladinCount = 0;
    bool tankTarget = false;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !sServerFacade.IsAlive(member))
            continue;

        if (!eliteOrBoss && !tankTarget && PlayerbotAI::IsTank(member))
        {
            PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
            if (memberAi)
            {
                Unit* memberTarget = memberAi->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
                tankTarget = memberTarget == target;
            }
            else
                tankTarget = member->GetSelectionGuid() == target->getObjectGuid();
        }

        if (member->GetClass() != CLASS_PALADIN || paladinCount >= kMaxJudgementPaladins)
            continue;
        PlayerbotAI* paladinAi = member == bot ? ai : PlayerbotAIStorage::Instance().GetAI(member);
        if (!paladinAi)
            continue; // a player paladin: not ours to assign
        if (member != bot && (member->GetMapId() != bot->GetMapId() || bot->GetDistance(member) > 100.0f))
            continue;
        JudgementPaladin& entry = paladins[paladinCount++];
        entry.id = member->GetObjectGuid().GetRawValue();
        entry.healer = PlayerbotAI::IsHeal(member);
        entry.tank = PlayerbotAI::IsTank(member);
        entry.canWisdom = paladinAi->HasSpell("seal of wisdom");
        entry.canLight = paladinAi->HasSpell("seal of light");
    }

    if (!IsJudgementUpkeepTarget(true, eliteOrBoss, tankTarget && inInstance, (uint8)target->GetHealthPercent()))
        return state;

    state.kind = JudgementKindFor(bot->GetObjectGuid().GetRawValue(), paladins, paladinCount);
    if (state.kind == JudgementKind::None)
        return state;

    bool const wisdom = state.kind == JudgementKind::Wisdom;
    // The debuff counts whoever applied it: a player paladin's still-fresh
    // Judgement is left alone and refreshed only once it is about to lapse.
    Aura* debuff = ai->GetAura(wisdom ? "judgement of wisdom" : "judgement of light", target);
    state.needsRefresh = JudgementNeedsRefresh(debuff != nullptr, debuff ? debuff->GetAuraDuration() : 0);
    state.anySealUp = ai->HasAnyAuraOf(bot, "seal of justice", "seal of command", "seal of righteousness", "seal of light", "seal of wisdom", NULL);
    state.wantedSealUp = ai->HasAura(wisdom ? "seal of wisdom" : "seal of light", bot);
    uint32 const judgementId = AI_VALUE2(uint32, "spell id", "judgement");
    state.judgementReady = judgementId && !bot->HasSpellCooldown(judgementId);
    return state;
}
} // namespace

bool JudgementUpkeepTrigger::IsActive()
{
    using namespace TortoiseBots;
    JudgementUpkeepState const state = ReadJudgementUpkeep(ai, bot, step == Step::SealWisdom, step == Step::SealLight);

    if (step == Step::Judge)
        return ShouldCastUpkeepJudgement(state.kind, state.wantedSealUp, state.needsRefresh);

    // A Seal step fires only for the owner of its own debuff.
    JudgementKind const mine = step == Step::SealWisdom ? JudgementKind::Wisdom : JudgementKind::Light;
    return state.kind == mine &&
        ShouldCastUpkeepSeal(state.kind, state.anySealUp, state.wantedSealUp, state.needsRefresh, state.judgementReady);
}

bool BlessingTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        std::vector<std::string> altBlessings;
        std::vector<std::string> haveBlessings;
        altBlessings.push_back("blessing of might");
        altBlessings.push_back("blessing of wisdom");
        altBlessings.push_back("blessing of kings");
        altBlessings.push_back("blessing of sanctuary");
        altBlessings.push_back("blessing of salvation");
        altBlessings.push_back("blessing of light");

        for (auto blessing : altBlessings)
        {
            if (AI_VALUE2(uint32, "spell id", blessing))
            {
                haveBlessings.push_back(blessing);

                const std::string greaterBlessing = "greater " + blessing;
                if (AI_VALUE2(uint32, "spell id", greaterBlessing))
                {
                    haveBlessings.push_back(greaterBlessing);
                }
            }
        }

        if (haveBlessings.empty())
        {
            return false;
        }

        bool noBlessings = true;
        for (auto blessing : haveBlessings)
        {
            if (ai->HasMyAura(blessing, bot))
            {
                noBlessings = false;
            }
        }

        return noBlessings;
    }

    return false;
}

bool GreaterBlessingTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        if (!bot->GetMap()->IsDungeon() && !bot->GetMap()->IsBattleGround())
            return false;

        std::vector<std::string> altBlessings;
        std::vector<std::string> haveBlessings;
        altBlessings.push_back("blessing of might");
        altBlessings.push_back("blessing of wisdom");
        altBlessings.push_back("blessing of kings");
        altBlessings.push_back("blessing of sanctuary");
        altBlessings.push_back("blessing of salvation");
        altBlessings.push_back("blessing of light");

        for (auto blessing : altBlessings)
        {
            const std::string greaterBlessing = "greater " + blessing;
            if (AI_VALUE2(uint32, "spell id", greaterBlessing))
            {
                haveBlessings.push_back(greaterBlessing);
            }
        }

        if (haveBlessings.empty())
        {
            return false;
        }

        bool noBlessings = true;
        for (auto blessing : haveBlessings)
        {
            if (ai->HasMyAura(blessing, bot))
            {
                noBlessings = false;
            }
        }

        return noBlessings;
    }

    return false;
}

// A group member (or the bot itself) this bot could still bless: it holds
// no blessing FROM THIS BOT and at least one role-priority pick is
// uncovered by any caster. Mirrors CastBlessingOnPartyAction::GetTarget so
// the trigger and the action always resolve the same member. Core aura rule
// (one blessing per target per caster): own live aura blocks (a second cast
// would evict it); another paladin's aura never blocks (different casters
// stack — each member converges to one blessing from EACH paladin).
Unit* FindBlessingCandidate(PlayerbotAI* ai, Player* bot, bool greaterOnly)
{
    static char const* const kBlessings[] =
    {
        "blessing of might", "blessing of wisdom", "blessing of kings",
        "blessing of sanctuary", "blessing of salvation", "blessing of light",
    };

    bool knowsAny = false;
    for (char const* blessing : kBlessings)
    {
        std::string const base(blessing);
        std::string const greaterBlessing = "greater " + base;
        bool const known = greaterOnly
            ? ai->GetAiObjectContext()->GetValue<uint32>("spell id", greaterBlessing)->Get() != 0
            : ai->GetAiObjectContext()->GetValue<uint32>("spell id", base)->Get() != 0;
        if (known)
        {
            knowsAny = true;
            break;
        }
    }

    if (!knowsAny)
        return nullptr;

    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return nullptr;

    std::list<Player*> healers, tanks, others;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member->GetMapId() != bot->GetMapId())
            continue;
        // Hidden GM masters can never land a spell (see the action-side
        // scan): skip them so they never wedge the trigger either.
        if (member != bot && member->GetVisibility() == VISIBILITY_OFF)
            continue;
        if (member == bot)
        {
            others.push_back(member);
            continue;
        }
        if (ai->IsHeal(member))
            healers.push_back(member);
        else if (ai->IsTank(member))
            tanks.push_back(member);
        else
            others.push_back(member);
    }

    // Visit rule: own live blessing blocks; any same-family cover from any
    // caster blocks that family only. (The action's full role-priority pick
    // runs at cast time; the trigger only needs "something could land".)
    auto CouldBless = [&](Player* member)
    {
        std::string const ordered = OrderedBlessingFor(ai, member);
        if (!ordered.empty())
            return !ai->HasAura(ordered, member) && !ai->HasAura("greater " + ordered, member);
        for (char const* blessing : kBlessings)
        {
            std::string const base(blessing);
            if (ai->HasMyAura(base, member) || ai->HasMyAura("greater " + base, member))
                return false;
        }
        return true;
    };

    for (std::list<Player*>* bucket : { &healers, &tanks, &others })
    {
        for (Player* member : *bucket)
        {
            if (sServerFacade.getDistance2d(member, bot) > 30.0f)
                continue;
            if (CouldBless(member) && sServerFacade.IsWithinLOSInMap(bot, member))
                return member;
        }
    }

    return nullptr;
}

bool BlessingOnPartyTrigger::IsActive()
{
    return FindBlessingCandidate(ai, bot, false) != nullptr;
}

bool GreaterBlessingOnPartyTrigger::IsActive()
{
    if (!bot->GetMap()->IsDungeon() && !bot->GetMap()->IsBattleGround())
        return false;

    Unit* target = FindBlessingCandidate(ai, bot, true);
    Player* targetPlayer = dynamic_cast<Player*>(target);
    return targetPlayer && IsInGroup_Helper(bot, targetPlayer);
}

bool NoPaladinAuraTrigger::IsActive()
{
    std::vector<std::string> altAuras;
    std::vector<std::string> haveAuras;
    altAuras.push_back("devotion aura");
    altAuras.push_back("retribution aura");
    altAuras.push_back("concentration aura");
    altAuras.push_back("sanctity aura");
    altAuras.push_back("shadow resistance aura");
    altAuras.push_back("fire resistance aura");
    altAuras.push_back("frost resistance aura");
    altAuras.push_back("crusader aura");

    for (auto aura : altAuras)
    {
        if (AI_VALUE2(uint32, "spell id", aura))
        {
            haveAuras.push_back(aura);
        }
    }

    if (haveAuras.empty())
    {
        return false;
    }

    bool hasAura = false;
    for (auto aura : haveAuras)
    {
        if (ai->HasMyAura(aura, bot))
        {
            hasAura = true;
            break;
        }
    }

    if (hasAura)
    {
        return false;
    }

    bool needAura = false;
    for (auto aura : haveAuras)
    {
        if (!ai->HasAura(aura, bot))
        {
            needAura = true;
            break;
        }
    }

    return needAura;
}

bool ChoiceAuraTrigger::IsActive()
{
    std::string const ordered = ai::OrderedAuraFor(ai);
    if (ordered.empty() || !ai->HasSpell(ordered))
        return false;
    Unit* target = GetTarget();
    return target && !ai->HasMyAura(ordered, bot);
}

bool HammerOfJusticeOnEnemyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    uint8 targetHP = AI_VALUE2(uint8, "health", GetTargetName());
    uint8 selfHP = AI_VALUE2(uint8, "health", "self target");
    uint8 selfMP = AI_VALUE2(uint8, "mana", "self target");
    bool isMoving = AI_VALUE2(bool, "moving", GetTargetName());

    if (isMoving && target->IsPlayer())
        return true;

    if (targetHP < 10)
        return true;

    if (selfHP < sPlayerbotAIConfig.lowHealth && selfMP > 10)
        return true;

    return false;
}

bool ConsecrationTrigger::IsActive()
{
    if (!ai->HasSpell("consecration"))
        return false;
    if (SpellNoCooldownTrigger::IsActive())
    {
        return AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumMana;
    }

    return false;
}

bool ExorcismTrigger::IsActive()
{
    if (SpellNoCooldownTrigger::IsActive())
    {
        if (AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumMana)
        {
            // Art of War makes Exorcism instant: always worth casting.
            // Otherwise Exorcism only affects undead/demon targets; core
            // rejects other types, so firing there wastes mana and the GCD.
            if (ai->HasAura("the art of war", bot))
                return true;

            if (Creature* target = dynamic_cast<Creature*>(GetTarget()))
            {
                uint32 type = target->GetCreatureType();
                return type == CREATURE_TYPE_UNDEAD || type == CREATURE_TYPE_DEMON;
            }
        }
    }

    return false;
}
