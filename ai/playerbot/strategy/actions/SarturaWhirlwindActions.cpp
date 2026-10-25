#include "playerbot/playerbot.h"
#include "SarturaWhirlwindActions.h"
#include "playerbot/strategy/values/PositionValue.h"

using namespace ai;

std::list<HazardPosition> ai::GetSarturaWhirlwindHazards(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 531 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() ||
        !ai->HasStrategy("sartura whirlwind", BotState::BOT_STATE_COMBAT))
        return {};

    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet()))
        return {};

    std::list<HazardPosition> hazards;
    // Resolve live positions and auras from the existing possible-target GUID cache.
    // Do not use damage-target eligibility: crowd control does not remove an aura.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
    {
        Unit* caster = ai->GetUnit(guid);
        if (!caster || !caster->IsAlive() || !caster->IsInCombat() || !caster->IsInMap(bot))
            continue;
        uint32 const aura = caster->GetEntry() == 15516 ? 26083 :
            (caster->GetEntry() == 15984 ? 26038 : 0);
        if (!aura || !caster->HasAura(aura))
            continue;
        SpellEntry const* parent = sSpellTemplate.LookupEntry<SpellEntry>(aura);
        SpellEntry const* damage = parent ?
            sSpellTemplate.LookupEntry<SpellEntry>(parent->EffectTriggerSpell[0]) : nullptr;
        if (!damage)
            continue;
        float const radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(damage->EffectRadiusIndex[0]));
        if (!std::isfinite(radius) || radius <= 0.0f)
            continue;
        // Match the core's caster-centered area radius, plus a modest response margin.
        // Core leeway is currently zero for NPCs; keep the same query as spell targeting.
        hazards.emplace_back(WorldPosition(caster), radius + caster->GetLeewayBonusRadius() + 2.0f);
    }
    return hazards;
}

bool ai::SarturaWhirlwindTooClose(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot)
        return false;
    WorldPosition const start(bot);
    for (HazardPosition const& hazard : GetSarturaWhirlwindHazards(ai))
        if (start.distance(hazard.first) <= hazard.second)
            return true;
    return false;
}

bool SarturaWhirlwindEscapeAction::Execute(Event& event)
{
    // Recheck manual holds and live aura state after the trigger was evaluated.
    if (!SarturaWhirlwindTooClose(ai))
        return false;
    // Whirlwind follows a moving NPC: refresh the shared geometry at execution,
    // rather than following the hazard value's previous one-second snapshot.
    RESET_AI_VALUE(std::list<HazardPosition>, "hazards");
    return MoveAwayFromHazard::Execute(event);
}
