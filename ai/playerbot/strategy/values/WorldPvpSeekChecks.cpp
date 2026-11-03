
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "../../../runtime/PlayerbotAIStorage.h"
#include "WorldPvpSeekChecks.h"
#include "EnemyPlayerValue.h"
#include "PossibleTargetsValue.h"
#include "../../../runtime/WorldPvpPolicy.h"
#include "../../../runtime/policy/pvp/WorldPvpFightPolicy.h"

using namespace ai;

static bool IsChallengeModeTarget(Player* target)
{
    return target->HasChallenge(CHALLENGE_SLOW_AND_STEADY) || target->HasChallenge(CHALLENGE_EXHAUSTION_MODE) ||
        target->HasChallenge(CHALLENGE_HARDCORE) || target->HasChallenge(CHALLENGE_VAGRANT_MODE) ||
        target->HasChallenge(CHALLENGE_BOARING_MODE) || target->HasChallenge(CHALLENGE_CRAFTMASTER) ||
        target->HasChallenge(CHALLENGE_LUNATIC) || target->HasChallenge(CHALLENGE_BREWMASTER) ||
        target->HasChallenge(CHALLENGE_HEROIC);
}

// Standing at the spirit healer the player revives at. Both factions often
// share a graveyard, and a fight started there turned into an endless
// revive-and-kill loop (Raven Hill Cemetery).
static bool NearOwnGraveyard(Player* player)
{
    WorldSafeLocsEntry const* grave = sObjectMgr.GetClosestGraveYard(
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
    return grave && grave->map_id == player->GetMapId() && player->GetDistance2d(grave->x, grave->y) < 40.0f;
}

bool WorldPvpSeekChecks::IsSeeking(PlayerbotAI* ai, Player* bot)
{
    if (!ai || !bot)
        return false;
    if (!sPlayerbotAIConfig.worldPvpSeek)
        return false;
    if (bot->InBattleGround())
        return false;
    return ai->HasStrategy("world pvp", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("world pvp", BotState::BOT_STATE_COMBAT);
}

bool WorldPvpSeekChecks::IsSeekTarget(PlayerbotAI* ai, Player* bot, Player* target)
{
    if (!ai || !bot || !target || target == bot)
        return false;
    if (bot->InBattleGround())
        return false;
    if (!sPlayerbotAIConfig.worldPvpSeek)
        return false;
    if (sPlayerbotAIConfig.worldPvpSeek < 2 && PlayerbotAIStorage::Instance().GetAI(target))
        return false;
    if (!target->IsPvP())
        return false;
    if (target->IsGameMaster() && !target->IsGMVisible())
        return false;
    if (IsChallengeModeTarget(target))
        return false;
    if (sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(target)))
        return false;
    if (!target->IsInWorld() || target->IsBeingTeleported() || !target->IsAlive())
        return false;
    if (target->GetMapId() != bot->GetMapId())
        return false;

    uint32 const nowMs = WorldTimer::getMSTime();
    uint32 const targetLow = target->GetGUIDLow();
    if (ai->worldPvpIgnoreGuidLow_ == targetLow &&
        TortoiseBots::WorldPvpIgnoreFresh(ai->worldPvpIgnoreStartMs_, nowMs))
        return false;

    float const aggroDistance = bot->GetHealth() > target->GetHealth() ?
        EnemyPlayerValue::GetMaxAttackDistance(bot) : 20.0f;
    if (!bot->IsWithinDist(target, aggroDistance))
        return false;
    if (fabs(bot->GetPositionZ() - target->GetPositionZ()) >= 30.0f)
        return false;
    if (NearOwnGraveyard(bot) || NearOwnGraveyard(target))
        return false;
    if (!bot->IsWithinLOSInMap(target, true))
        return false;

    std::int32_t const gap = std::int32_t(target->GetLevel()) - std::int32_t(bot->GetLevel());
    std::uint32_t const odds = TortoiseBots::WorldPvpLevelGapOdds(gap);
    if (odds == 0)
        return false;
    if (odds >= 100)
        return true;

    uint32 const nowSec = nowMs / 1000;
    bool known = false;
    if (ai->WorldPvpGapAccepted(targetLow, nowSec, known))
        return true;
    if (known)
        return false;
    if (ai->worldPvpPursuitGuidLow_ == targetLow && ai->worldPvpPursuitStartMs_)
    {
        ai->WorldPvpNoteGapDecision(targetLow, nowSec, true);
        return true;
    }
    bool const accept = urand(0, 99) < odds;
    ai->WorldPvpNoteGapDecision(targetLow, nowSec, accept);
    return accept;
}

Unit* WorldPvpSeekChecks::FindAssistTarget(PlayerbotAI* ai, Player* bot)
{
    if (!IsSeeking(ai, bot))
        return nullptr;

    std::list<ObjectGuid> candidates = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest friendly players")->Get();
    for (ObjectGuid const& guid : candidates)
    {
        Unit* unit = ai->GetUnit(guid);
        Player* victim = dynamic_cast<Player*>(unit);
        if (!victim || victim == bot)
            continue;
        if (!sServerFacade.IsFriendlyTo(victim, bot))
            continue;
        if (!bot->IsWithinDist(victim, 30.0f))
            continue;

        if (Unit* victimTarget = victim->GetVictim())
        {
            if (Player* enemy = dynamic_cast<Player*>(victimTarget))
            {
                if (!sServerFacade.IsFriendlyTo(enemy, bot) &&
                    EnemyPlayersValue::IsValid(enemy, bot) && IsSeekTarget(ai, bot, enemy))
                    return enemy;
            }
        }
        for (Unit* attacker : victim->GetAttackers())
        {
            Player* enemy = dynamic_cast<Player*>(attacker);
            if (!enemy)
                continue;
            if (sServerFacade.IsFriendlyTo(enemy, bot))
                continue;
            if (EnemyPlayersValue::IsValid(enemy, bot) && IsSeekTarget(ai, bot, enemy))
                return enemy;
        }
    }

    return nullptr;
}
