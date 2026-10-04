#pragma once

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
// earned needAmmo = 8/2 restock path. The BuyAction 1-stack-per-visit ammo
// cap is likewise cheat-only; quiver flips are gated in BuyAction via the
// AH-flip flag (the equip path already returns NONE for non-hunters).
inline bool SuppressAmmoBuy(bool hasItemCheat) { return hasItemCheat; }
}  // namespace ai
