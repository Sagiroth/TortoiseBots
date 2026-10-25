#include "playerbot/playerbot.h"
#include "KriCloudActions.h"
#include "Objects/DynamicObject.h"
#include "playerbot/strategy/values/PositionValue.h"

using namespace ai;

std::list<HazardPosition> ai::GetKriCloudHazards(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 531 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !ai->HasStrategy("kri cloud", BotState::BOT_STATE_COMBAT))
        return {};

    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    BotState const state = bot->IsInCombat() ? BotState::BOT_STATE_COMBAT : BotState::BOT_STATE_NON_COMBAT;
    if (ai->HasStrategy("stay", state) || ai->HasStrategy("follow", state) ||
        ai->HasStrategy("wait for attack", state) || ai->HasStrategy("grind", state) ||
        (hold != positions.end() && hold->second.isSet()))
        return {};

    std::list<HazardPosition> hazards;
    // The host's nearest-dynamic-object value is empty. Resolve the actual
    // cloud through Kri's corpse, using the existing bounded corpse GUID cache.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest corpses"))
    {
        Unit* caster = ai->GetUnit(guid);
        if (!caster || caster->GetEntry() != 15511 || caster->IsAlive() || !caster->IsInMap(bot))
            continue;
        DynamicObject* cloud = caster->GetDynObject(25786);
        if (!cloud || !cloud->IsInWorld() || !cloud->IsInMap(bot) || !cloud->GetDuration())
            continue;
        float const radius = cloud->GetRadius();
        if (!std::isfinite(radius) || radius <= 0.0f)
            continue;
        // Use the live object, not a guessed duration or the corpse's location.
        hazards.emplace_back(WorldPosition(cloud), radius + 2.0f);
    }
    return hazards;
}

bool ai::KriCloudTooClose(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot)
        return false;
    WorldPosition const start(bot);
    for (HazardPosition const& hazard : GetKriCloudHazards(ai))
        if (start.distance(hazard.first) <= hazard.second)
            return true;
    return false;
}

bool KriCloudEscapeAction::Execute(Event& event)
{
    // Recheck manual holds and live cloud state after the trigger was evaluated.
    if (!KriCloudTooClose(ai))
        return false;
    // Refresh at execution: a corpse despawn can remove its cloud between
    // the trigger and the movement action.
    RESET_AI_VALUE(std::list<HazardPosition>, "hazards");
    return MoveAwayFromHazard::Execute(event);
}
