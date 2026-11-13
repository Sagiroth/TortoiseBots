
#include "playerbot/playerbot.h"
#include "playerbot/FankrissGauntletPolicy.h"
#include "TargetValue.h"
#include "ObjectAccessor.h"
#include "playerbot/policy/combat/CcAreaGuardPolicy.h"
#include "CcMarkHoldPolicy.h"
#include "PeriodicDamageAura.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/EnemyAreaSpell.h"
#include "PossibleAttackTargetsValue.h"
#include "RtiTargetValue.h"
#include "Objects/Unit.h"
#include "LastMovementValue.h"
#include "playerbot/strategy/values/Formations.h"

using namespace ai;

Unit* TargetValue::FindTarget(FindTargetStrategy* strategy)
{
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        // The cached candidate list may predate the Fankriss pull.
        if (!unit || IgnoreFankrissGauntlet(bot, unit))
            continue;

        ThreatManager &threatManager = sServerFacade.GetThreatManager(unit);
        strategy->CheckAttacker(unit, &threatManager);
    }

    return strategy->GetResult();
}

Unit* TargetValue::GetExplicitAttackTarget()
{
    ObjectGuid guid = AI_VALUE(ObjectGuid, "explicit attack target");
    if (guid.IsEmpty())
        return nullptr;

    Unit* target = ai->GetUnit(guid);
    // An explicit order carries the walk to the target with it, so it stays valid
    // out to the order range, not just the background sight distance. The bot closes
    // the distance through the combat reach chain after accepting the order.
    if (!target || !PossibleAttackTargetsValue::IsValid(target, bot,
            sPlayerbotAIConfig.reactDistance, false, false))
        return nullptr;

    return target;
}

namespace
{
    class FindLastCcTargetStrategy : public FindNonCcTargetStrategy
    {
    public:
        FindLastCcTargetStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}

        void CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/) override
        {
            if (result || !attacker || attacker->IsPlayer() || !sServerFacade.IsAlive(attacker))
                return;
            // Only a mark that is already fighting: never open on a marked
            // mob of an unpulled pack.
            if (!sServerFacade.IsInCombat(attacker))
                return;
            if (IsCcMark(attacker))
                result = attacker;
        }
    };

    // The hold window (CcMarkHoldSeconds) lives in CcMarkHoldPolicy.h.
    // A mob running in may be a few yards outside the CC spell's range.
    constexpr float CcGraceRangeSlack = 5.0f;
    // Cast-time CC a CC bot opens with on its mark (Sap and traps cannot
    // hit a mob that is already fighting).
    char const* const GraceCcSpells[] = { "polymorph", "shackle undead", "hibernate", "banish", "turn undead",
        "fear", "scare beast" };
}

Unit* TargetValue::FindLastCcTarget()
{
    FindLastCcTargetStrategy strategy(ai);
    // "possible attack targets" drops stunned/rooted/feared mobs while a
    // free one is left, so the tournaments returning nothing does not mean
    // only marks are left; check the raw attackers first.
    if (!strategy.OnlyCcMarksLeft())
        return nullptr;
    return FindTarget(&strategy);
}

bool FindNonCcTargetStrategy::OnlyCcMarksLeft(Unit* except)
{
    Player* bot = ai->GetBot();
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    // Live enemies seen, `except` included: an empty list is not "only marks
    // left" (CcOnlyMarksLeft).
    int seen = 0;
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        if (unit == except)
        {
            ++seen;
            continue;
        }
        // Same filter "possible attack targets" applies before its CC strip
        // (ignoreCC = true): crowd-controlled mobs stay in.
        if (!PossibleAttackTargetsValue::IsValid(unit, bot, sPlayerbotAIConfig.sightDistance, true, false))
            continue;
        ++seen;
        if (!IsCcMark(unit))
            return CcOnlyMarksLeft(seen, true);
    }
    return CcOnlyMarksLeft(seen, false);
}

bool CcGuard::OnlyMarksLeftCached()
{
    if (onlyMarksLeft < 0)
        onlyMarksLeft = OnlyCcMarksLeft() ? 1 : 0;
    return onlyMarksLeft == 1;
}

bool CcGuard::IsHeldCcMark(Unit* unit)
{
    if (!unit || !IsCcTarget(unit))
        return false;
    return !OnlyMarksLeftCached();
}

bool CcGuard::IsOwnReadyCcMark(Unit* unit)
{
    // Cheap test first: only this bot's own assigned mark qualifies.
    if (!unit || ai->GetAiObjectContext()->GetValue<Unit*>("rti cc target")->Get() != unit)
        return false;
    return IsCcBotReadyFor(ai, ai->GetBot(), unit);
}

bool CcGuard::ProtectedEnemyNear(WorldObject* center, float radius, bool includeMarks, Unit* skip)
{
    if (!center)
        return false;
    Player* bot = ai->GetBot();
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !CcGuardCountsUnit(unit == skip) || !sServerFacade.IsAlive(unit) ||
            !center->IsWithinDistInMap(unit, radius, false))
            continue;
        // Cheapest test first; each later one only runs when the earlier
        // ones did not already protect the unit.
        bool const breakable = PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot);
        bool const held = !breakable && includeMarks && IsHeldCcMark(unit);
        // This bot's own mark, which it will (re)CC: never splash it, held
        // or free (build 11: the mage's own Flamestrike hit its Moon mob).
        bool const ownMark = !breakable && !held && IsOwnReadyCcMark(unit);
        if (CcAreaSpellBlocked(breakable, held, ownMark, ownMark && OnlyMarksLeftCached()))
            return true;
    }
    return false;
}

bool CcGuard::ReadyCcMarkNear(WorldObject* center, float radius)
{
    Group* group = ai->GetBot()->GetGroup();
    if (!center || !group)
        return false;
    // Once only CC marks are left they are killed, not CC'd: Blade Flurry
    // is allowed again. Checked once, before the scan (the cached answer
    // does not change inside it).
    if (OnlyMarksLeftCached())
        return false;
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit) || !center->IsWithinDistInMap(unit, radius, false) ||
            !IsCcMark(unit))
            continue;
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* player = sObjectMgr.GetPlayer(itr->guid);
            if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) ||
                player->GetMapId() != unit->GetMapId())
                continue;
            PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(player);
            if (memberAi && PAI_VALUE(Unit*, "rti cc target") == unit &&
                IsCcBotReadyFor(memberAi, player, unit))
                return true;
        }
    }
    return false;
}

bool FindNonCcTargetStrategy::IsCrowdControlled(PlayerbotAI* ai, Unit* unit)
{
    if (!unit)
        return false;
    return PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, ai->GetBot()) ||
        sServerFacade.IsFeared(unit) || ai->HasAura("banish", unit);
}

bool FindNonCcTargetStrategy::IsCcSpell(SpellEntry const* info)
{
    if (!info)
        return false;
    uint32 const ccMask = (1 << (MECHANIC_POLYMORPH - 1)) | (1 << (MECHANIC_BANISH - 1)) |
        (1 << (MECHANIC_FEAR - 1)) | (1 << (MECHANIC_SLEEP - 1)) | (1 << (MECHANIC_KNOCKOUT - 1)) |
        (1 << (MECHANIC_SHACKLE - 1));
    return (info->GetAllSpellMechanicMask() & ccMask) != 0;
}

bool FindNonCcTargetStrategy::IsCastingCcAt(Unit* caster, Unit* target)
{
    Spell* spell = caster ? caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    if (!spell || !spell->m_spellInfo)
        return false;
    // Compare guids: the caster's cached unit-target pointer can point at a unit the map freed this tick.
    if (spell->m_targets.getUnitTargetGuid() != (target ? target->GetObjectGuid() : ObjectGuid()))
        return false;
    return IsCcSpell(spell->m_spellInfo);
}

bool CcGuard::PartyDotOn(Unit* unit)
{
    if (!unit)
        return false;
    Player* const bot = ai->GetBot();
    Group* const group = bot->GetGroup();
    for (uint32 const type : kPeriodicDamageAuraTypes)
        for (Aura const* aura : unit->GetAurasByType(static_cast<AuraType>(type)))
        {
            Unit* const caster = aura ? aura->GetCaster() : nullptr;
            if (!caster)
                continue;
            Player* const owner = caster->GetCharmerOrOwnerPlayerOrPlayerItself();
            if (owner && (owner == bot || (group && group->IsMember(owner->GetObjectGuid()))))
                return true;
        }
    return false;
}

bool CcGuard::IsCcMarkedUnit(Unit* unit)
{
    return unit && IsCcMark(unit);
}

bool CcGuard::IsCrowdControlledUnit(Unit* unit)
{
    return unit && IsCrowdControlled(ai, unit);
}

bool CcGuard::BreakableCcNear(float x, float y, float z, uint32 mapId, float radius)
{
    Player* const bot = ai->GetBot();
    std::list<ObjectGuid> const possible =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets no los")->Get();
    return ai::AnyBreakableCcInArea(possible,
        [this](ObjectGuid const& guid) { return ai->GetUnit(guid); },
        [](Unit* unit) { return unit->IsAlive(); },
        [](Unit* unit, uint32 map) { return unit->GetMapId() == map; },
        [](Unit* unit, float px, float py, float pz) { return unit->GetDistance(px, py, pz); },
        [this, bot](Unit* unit) { return PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot); },
        mapId, x, y, z, radius);
}

bool CcGuard::BreakableCcInArea(SpellEntry const* info, Unit* spellTarget)
{
    if (!info || !spellTarget)
        return false;
    bool atTarget = false;
    float radius = 0.0f;
    if (!EnemyAreaOf(info, atTarget, radius) || radius <= 0.0f)
        return false;
    // A ground area or a chain lands on the spell's target; a caster-centred
    // area or a cone is judged around this bot.
    WorldObject* const center = atTarget ? static_cast<WorldObject*>(spellTarget) : ai->GetBot();
    if (!center || !center->IsInWorld())
        return false;
    return BreakableCcNear(center->GetPositionX(), center->GetPositionY(), center->GetPositionZ(),
        center->GetMapId(), radius);
}

bool CcGuard::CcInRunningArea(Spell const* spell)
{
    if (!spell || !spell->m_spellInfo)
        return false;
    bool atTarget = false;
    float radius = 0.0f;
    if (!EnemyAreaOf(spell->m_spellInfo, atTarget, radius) || radius <= 0.0f)
        return false;
    Player* const bot = ai->GetBot();
    float x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    uint32 mapId = bot->GetMapId();
    if (atTarget)
    {
        // Where the effect runs: the ground destination the cast was given, or
        // the unit a chain jumps from (a chain has no destination).
        if (spell->m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
            spell->m_targets.getDestination(x, y, z);
        else
        {
            // The cached unit-target pointer is refreshed only in the caster's next Spell::update: resolve the guid the
            // cast holds, and a target that already left the world leaves the area at the bot's own position.
            ObjectGuid const targetGuid = spell->m_targets.getUnitTargetGuid();
            Unit* const target = targetGuid ? ObjectAccessor::GetUnit(*bot, targetGuid) : nullptr;
            if (target)
            {
                x = target->GetPositionX();
                y = target->GetPositionY();
                z = target->GetPositionZ();
                mapId = target->GetMapId();
            }
        }
    }
    return BreakableCcNear(x, y, z, mapId, radius);
}

bool CcGuard::GroupCastsCcAt(Unit* unit)
{
    Group* group = unit ? ai->GetBot()->GetGroup() : nullptr;
    if (!group)
        return unit && IsCastingCcAt(ai->GetBot(), unit);
    for (Group::MemberSlotList::const_iterator itr = group->GetMemberSlots().begin(); itr != group->GetMemberSlots().end(); ++itr)
    {
        Player* player = sObjectMgr.GetPlayer(itr->guid);
        if (player && player->GetMapId() == unit->GetMapId() && IsCastingCcAt(player, unit))
            return true;
    }
    return false;
}

bool FindNonCcTargetStrategy::IsCcTarget(Unit* attacker)
{
    // Mark first: the protection test only runs for the one or two marks.
    return IsCcMark(attacker) && IsCcProtected(attacker);
}

bool FindNonCcTargetStrategy::IsCcProtected(Unit* unit)
{
    // CcMarkHeld is monotone: fill the cheap facts first and stop as soon
    // as the mark is held.
    CcMarkHoldState state;
    state.crowdControlled = IsCrowdControlled(ai, unit);
    // A mark of an unpulled pack stays untouched until the pull.
    state.inCombat = sServerFacade.IsInCombat(unit);
    if (CcMarkHeld(state))
        return true;

    Group* group = ai->GetBot()->GetGroup();
    if (!group)
        return false;

    Creature* creature = unit->ToCreature();
    if (creature)
        state.combatSeconds = static_cast<long>(creature->GetCombatTime());
    bool const inWindow = state.combatSeconds >= 0 && state.combatSeconds <= CcMarkHoldSeconds;

    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player* player = sObjectMgr.GetPlayer(itr->guid);
        if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) || player->GetMapId() != unit->GetMapId())
            continue;

        // The sheep is on its way: do not hit (or drop the target of) the
        // mob under a group member's CC cast bar.
        state.ccCastBarOnIt = IsCastingCcAt(player, unit);

        // Start of the fight: the mark's CC bot gets the hold window to
        // land the first CC if it can CC the mob now.
        if (inWindow)
        {
            PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(player);
            state.ccBotReady = memberAi && PAI_VALUE(Unit*, "rti cc target") == unit &&
                IsCcBotReadyFor(memberAi, player, unit);
        }

        if (CcMarkHeld(state))
            return true;
    }

    return false;
}

bool FindNonCcTargetStrategy::IsCcBotReadyFor(PlayerbotAI* memberAi, Player* player, Unit* unit)
{
    // The CC bot: "cc" on, knows a cast-time CC spell that fits the mob's
    // creature type, off cooldown, and within its spell range (+ slack for
    // a mob running in). The caller checks that the mark is its "rti cc
    // target".
    if (!memberAi || !player || !unit || !memberAi->HasStrategy("cc", BotState::BOT_STATE_COMBAT))
        return false;
    if (!player->IsWithinDistInMap(unit, memberAi->GetRange("spell") + CcGraceRangeSlack))
        return false;
    for (char const* spellName : GraceCcSpells)
    {
        uint32 spellId = PAI_VALUE2(uint32, "spell id", spellName);
        if (!spellId || !memberAi->HasSpell(spellId) || player->HasSpellCooldown(spellId))
            continue;
        SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
        if (!info || (info->TargetCreatureType && !(info->TargetCreatureType & unit->GetCreatureTypeMask())))
            continue;
        return true;
    }
    return false;
}

bool FindNonCcTargetStrategy::IsCcMark(Unit* attacker)
{
    Group* group = ai->GetBot()->GetGroup();
    if (group)
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *player = sObjectMgr.GetPlayer(itr->guid);
            if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player))
                continue;

            if (PlayerbotAIStorage::Instance().GetAI(player))
            {
                if (PAI_VALUE(Unit*,"rti cc target") == attacker)
                    return true;

                std::string rti = PAI_VALUE(std::string,"rti cc");
                int index = RtiTargetValue::GetRtiIndex(rti);
                if (index != -1)
                {
                    uint64 guid = group->GetTargetIcon(index);
                    if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
                        return true;
                }
            }
        }

        uint64 guid = group->GetTargetIcon(4);
        if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
            return true;
    }

    return false;
}

void FindTargetStrategy::GetPlayerCount(Unit* creature, int* tankCount, int* dpsCount)
{
    Player* bot = ai->GetBot();
    if (tankCountCache.find(creature) != tankCountCache.end())
    {
        *tankCount = tankCountCache[creature];
        *dpsCount = dpsCountCache[creature];
        return;
    }

    *tankCount = 0;
    *dpsCount = 0;

    Unit::AttackerSet attackers(creature->GetAttackers());
    for (std::set<Unit*>::const_iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* attacker = *i;
        if (!attacker || !sServerFacade.IsAlive(attacker) || attacker == bot)
            continue;

        Player* player = dynamic_cast<Player*>(attacker);
        if (!player)
            continue;

        if (ai->IsTank(player))
            (*tankCount)++;
        else
            (*dpsCount)++;
    }

    tankCountCache[creature] = *tankCount;
    dpsCountCache[creature] = *dpsCount;
}

TravelTarget* LeaderTravelTargetValue::Calculate()
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    Player* player = ai->GetGroupMaster();
    if (!player || player == bot || !PlayerbotAIStorage::Instance().GetAI(player) || !ai->IsSafe(player))
        return target;

    if (bot->GetGroup() && !ai->IsGroupLeader())
        if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
            !ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) &&
            !ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            return target;

     TravelTarget* leaderTarget = PAI_VALUE(TravelTarget*, "travel target");

     if (!leaderTarget)
         return target;

     return leaderTarget;
}

WorldPosition LastLongMoveValue::Calculate()
{
    LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");

    if (lastMove.lastPath.empty())
        return WorldPosition();

    return lastMove.lastPath.GetBack();
}

WorldPosition HomeBindValue::Calculate()
{
    return WorldPosition(bot->GetHomeBindLocation());
}

std::string HomeBindValue::Format()
{
    WorldPosition pos = this->Calculate();
    return chat->formatWorldPosition(pos);
}

void PullTargetValue::Set(Unit* unit)
{
    guid = unit ? unit->getObjectGuid() : ObjectGuid();
}

Unit* PullTargetValue::Get()
{
    Unit* unit = nullptr;
    if (!guid.IsEmpty())
    {
        unit = sObjectAccessor.GetUnit(*bot, guid);
    }

    return unit;
}

Unit* FollowTargetValue::Calculate()
{
    Unit* followTarget = AI_VALUE(GuidPosition, "manual follow target").GetUnit(bot->GetInstanceId());
    if (followTarget == nullptr)
    {
        Formation* formation = AI_VALUE(Formation*, "formation");
        if (formation && !formation->GetTargetName().empty())
        {
            followTarget = AI_VALUE(Unit*, formation->GetTargetName());
        }
        else
        {
            followTarget = AI_VALUE(Unit*, "master target");
        }
    }

    return followTarget;
}

Unit* ClosestAttackerTargetingMeTargetValue::Calculate()
{
    Unit* result = nullptr;
    float closest = 9999.0f;

    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers targeting me");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker)
        {
            const float distance = bot->GetCombatDistance(attacker);
            if (distance < closest)
            {
                closest = distance;
                result = attacker;
            }
        }
    }

    return result;
}

std::list<ObjectGuid> FriendlyManualTargetsValue::Get()
{
    value.remove_if([&](const ObjectGuid& playerGuid)
    {
        Unit* player = ai->GetUnit(playerGuid);
        if (ai->IsSafe(player))
        {
            Player* playerTarget = dynamic_cast<Player*>(player);
            if (playerTarget && IsInGroup_Helper(bot, playerTarget))
            {
                return false;
            }
        }

        return true;
    });

    return value;
}

std::list<ObjectGuid> FriendlyManualTargetsValue::LazyGet()
{
    return Get();
}
