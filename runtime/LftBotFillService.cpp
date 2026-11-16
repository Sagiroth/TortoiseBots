#include "LftBotFillService.h"
#include "BotActivityLease.h"
#include "runtime/policy/hire/LftCompPolicy.h"
#include "runtime/policy/hire/LftPartyAcceptPolicy.h"
#include "runtime/policy/hire/LftRoleKitPolicy.h"
#include "runtime/policy/lfg/LftDungeonRangePolicy.h"

#include "BotManager.h"
#include "AhBotSellerAccount.h"
#include "HireLifecycle.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/AiFactory.h"
#include "../ai/playerbot/PlayerbotDbStore.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
#include "LFT/LFTMgr.h"
#ifndef MANGOSSERVER_LFTMGR_H
#error "TortoiseBots LFT fill requires Penqle core #416 (LFTMgr.h)"
#endif
#include "../host/ModuleLog.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>

namespace TortoiseBots
{

namespace
{
bool HasInstance(std::vector<std::string> const& v, std::string const& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

constexpr std::size_t kMaxLoggedUnknownLftCodes = 64;

LftLevelRange const* FindDungeonLevelRange(std::string const& raw)
{
    LftLevelRange const* range = LftDungeonRange(raw);
    if (!range)
    {
        static std::set<std::string> loggedUnknown;
        static bool loggedOverflow = false;
        if (loggedUnknown.find(raw) == loggedUnknown.end())
        {
            if (loggedUnknown.size() < kMaxLoggedUnknownLftCodes)
            {
                loggedUnknown.insert(raw);
                TB_LOG_BASIC("TortoiseBots: LFT fill: no level range for instance '%s'; bots do not fill it (add it to LftDungeonRangePolicy.h)", raw.substr(0, 32).c_str());
            }
            else if (!loggedOverflow)
            {
                loggedOverflow = true;
                TB_LOG_BASIC("TortoiseBots: LFT fill: more unknown instance codes; not logged further");
            }
        }
        return nullptr;
    }

    return range;
}

LFTManager::QueuedInfo const* FindQueued(std::vector<LFTManager::QueuedInfo> const& queued, ObjectGuid const& guid)
{
    for (LFTManager::QueuedInfo const& info : queued)
        if (info.guid == guid)
            return &info;
    return nullptr;
}

// Same offer, as far as the module API can tell (see LftQueuedTogether): both
// in an offer, in one group, and queued in one block.
bool InSameOffer(Player* bot, Player* anchor, std::vector<LFTManager::QueuedInfo> const& queued)
{
    if (!sLFTMgr.IsInOffer(bot->GetObjectGuid()) || !sLFTMgr.IsInOffer(anchor->GetObjectGuid()))
        return false;
    Group* group = bot->GetGroup();
    LFTManager::QueuedInfo const* a = FindQueued(queued, bot->GetObjectGuid());
    LFTManager::QueuedInfo const* b = FindQueued(queued, anchor->GetObjectGuid());
    return LftQueuedTogether(group && group == anchor->GetGroup(), a && b,
        a ? a->joinTime : 0, b ? b->joinTime : 0,
        a ? a->instances : std::vector<std::string>(), b ? b->instances : std::vector<std::string>());
}
}


LftBotFillService& LftBotFillService::Instance()
{
    static LftBotFillService instance;
    return instance;
}

void LftBotFillService::Initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;
    m_elapsedMs = 0;
    m_pending.clear();

    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotLftEnabled)
    {
        TB_LOG_BASIC("TortoiseBots: LFT random fill disabled (ai %u lft %u); own-party offer acceptance stays on",
            sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.randomBotLftEnabled);
        return;
    }
    TB_LOG_BASIC("TortoiseBots: LFT fill enabled interval %u max %u (observe GetQueuedPlayers, native QueuePlayer/offers, reconcile)",
        sPlayerbotAIConfig.randomBotLftUpdateInterval, sPlayerbotAIConfig.randomBotLftMaxFillsPerInterval);
}

bool LftBotFillService::IsEligibleCandidate(Player* bot) const
{
    if (!bot) return false;
    if (!bot->IsInWorld()) return false;
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless()) return false;
    if (AhBotSellerAccount::Instance().IsSellerGuid(bot->GetGUIDLow())) return false;
    if (!BotManager::Instance().IsRandomBot(bot->GetObjectGuid())) return false;
    if (sLFTMgr.IsQueued(bot->GetObjectGuid()) || sLFTMgr.IsInOffer(bot->GetObjectGuid())) return false;
    if (bot->InBattleGround() || bot->InBattleGroundQueue()) return false;
    if (bot->IsBeingTeleported()) return false;
    if (bot->IsTaxiFlying()) return false;
    if (bot->IsInCombat()) return false;
    if (!bot->IsAlive()) return false;
    if (bot->GetGroup()) return false;
    if (bot->GetLevel() < 10) return false;
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        if (ai->HasActivePlayerMaster())
            return false;
    // Lease arbitration (issue #89): host guards above stay authoritative.
    if (!BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow()))
        return false;
    return true;
}

bool LftBotFillService::IsModuleOwnedHeadlessBot(Player const* bot) const
{
    if (!bot) return false;
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless()) return false;
    if (AhBotSellerAccount::Instance().IsSellerGuid(bot->GetGUIDLow())) return false;
    if (!BotManager::Instance().IsRandomBot(bot->GetObjectGuid())) return false;
    const Player* p = bot;
    // Human-owned bots (with active master) preserve manual role semantics – not owned by fill.
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(const_cast<Player*>(p)))
        if (ai->HasActivePlayerMaster())
            return false;
    return true;
}

uint8 LftBotFillService::GetBotRoleMask(Player const* bot) const
{
    if (!bot) return 0;
    BotRoles r = AiFactory::GetPlayerRoles(bot);
    // A shaman whose tank identity comes from its levelling build before the
    // key talents cannot fill the tank slot (RoleMismatchReason) but is still
    // a damage body; without the bit the queue would have no role for it at
    // all (issue #1125).
    return TortoiseBots::LftRoleMaskForLatentShamanTank(bot->GetClass() == CLASS_SHAMAN,
        static_cast<uint8>(r), AiFactory::HasShamanTankTalents(bot));
}

std::string LftBotFillService::RoleMismatchReason(Player* bot, uint8 needRole) const
{
    if (!bot)
        return "no bot";
    // Natural role only, unless the operator opted into role borrowing.
    // A level 10+ bot with no talent points spent yet (just seeded, talents
    // still pending) reports its class default spec - holy for a priest - so
    // its real role is unknown. Borrowing respecs it to the slot anyway.
    if (!sPlayerbotAIConfig.randomBotLftAllowRoleBorrow && bot->GetLevel() >= 10)
    {
        std::map<uint32, int32> tabs = AiFactory::GetPlayerSpecTabs(bot);
        if (tabs[0] + tabs[1] + tabs[2] == 0)
            return "spec unknown (no talents spent yet)";
    }
    uint8 mask = GetBotRoleMask(bot);
    if ((mask & needRole) == 0)
        return "role mismatch (own spec cannot fill)";
    if (needRole == LFT_ROLE_TANK)
    {
        uint8 cls = bot->GetClass();
        if (cls == CLASS_WARRIOR || cls == CLASS_PALADIN || cls == CLASS_SHAMAN)
        {
            Item const* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            ItemPrototype const* proto = offhand ? offhand->GetProto() : nullptr;
            bool shield = proto && proto->Class == ITEM_CLASS_ARMOR &&
                proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
            // Best-shield-from-bags quick fix runs at queue time; eligibility
            // here only checks the currently equipped off-hand so the caller
            // can log a precise skip reason.
            if (!shield)
                return "tank-spec without shield equipped";
        }
        if (cls == CLASS_DRUID && !bot->HasSpell(5487) && !bot->HasSpell(9634))
            return "druid tank without bear form";
        // A shaman tanks the slot when it has a key tank talent or already
        // plays the tank kit (a 7.3 build below the first key talent at 25);
        // a DPS shaman has neither and stays out ("borrow" aside).
        if (cls == CLASS_SHAMAN)
        {
            if (!AiFactory::HasShamanTankTalents(bot) && !(GetCombatKitRoleMask(bot) & kLftRoleTank))
                return "shaman tank without tank talents";
        }
    }
    if (needRole == LFT_ROLE_HEALER)
    {
        // Healing suite by spell ownership: priest / paladin / shaman /
        // druid rank-1 heals. No AI context here (AI_VALUE needs an engine),
        // so check the book directly — the level-up learner teaches ranks.
        static uint32 const heals[] = {
            2050, 2052, 2053, 2060, 2061, 10915,
            635, 639, 647, 1026,
            331, 332, 547, 8004, 8008, 8010,
            5185, 5186, 5187, 8936, 8938, 8939, 0
        };
        for (uint32 const* id = heals; *id; ++id)
            if (bot->HasSpell(*id))
                return "";
        return "healer-spec without healing spell";
    }
    return "";
}

bool LftBotFillService::EquipBestShieldFromBags(Player* bot) const
{
    if (!bot)
        return false;
    Item const* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    ItemPrototype const* worn = offhand ? offhand->GetProto() : nullptr;
    if (worn && worn->Class == ITEM_CLASS_ARMOR && worn->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    // Backpack (bag 0, slots 23..38) and equipped bags (bag slots 19..22).
    auto forEachBagItem = [&](auto const& consider)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                consider(item);

        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag const* pBag = (Bag const*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            uint32 size = pBag ? pBag->GetBagSize() : 0;
            for (uint32 slot = 0; slot < size; ++slot)
                if (Item* item = bot->GetItemByPos(bag, static_cast<uint8>(slot)))
                    consider(item);
        }
    };

    // Find the shield first: nothing about the bot's weapon may change unless
    // there is a shield to put on afterwards, or a candidate that cannot be
    // filled would be left behind with a one-hander and an empty hand.
    Item* best = nullptr;
    forEachBagItem([&](Item* item)
    {
        ItemPrototype const* proto = item->GetProto();
        if (!proto || proto->Class != ITEM_CLASS_ARMOR || proto->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD)
            return;
        if (bot->CanUseItem(item) != EQUIP_ERR_OK)
            return;
        if (!best || proto->ItemLevel > best->GetProto()->ItemLevel)
            best = item;
    });

    if (!best)
        return false;

    // A tank still wielding a two-hander cannot take the shield at all: core
    // refuses the off-hand while a 2H is used. Put the best one-hander the core
    // accepts into the main hand (core's own CanEquipItem applies class, skill
    // and level rules) and keep the two-hander, so it can be handed back if the
    // shield then cannot go on after all.
    Item* swappedOut = nullptr;
    if (bot->IsTwoHandUsed())
    {
        Item* bestMh = nullptr;
        forEachBagItem([&](Item* item)
        {
            ItemPrototype const* proto = item->GetProto();
            if (!proto || proto->Class != ITEM_CLASS_WEAPON ||
                (proto->InventoryType != INVTYPE_WEAPON && proto->InventoryType != INVTYPE_WEAPONMAINHAND))
                return;

            uint16 dest = 0;
            if (bot->CanEquipItem(EQUIPMENT_SLOT_MAINHAND, dest, item, true) != EQUIP_ERR_OK)
                return;

            if (!bestMh || proto->ItemLevel > bestMh->GetProto()->ItemLevel)
                bestMh = item;
        });

        if (!bestMh)
            return false;

        swappedOut = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        uint16 src = (static_cast<uint16>(bestMh->GetBagSlot()) << 8) | bestMh->GetSlot();
        uint16 dst = (static_cast<uint16>(INVENTORY_SLOT_BAG_0) << 8) | EQUIPMENT_SLOT_MAINHAND;
        bot->SwapItem(src, dst);
    }

    uint16 shieldDest = 0;
    if (bot->CanEquipItem(EQUIPMENT_SLOT_OFFHAND, shieldDest, best, true) != EQUIP_ERR_OK)
    {
        // Hand the two-hander back rather than leaving the bot with a
        // one-hander and an empty off-hand.
        if (swappedOut && bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) != swappedOut)
        {
            uint16 src = (static_cast<uint16>(swappedOut->GetBagSlot()) << 8) | swappedOut->GetSlot();
            uint16 dst = (static_cast<uint16>(INVENTORY_SLOT_BAG_0) << 8) | EQUIPMENT_SLOT_MAINHAND;
            bot->SwapItem(src, dst);
        }

        return false;
    }

    uint8 bagIndex = best->GetBagSlot();
    uint8 slot = best->GetSlot();
    uint16 src = (static_cast<uint16>(bagIndex) << 8) | slot;
    uint16 dst = (static_cast<uint16>(INVENTORY_SLOT_BAG_0) << 8) | EQUIPMENT_SLOT_OFFHAND;
    bot->SwapItem(src, dst);

    Item const* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    ItemPrototype const* equippedProto = equipped ? equipped->GetProto() : nullptr;
    if (equippedProto && equippedProto->Class == ITEM_CLASS_ARMOR && equippedProto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    // The swap was refused after all: undo the weapon change.
    if (swappedOut && bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) != swappedOut)
    {
        uint16 backSrc = (static_cast<uint16>(swappedOut->GetBagSlot()) << 8) | swappedOut->GetSlot();
        uint16 backDst = (static_cast<uint16>(INVENTORY_SLOT_BAG_0) << 8) | EQUIPMENT_SLOT_MAINHAND;
        bot->SwapItem(backSrc, backDst);
    }

    return false;
}

uint8 LftBotFillService::GetCombatKitRoleMask(Player const* bot) const
{
    if (!bot)
        return 0;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(const_cast<Player*>(bot));
    if (!ai)
        return 0;

    // Same named-spec tables as BotPlayerAdapter::GetBotRoles (ambient
    // strategies such as "dps assist" ride along with every spec and must not
    // produce a role bit).
    bool kitTank = ai->HasStrategy("protection", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("tank feral", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("bear", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("tank", BotState::BOT_STATE_COMBAT);
    bool kitHealer = ai->HasStrategy("holy", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("discipline", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("restoration", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("heal", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("resto", BotState::BOT_STATE_COMBAT);
    static char const* const kitDpsSpecs[] = { "arms", "fury",
        "retribution", "beast mastery", "marksmanship", "survival",
        "combat", "assassination", "subtlety", "shadow",
        "elemental", "enhancement", "frost", "fire", "arcane",
        "affliction", "demonology", "destruction", "dps feral",
        "balance", "cat" };
    bool kitDps = false;
    for (char const* spec : kitDpsSpecs)
        if (ai->HasStrategy(spec, BotState::BOT_STATE_COMBAT))
        {
            kitDps = true;
            break;
        }

    return (kitTank ? kLftRoleTank : 0) | (kitHealer ? kLftRoleHealer : 0) | (kitDps ? kLftRoleDamage : 0);
}

void LftBotFillService::ClearForcedRole(uint32 guidLow)
{
    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    if (Player* p = sObjectAccessor.FindPlayer(guid))
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(p))
        {
            ai->SetForcedRole(0);
            sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
            // The same rebuild `.bot role` runs: "auto talents" only applies
            // the premade, and a bot that already has it changes nothing
            // there, so on its own it left the cleared role's kit installed
            // (issue #1125). ResetStrategies reads the role and rebuilds.
            ai->DoSpecificAction("auto talents");
            ai->ResetStrategies();
        }
}

void LftBotFillService::ApplyForcedRole(Player* bot, uint8 role)
{
    if (!bot)
        return;
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai->SetForcedRole(role);
        sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
        ai->DoSpecificAction("auto talents");
        ai->ResetStrategies();
    }
}

void LftBotFillService::ReconcilePending(bool cancelAll, std::vector<std::string> const* activeInstances)
{
    std::vector<uint32> toErase;
    toErase.reserve(m_pending.size());
    for (auto const& kv : m_pending)
    {
        uint32 guidLow = kv.first;
        std::string const& inst = kv.second;
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        bool inOffer = sLFTMgr.IsInOffer(guid);
        bool queued = sLFTMgr.IsQueued(guid);
        if (!queued && !inOffer)
        {
            // Grouped for the dungeon: the queue let go because the match
            // completed. The forced role must survive - ResetStrategies reads
            // it when the bot adopts the group leader as master. Only the
            // lease expiry/evict path clears it (OnLeaseEvicted/Shutdown).
            if (Player* formed = sObjectAccessor.FindPlayer(guid))
                if (formed->GetGroup())
                {
                    toErase.push_back(guidLow);
                    continue;
                }
            ClearForcedRole(guidLow);
            toErase.push_back(guidLow);
            continue;
        }
        if (inOffer)
            continue; // keep pending that is in offer – let native offer finish; AcceptPendingOffers will drive it
        if (cancelAll)
        {
            sLFTMgr.LeaveQueue(guid);
            ClearForcedRole(guidLow);
            TB_LOG_DETAIL("TortoiseBots: LFT fill reconcile cancel bot %u (no human waiting, cancelAll)", guidLow);
            toErase.push_back(guidLow);
            continue;
        }
        if (activeInstances)
        {
            bool stillActive = false;
            for (std::string const& a : *activeInstances)
                if (a == inst) { stillActive = true; break; }
            if (!stillActive)
            {
                sLFTMgr.LeaveQueue(guid);
                ClearForcedRole(guidLow);
                TB_LOG_DETAIL("TortoiseBots: LFT fill reconcile cancel bot %u instance %s (no longer active)", guidLow, inst.c_str());
                toErase.push_back(guidLow);
            }
        }
    }
    for (uint32 g : toErase)
    {
        m_pending.erase(g);
        BotActivityLeaseManager::Instance().Release(g, BotActivity::LftQueued);
    }
}

void LftBotFillService::AcceptPendingOffers()
{
    // Copy keys to avoid iterator invalidation if core completes offers during AcceptOffer.
    std::vector<uint32> keys;
    keys.reserve(m_pending.size());
    for (auto const& kv : m_pending)
        keys.push_back(kv.first);
    for (uint32 guidLow : keys)
    {
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        if (!sLFTMgr.IsInOffer(guid))
            continue;
        Player* bot = sObjectAccessor.FindPlayer(guid);
        if (!bot) continue;
        // Only for module-owned Headless bots – humans still explicitly accept.
        // Preserves core cancellation/requeue: core validates live+offer and reuses
        // HandleOfferAccept/CompleteOffer semantics (timers, S2C_OFFER_UPDATE_COUNT, etc.).
        if (!IsModuleOwnedHeadlessBot(bot))
            continue;
        // Extra safety: ensure it's still pending fill-owned (not a manually queued bot)
        if (m_pending.find(guidLow) == m_pending.end())
            continue;
        // World-thread only generic core API (PR #416). Reuses native offer accept.
        bool ok = sLFTMgr.AcceptOffer(guid);
        if (ok)
            TB_LOG_DETAIL("TortoiseBots: LFT fill bot %s (%s) accepted offer (pending %u)", bot->GetName(), guid.GetString().c_str(), (uint32)m_pending.size());
    }
}

void LftBotFillService::AcceptPartyBotOffers()
{
    // Own-party acceptance: a real player queued LFT in a party with their own
    // managed bots (alts and hires). The core auto-answers the
    // rolecheck for them (rolecheck hook: forced role, then named combat
    // specs, then AiFactory spec/gear), but the formed offer
    // still needs every member to accept.
    // Accept only those: managed bot, in an offer, not fill-owned, whose
    // master or group leader is a real (network) player in the same offer.
    // No queued-snapshot scan: only bots already in an offer are visited, via
    // the in-memory managed-bot list (GetAllBots filters in world + Headless).
    std::vector<Player*> bots = BotManager::Instance().GetAllBots();
    // Queue snapshot for the hires' same-offer check, taken once, only if needed.
    std::vector<LFTManager::QueuedInfo> queued;
    bool haveQueued = false;
    for (Player* bot : bots)
    {
        if (!bot)
            continue;
        ObjectGuid guid = bot->GetObjectGuid();
        uint32 guidLow = guid.GetCounter();
        // Never touch fill-owned bots (they have their own acceptor above),
        // nor random-pool bots (fill candidates, not party companions), except hires: a hire is a pool
        // character that is its hirer's companion (#1284).
        if (m_pending.find(guidLow) != m_pending.end())
            continue;
        bool const randomPool = BotManager::Instance().IsRandomBot(guid);
        bool const hired = HireLifecycle::Instance().IsHired(guid);
        if (!LftPartyBotMayAccept(randomPool, hired))
            continue;
        if (!sLFTMgr.IsInOffer(guid))
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || !ai->HasActivePlayerMaster())
            continue;
        // The human must be part of this run: master or group leader, live,
        // real (network transport), in an offer. A hire also needs that human
        // to be its own hirer, in the same offer (LftPartyOfferMayAccept).
        ObjectGuid const hireMaster = hired ? HireLifecycle::Instance().GetMaster(guid) : ObjectGuid();
        Player* candidates[2] = { ai->GetLiveMaster(), nullptr };
        if (Group* group = bot->GetGroup())
            candidates[1] = sObjectMgr.GetPlayer(group->GetLeaderGuid());
        Player* anchor = nullptr;
        for (Player* candidate : candidates)
        {
            if (!candidate || candidate == bot || !candidate->IsInWorld() || !candidate->GetSession() ||
                !candidate->GetSession()->HasNetworkTransport() ||
                !sLFTMgr.IsInOffer(candidate->GetObjectGuid()))
                continue;
            bool const anchorIsHireMaster = !hireMaster.IsEmpty() && candidate->GetObjectGuid() == hireMaster;
            bool sameOffer = false;
            if (hired)
            {
                if (!haveQueued)
                {
                    queued = sLFTMgr.GetQueuedPlayers();
                    haveQueued = true;
                }
                sameOffer = InSameOffer(bot, candidate, queued);
            }
            if (LftPartyOfferMayAccept(randomPool, hired, anchorIsHireMaster, sameOffer))
            {
                anchor = candidate;
                break;
            }
        }
        if (!anchor)
            continue;
        // World-thread only generic core API (PR #416). Reuses native offer accept.
        if (sLFTMgr.AcceptOffer(guid))
            TB_LOG_DETAIL("TortoiseBots: LFT party bot %s accepted offer with %s",
                bot->GetName(), anchor->GetName());
    }
}

void LftBotFillService::Update(uint32_t diff)
{
    if (!m_initialized)
        return;
    if (!sPlayerbotAIConfig.enabled)
    {
        if (!m_pending.empty())
            ReconcilePending(true, nullptr);
        return;
    }

    uint32 interval = sPlayerbotAIConfig.randomBotLftUpdateInterval;
    if (interval < 5000) interval = 5000;
    if (interval > 60000) interval = 60000;
    m_elapsedMs += diff;
    if (m_elapsedMs < interval)
        return;
    m_elapsedMs = 0;

    // Own-party bots accept on the same cadence, independent of the
    // random-fill switch: without this the 90s native offer timer expires and
    // non-accepting party bots are dropped from the queue. Never touches
    // fill-owned (m_pending) or random-pool bots.
    AcceptPartyBotOffers();

    if (!sPlayerbotAIConfig.randomBotLftEnabled)
    {
        if (!m_pending.empty())
            ReconcilePending(true, nullptr);
        return;
    }

    // Snapshot native queue (copy, no private map exposure)
    std::vector<LFTManager::QueuedInfo> queued = sLFTMgr.GetQueuedPlayers();

    // Drive pending offers to completion for our Headless bots via generic core AcceptOffer.
    // Humans still explicitly accept via addon C2S_OFFER_ACCEPT; we never auto-accept humans.
    // Preserve core cancellation/requeue by reusing native HandleOfferAccept semantics.
    if (!m_pending.empty())
        AcceptPendingOffers();

    // Reconcile stale pending tracking (left queue/offer). A bot that left
    // because its offer completed is now in the dungeon group: keep its
    // forced role (ResetStrategies needs it at master adoption) and only
    // drop pending tracking. Genuinely stale bots get ClearForcedRole.
    // This must run even when MaxFillsPerInterval==0, so it is before the max gate.
    {
        std::vector<uint32> stale;
        for (auto const& kv : m_pending)
        {
            ObjectGuid guid(HIGHGUID_PLAYER, kv.first);
            if (!sLFTMgr.IsQueued(guid) && !sLFTMgr.IsInOffer(guid))
                stale.push_back(kv.first);
        }
        for (uint32 g : stale)
        {
            ObjectGuid guid(HIGHGUID_PLAYER, g);
            if (Player* formed = sObjectAccessor.FindPlayer(guid))
                if (formed->GetGroup())
                {
                    m_pending.erase(g);
                    BotActivityLeaseManager::Instance().Release(g, BotActivity::LftQueued);
                    continue;
                }
            ClearForcedRole(g);
            m_pending.erase(g);
            BotActivityLeaseManager::Instance().Release(g, BotActivity::LftQueued);
        }
    }

    uint32 maxPerInterval = sPlayerbotAIConfig.randomBotLftMaxFillsPerInterval;
    if (maxPerInterval > 5) maxPerInterval = 5;

    // Identify human queued entries (not module-owned)
    std::vector<LFTManager::QueuedInfo> humanQueued;
    humanQueued.reserve(queued.size());
    for (auto const& q : queued)
    {
        if (!BotManager::Instance().IsBot(q.guid))
            humanQueued.push_back(q);
    }

    if (humanQueued.empty())
    {
        if (!m_pending.empty())
            ReconcilePending(true, nullptr);
        return;
    }

    // Distinct instances where humans wait
    std::set<std::string> distinctInstances;
    for (auto const& h : humanQueued)
        for (auto const& inst : h.instances)
            if (!inst.empty())
                distinctInstances.insert(inst);

    if (distinctInstances.empty())
    {
        if (!m_pending.empty())
            ReconcilePending(true, nullptr);
        return;
    }

    std::vector<std::string> activeInstances(distinctInstances.begin(), distinctInstances.end());

    // Reconcile pending whose instance is no longer active (and not in offer)
    ReconcilePending(false, &activeInstances);

    // If fills are disabled this interval, stop after reconciliation/acceptance.
    // Reconciliation already ran above, satisfying MaxFills==0 requirement.
    if (maxPerInterval == 0)
        return;

    // For each instance, fill missing roles per team/hardcore partition
    // Collect candidates once per tick (in-memory, no DB scan)
    std::vector<Player*> allBots = BotManager::Instance().GetAllBots();
    std::vector<Player*> eligibleCandidates;
    eligibleCandidates.reserve(allBots.size());
    for (Player* p : allBots)
        if (IsEligibleCandidate(p))
            eligibleCandidates.push_back(p);

    if (eligibleCandidates.empty())
        return;

    // Shuffle candidates to avoid low-guid bias (like BG service)
    for (size_t i = eligibleCandidates.size(); i > 1; --i)
    {
        size_t j = urand(0, static_cast<uint32>(i - 1));
        std::swap(eligibleCandidates[i - 1], eligibleCandidates[j]);
    }

    uint32 filledThisTick = 0;

    for (std::string const& instance : activeInstances)
    {
        if (filledThisTick >= maxPerInterval)
            break;

        // Partition humans for this instance by team/hardcore
        struct Partition
        {
            uint32 team = 0;
            bool hardcore = false;
            std::vector<LFTManager::QueuedInfo> humans;
        };
        std::vector<Partition> partitions;
        for (auto const& h : humanQueued)
        {
            if (!HasInstance(h.instances, instance))
                continue;
            bool found = false;
            for (auto& p : partitions)
                if (p.team == h.team && p.hardcore == h.isHardcore)
                {
                    p.humans.push_back(h);
                    found = true;
                    break;
                }
            if (!found)
            {
                Partition pr;
                pr.team = h.team;
                pr.hardcore = h.isHardcore;
                pr.humans.push_back(h);
                partitions.push_back(std::move(pr));
            }
        }

        for (Partition const& part : partitions)
        {
            if (filledThisTick >= maxPerInterval)
                break;

            // Replay the core offer match over entries that may join a NEW
            // offer (runtime/LftCompPolicy.h). Committed (in-offer) members
            // keep their assigned slots and never shape a new offer; joinTime
            // order is stable (guidLow tiebreak) and a party block is tried
            // atomically in core group-slot order. A block holding a human
            // that fits nowhere waits instead of stranding bots around it
            // (nothing the fill queues can ever place).
            LftCompResult comp;
            {
                // in-offer guidLows in this partition/instance, for exclusion.
                std::unordered_set<uint32_t> committed;
                for (auto const& q : queued)
                {
                    if (!HasInstance(q.instances, instance))
                        continue;
                    if (q.team != part.team || q.isHardcore != part.hardcore)
                        continue;
                    if (sLFTMgr.IsInOffer(q.guid))
                        committed.insert(q.guid.GetCounter());
                }
                std::vector<LftCompEntry> entries;
                entries.reserve(queued.size());
                bool hasHuman = false;
                // blockId approximates the core queueLeaderGuid seam with the
                // live group the entrant stands in now: leader guidLow as
                // block, member-slot index as order. Entrants outside a live
                // group stand alone, like a solo queue in the core.
                for (auto const& q : queued)
                {
                    if (!HasInstance(q.instances, instance))
                        continue;
                    if (q.team != part.team || q.isHardcore != part.hardcore)
                        continue;
                    if (committed.count(q.guid.GetCounter()))
                        continue;
                    bool human = !BotManager::Instance().IsBot(q.guid);
                    if (human)
                        hasHuman = true;
                    uint32_t self = q.guid.GetCounter();
                    uint32_t block = self;
                    uint32_t seq = 0;
                    // The core groups by stored queueLeaderGuid, which this
                    // world-thread tick cannot read for offline/changed
                    // parties. Approximate with the live group the entrant
                    // stands in now: leader guidLow as block, member-slot
                    // index as order. Entrants outside a live group stand
                    // alone, like a solo queue (or an unchanged party member
                    // by the same leader key) in the core.
                    if (Player* live = sObjectAccessor.FindPlayer(q.guid))
                        if (Group* group = live->GetGroup())
                            if (group->GetMembersCount() > 1)
                            {
                                block = group->GetLeaderGuid().GetCounter();
                                uint32_t slot = 0;
                                for (Group::MemberSlot const& member : group->GetMemberSlots())
                                {
                                    if (member.guid == q.guid)
                                    {
                                        seq = slot;
                                        break;
                                    }
                                    ++slot;
                                }
                            }
                    LftCompEntry entry;
                    entry.guidLow = self;
                    entry.joinTime = static_cast<uint32_t>(q.joinTime);
                    entry.roleMask = q.roleMask;
                    entry.blockId = block;
                    entry.blockSeq = seq;
                    entry.human = human;
                    entries.push_back(entry);
                }
                if (!hasHuman)
                    continue;
                if (entries.empty())
                    continue; // everyone unqueued is already committed: wait for the offer
                comp = LftComputeMissingRoles(entries);
                if (comp.blockedHumans)
                {
                    TB_LOG_DETAIL("TortoiseBots: LFT fill waits for instance %s (human party fits no 1/1/3 slot; no bots queued)", instance.c_str());
                    continue;
                }
            }
            uint8 needTank = comp.needTank;
            uint8 needHeal = comp.needHeal;
            uint8 needDps = comp.needDps;
            if (needTank == 0 && needHeal == 0 && needDps == 0)
                continue;

            // Use the authoritative instance range, never the waiting humans'
            // average level. Unknown or corrupt/custom instances fail closed.
            LftLevelRange const* range = FindDungeonLevelRange(instance);
            if (!range)
                continue;
            uint32 low = range->minLevel;
            uint32 high = range->maxLevel;

            std::vector<uint8> neededRoles;
            for (uint8 i = 0; i < needTank; ++i) neededRoles.push_back(LFT_ROLE_TANK);
            for (uint8 i = 0; i < needHeal; ++i) neededRoles.push_back(LFT_ROLE_HEALER);
            for (uint8 i = 0; i < needDps; ++i) neededRoles.push_back(LFT_ROLE_DAMAGE);

            // For each needed role, find a suitable candidate
            for (uint8 needRole : neededRoles)
            {
                if (filledThisTick >= maxPerInterval)
                    break;

                Player* chosen = nullptr;
                size_t chosenIdx = SIZE_MAX;
                for (size_t idx = 0; idx < eligibleCandidates.size(); ++idx)
                {
                    Player* cand = eligibleCandidates[idx];
                    if (!cand) continue;
                    if (cand->GetLevel() < low || cand->GetLevel() > high)
                        continue;
                    if (cand->GetTeam() != part.team)
                        continue;
                    if (cand->IsHardcore() != part.hardcore)
                        continue;
                    // Issue #189 Phase 4: fail closed on natural-role gear.
                    // Count skips per role so the empty slot logs a reason.
                    std::string mismatch = RoleMismatchReason(cand, needRole);
                    if (!mismatch.empty())
                    {
                        if (needRole == LFT_ROLE_TANK &&
                            (cand->GetClass() == CLASS_WARRIOR || cand->GetClass() == CLASS_PALADIN ||
                                cand->GetClass() == CLASS_SHAMAN) &&
                            mismatch == "tank-spec without shield equipped" &&
                            EquipBestShieldFromBags(cand) &&
                            RoleMismatchReason(cand, needRole).empty())
                            mismatch.clear();
                    }
                    if (!mismatch.empty())
                    {
                        std::string key = instance + ":" + std::to_string(needRole) + ":" + mismatch;
                        m_skipReasons[key]++;
                        continue;
                    }
                    bool alreadyPending = m_pending.find(cand->GetObjectGuid().GetCounter()) != m_pending.end();
                    if (alreadyPending)
                        continue;
                    if (sLFTMgr.IsQueued(cand->GetObjectGuid()) || sLFTMgr.IsInOffer(cand->GetObjectGuid()))
                        continue;
                    chosen = cand;
                    chosenIdx = idx;
                    break;
                }
                if (!chosen)
                    continue;

                uint32 guidLow = chosen->GetObjectGuid().GetCounter();
                BotActivity previousActivity = BotActivityLeaseManager::Instance().GetActivity(guidLow);
                // 10-minute LftQueued lease arbitrates dungeon-fill ownership.
                // Denied (Trading/BgQueued/PlayerMaster) -> skip candidate.
                if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::LftQueued, 600000))
                    continue;
                std::vector<std::string> instVec;
                instVec.push_back(instance);
                // Forced role when the operator opted into role borrowing, or
                // when the installed kit would not play the slot the spec was
                // picked for (issue #966): a feral druid masks Tank|DPS but
                // defaults to its Cat kit, so the queued tank slot must hand
                // the role to the bot or the group leans on a cat. Single-role
                // kits already match their slot and skip the handoff.
                bool assignRole = sPlayerbotAIConfig.randomBotLftAllowRoleBorrow;
                if (!assignRole)
                {
                    LftRoleKitInputs kitDecision;
                    kitDecision.queuedRole = needRole;
                    kitDecision.talentRoleMask = GetBotRoleMask(chosen);
                    kitDecision.kitRoleMask = GetCombatKitRoleMask(chosen);
                    assignRole = LftKitNeedsRoleAssignment(kitDecision);
                }
                if (assignRole)
                    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(chosen))
                    {
                        // Same rebuild as `.bot role`: a stored strategy
                        // snapshot would replay the old spec's kit (e.g.
                        // "shadow pve") under the borrowed role, and the
                        // role-dependent build (shaman tank) is rebuilt from
                        // the role here rather than whenever an unrelated
                        // reset happens to run (issue #1125).
                        ai->SetForcedRole(needRole);
                        sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
                        ai->DoSpecificAction("auto talents");
                        ai->ResetStrategies();
                    }

                bool ok = sLFTMgr.QueuePlayer(chosen, instVec, needRole);
                if (!ok)
                {
                    if (assignRole)
                        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(chosen))
                        {
                            ai->SetForcedRole(0);
                            sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
                            ai->DoSpecificAction("auto talents");
                            ai->ResetStrategies();
                        }
                    BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::LftQueued,
                        previousActivity == BotActivity::Grinding ? BotActivity::Grinding : BotActivity::Idle);
                    continue;
                }

                m_pending[guidLow] = instance;

                // Remove from eligible pool for this tick
                eligibleCandidates[chosenIdx] = nullptr;

                ++filledThisTick;
                const char* roleStr = (needRole == LFT_ROLE_TANK ? "tank" : (needRole == LFT_ROLE_HEALER ? "heal" : "dps"));
                TB_LOG_DETAIL("TortoiseBots: LFT fill queued bot %s (%s) level %u team %u %s for instance %s role %s (authoritative range %u-%u, pending %u)",
                    chosen->GetName(), chosen->GetObjectGuid().GetString().c_str(), chosen->GetLevel(), chosen->GetTeam(),
                    chosen->IsHardcore() ? "hardcore" : "softcore", instance.c_str(), roleStr, low, high, (uint32)m_pending.size());
            }
        }
    }

    if (filledThisTick)
        TB_LOG_DEBUG("TortoiseBots: LFT fill tick queued %u (max %u) activeInstances %u", filledThisTick, maxPerInterval, (uint32)activeInstances.size());
    // A missing bot beats a fake tank: log why each role stayed empty.
    for (auto const& kv : m_skipReasons)
        TB_LOG_DETAIL("TortoiseBots: LFT fill skipped %s", kv.first.c_str());
    m_skipReasons.clear();
}

void LftBotFillService::Shutdown()
{
    if (!m_initialized)
        return;
    // Leave any pending queued bots (not in offer) on shutdown and clear forced role on every exit path.
    for (auto const& kv : m_pending)
    {
        ObjectGuid guid(HIGHGUID_PLAYER, kv.first);
        if (sLFTMgr.IsQueued(guid) && !sLFTMgr.IsInOffer(guid))
        {
            sLFTMgr.LeaveQueue(guid);
        }
        ClearForcedRole(kv.first);
        BotActivityLeaseManager::Instance().Release(kv.first, BotActivity::LftQueued);
    }
    m_pending.clear();
    m_initialized = false;
    m_elapsedMs = 0;
}

void LftBotFillService::OnLeaseEvicted(uint32_t guidLow)
{
    if (!guidLow)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    sLFTMgr.LeaveQueue(guid);
    ClearForcedRole(guidLow);
    m_pending.erase(guidLow);
}

} // namespace TortoiseBots
