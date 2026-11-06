#include "PassingBuffsService.h"

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/GroupBuffPolicy.h"
#include "../ai/playerbot/PassingBuffsPolicy.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/ServerFacade.h"
#include "../host/ModuleLog.h"

#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "SharedDefines.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <list>

namespace TortoiseBots
{

namespace
{

constexpr uint32_t kSweepIntervalMs = 2500;
constexpr uint32_t kMaxCastsPerSweep = 8;

bool EligibleBot(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        bot->InBattleGround() || bot->IsTaxiFlying() || !bot->GetSession() ||
        !bot->GetSession()->IsHeadless())
        return false;
    if (!BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
        return false;
    if (bot->GetGroup())
        return false;
    if (sServerFacade.IsInCombat(bot))
        return false;
    return true;
}

bool EligiblePlayer(Player* player, Player* bot)
{
    if (!player || player == bot || !player->IsInWorld() || !player->IsAlive())
        return false;
    WorldSession* session = player->GetSession();
    if (!session || !session->HasNetworkTransport())
        return false;
    if (BotManager::Instance().IsBot(player->GetObjectGuid()))
        return false;
    if (player->IsGameMaster() && !player->IsGMVisible())
        return false;
    if (player->GetVisibility() == VISIBILITY_OFF)
        return false;
    if (!sServerFacade.IsFriendlyTo(bot, player))
        return false;
    return true;
}

ai::PassingBuffClass BotBuffClass(Player* bot)
{
    switch (bot->GetClass())
    {
        case CLASS_PALADIN: return ai::PassingBuffClass::Paladin;
        case CLASS_PRIEST: return ai::PassingBuffClass::Priest;
        case CLASS_MAGE: return ai::PassingBuffClass::Mage;
        case CLASS_DRUID: return ai::PassingBuffClass::Druid;
        default: return ai::PassingBuffClass::None;
    }
}

char const* BuffForBot(PlayerbotAI* ai, Player* bot, Player* target)
{
    ai::PassingBuffClass cls = BotBuffClass(bot);
    if (cls == ai::PassingBuffClass::Paladin)
    {
        bool tank = ai->IsTank(target);
        bool healer = ai->IsHeal(target);
        bool ranged = ai->IsRanged(target);
        return ai::PassingBlessingName(tank, healer, ranged, target->GetClass());
    }
    bool targetHasMana = target->GetMaxPower(POWER_MANA) > 0;
    return ai::PassingBuffName(cls, targetHasMana);
}

} // namespace

PassingBuffsService& PassingBuffsService::Instance()
{
    static PassingBuffsService instance;
    return instance;
}

void PassingBuffsService::Update(uint32_t diff)
{
    if (!sPlayerbotAIConfig.passingBuffs)
        return;
    m_elapsedMs += diff;
    if (m_elapsedMs < kSweepIntervalMs)
        return;
    m_elapsedMs = 0;

    std::vector<Player*> realPlayers;
    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (!session || !session->HasNetworkTransport())
            continue;
        Player* player = session->GetPlayer();
        if (player && player->IsInWorld())
            realPlayers.push_back(player);
    }
    if (realPlayers.empty())
        return;
    SweepNow(realPlayers);
}

void PassingBuffsService::SweepNow(std::vector<Player*> const& realPlayers)
{
    uint32_t casts = 0;
    time_t now = time(nullptr);
    m_seenPairsNext.clear();
    for (Player* player : realPlayers)
    {
        if (casts >= kMaxCastsPerSweep)
            break;
        if (!player || !player->IsInWorld() || !player->IsAlive())
            continue;

        std::list<Unit*> nearby;
        MaNGOS::AnyFriendlyUnitInObjectRangeCheck check(player, sPlayerbotAIConfig.sightDistance);
        MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck> searcher(nearby, check);
        Cell::VisitAllObjects(player, searcher, sPlayerbotAIConfig.sightDistance);

        std::vector<Player*> bots;
        for (Unit* unit : nearby)
        {
            Player* bot = dynamic_cast<Player*>(unit);
            if (!bot || !EligibleBot(bot))
                continue;
            if (!EligiblePlayer(player, bot))
                continue;
            float dist = sServerFacade.GetDistance2d(bot, player);
            if (!ai::PassingInMeetingRange(dist, sPlayerbotAIConfig.sightDistance))
                continue;
            if (!sServerFacade.IsWithinLOSInMap(bot, player))
                continue;
            bots.push_back(bot);
        }
        std::sort(bots.begin(), bots.end(), [](Player* a, Player* b)
            { return a->GetObjectGuid().GetRawValue() < b->GetObjectGuid().GetRawValue(); });

        for (Player* bot : bots)
        {
            if (casts >= kMaxCastsPerSweep)
                break;
            PairKey key{bot->GetGUIDLow(), player->GetGUIDLow()};
            m_seenPairsNext.emplace(key, true);
            bool known = m_seenPairs.find(key) != m_seenPairs.end();
            if (!ai::PassingIsNewMeeting(!known))
                continue;
            if (TryHeal(bot, player, now) || TryBuff(bot, player, now))
                ++casts;
        }
    }
    m_seenPairs.swap(m_seenPairsNext);

    for (auto it = m_seenPairs.begin(); it != m_seenPairs.end();)
    {
        if (m_seenPairsNext.find(it->first) == m_seenPairsNext.end() &&
            m_buffCooldowns.find(it->first) == m_buffCooldowns.end() &&
            m_healCooldowns.find(it->first) == m_healCooldowns.end())
            it = m_seenPairs.erase(it);
        else
            ++it;
    }
}

bool PassingBuffsService::TryHeal(Player* bot, Player* player, time_t now)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;
    char const* heal = ai::PassingHealName(BotBuffClass(bot));
    if (!heal)
        return false;
    if (!player->HealthBelowPct(ai::kPassingHealBelowPct) || ai->HasAura(heal, player))
        return false;
    PairKey key{bot->GetGUIDLow(), player->GetGUIDLow()};
    auto cd = m_healCooldowns.find(key);
    if (cd != m_healCooldowns.end() && !ai::PassingCooldownReady(cd->second, uint32_t(now), ai::kPassingHealCooldownSec))
        return false;
    for (auto it = m_healCooldowns.begin(); it != m_healCooldowns.end();)
    {
        if (uint32_t(now) >= it->second + ai::kPassingHealCooldownSec)
            it = m_healCooldowns.erase(it);
        else
            ++it;
    }
    // The bot itself stays out of combat; the wounded player may be fighting.
    std::string healName(heal);
    uint32_t healId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", healName)->Get();
    if (!healId || !ai->HasSpell(healId))
        return false;
    SpellEntry const* healInfo = sServerFacade.LookupSpellInfo(healId);
    if (!healInfo)
        return false;
    // Only instant HoTs (Renew, Rejuvenation): the bot casts on the move.
    if (GetSpellCastTime(healInfo, bot) > 0 || (IsChanneledSpell(healInfo) && GetSpellDuration(healInfo) > 0))
        return false;
    if (!ai->CanCastSpell(healId, player))
        return false;
    if (!ai->CastSpell(healId, player, nullptr, false))
        return false;
    m_healCooldowns[key] = now;
    TB_LOG_BASIC("TortoiseBots: passing heal %s -> %s (%s)", bot->GetName(), player->GetName(), heal);
    return true;
}

bool PassingBuffsService::TryBuff(Player* bot, Player* player, time_t now)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;
    char const* buff = BuffForBot(ai, bot, player);
    if (!buff)
        return false;
    PairKey key{bot->GetGUIDLow(), player->GetGUIDLow()};
    auto cd = m_buffCooldowns.find(key);
    if (cd != m_buffCooldowns.end() && !ai::PassingCooldownReady(cd->second, uint32_t(now), ai::kPassingBuffCooldownSec))
        return false;
    for (auto it = m_buffCooldowns.begin(); it != m_buffCooldowns.end();)
    {
        if (uint32_t(now) >= it->second + ai::kPassingBuffCooldownSec)
            it = m_buffCooldowns.erase(it);
        else
            ++it;
    }
    if (urand(0, ai::kPassingBuffChanceDivisor - 1) != 0)
        return false;
    std::string buffName(buff);
    uint32_t spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", buffName)->Get();
    if (!spellId || !ai->HasSpell(spellId))
        return false;
    // Highest rank the bot knows that is allowed on the player's level: the
    // name lookup lands the top known rank, whose MaxTargetLevel the core
    // refuses on a low-level player. Walk the chain down to the first rank
    // the player qualifies for.
    uint32_t playerLevel = player->GetLevel();
    for (;;)
    {
        SpellEntry const* rankInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!rankInfo)
            return false;
        if (rankInfo->MaxTargetLevel == 0 || playerLevel <= rankInfo->MaxTargetLevel)
            break;
        uint32_t prev = sSpellMgr.GetPrevSpellInChain(spellId);
        if (!prev || !ai->HasSpell(prev))
            return false;
        spellId = prev;
    }
    SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
    if (!info)
        return false;
    // Only instant spells: the bot casts on the move and never stops.
    if (GetSpellCastTime(info, bot) > 0 || (IsChanneledSpell(info) && GetSpellDuration(info) > 0))
        return false;
    // Keep half the mana bar after the cast, same floor the upkeep uses.
    uint32_t cost = info->manaCost
        ? info->manaCost
        : uint32_t(info->ManaCostPercentage) * bot->GetCreateMana() / 100;
    if (!ai::ManaStaysAboveFloor(bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA), cost, 50))
        return false;
    // Missing buff and nothing stronger: the group variant covers too.
    std::string groupName = ai::GroupBuffVariantFor(buffName);
    if (ai->HasAura(buffName, player))
        return false;
    if (!groupName.empty() && ai->HasAura(groupName, player))
        return false;
    if (!ai->CanCastSpell(spellId, player))
        return false;
    if (!ai->CastSpell(spellId, player, nullptr, false))
        return false;
    m_buffCooldowns[key] = now;
    TB_LOG_BASIC("TortoiseBots: passing buff %s -> %s (%s)", bot->GetName(), player->GetName(), buff);
    return true;
}

} // namespace TortoiseBots
