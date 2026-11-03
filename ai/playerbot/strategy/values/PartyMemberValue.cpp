
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "PartyMemberValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "FreeMoveValues.h"
#include "LastMovementValue.h"

using namespace ai;

// A matching member out of line of sight is only remembered in outOfSight
// (the first one seen); the search goes on for a member in sight. Before,
// the first match in list order was returned whatever its line of sight, so
// a member the bot could not see (behind a wall, upstairs, on a ledge it
// could not path to) was picked on every check and the cast failed, while
// the members after it never got the buff, cure or resurrection.
// mod-playerbots filters out-of-LOS members outright; here they stay a
// fallback so the reach prerequisite can still walk to a lone member out of
// sight (a corpse around a corner). The LOS test runs only on members that
// already match, stopping at the first one in sight.
Unit* PartyMemberValue::FindPartyMember(std::list<Player*>* party, FindPlayerPredicate &predicate, bool ignoreTanks, Unit*& outOfSight)
{
    for (std::list<Player*>::iterator i = party->begin(); i != party->end(); ++i)
    {
        Player* player = *i;

        if (!player)
            continue;

        if (ignoreTanks && ai->IsTank(player))
            continue;

        if (bot->GetGroup() && !IsInGroup_Helper(player, bot) && !CanFreeMoveValue::CanFreeMoveTo(ai, player))
            continue;

        if (Check(player) && predicate.Check(player))
        {
            if (sServerFacade.IsWithinLOSInMap(bot, player))
                return player;
            if (!outOfSight)
                outOfSight = player;
        }

        Pet* pet = player->GetPet();
        if (pet && Check(pet) && predicate.Check(pet))
        {
            if (sServerFacade.IsWithinLOSInMap(bot, pet))
                return pet;
            if (!outOfSight)
                outOfSight = pet;
        }
    }

    return NULL;
}

Unit* PartyMemberValue::FindPartyMember(FindPlayerPredicate &predicate, bool ignoreOutOfGroup, bool ignoreTanks)
{
    Player* master = GetMaster();

    std::list<ObjectGuid> nearestPlayers;

    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || bot->GetMapId() != member->GetMapId()) continue;

            if (member != bot)
            {
                if (member->GetSubGroup() != bot->GetSubGroup())
                {
                    nearestPlayers.push_back(member->getObjectGuid());
                }
                else
                {
                    nearestPlayers.push_front(member->getObjectGuid());
                }
            }
        }
    }

    bool allowBufOutOfGroupPlayers = !ignoreOutOfGroup && !bot->GetGroup() && !master;

    if (allowBufOutOfGroupPlayers && !ai->AllowActivity(OUT_OF_PARTY_ACTIVITY))
        allowBufOutOfGroupPlayers = false;

    if (allowBufOutOfGroupPlayers && AI_VALUE2(float, "distance", "master target") > sPlayerbotAIConfig.sightDistance)
        allowBufOutOfGroupPlayers = false;

    if (allowBufOutOfGroupPlayers && AI_VALUE2(uint32, "current mount speed", "self target"))
        allowBufOutOfGroupPlayers = false;

    if (allowBufOutOfGroupPlayers && !AI_VALUE(LastMovement&, "last movement").lastPath.empty() && AI_VALUE(LastMovement&, "last movement").lastPath.GetBack().distance(bot) > sPlayerbotAIConfig.sightDistance)
        allowBufOutOfGroupPlayers = false;

    if (allowBufOutOfGroupPlayers)
    {
        std::list<ObjectGuid> nearestOutOfGroupPlayers;
        if (ai->AllowActivity(OUT_OF_PARTY_ACTIVITY))
            nearestOutOfGroupPlayers = AI_VALUE(std::list<ObjectGuid>, "nearest friendly players");

        if (nearestOutOfGroupPlayers.size() < 100)
            nearestPlayers.insert(nearestPlayers.end(), nearestOutOfGroupPlayers.begin(), nearestOutOfGroupPlayers.end());
    }

    std::list<Player*> healers, tanks, others, masters;
    if (master) masters.push_back(master);
    for (std::list<ObjectGuid>::iterator i = nearestPlayers.begin(); i != nearestPlayers.end(); ++i)
    {
        Player* player = dynamic_cast<Player*>(ai->GetUnit(*i));
        if (!player || player == bot)
        {
            continue;
        }

        if (ai->IsHeal(player))
        {
            healers.push_back(player);
        }
        else if (ai->IsTank(player) && !ignoreTanks)
        {
            tanks.push_back(player);
        }
        else if (player != master)
        {
            others.push_back(player);
        }
    }

    std::list<std::list<Player*>* > lists;
    lists.push_back(&healers);
    lists.push_back(&tanks);
    lists.push_back(&masters);
    lists.push_back(&others);

    Unit* outOfSight = nullptr;
    for (std::list<std::list<Player*>* >::iterator i = lists.begin(); i != lists.end(); ++i)
    {
        std::list<Player*>* party = *i;
        Unit* target = FindPartyMember(party, predicate, ignoreTanks, outOfSight);
        if (target)
            return target;
    }

    // Nobody matching is in sight: fall back to the first match, as before.
    if (outOfSight)
        return outOfSight;

    if (allowBufOutOfGroupPlayers)
    {
        if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target"))
        {
            Unit* target = rpgTarget.GetCreature(bot->GetInstanceId());

            if (target && sServerFacade.IsFriendlyTo(bot, target) && predicate.Check(target) && CanFreeMoveValue::CanFreeMoveTo(ai, target))
                return target;
        }
    }

    return NULL;
}

bool PartyMemberValue::Check(Unit* player)
{
    if (!player || player == bot || player->GetMapId() != bot->GetMapId())
        return false;

    // A hidden GM (VISIBILITY_OFF; GM.Visible = 0 logs GMs in hidden and
    // `.gm off` does not unhide them) can never be a spell target: the core
    // drops such a player in Spell::CheckTarget after the cast has started,
    // so the mana is spent and nothing lands. Left selectable, a party buff
    // picked that member every tick forever and the members after it in the
    // list never got the buff (paladin blessing loop, 2026-10-11).
    // mod-playerbots skips GMs as blessing targets the same way.
    if (player->IsPlayer() && player->GetVisibility() == VISIBILITY_OFF)
        return false;

    return bot->IsWithinDistInMap(player, sPlayerbotAIConfig.sightDistance, false);
}

bool PartyMemberValue::IsTargetOfSpellCast(Unit* target, SpellEntryPredicate &predicate)
{
    std::list<ObjectGuid> nearestPlayers = AI_VALUE(std::list<ObjectGuid>, "nearest friendly players");
    ObjectGuid targetGuid = target ? target->getObjectGuid() : bot->getObjectGuid();
    Player* targetPlayer = target && target->GetTypeId() == TYPEID_PLAYER ? static_cast<Player*>(target) : nullptr;
    ObjectGuid corpseGuid = targetPlayer && targetPlayer->GetCorpse() ? targetPlayer->GetCorpse()->getObjectGuid() : ObjectGuid();

    for (std::list<ObjectGuid>::iterator i = nearestPlayers.begin(); i != nearestPlayers.end(); ++i)
    {
        Player* player = dynamic_cast<Player*>(ai->GetUnit(*i));
        if (!player || player == bot)
            continue;

        if (player->IsNonMeleeSpellCasted(true))
        {
            for (int type = CURRENT_GENERIC_SPELL; type < CURRENT_MAX_SPELL; type++)
            {
                Spell* spell = player->GetCurrentSpell((CurrentSpellTypes)type);
                if (spell && predicate.Check(spell->m_spellInfo))
                {
                    ObjectGuid unitTarget = spell->m_targets.getUnitTargetGuid();
                    if (unitTarget == targetGuid)
                        return true;

                    if (corpseGuid)
                    {
                        ObjectGuid corpseTarget = spell->m_targets.getCorpseTargetGuid();
                        if (corpseTarget == corpseGuid)
                            return true;
                    }
                }
            }
        }
    }

    return false;
}
