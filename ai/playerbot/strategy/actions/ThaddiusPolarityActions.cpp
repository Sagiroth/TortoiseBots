#include "playerbot/playerbot.h"
#include "ThaddiusPolarityActions.h"
#include "Movement/spline/MoveSplineInit.h"
#include "playerbot/policy/raid/ThaddiusPolarityPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace ai;

namespace
{
bool HasThaddiusHold(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    bool const ordered = (order && PossibleTargetsValue::IsValid(order, ai->GetBot(), true)) ||
        AI_VALUE(Unit*, "rti target");
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return ordered || ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
}

ThaddiusPolaritySide GetSide(Player* player)
{
    return player ? GetThaddiusPolaritySide(player->HasAura(28059), player->HasAura(28084)) :
        ThaddiusPolaritySide::None;
}

bool PathClearsOppositeCharges(Player* bot, WorldPosition const& start,
                               std::vector<WorldPosition> const& path)
{
    std::vector<Player*> opposites;
    ThaddiusPolaritySide const side = GetSide(bot);
    Map::PlayerList const& players = bot->GetMap()->GetPlayers();
    for (const auto& itr : players)
    {
        Player* other = itr.getSource();
        if (other && other != bot && other->IsAlive() && GetSide(other) != ThaddiusPolaritySide::None &&
            GetSide(other) != side)
            opposites.push_back(other);
    }

    WorldPosition previous = start;
    for (WorldPosition const& next : path)
    {
        for (Player* other : opposites)
            if (!ThaddiusSegmentClearsOpposite({previous.getX(), previous.getY()},
                    {next.getX(), next.getY()}, {other->GetPositionX(), other->GetPositionY()}))
                return false;
        previous = next;
    }
    for (Player* other : opposites)
        if (std::hypot(previous.getX() - other->GetPositionX(),
                       previous.getY() - other->GetPositionY()) < THADDIUS_POLARITY_OPPOSITE_CLEARANCE)
            return false;
    return true;
}
}

bool ai::HasThaddiusPolarity(Player* player)
{
    return GetSide(player) != ThaddiusPolaritySide::None;
}

Unit* ai::GetActiveThaddius(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!bot || bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() ||
        !ai->HasStrategy("thaddius polarity", BotState::BOT_STATE_COMBAT) || ai->IsHeal(bot) || HasThaddiusHold(ai) ||
        !HasThaddiusPolarity(bot))
        return nullptr;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15928 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot))
            return boss;
    }
    return nullptr;
}

bool ai::ShouldHoldThaddiusPolarityPosition(PlayerbotAI* ai)
{
    Unit* boss = GetActiveThaddius(ai);
    if (!boss)
        return false;
    Player* bot = ai->GetBot();
    auto const point = GetThaddiusPolarityPoint(boss->GetPositionX(), boss->GetPositionY(), GetSide(bot));
    bool const needsMove = ThaddiusPolarityNeedsMove(bot->GetPositionX(), bot->GetPositionY(), point);
    if (needsMove)
        return false;
    for (auto const& itr : bot->GetMap()->GetPlayers())
    {
        Player* other = itr.getSource();
        if (other && other != bot && other->IsAlive() && GetSide(other) != ThaddiusPolaritySide::None &&
            GetSide(other) != GetSide(bot) &&
            std::hypot(bot->GetPositionX() - other->GetPositionX(),
                       bot->GetPositionY() - other->GetPositionY()) < THADDIUS_POLARITY_OPPOSITE_CLEARANCE)
            return ShouldHoldThaddiusPosition(needsMove, false);
    }
    return ShouldHoldThaddiusPosition(needsMove, true);
}

bool ThaddiusPolarityStackAction::isUseful()
{
    Unit* boss = GetActiveThaddius(ai);
    if (!MovementAction::isUseful() || !boss)
        return false;
    ThaddiusPolarityPoint const point = GetThaddiusPolarityPoint(boss->GetPositionX(), boss->GetPositionY(), GetSide(bot));
    return ThaddiusPolarityNeedsMove(bot->GetPositionX(), bot->GetPositionY(), point);
}

bool ThaddiusPolarityStackAction::Execute(Event& /*event*/)
{
    Unit* boss = GetActiveThaddius(ai);
    if (!boss || bot->GetTransport() || bot->IsTaxiFlying())
        return false;

    ThaddiusPolarityPoint const destination =
        GetThaddiusPolarityPoint(boss->GetPositionX(), boss->GetPositionY(), GetSide(bot));
    if (!ThaddiusPolarityNeedsMove(bot->GetPositionX(), bot->GetPositionY(), destination))
        return true;

    WorldPosition const start(bot);
    WorldPosition point(bot->GetMapId(), destination.x, destination.y, start.getZ());
    point.setZ(point.GetHeight());
    if (!IsMovingAllowed(point.GetMapId(), point.getX(), point.getY(), point.getZ()) ||
        !std::isfinite(point.getZ()) || std::fabs(point.getZ() - start.getZ()) > 5.0f ||
        IsHazardNearPosition(point) ||
        !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()))
        return false;

    auto path = start.GetPathTo(point, bot);
    if (!point.isPathTo(path) || !PathClearsOppositeCharges(bot, start, path))
        return false;

    // Launch this exact route. MoveTo may reuse lastPath, shortcut it or bend it
    // around hazards, invalidating the charge check above. Stop first because
    // the core replaces spline point zero with the current stopped position.
    bot->HandleEmoteState(0);
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    if (bot->IsNonMeleeSpellCasted(true, false, true))
        ai->InterruptSpell(false);
    ai->StopMoving();
    WorldPosition const stopped(bot);
    path.insert(path.begin(), stopped);
    if (path.size() < 2 || !PathClearsOppositeCharges(bot, stopped, path))
        return false;

    auto const points = WorldPosition().toPointsArray(path);
    Movement::MoveSplineInit init(*bot, "ThaddiusPolarityStack");
    init.MovebyPath(points);
    init.SetWalk(false);
    if (!init.Launch())
        return false;
    AI_VALUE(LastMovement&, "last movement").lastPath.clear();
    ClearIdleState();
    WaitForReach(points);
    return true;
}
