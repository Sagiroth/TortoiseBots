#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Shared character-deletion primitives for the two paths that wipe a
// module-owned character: the managed random-bot pool reset (issue #265) and
// the dismissal of a hired companion (issue #192 follow-up).
//
// The core's Player::DeleteFromDB removes the character, its group, guild and
// petition membership, pets, items, social entries and mail, but it does NOT
// touch auction listings, and it knows nothing about the module's own tables.
// Both callers therefore settle auctions first (refunding live bids through
// the core's normal cancel mail) and then clear the module rows.

class AuctionEntry;
class AuctionHouseObject;

namespace TortoiseBots
{

struct OwnedAuction
{
    AuctionHouseObject* house = nullptr;
    AuctionEntry* auction = nullptr;
};

// Every in-memory listing owned by the character, across all auction houses.
void CollectOwnedAuctions(uint32_t guidLow, std::vector<OwnedAuction>& out);

// True (and count > 0) when the character still owns an in-memory listing.
bool CharacterOwnsAuctions(uint32_t guidLow, uint32_t& count);

// Refunds the bidder of every owned listing through the core's cancel-mail
// path, destroys the listed items and removes the listings. Returns false with
// `error` set when a bid cannot be resolved to its owner: the caller must then
// leave the character untouched and retry, because that bid would otherwise be
// lost without a refund. A hardcore bidder is a deliberate no-refund (the core
// skips them the same way).
bool SettleCharacterAuctions(uint32_t guidLow, uint32_t& settled, std::string& error);

// Core character deletion plus the module-owned per-character rows the core
// does not know about: the durable ownership row, the hire ledger row, the
// stored AI state and the armory snapshot.
void DeleteCharacterEverywhere(uint32_t guidLow, uint32_t accountId);

} // namespace TortoiseBots
