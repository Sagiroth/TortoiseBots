#pragma once

#include <cstdint>

namespace TortoiseBots
{
// Fresh-bot gate for initial gear seeding.
//
// Initial gear (PlayerbotFactory enrichment) is for freshly created pool bots
// only — it must never overwrite earned progression gear. Facade values are
// process-local, so freshness is a dual heuristic evaluated by the caller:
//   - seededMark: sRandomBotFacade.GetValue(guidLow, "seeded"); covers repeat
//     logins and timer ticks within one process lifetime.
//   - playedTimeSeconds: Player::GetTotalPlayedTime(); persisted in the
//     characters table, so it covers mangosd restarts that wipe facade values.
//
// Truth table:
//   played == 0, unseeded -> seed (fresh pool bot)
//   played == 0, seeded   -> skip (already seeded this process)
//   played >  0, unseeded -> skip (veteran after a restart; gear is earned)
//   played >  0, seeded   -> skip (established bot)
inline bool NeedsInitialGearSeeding(std::uint32_t playedTimeSeconds, std::uint32_t seededMark)
{
    return playedTimeSeconds == 0 && seededMark == 0;
}
}
