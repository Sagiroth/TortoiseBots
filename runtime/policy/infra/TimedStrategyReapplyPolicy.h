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
// Two holds, either one skips the re-apply:
//
//  - A bot in a group with a real player's character, online or not. The party
//    is the player's and the fill's, not the roulette's. This is the hold that
//    covers a bot whose natural kit already matched its LFT slot: the fill
//    assigns no forced role there, so the role check alone let exactly that
//    bot through. An offline member has no session to inspect, so its
//    character's account decides (TimedReapplyMemberIsRealPlayer); the group
//    slot list keeps the guid either way.
//  - A bot carrying a forced role (PlayerbotAI::m_forcedRole) while grouped.
//    The LFT fill, `.bot role`, the hire provisioner and the 7.3 shaman tank
//    factory set it; PlayerbotAI::ResetStrategies is the one place that
//    applies it, and it stays the standing order until the bot leaves the
//    group (LeaveGroupAction/LftBotFillService clear the role).
//
// A solo bot, and a grouped bot with no forced role in an all-bot group,
// keep today's behaviour exactly.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_timed_strategy_reapply_policy.cpp).

#include <cstdint>

namespace TortoiseBots
{

struct TimedReapplyInputs
{
    uint8_t forcedRole = 0;          // PlayerbotAI::GetForcedRole(); 0 = none
    bool inGroup = false;            // the bot's Player::GetGroup() is set
    bool groupHasRealPlayer = false; // a real player's character is in that group (implies inGroup)
};

// True when the timed strategy re-apply must be skipped: the bot stands in a
// player's party, or it holds a forced role that its group still honours, so
// its kit stays the one the group and the role built.
inline bool TimedReapplyHoldsRole(TimedReapplyInputs const& in)
{
    return in.groupHasRealPlayer || (in.forcedRole != 0 && in.inGroup);
}

// One group member slot, as the group slot list and the character cache can
// describe it. `self` is the bot whose re-apply is in question.
struct TimedReapplyMember
{
    bool self = false;
    bool online = false;    // a live Player object resolved for the slot
    bool hasClient = false; // online only: PlayerbotAI::IsRealPlayer (network transport)
    bool hasAccount = false; // offline only: the character cache knows the account
    bool botAccount = false; // offline only: the account belongs to the module's bot infrastructure
};

// True when the member is a real player's character. An online character is
// judged by its transport: a headless bot has no network client, so an
// all-bot group stays the pool's even while its bots are online. An offline
// character has no session to ask, so its account decides: any account the
// module does not own as bot infrastructure belongs to a human, and the party
// is that human's run. A slot the character cache cannot resolve is not
// counted, since its account flag is unknown.
inline bool TimedReapplyMemberIsRealPlayer(TimedReapplyMember const& m)
{
    if (m.self)
        return false;
    if (m.online)
        return m.hasClient;
    return m.hasAccount && !m.botAccount;
}

} // namespace TortoiseBots
