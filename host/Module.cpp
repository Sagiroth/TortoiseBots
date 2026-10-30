#include "Module.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotChatAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotAddonAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotHostAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "LftFillAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotLfgAdapter.h"
#include "BotGearAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotPacketAdapter.h"
#include "RaidBossDiagnostics.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotPlayerAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "HireRecruiterAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "HireGroupAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ClaimGuildAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldBuffKillAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldBuffAuraAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldBuffPvpAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldBuffRaidAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "HireUnlockKillAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "CombatMonitorAdapter.h"

namespace TortoiseBots {

void RegisterScripts()
{
    new BotHostAdapter();
    new LftFillAdapter();
    new BotLfgAdapter();
    new BotPacketAdapter();
    new BotGearAdapter();
    new RaidBossDiagnostics();
    new BotPlayerAdapter();
    new BotUnitAdapter();
    new BotChatAdapter();
    new BotAddonAdapter();
    // Issue #192: on-demand companion hiring (module-only gossip + group hooks).
    new HireRecruiterAdapter();
    new HireGroupAdapter();
    // Issue #489: guild departure / disband hooks for claimed bots.
    new ClaimGuildAdapter();
    // Issue #492: Onyxia/Nefarian -> invisible Rally credit for the group.
    new WorldBuffKillAdapter();
    // Issue #492: aura-gain unlocks (receiver only, recruiter casters out).
    new WorldBuffAuraAdapter();
    // Issue #492: Silithyst opposite-faction kills in Silithus.
    new WorldBuffPvpAdapter();
    // Issue #492: keep-buffs restore + Upper Kara strip.
    new WorldBuffRaidAdapter();
    // Issue #490 PR 2: hire-unlock boss kills, counters and contracts page.
    new HireUnlockKillAdapter();
    new CombatMonitorUnitAdapter();
    new CombatMonitorCastAdapter();
}

} // namespace TortoiseBots
