// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:unknown_type_name,clang:undeclared_var_use,clang:use_of_undeclared_identifier,clang:incomplete_member_access,clang:member_decl_does_not_match,clang:all,clang:pp_include_next
#include "LftBotFillService.h"

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/AiFactory.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
#include "LFT/LFTMgr.h"
#ifndef MANGOSSERVER_LFTMGR_H
#error "TortoiseBots LFT fill requires Penqle core #413 (LFTMgr.h)"
#endif

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_set>

namespace TortoiseBots
{

namespace
{
bool HasInstance(std::vector<std::string> const& v, std::string const& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

uint8 PickRole(uint8 mask, uint8 tanks, uint8 healers, uint8 dps)
{
    if ((mask & LFT_ROLE_TANK) && tanks < 1) return LFT_ROLE_TANK;
    if ((mask & LFT_ROLE_HEALER) && healers < 1) return LFT_ROLE_HEALER;
    if ((mask & LFT_ROLE_DAMAGE) && dps < 3) return LFT_ROLE_DAMAGE;
    return 0;
}

struct DungeonLevelRange
{
    uint8 minLevel;
    uint8 maxLevel;
};

std::string NormalizeInstanceToken(std::string const& raw)
{
    std::string normalized;
    normalized.reserve(raw.size());
    for (unsigned char character : raw)
        if (std::isalnum(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized;
}

DungeonLevelRange const* FindDungeonLevelRange(std::string const& raw)
{
    // Source: Soromeister/LFT v0.0.3.3 LFT.allDungeons (authoritative queue availability).
    // Exact codes/names/minLevel/maxLevel from the addon; unknown/corrupt ranges fail closed.
    // Keys are NormalizeInstanceToken(raw) = alnum lowercased, so addon codes and normalized
    // display names both resolve. No DBC fallback, no invented Turtle-only ranges beyond this list.
    static std::unordered_map<std::string, DungeonLevelRange> const ranges = {
        {"ragefirechasm", {13, 18}}, {"rfc", {13, 18}},
        {"wailingcaverns", {17, 24}}, {"wc", {17, 24}},
        {"thedeadmines", {17, 24}}, {"deadmines", {17, 24}}, {"dm", {17, 24}},
        {"shadowfangkeep", {22, 30}}, {"sfk", {22, 30}},
        {"thestockade", {22, 30}}, {"stockade", {22, 30}}, {"stocks", {22, 30}},
        {"blackfathomdeeps", {23, 32}}, {"bfd", {23, 32}},
        {"scarletmonasterygraveyard", {27, 36}}, {"smgy", {27, 36}},
        {"scarletmonasterylibrary", {28, 39}}, {"smlib", {28, 39}},
        {"gnomeregan", {29, 38}}, {"gnomer", {29, 38}},
        {"razorfenkraul", {29, 38}}, {"rfk", {29, 38}},
        {"thecrescentgrove", {32, 38}}, {"crescentgrove", {32, 38}}, {"tcg", {32, 38}},
        {"scarletmonasteryarmory", {32, 41}}, {"smarmory", {32, 41}},
        {"scarletmonasterycathedral", {35, 45}}, {"smcath", {35, 45}},
        {"razorfendowns", {36, 46}}, {"rfd", {36, 46}},
        {"uldaman", {40, 51}}, {"ulda", {40, 51}},
        {"gilneascity", {42, 50}}, {"gilneas", {42, 50}},
        {"zulfarrak", {44, 54}}, {"zulfarak", {44, 54}}, {"zf", {44, 54}},
        {"maraudonorange", {47, 55}}, {"maraorange", {47, 55}},
        {"maraudonpurple", {45, 55}}, {"marapurple", {45, 55}},
        {"maraudonprincess", {47, 55}}, {"maraprincess", {47, 55}},
        {"templeofatalhakkar", {50, 60}}, {"st", {50, 60}},
        {"hateforgequarry", {50, 60}}, {"hfq", {50, 60}},
        {"blackrockdepths", {52, 60}}, {"brd", {52, 60}},
        {"blackrockdepthsarena", {52, 60}}, {"brdarena", {52, 60}},
        {"blackrockdepthsemperor", {54, 60}}, {"brdemp", {54, 60}},
        {"lowerblackrockspire", {55, 60}}, {"lbrs", {55, 60}},
        {"diremauleast", {55, 60}}, {"dme", {55, 60}},
        {"diremaulnorth", {57, 60}}, {"dmn", {57, 60}},
        {"diremaultribute", {57, 60}}, {"dmt", {57, 60}},
        {"diremaulwest", {57, 60}}, {"dmw", {57, 60}},
        {"scholomance", {58, 60}}, {"scholo", {58, 60}},
        {"stratholmeundeaddistrict", {58, 60}}, {"stratud", {58, 60}},
        {"stratholmescarletbastion", {58, 60}}, {"stratlive", {58, 60}},
        {"karazhancrypt", {58, 60}}, {"kc", {58, 60}},
        {"cavernsoftimeblackmorass", {60, 60}}, {"cotbm", {60, 60}},
        {"stormwindvault", {60, 60}}, {"swv", {60, 60}}
    };

    std::string token = NormalizeInstanceToken(raw);
    auto it = ranges.find(token);
    if (it == ranges.end() || it->second.minLevel == 0 ||
        it->second.minLevel > it->second.maxLevel || it->second.maxLevel > 60)
    {
        static std::set<std::string> loggedUnknown;
        if (loggedUnknown.insert(raw).second)
            sLog.outError("TortoiseBots: LFT fill has no valid authoritative level range for instance '%s'; skipping", raw.c_str());
        return nullptr;
    }

    return &it->second;
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
        sLog.outString("TortoiseBots: LFT fill disabled (ai %u lft %u)", sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.randomBotLftEnabled);
        return;
    }
    sLog.outString("TortoiseBots: LFT fill enabled interval %u max %u (observe GetQueuedPlayers, native QueuePlayer/offers, reconcile)",
        sPlayerbotAIConfig.randomBotLftUpdateInterval, sPlayerbotAIConfig.randomBotLftMaxFillsPerInterval);
}

bool LftBotFillService::IsEligibleCandidate(Player* bot) const
{
    if (!bot) return false;
    if (!bot->IsInWorld()) return false;
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless()) return false;
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
    return true;
}

bool LftBotFillService::IsModuleOwnedHeadlessBot(Player const* bot) const
{
    if (!bot) return false;
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless()) return false;
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
    return static_cast<uint8>(r);
}

void LftBotFillService::ClearForcedRole(uint32 guidLow)
{
    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    if (Player* p = sObjectAccessor.FindPlayer(guid))
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(p))
            ai->SetForcedRole(0);
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
            // Completed, cancelled, or grouped – pending is stale. Clear forced role on every exit.
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
            sLog.outString("TortoiseBots: LFT fill reconcile cancel bot %u (no human waiting, cancelAll)", guidLow);
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
                sLog.outString("TortoiseBots: LFT fill reconcile cancel bot %u instance %s (no longer active)", guidLow, inst.c_str());
                toErase.push_back(guidLow);
            }
        }
    }
    for (uint32 g : toErase)
        m_pending.erase(g);
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
        // World-thread only generic core API (a2bd178, PR #413 final). Reuses native offer accept.
        bool ok = sLFTMgr.AcceptOffer(guid);
        if (ok)
            sLog.outString("TortoiseBots: LFT fill bot %s (%s) accepted offer (pending %u)", bot->GetName(), guid.GetString().c_str(), (uint32)m_pending.size());
    }
}

void LftBotFillService::Update(uint32_t diff)
{
    if (!m_initialized)
        return;
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotLftEnabled)
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

    // Snapshot native queue (copy, no private map exposure)
    std::vector<LFTManager::QueuedInfo> queued = sLFTMgr.GetQueuedPlayers();

    // Drive pending offers to completion for our Headless bots via generic core AcceptOffer.
    // Humans still explicitly accept via addon C2S_OFFER_ACCEPT; we never auto-accept humans.
    // Preserve core cancellation/requeue by reusing native HandleOfferAccept semantics.
    if (!m_pending.empty())
        AcceptPendingOffers();

    // Reconcile stale pending tracking (left queue/offer) with forced-role cleanup on every path.
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
            ClearForcedRole(g);
            m_pending.erase(g);
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

            // Gather already queued players (humans + bots) for this instance/team/hardcore
            std::vector<LFTManager::QueuedInfo> together;
            together.reserve(queued.size());
            for (auto const& q : queued)
            {
                if (!HasInstance(q.instances, instance))
                    continue;
                if (q.team != part.team || q.isHardcore != part.hardcore)
                    continue;
                together.push_back(q);
            }

            if (together.empty())
                continue;
            bool hasHuman = false;
            for (auto const& t : together)
                if (!BotManager::Instance().IsBot(t.guid)) { hasHuman = true; break; }
            if (!hasHuman)
                continue;

            // Compute role assignment for together sorted by joinTime
            std::sort(together.begin(), together.end(), [](LFTManager::QueuedInfo const& a, LFTManager::QueuedInfo const& b){ return a.joinTime < b.joinTime; });
            uint8 tanks = 0, healers = 0, dps = 0;
            for (auto const& q : together)
            {
                uint8 role = PickRole(q.roleMask, tanks, healers, dps);
                if (!role) continue;
                if (role == LFT_ROLE_TANK) ++tanks;
                else if (role == LFT_ROLE_HEALER) ++healers;
                else if (role == LFT_ROLE_DAMAGE) ++dps;
            }
            uint8 needTank = (tanks < 1) ? 1 - tanks : 0;
            uint8 needHeal = (healers < 1) ? 1 - healers : 0;
            uint8 needDps = (dps < 3) ? 3 - dps : 0;
            if (needTank == 0 && needHeal == 0 && needDps == 0)
                continue;

            // Use the authoritative instance range, never the waiting humans'
            // average level. Unknown or corrupt/custom instances fail closed.
            DungeonLevelRange const* range = FindDungeonLevelRange(instance);
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
                    uint8 candMask = GetBotRoleMask(cand);
                    if ((candMask & needRole) == 0)
                        continue;
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
                std::vector<std::string> instVec;
                instVec.push_back(instance);
                // Set forced role so AI rebuilds with correct spec (tank/heal/dps)
                if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(chosen))
                    ai->SetForcedRole(needRole);

                bool ok = sLFTMgr.QueuePlayer(chosen, instVec, needRole);
                if (!ok)
                {
                    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(chosen))
                        ai->SetForcedRole(0);
                    continue;
                }

                m_pending[guidLow] = instance;

                // Remove from eligible pool for this tick
                eligibleCandidates[chosenIdx] = nullptr;

                ++filledThisTick;
                const char* roleStr = (needRole == LFT_ROLE_TANK ? "tank" : (needRole == LFT_ROLE_HEALER ? "heal" : "dps"));
                sLog.outString("TortoiseBots: LFT fill queued bot %s (%s) level %u team %u %s for instance %s role %s (authoritative range %u-%u, pending %u)",
                    chosen->GetName(), chosen->GetObjectGuid().GetString().c_str(), chosen->GetLevel(), chosen->GetTeam(),
                    chosen->IsHardcore() ? "hardcore" : "softcore", instance.c_str(), roleStr, low, high, (uint32)m_pending.size());
            }
        }
    }

    if (filledThisTick)
        sLog.outString("TortoiseBots: LFT fill tick queued %u (max %u) activeInstances %u", filledThisTick, maxPerInterval, (uint32)activeInstances.size());
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
    }
    m_pending.clear();
    m_initialized = false;
    m_elapsedMs = 0;
}

} // namespace TortoiseBots
