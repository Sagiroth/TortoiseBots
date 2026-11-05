#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// Enchant-order trade checks (guild enchant service).
//
// Pure decision rules for the two Meet trades: the materials trade (E1) and
// the enchant trade (E2). The world-facing reads live with the lead's actions
// (the existing TradeStatusAction and whatever opens the order trades): this
// header only takes the already-read window state, so the standalone test
// never links the server. Caller mapping:
//   traderIsGiver       = trader == order giver
//   hasItem             = trader's TradeData has TRADE_SLOT_NONTRADED filled
//   fitsEnchant         = catalogue fit check for the ordered enchant (sibling worker)
//   hasPermanentEnchant = parked item's PERM_ENCHANTMENT_SLOT is non-zero
//   replaceConfirmed    = giver already answered yes (see ReplaceAnswerIsYes)
//   traderExtras        = any trader normal-slot item, or trader gold > 0
//   offered             = trader's normal-slot stacks in slot order
//   allowed             = order reagents with still-missing counts
// The bot never puts its own items or gold in either window, so nothing here
// reads the bot side of the trade.

namespace ai
{
    // E2 enchant-trade verdict. Token spellings double as the refusal reason.
    enum class EnchantTradeVerdict : uint8_t
    {
        Ok,          // cast the enchant
        WrongPlayer, // trader is not the giver
        NoItem,      // nothing in the trader's "Will not be traded" slot
        WrongItem,   // the parked item does not fit the ordered enchant
        AskReplace,  // parked item already carries a permanent enchant: ask first
        Extras       // trader added normal-slot items or gold: refuse the window
    };

    inline char const* EnchantTradeToken(EnchantTradeVerdict verdict)
    {
        switch (verdict)
        {
            case EnchantTradeVerdict::Ok:          return "ok";
            case EnchantTradeVerdict::WrongPlayer: return "wrongplayer";
            case EnchantTradeVerdict::NoItem:      return "noitem";
            case EnchantTradeVerdict::WrongItem:   return "wrongitem";
            case EnchantTradeVerdict::AskReplace:  return "askreplace";
            case EnchantTradeVerdict::Extras:      return "extras";
        }
        return "extras";
    }

    struct EnchantTradeState
    {
        bool traderIsGiver = false;
        bool hasItem = false;
        bool fitsEnchant = false;
        bool hasPermanentEnchant = false;
        bool replaceConfirmed = false;
        bool traderExtras = false;
    };

    // Refusals beat item checks: a window with foreign items or gold is
    // refused before the parked item is even inspected.
    inline EnchantTradeVerdict CheckEnchantTrade(EnchantTradeState const& state)
    {
        if (!state.traderIsGiver)
            return EnchantTradeVerdict::WrongPlayer;
        if (state.traderExtras)
            return EnchantTradeVerdict::Extras;
        if (!state.hasItem)
            return EnchantTradeVerdict::NoItem;
        if (!state.fitsEnchant)
            return EnchantTradeVerdict::WrongItem;
        if (state.hasPermanentEnchant && !state.replaceConfirmed)
            return EnchantTradeVerdict::AskReplace;
        return EnchantTradeVerdict::Ok;
    }

    // E1 materials-trade verdict. Token spellings double as the refusal reason.
    enum class MaterialsTradeVerdict : uint8_t
    {
        Accept,     // everything offered is an order reagent (surplus allowed)
        NotMaterial,// first offered stack that is no order reagent
        NoGold      // trader offered gold
    };

    inline char const* MaterialsTradeToken(MaterialsTradeVerdict verdict)
    {
        switch (verdict)
        {
            case MaterialsTradeVerdict::Accept:      return "accept";
            case MaterialsTradeVerdict::NotMaterial: return "notmaterial";
            case MaterialsTradeVerdict::NoGold:      return "nogold";
        }
        return "notmaterial";
    }

    struct ReagentNeed
    {
        uint32_t itemId = 0;
        uint32_t stillNeeded = 0;
    };

    struct OfferedStack
    {
        uint32_t itemId = 0;
        uint32_t count = 0;
    };

    struct MaterialsTradeState
    {
        std::vector<ReagentNeed> allowed;
        std::vector<OfferedStack> offered;
        uint32_t traderGold = 0;
    };

    struct MaterialsTradeResult
    {
        MaterialsTradeVerdict verdict = MaterialsTradeVerdict::Accept;
        uint32_t offendingItemId = 0; // set only for NotMaterial
    };

    // Extra counts of allowed reagents are accepted: the surplus reaches the
    // guild bank at the end. stillNeeded only tells the lead what is missing.
    inline MaterialsTradeResult CheckMaterialsTrade(MaterialsTradeState const& state)
    {
        for (OfferedStack const& stack : state.offered)
        {
            bool known = false;
            for (ReagentNeed const& need : state.allowed)
            {
                if (need.itemId != 0 && need.itemId == stack.itemId)
                {
                    known = true;
                    break;
                }
            }
            if (!known)
            {
                MaterialsTradeResult result;
                result.verdict = MaterialsTradeVerdict::NotMaterial;
                result.offendingItemId = stack.itemId;
                return result;
            }
        }
        if (state.traderGold != 0)
        {
            MaterialsTradeResult result;
            result.verdict = MaterialsTradeVerdict::NoGold;
            return result;
        }
        return MaterialsTradeResult();
    }

    // Try budget for either trade window.
    enum class TradeTryAction : uint8_t
    {
        KeepWaiting, // window still fresh, or the trader is mid-placement
        CancelRetry, // try timed out idle: cancel and open the next one
        GiveUp       // last try timed out idle: whisper once, wait for Meet
    };

    inline char const* TradeTryToken(TradeTryAction action)
    {
        switch (action)
        {
            case TradeTryAction::KeepWaiting: return "wait";
            case TradeTryAction::CancelRetry: return "retry";
            case TradeTryAction::GiveUp:      return "giveup";
        }
        return "wait";
    }

    constexpr uint32_t kEnchantTradeTryMs = 2u * 60u * 1000u;
    constexpr uint32_t kEnchantTradeMaxTries = 3;
    constexpr uint32_t kReplaceAnswerMs = 2u * 60u * 1000u;

    // Trade waiting counts toward neither the order time nor the no-progress
    // limit; the lead subtracts the waited span. The value only records that.
    constexpr bool kTradeWaitCountsTowardOrderLimits = false;

    // Server-uptime-ms difference that survives the ~49-day wrap.
    inline uint32_t TradeElapsedMs(uint32_t nowMs, uint32_t sinceMs)
    {
        int32_t diff = static_cast<int32_t>(nowMs - sinceMs);
        return diff > 0 ? static_cast<uint32_t>(diff) : 0;
    }

    // triesSoFar counts opened tries including the current window (first
    // window opens with 1); each cancel-and-reopen increments it. A trader
    // mid-placement is not yanked at the sharp deadline, but the window still
    // closes one window later, so an abandoned full window can never linger.
    inline TradeTryAction CheckTradeTry(uint32_t nowMs, uint32_t openedMs,
        uint32_t triesSoFar, bool traderActive)
    {
        uint32_t elapsed = TradeElapsedMs(nowMs, openedMs);
        if (elapsed < kEnchantTradeTryMs)
            return TradeTryAction::KeepWaiting;
        if (traderActive && elapsed < 2 * kEnchantTradeTryMs)
            return TradeTryAction::KeepWaiting;
        if (triesSoFar >= kEnchantTradeMaxTries)
            return TradeTryAction::GiveUp;
        return TradeTryAction::CancelRetry;
    }

    // Replace confirmation: "yes", case-insensitive and trimmed; anything else
    // (including silence) is no.
    inline bool ReplaceAnswerIsYes(std::string const& text)
    {
        std::string::size_type first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;
        std::string::size_type last = text.find_last_not_of(" \t\r\n");
        if (last - first + 1 != std::string::size_type(3))
            return false;
        return std::tolower(static_cast<unsigned char>(text[first])) == 'y' &&
               std::tolower(static_cast<unsigned char>(text[first + 1])) == 'e' &&
               std::tolower(static_cast<unsigned char>(text[first + 2])) == 's';
    }

    inline bool ReplaceWaitExpired(uint32_t nowMs, uint32_t askedMs)
    {
        return TradeElapsedMs(nowMs, askedMs) >= kReplaceAnswerMs;
    }

    constexpr char const* kEnchantNoItemText = "Put your item in Will not be traded.";
    constexpr char const* kEnchantWrongItemText = "That item does not fit the enchant.";
    constexpr char const* kEnchantWrongPlayerText = "This trade is not for you.";
    constexpr char const* kEnchantExtrasText = "Take your items out, this trade is only for the enchant.";
    constexpr char const* kMaterialsNotMaterialText = "That is not part of this order.";
    constexpr char const* kMaterialsNoGoldText = "I take no gold for this order.";
    constexpr char const* kTradeGiveUpText = "Trade timed out. Press Meet when you are ready.";

    inline std::string EnchantAskReplaceText(std::string const& currentEnchant)
    {
        return "It already has " + currentEnchant + ", replace it? Answer yes";
    }
}
