
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "DungeonTriggers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/HazardsValue.h"
#include "playerbot/strategy/actions/MovementActions.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool EnterDungeonTrigger::IsActive()
{
    // Don't trigger if strategy already set
    if (!ai->HasStrategy(dungeonStrategy, BotState::BOT_STATE_COMBAT))
    {
        // If the bot is ready
        if (bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            // If the bot is on the specified dungeon
            Map* map = bot->GetMap();
            if (map && (map->IsDungeon() || map->IsRaid()))
            {
                return map->GetId() == mapID;
            }
        }
    }

    return false;
}

bool LeaveDungeonTrigger::IsActive()
{
    // Don't trigger if strategy already unset
    if (ai->HasStrategy(dungeonStrategy, BotState::BOT_STATE_COMBAT))
    {
        // If the bot is ready
        if (bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            // If the bot is not on the specified dungeon
            return bot->GetMapId() != mapID;
        }
    }

    return false;
}

bool StartBossFightTrigger::IsActive()
{
    // Don't trigger if strategy already set
    if (!ai->HasStrategy(bossStrategy, BotState::BOT_STATE_COMBAT))
    {
        // If the bot is ready
        if (bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (attacker && attacker->GetEntry() == bossID)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool EndBossFightTrigger::IsActive()
{
    // Don't trigger if strategy already unset
    if (ai->HasStrategy(bossStrategy, BotState::BOT_STATE_COMBAT))
    {
        // We consider the fight is over if not in combat
        return !ai->IsStateActive(BotState::BOT_STATE_COMBAT);
    }

    return false;
}

bool CloseToHazardTrigger::IsActive()
{
    // If the bot is ready
    bool closeToHazard = false;
    if (bot->IsInWorld() && !bot->IsBeingTeleported())
    {
        const std::list<ObjectGuid>& possibleHazards = GetPossibleHazards();
        for (const ObjectGuid& possibleHazardGuid : possibleHazards)
        {
            if (IsHazardValid(possibleHazardGuid))
            {
                // Check if the bot is inside the hazard
                const float distanceToHazard = GetDistanceToHazard(possibleHazardGuid);
                if (distanceToHazard <= hazardRadius)
                {
                    closeToHazard = true;
                }
            }

            // Cache the hazards
            Hazard hazard(possibleHazardGuid, hazardDuration, hazardRadius);
            SET_AI_VALUE(Hazard, "add hazard", std::move(hazard));
        }
    }

    // Don't trigger if the bot is moving
    if (closeToHazard)
    {
        const Action* lastExecutedAction = ai->GetLastExecutedAction(BotState::BOT_STATE_COMBAT);
        if (lastExecutedAction)
        {
            const MovementAction* movementAction = dynamic_cast<const MovementAction*>(lastExecutedAction);
            if (movementAction)
            {
                closeToHazard = false;
            }
        }
    }

    return closeToHazard;
}

bool CloseToHazardTrigger::IsHazardValid(const ObjectGuid& hazzardGuid)
{
    if (hazzardGuid.IsGameObject())
    {
        return ai->GetGameObject(hazzardGuid) != nullptr;
    }
    else if (hazzardGuid.IsCreature())
    {
        return ai->GetCreature(hazzardGuid) != nullptr;
    }

    return false;
}

float CloseToHazardTrigger::GetDistanceToHazard(const ObjectGuid& hazzardGuid)
{
    if (hazzardGuid.IsGameObject())
    {
        GameObject* gameObjectHazard = ai->GetGameObject(hazzardGuid);
        if (gameObjectHazard)
        {
            return bot->GetDistance(gameObjectHazard) + gameObjectHazard->GetObjectBoundingRadius();
        }
    }
    else if (hazzardGuid.IsCreature())
    {
        Creature* creatureHazard = ai->GetCreature(hazzardGuid);
        if (creatureHazard)
        {
            return bot->GetCombatDistance(creatureHazard);
        }
    }

    return 99999999.0f;
}

std::list<ObjectGuid> CloseToGameObjectHazardTrigger::GetPossibleHazards()
{
    // This game objects have a maximum range equal to the sight distance on config file (default 60 yards)
    return AI_VALUE2(std::list<ObjectGuid>, "nearest game objects no los", gameObjectID);
}

bool EnvironmentalHazardTrigger::IsActive()
{
    bool closeToHazard = false;

    if (bot->IsInWorld() && !bot->IsBeingTeleported())
    {
        for (const ObjectGuid& guid : AI_VALUE(std::list<ObjectGuid>, "nearest game objects"))
        {
            GameObject* go = ai->GetGameObject(guid);
            if (!go)
                continue;

            if (!sServerFacade.IsEnvironmentalDamageTrap(go))
                continue;

            GameObjectInfo const* goInfo = go->GetGOInfo();

            // React a bit before the real damage boundary (goInfo->trap.radius is exactly
            // what GameObject::Update uses to decide who to hit) - a small fixed margin, not
            // a large floor. Many of these traps (e.g. damage-only "Campfire"/"Fire" GOs -
            // distinct objects from the buff-only "Cozy Fire" campfires, which carry no
            // damage effect and aren't flagged as hazards at all) have a real radius of ~1
            // yard and sit right next to a legitimate bot destination (trainer/anvil/vendor);
            // a big floor here (previously max(radius*1.3, 5.0)) made the bot treat "close
            // enough to interact" as "too close to the hazard," so it kept getting shoved away
            // and walking straight back through the real danger zone to reach its actual goal.
            // A tight margin lets the bot approach its real destination without
            // false-triggering, while still reacting before the literal damage radius for
            // genuinely bigger hazards.
            float reactRadius = goInfo->trap.radius + 2.0f;
            float distance = bot->GetDistance(go) + go->GetObjectBoundingRadius();
            if (distance <= reactRadius)
                closeToHazard = true;

            // The radius registered here is deliberately bigger than reactRadius above -
            // it's what MoveAwayFromHazard uses to compute how
            // far to flee (frand(radius, radius*1.5)) and what movement/pathing uses to reject
            // a destination near this GO. Found via live testing that some bots have a fixed
            // wander/idle anchor point sitting almost exactly on a trap tile (e.g. a campfire
            // in a Dun Morogh social hub); fleeing only reactRadius+50% (~4-5 yards for a
            // 1-yard trap) never got them outside the zone their own wander logic considers
            // "close enough to stop," so they kept drifting straight back onto the death tile
            // every couple of minutes. A bigger registered radius makes the escape distance -
            // and any future destination-picking near this GO - actually clear that zone.
            float registeredRadius = std::max(reactRadius, 12.0f);
            Hazard hazard(guid, hazardDuration, registeredRadius);
            SET_AI_VALUE(Hazard, "add hazard", std::move(hazard));
        }
    }

    return closeToHazard;
}

std::list<ObjectGuid> CloseToCreatureHazardTrigger::GetPossibleHazards()
{
    std::list<ObjectGuid> possibleHazards;

    std::list<Unit*> creatures;
    MaNGOS::AllCreaturesOfEntryInRange u_check(bot, creatureID, hazardRadius);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(creatures, u_check);
    Cell::VisitAllObjects(bot, searcher, hazardRadius);
    for (Unit* unit : creatures)
    {
        possibleHazards.push_back(unit->getObjectGuid());
    }

    return possibleHazards;
}

bool CloseToCreatureHazardTrigger::IsHazardValid(const ObjectGuid& hazzardGuid)
{
    Creature* creatureHazard = ai->GetCreature(hazzardGuid);
    if (creatureHazard)
    {
        // Check if the creature is not targeting the bot
        if (!creatureHazard->GetVictim() || (creatureHazard->GetVictim()->getObjectGuid() != bot->getObjectGuid()))
        {
            return true;
        }
    }

    return false;
}

std::list<ObjectGuid> CloseToHostileCreatureHazardTrigger::GetPossibleHazards()
{
    std::list<ObjectGuid> possibleHazards;
    std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");

    for (const ObjectGuid& attackerGuid : attackers)
    {
        Creature* attacker = ai->GetCreature(attackerGuid);
        if (attacker && attacker->GetEntry() == creatureID)
        {
            possibleHazards.push_back(attackerGuid);
        }
    }

    return possibleHazards;
}

bool CloseToCreatureTrigger::IsActive()
{
    // If the bot is ready
    if (bot->IsInWorld() && !bot->IsBeingTeleported())
    {
        AiObjectContext* context = ai->GetAiObjectContext();

        // Iterate through the near creatures
        std::list<Unit*> creatures;
        MaNGOS::AllCreaturesOfEntryInRange u_check(bot, creatureID, range);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(creatures, u_check);
        Cell::VisitAllObjects(bot, searcher, range);
        for (Unit* unit : creatures)
        {
            Creature* creature = (Creature*)unit;
            if (creature)
            {
                // Check if the bot is not being targeted by the creature
                if (!creature->GetVictim() || (creature->GetVictim()->getObjectGuid() != bot->getObjectGuid()))
                {
                    // See if the creature is within the specified distance
                    if (bot->IsWithinDist(creature, range))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool ItemReadyTrigger::IsActive()
{
    // Check if the bot has the item or if it has cheats enabled
    if (bot->HasItemCount(itemID, 1) || ai->HasCheat(BotCheatMask::item))
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemID);
        if (proto)
        {
            // Check if the item is in cooldown
            bool inCooldown = false;
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                {
                    continue;
                }

                const uint32 spellID = proto->Spells[i].SpellId;
                if (spellID > 0)
                {
                    if (!sServerFacade.IsSpellReady(bot, spellID) ||
                        !sServerFacade.IsSpellReady(bot, spellID, itemID))
                    {
                        inCooldown = true;
                        break;
                    }
                }
            }

            return !inCooldown;
        }
    }

    return false;
}

bool ItemBuffReadyTrigger::IsActive()
{
    if (!bot->HasAura(buffID))
    {
        return ItemReadyTrigger::IsActive();
    }

    return false;
}

bool RaidBombDebuffTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    // Spell-ID based: Geddon Living Bomb, Vael Burning Adrenaline
    // (Turtle 23620 + classic 18173), Grobbulus Mutating Injection.
    static const uint32 bombSpells[] = { 20475, 23620, 18173, 23478, 28169 };
    for (uint32 spellId : bombSpells)
    {
        if (ai->HasAura(spellId, bot))
            return true;
    }
    return false;
}

bool FourHorsemenMarkTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    // Thane 28832, Blaumeux 28833, Mograine 28834, Zeliek 28835.
    static const uint32 markSpells[] = { 28832, 28833, 28834, 28835 };
    for (uint32 spellId : markSpells)
    {
        if (Aura* aura = ai->GetAura(spellId, bot))
        {
            if (aura->GetStackAmount() >= 3)
                return true;
        }
    }
    return false;
}

namespace
{
bool IsRaidDragonEntry(uint32 entry)
{
    switch (entry)
    {
        case 10184: // Onyxia
        case 14601: // Ebonroc
        case 11981: // Flamegor
        case 11983: // Firemaw
        case 11583: // Nefarian
        case 60748: // Solnius (Emerald Sanctum, Acid Breath 24839)
            return true;
        default:
            return false;
    }
}
} // namespace

bool DragonBreathRiskTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    Unit* target = GetTarget();
    if (!target || !sServerFacade.IsAlive(target) || !target->IsCreature())
        return false;
    if (!IsRaidDragonEntry(target->GetEntry()))
        return false;
    // Tanks hold the head; the trigger tells non-tanks to flank. Tank
    // positioning itself is an explicit .bot raid tankface command.
    if (ai->IsTank(bot))
        return false;
    const float dist = bot->GetDistance(target);
    if (dist > sPlayerbotAIConfig.spellDistance + 10.0f)
        return false;
    // Angle between the dragon's facing and the bot, in radians.
    float facing = target->GetOrientation();
    float toBot = target->GetAngle(bot);
    float diff = fabs(toBot - facing);
    while (diff > M_PI_F)
        diff = fabs(diff - 2.0f * M_PI_F);
    // Front 90-degree cone (breath/cleave) or rear 60-degree tail cone.
    const float frontHalf = M_PI_F / 4.0f;
    const float rearHalf = M_PI_F / 6.0f;
    return diff < frontHalf || diff > (M_PI_F - rearHalf);
}

bool RaidSpreadNeededTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    // Melee/tanks stack by design; spread is a ranged survival behavior.
    if (!ai->IsRanged(bot))
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (sServerFacade.getDistance2d(bot, member) < 10.0f)
            return true;
    }
    return false;
}
