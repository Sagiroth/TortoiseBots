#include "playerbot/playerbot.h"
#include "RpgCampAction.h"
#include "playerbot/CampMgr.h"
#include "playerbot/ChatHelper.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/rpg/CampPolicy.h"
#include "Timer.h"
#include "World.h"
#include <algorithm>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace ai;

namespace
{
    std::mutex s_greetMutex;
    std::unordered_map<uint32, uint32> s_lastPlayerGreetMs;

    bool IsNightGameTime()
    {
        time_t const gameTime = sWorld.GetGameTime();
        std::tm tmBuf{};
        localtime_r(&gameTime, &tmBuf);
        return TortoiseBots::IsCampNightHour(static_cast<uint32_t>(tmBuf.tm_hour));
    }

    bool HasTentNearPoint(Player* bot, float x, float y, float z, float rangeYd)
    {
        if (!bot)
            return false;

        static uint32 const tentEntries[] = {
            TortoiseBots::CAMP_TENT_GO_ALLIANCE,
            TortoiseBots::CAMP_TENT_GO_HORDE,
            TortoiseBots::CAMP_TENT_GO_ALT_1,
            TortoiseBots::CAMP_TENT_GO_ALT_2
        };

        float const searchRange = TortoiseBots::CAMP_SPOT_DETECT_RANGE_YD + rangeYd;
        for (uint32 entry : tentEntries)
        {
            if (GameObject* go = bot->FindNearestGameObject(entry, searchRange))
            {
                if (go->isSpawned() && go->GetDistance(x, y, z) <= rangeYd)
                    return true;
            }
        }
        return false;
    }

    bool HasCookableFood(PlayerbotAI* ai, AiObjectContext* context)
    {
        std::vector<uint32> spellIds = AI_VALUE(std::vector<uint32>, "craft spells");
        for (uint32 spellId : spellIds)
        {
            SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
            if (!info || info->RequiresSpellFocus != 1)
                continue;
            if (AI_VALUE2(bool, "can craft spell", spellId) && AI_VALUE2(bool, "should craft spell", spellId))
                return true;
        }
        return false;
    }

    bool TryCookRawFood(PlayerbotAI* ai, Player* bot, AiObjectContext* context)
    {
        std::vector<uint32> spellIds = AI_VALUE(std::vector<uint32>, "craft spells");
        std::shuffle(spellIds.begin(), spellIds.end(), *GetRandomGenerator());
        for (uint32 spellId : spellIds)
        {
            SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
            if (!info || info->RequiresSpellFocus != 1)
                continue;
            if (!AI_VALUE2(bool, "can craft spell", spellId))
                continue;
            if (!AI_VALUE2(bool, "should craft spell", spellId))
                continue;
            if (ai->CastSpell(spellId, bot))
                return true;
        }
        return false;
    }

    void TryGreetNearbyPlayer(PlayerbotAI* ai, Player* bot, AiObjectContext* context, PlayerbotAI::CampSessionState& state, uint32 now)
    {
        std::list<ObjectGuid> players = AI_VALUE(std::list<ObjectGuid>, "nearest friendly players");
        Player* target = nullptr;
        for (ObjectGuid const& guid : players)
        {
            Player* candidate = sObjectMgr.GetPlayer(guid);
            if (!candidate || !candidate->IsInWorld() || !candidate->IsAlive())
                continue;
            if (!ai->IsRealPlayer(candidate))
                continue;
            if (bot->GetDistance(candidate) <= TortoiseBots::CAMP_PLAYER_GREET_RANGE_YD)
            {
                target = candidate;
                break;
            }
        }
        if (!target)
            return;

        uint32 const cooldownMs = sPlayerbotAIConfig.campGreetCooldownSec * 1000;
        uint32 lastPlayerMs = 0;
        {
            std::lock_guard<std::mutex> lock(s_greetMutex);
            auto it = s_lastPlayerGreetMs.find(target->GetGUIDLow());
            if (it != s_lastPlayerGreetMs.end())
                lastPlayerMs = it->second;
            if (!TortoiseBots::ShouldGreetPlayerAtCamp(now, state.lastGreetMs, lastPlayerMs, cooldownMs))
                return;
            s_lastPlayerGreetMs[target->GetGUIDLow()] = now;
        }

        state.lastGreetMs = now;
        bot->SetFacingTo(bot->GetAngle(target));
        bot->HandleEmoteCommand(urand(0, 1) ? EMOTE_ONESHOT_WAVE : EMOTE_ONESHOT_BOW);
        if (TortoiseBots::ShouldSayCampLine(sPlayerbotAIConfig.campGreetChancePct, urand(0, 99)))
            ai->Say(TortoiseBots::PickCampGreetingLine(urand(0, 999)));
    }
}

bool RpgCampAction::isUseful()
{
    if (!ai || !bot || !bot->IsAlive())
        return false;
    return ai->GetCampState().phase != PlayerbotAI::CampPhase::Idle;
}

bool RpgCampAction::Execute(Event& /*event*/)
{
    PlayerbotAI::CampSessionState& state = ai->GetCampState();
    uint32 const now = WorldTimer::getMSTime();

    if (state.phase == PlayerbotAI::CampPhase::MovingToSpot)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId ||
            (!state.isJoiner && HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD)))
        {
            ai->AbandonCamp();
            return false;
        }

        float const dist = bot->GetDistance(state.x, state.y, state.z);
        if (dist > TortoiseBots::CAMP_ARRIVAL_RANGE_YD)
        {
            if (!MoveTo(state.mapId, state.x, state.y, state.z))
            {
                ai->AbandonCamp();
                state.cooldownUntilMs = now + TortoiseBots::CAMP_ROLL_RETRY_COOLDOWN_MS;
                return false;
            }
            ai->SetActionDuration(1000);
            return true;
        }

        ai->StopMoving();
        bot->SetFacingTo(state.orientation);

        if (state.isJoiner)
        {
            state.savedRestBonus = bot->GetRestBonus();
            state.hasSavedRestBonus = true;
            bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            state.phase = PlayerbotAI::CampPhase::Camping;
            state.startedMs = now;
            state.nextEmoteMs = now + 2000;
            state.emoteStep = static_cast<uint8>(TortoiseBots::CampEmoteStep::SitEat);
            ai->SetActionDuration(2000);
            return true;
        }

        state.phase = PlayerbotAI::CampPhase::SettingUp;
    }

    if (state.phase == PlayerbotAI::CampPhase::SettingUp)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId ||
            HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD) ||
            bot->HasSpellCooldown(TortoiseBots::CAMP_TENT_SPELL_ID) ||
            !bot->HasItemCount(TortoiseBots::CAMP_TENT_ITEM_ID, 1))
        {
            ai->AbandonCamp();
            return false;
        }

        state.savedRestBonus = bot->GetRestBonus();
        state.hasSavedRestBonus = true;

        ai->DoSpecificAction("use", Event("rpg camp", ChatHelper::formatQItem(TortoiseBots::CAMP_TENT_ITEM_ID)), true);

        if (bot->HasSpell(TortoiseBots::CAMP_FIRE_SPELL_ID) &&
            bot->HasItemCount(TortoiseBots::CAMP_SIMPLE_WOOD_ITEM_ID, 1) &&
            sServerFacade.IsSpellReady(bot, TortoiseBots::CAMP_FIRE_SPELL_ID))
        {
            ai->CastSpell(TortoiseBots::CAMP_FIRE_SPELL_ID, bot);
        }

        TortoiseBots::ActiveCampInfo info;
        info.botGuid = bot->GetGUIDLow();
        info.mapId = bot->GetMapId();
        info.zoneId = bot->GetZoneId();
        info.x = state.x;
        info.y = state.y;
        info.z = state.z;
        info.spotName = state.spotName;
        sCampMgr.SetActiveCamp(info);

        state.phase = PlayerbotAI::CampPhase::Camping;
        state.startedMs = now;
        state.nextEmoteMs = now + 2500;
        state.emoteStep = static_cast<uint8>(TortoiseBots::CampEmoteStep::SitEat);
        ai->SetActionDuration(2500);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::Camping)
    {
        uint64_t const elapsedMs = static_cast<uint64_t>(now - state.startedMs);
        uint64_t const sitCapMs = state.stayCapMs ? static_cast<uint64_t>(state.stayCapMs)
                                                  : static_cast<uint64_t>(sPlayerbotAIConfig.campSitDurationSec) * 1000ULL;
        bool const pastGrace = elapsedMs >= TortoiseBots::CAMP_TENT_SPAWN_GRACE_MS;
        float const tentCheckRange = state.isJoiner
            ? (TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD + TortoiseBots::CAMP_JOIN_OFFSET_RADIUS_YD)
            : TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD;
        bool const tentStillStanding = HasTentNearPoint(bot, state.x, state.y, state.z, tentCheckRange);

        if (TortoiseBots::ShouldAbandonCamp(
                sServerFacade.IsInCombat(bot),
                bot->IsAlive(),
                ai->IsAutonomousCampEligible(),
                bot->GetMapId() == state.mapId,
                elapsedMs,
                sitCapMs,
                tentStillStanding,
                pastGrace))
        {
            if (state.isJoiner && bot->IsAlive())
                bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            ai->AbandonCamp();
            return true;
        }

        TryGreetNearbyPlayer(ai, bot, context, state, now);

        if (now >= state.nextEmoteMs)
        {
            bool const isNight = IsNightGameTime();
            auto step = static_cast<TortoiseBots::CampEmoteStep>(state.emoteStep);
            switch (step)
            {
                case TortoiseBots::CampEmoteStep::SitEat:
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
                    bot->SetStandState(UNIT_STAND_STATE_SIT);
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
                    break;
                case TortoiseBots::CampEmoteStep::Cook:
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    TryCookRawFood(ai, bot, context);
                    break;
                case TortoiseBots::CampEmoteStep::ClassFlavour:
                {
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    Pet* pet = bot->GetPet();
                    bool const hasPet = pet && pet->IsAlive();
                    TortoiseBots::CampFlavourSpec const spec = TortoiseBots::PickCampFlavourAction(
                        bot->GetClass(),
                        static_cast<uint32_t>(bot->GetHealthPercent()),
                        hasPet,
                        urand(0, 99));
                    if (spec.spellId != 0 && sServerFacade.LookupSpellInfo(spec.spellId))
                        bot->CastSpell(bot, spec.spellId, true);
                    if (spec.emoteId != 0)
                        bot->HandleEmoteCommand(spec.emoteId);
                    if (spec.petRest && hasPet)
                        pet->SetStandState(UNIT_STAND_STATE_SLEEP);
                    break;
                }
                case TortoiseBots::CampEmoteStep::Dance:
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    bot->HandleEmoteCommand(EMOTE_STATE_DANCE);
                    break;
                case TortoiseBots::CampEmoteStep::Sleep:
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    bot->HandleEmoteCommand(EMOTE_STATE_SLEEP);
                    break;
            }
            bool const canCook = !state.isJoiner && HasCookableFood(ai, context);
            state.emoteStep = static_cast<uint8>(TortoiseBots::NextCampEmoteStep(step, canCook, isNight));
            state.nextEmoteMs = now + TortoiseBots::CAMP_EMOTE_ROTATE_MS;
        }

        ai->SetActionDuration(2000);
        return true;
    }

    return false;
}
