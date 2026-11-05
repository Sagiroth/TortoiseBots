#pragma once

// Enchant-order lifecycle: the state transitions the lead's guild action runs.
// Working/Waiting enchant orders gather through the existing craft ladder
// (W10 owns Ready and its whisper). This header answers what the action does
// once a Meet request exists or a trade window opens: capital teleport,
// Coming walk, the materials trade, the enchant trade, try budgets and the
// done check. Header-only and free of core types on purpose: the action feeds
// plain facts (states, distances, trade verdicts), so the standalone test
// never links the server. Caller mapping:
//   state            = the order's stored state (Working/Waiting/Ready/Coming/Enchanting)
//   needItems        = reason is needitems (Waiting) or the giver ticked bring=
//   traderGuid/ask   = live trade window / replace question state
//   verdicts         = ai::CheckEnchantTrade / CheckMaterialsTrade outcomes
//   fitsEnchant      = W3 EnchantFitsItem(spell, item)
// W10 owns the reagent list for the materials trade; W3 owns the enchant name
// for the replace question and the thanks line.

#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// What the lifecycle action does with a stored enchant row this tick. One
// outcome per tick; the action runs at most one world effect per pass.
enum class EnchantLifecycleStep : uint8_t
{
    Idle,          // no live enchant row, or nothing to do (bot tied up, waits elsewhere)
    Teleport,      // Working/Waiting row, bot out of capital, teleport now
    RetryTeleport, // teleport failed: try again on a later tick, not every tick
    ToComing,      // Ready (or Waiting needitems) with a live Meet: state Coming
    Walk,          // Coming: walk toward the giver
    OpenTrade,     // Coming in trade distance: open the trade, state Enchanting
    MeetExpired,   // Coming: giver left the capital or logged out: back to Ready/Waiting
    CheckTrades,   // Enchanting with a live window: run the per-update trade checks
    TimeoutTick,   // Enchanting: try budget or replace wait elapsed (cancel/retry or give up)
    Finish,        // enchant trade accepted: verify the enchant landed, state Done
    ToReady,       // enchant trade failed its check: back to Ready/Waiting
};

struct EnchantLifecycleFacts
{
    bool liveEnchantOrder = false; // unfinished enchant row on this bot
    bool gathering = false;        // Working, or Waiting for another reason than needitems
    bool botTiedUp = false;        // dungeon/raid/BG or grouped with a real player
    bool botInCapital = false;     // bot stands in its faction capital
    bool teleportOk = false;       // WaitReason == Ok (not combat/instance/group/dead/taxi)
    bool teleportFailed = false;   // last TeleportToCapitalInn call failed
    bool meetPending = false;      // PendingMeet has a giver stored
    bool giverOnline = false;      // meet giver is online
    bool giverInCapital = false;   // giver stands in the bot's capital
    bool tradeOpen = false;        // a trade window is open with anyone
    bool tradeOpenWithGiver = false;// the open window's trader is the giver
    bool atTradeDistance = false;  // bot within trade distance of the giver
    bool giverLeft = false;        // giver logged out or left the capital
};

// Working/Waiting rows teleport to the capital inn once (W10's ladder only
// names bank/AH legs inside the capital). Never while tied up, in combat or
// otherwise unwalkable: the row just waits.
inline EnchantLifecycleStep DecideEnchantGatherTeleport(EnchantLifecycleFacts const& facts)
{
    if (!facts.liveEnchantOrder || !facts.gathering || facts.botTiedUp)
        return EnchantLifecycleStep::Idle;
    if (facts.botInCapital)
        return EnchantLifecycleStep::Idle;
    if (facts.teleportFailed)
        return EnchantLifecycleStep::RetryTeleport;
    if (!facts.teleportOk)
        return EnchantLifecycleStep::Idle;
    return EnchantLifecycleStep::Teleport;
}

// Ready (or Waiting needitems) with a Meet press: the bot comes when the
// giver is online, stands in the capital, and the bot is free. A giver who
// left or logged out keeps the request; the Coming leg below expires it.
inline EnchantLifecycleStep DecideEnchantMeet(EnchantLifecycleFacts const& facts)
{
    if (!facts.liveEnchantOrder || facts.gathering || facts.botTiedUp)
        return EnchantLifecycleStep::Idle;
    if (!facts.meetPending || !facts.giverOnline || !facts.giverInCapital)
        return EnchantLifecycleStep::Idle;
    return EnchantLifecycleStep::ToComing;
}

// Coming leg: walk until close, open the trade once in range, expire the
// Meet when the giver is gone.
inline EnchantLifecycleStep DecideEnchantComing(EnchantLifecycleFacts const& facts)
{
    if (!facts.liveEnchantOrder)
        return EnchantLifecycleStep::Idle;
    if (facts.giverLeft)
        return EnchantLifecycleStep::MeetExpired;
    if (!facts.meetPending)
        return EnchantLifecycleStep::Idle;
    if (facts.atTradeDistance)
        return EnchantLifecycleStep::OpenTrade;
    return EnchantLifecycleStep::Walk;
}

// Which trade the open window is: materials while reagents are still
// missing (needitems or the giver ticked bring=), else the enchant trade.
inline bool EnchantWindowIsMaterialsTrade(bool needitems)
{
    return needitems;
}

// A trade window with anyone but the giver while an enchant order runs this
// leg is refused: the lead cancels it with the wrong-player whisper.
inline bool EnchantTradeWithStranger(bool tradeOpen, bool tradeOpenWithGiver)
{
    return tradeOpen && !tradeOpenWithGiver;
}

// Done check: after the enchant trade completes, the giver's item must carry
// the ordered enchant in its permanent slot before the row ends done. The
// caller resolves the parked enchant id; 0 (no item, empty slot) fails.
inline bool EnchantDoneVerified(uint32_t parkedEnchantId, uint32_t orderedEnchantId)
{
    return parkedEnchantId != 0 && parkedEnchantId == orderedEnchantId;
}

// Trade waiting counts toward neither the order time nor the no-progress
// limit: Ready, Coming and Enchanting pause the clocks the same way Waiting
// needitems already does.
inline bool EnchantStatePausesOrderClocks(bool isEnchantOrder, uint8_t stateReady,
    uint8_t stateComing, uint8_t stateEnchanting, uint8_t state)
{
    if (!isEnchantOrder)
        return false;
    return state == stateReady || state == stateComing || state == stateEnchanting;
}

// Server restart: Coming/Enchanting rows load back as Ready (or Waiting
// needitems): no meet request survives.
inline uint8_t EnchantRestartState(uint8_t stateComing, uint8_t stateEnchanting,
    uint8_t stateReady, uint8_t stateWaitingNeeditems, bool needitems, uint8_t state)
{
    if (state == stateComing || state == stateEnchanting)
        return needitems ? stateWaitingNeeditems : stateReady;
    return state;
}

} // namespace GuildOrders
} // namespace TortoiseBots
