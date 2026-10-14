#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "PartyMemberToSoulstone.h"
#include "playerbot/ServerFacade.h"
#include "../../../../runtime/WarlockSoulstonePolicy.h"

using namespace ai;

class IsTargetOfSoulstoneSpell : public SpellEntryPredicate
{
public:
    virtual bool Check(SpellEntry const* spell) override
    {
        for (int i = 0; i < 3; i++)
        {
            if(spell->Effect[i] == SPELL_EFFECT_SELF_RESURRECT)
                return true;
        }
        return false;
    }
};

static bool HasOtherWarlockInGroup(Player* bot, Group* group)
{
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot)
            continue;
        if (member->GetClass() == CLASS_WARLOCK)
            return true;
    }
    return false;
}

static bool BotIsAlreadySoulstoned(Player* bot)
{
    Unit::SpellAuraHolderMap const& auras = bot->GetSpellAuraHolderMap();
    for (auto const& pair : auras)
    {
        SpellEntry const* spellInfo = pair.second->GetSpellProto();
        for (int i = 0; i < 3; i++)
        {
            if (spellInfo->Effect[i] == SPELL_EFFECT_SELF_RESURRECT)
                return true;
        }
    }
    return false;
}

// The group as the warlock sees it, for TortoiseBots::DecideSoulstoneTarget
// (runtime/WarlockSoulstonePolicy.h: healer, else tank, else master/leader).
// Members on other maps and offline members are left out, so they never hold
// a tier the warlock would wait on.
Unit* PartyMemberToSoulstone::Calculate()
{
    // If a master player has manually assigned revive targets, defer to that
    if (!AI_VALUE(std::list<ObjectGuid>, "revive targets").empty())
        return nullptr;

    TortoiseBots::SoulstoneFacts facts;
    facts.botInCombat = bot->IsInCombat();
    if (facts.botInCombat)
        return nullptr;

    Group* group = bot->GetGroup();
    facts.inGroup = group != nullptr;
    facts.botHasSoulstoneAura = BotIsAlreadySoulstoned(bot);

    std::vector<TortoiseBots::SoulstoneCandidate> candidates;
    std::vector<Player*> players;
    if (group)
    {
        Player* master = GetMaster();
        float const range = ai->GetRange("spell");
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || member->GetMapId() != bot->GetMapId() || member->InBattleGround())
                continue;

            TortoiseBots::SoulstoneCandidate c;
            c.isHealer = ai->IsHeal(member);
            c.isTank = !c.isHealer && ai->IsTank(member);
            c.isLeader = member == master || group->IsLeader(member->GetObjectGuid());
            c.alive = member->IsAlive();
            if (c.alive && TortoiseBots::SoulstoneTierOf(c) != TortoiseBots::SoulstoneTier::None)
            {
                c.inRange = sServerFacade.getDistance2d(bot, member) <= range;
                c.inLineOfSight = c.inRange && sServerFacade.IsWithinLOSInMap(bot, member);
                c.hasSoulstoneAura = ai->HasAura("soulstone resurrection", member);
                // Only worth the nearby-caster scan for a member that is otherwise ready.
                if (c.inLineOfSight && !c.hasSoulstoneAura)
                {
                    IsTargetOfSoulstoneSpell predicate;
                    c.targetedByOtherWarlock = IsTargetOfSpellCast(member, predicate);
                }
            }
            candidates.push_back(c);
            players.push_back(member);
        }
    }

    TortoiseBots::SoulstoneChoice choice = TortoiseBots::DecideSoulstoneTarget(facts, candidates);
    switch (choice.kind)
    {
    case TortoiseBots::SoulstoneChoiceKind::Self:
        return ai->GetBot();
    case TortoiseBots::SoulstoneChoiceKind::Member:
        // Multiple warlocks in group may target same player. Each roll D20 to stagger casts.
        if (HasOtherWarlockInGroup(bot, group) && urand(1, 20) != 20)
            return nullptr;
        return players[choice.index];
    default:
        return nullptr;
    }
}
