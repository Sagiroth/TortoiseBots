#pragma once

// Reaction engine fast path (ReactionEngine::HasReactionWork): when may a
// reaction pass be skipped? A bot with no live player master, full health and
// mana, no victim and no danger aura has nothing to react to, except a chat
// command that is still waiting in its queue (PlayerbotAI::HandleCommands
// drains that queue inside the pass). The old check looked at the master only,
// so a bound hire (the commander stands outside its group, #935) that was idle
// at full health never read `.bot command <bot> talents` or `spells ...`: the
// order sat in the queue and the run's pre-check got "no answer" in every run
// of 2026-10-17 (before the bind, with the hirer as master, it answered in
// 40 ms). Pure decision logic, unit-tested in tools/test_reaction_work_policy.cpp.

#include <cstddef>

namespace TortoiseBots
{

// True when the reaction pass must run whatever the bot's other state says.
inline bool ReactionPassForced(bool hasRealPlayerMaster, std::size_t queuedChatCommands)
{
    return hasRealPlayerMaster || queuedChatCommands > 0;
}

} // namespace TortoiseBots
