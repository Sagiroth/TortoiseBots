#pragma once

#include <cstdint>

// Pool item-cheat ammo policy (task H): with AiPlayerbot.RndBotCheats=item
// the per-tick refill (PlayerbotAI.cpp) tops the equipped ammo stack back up,
// so firing never consumes anything and vendor ammo is never a restock - it
// only burns the trainer purse. Live pool 2026-10-03: same-bot same-arrow
// batches up to 10 per vendor visit (the BuyAction n<10 loop re-firing).
// Core-free so the standalone g++ policy test can include it; callers pass
// the already-resolved cheat answer.

namespace ai
{
// True when the ammo restock demand (ITEM_USAGE_AMMO) must be skipped: the
// cheat refills the stack every tick, so there is nothing to restock. The
// EQUIP checks (empty slot / better ammo) run before this gate, so cheat
// bots still equip ammo. Bots without the cheat (owned/hired) keep the
// earned needAmmo = 8/2 restock path. BuyAction caps the rest with
// VendorAmmoBuyAllowed below; vendor->AH flips (quivers included) are never
// bought.
inline bool SuppressAmmoBuy(bool hasItemCheat) { return hasItemCheat; }

// Per-bot cooldown between two vendor ammo buys of an item-cheat bot. The
// rpg buy re-fires every few seconds at the same vendor, so a per-visit cap
// still bought a stack each time (live pool: 17 stacks in 2 min).
constexpr int64_t CHEAT_AMMO_BUY_COOLDOWN_SECONDS = 5 * 60;

// True when BuyAction may buy one more stack of this vendor ammo (arrows,
// bullets, thrown). restockUsage is the ITEM_USAGE_AMMO answer, which
// already compares carried stacks with the 8/2 need; any other answer
// (EQUIP: empty ammo slot / better tier) is met from a stack the bot already
// carries. cheatReadyAtSec is the stored "cheat ammo buy ready" time.
inline bool VendorAmmoBuyAllowed(bool restockUsage, float carriedStacks, bool hasItemCheat,
                                 int64_t nowSec, int64_t cheatReadyAtSec)
{
    if (!restockUsage && carriedStacks > 0.0f)
        return false;
    if (hasItemCheat && nowSec < cheatReadyAtSec)
        return false;
    return true;
}
}  // namespace ai
