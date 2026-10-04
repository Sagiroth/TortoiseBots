#pragma once

// Pool item-cheat vendor policy (task H): with AiPlayerbot.RndBotCheats=item
// the per-tick refill (PlayerbotAI.cpp) tops the equipped ammo stack back up,
// so firing never consumes anything and vendor ammo/quiver buys only burn the
// trainer purse. Live pool 2026-10-03: same-bot same-arrow batches up to 10
// per vendor visit (the BuyAction n<10 loop re-firing), 34 Small Quiver buys
// on one priest in 5 minutes. Core-free so the standalone g++ policy test
// can include it; callers pass the already-resolved cheat/class answers.

namespace ai
{
// True when a vendor-stock ammo buy (ITEM_USAGE_AMMO) must be skipped: the
// cheat refills the stack every tick, so there is nothing to restock. Bots
// without the cheat (owned/hired) keep the earned restock path.
inline bool SuppressAmmoBuy(bool hasItemCheat) { return hasItemCheat; }

// True when a non-hunter's vendor-stock quiver/ammo pouch must not read as
// a plain-bag upgrade: the container only holds ammo, so for a class without
// a ranged kit it is a strictly worse bag. Hunters keep their dedicated
// quiver path (attack speed + first-quiver seeding); no-cheat bots are
// untouched either way.
inline bool SuppressNonHunterQuiverBuy(bool hasItemCheat, bool isHunter)
{
    return hasItemCheat && !isHunter;
}
}  // namespace ai
