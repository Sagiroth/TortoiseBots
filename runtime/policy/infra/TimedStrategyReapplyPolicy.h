#pragma once

// Issue #1605: the timed RandomBotCombatStrategies/RandomBotNonCombatStrategies
// re-apply (RandomBotService::Update -> RandomBotFacade::ChangeStrategy every
// Min..MaxRandomBotChangeStrategyTime) belongs to free pool bots. It skipped a
// Dungeon lease holder and never runs for hires, but it did run for a pool bot
// the dungeon finder had filled into a real player's group: its "+dps" is the
// class DPS alias (shadow for a priest), and Engine::addStrategy drops the
// sibling spec, so the heal slot the LFT fill handed the bot ("LFT: <name>
// strategies rebuilt as heal") was gone mid-run - the priest never healed
// again. With the shipped list its "+dps assist" still undid a player's own
// `.bot co -dps assist`.
//
// The bot carries that role in PlayerbotAI::m_forcedRole (set by the LFT fill,
// `.bot role`, the hire provisioner and the 7.3 shaman tank factory), and
// PlayerbotAI::ResetStrategies is the one place that re-applies it. While the
// bot is in a group - a player's party or a server-owned LFT group - the role
// is a standing order and the timed re-apply must leave the whole kit alone;
// once the bot leaves, LeaveGroupAction/LftBotFillService clear the role and
// the roulette resumes. A solo bot, and a grouped bot with no forced role,
// keep today's behaviour exactly.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_timed_strategy_reapply_policy.cpp).

#include <cstdint>

namespace TortoiseBots
{

struct TimedReapplyInputs
{
    uint8_t forcedRole = 0; // PlayerbotAI::GetForcedRole(); 0 = none
    bool inGroup = false;   // the bot's Player::GetGroup() is set
};

// True when the timed strategy re-apply must be skipped: the bot holds a
// forced role and is still grouped, so its kit stays the one the role built.
inline bool TimedReapplyHoldsRole(TimedReapplyInputs const& in)
{
    return in.forcedRole != 0 && in.inGroup;
}

} // namespace TortoiseBots
