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

    float SnapGroundZ(Player* bot, float x, float y, float z)
    {
        if (Map* map = bot ? bot->GetMap() : nullptr)
        {
            float const floor = map->GetHeight(x, y, z + 5.0f);
            if (floor > INVALID_HEIGHT)
                return floor;
        }
        return z;
    }

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
            ai->AbandonCamp("spot no longer valid");
            return false;
        }

        TortoiseBots::CampPoint target = state.isJoiner
            ? TortoiseBots::ComputeJoinerSeatPoint(state.x, state.y, state.z, state.orientation)
            : TortoiseBots::ComputeTentCastPoint(state.x, state.y, state.z, state.orientation);
        target.z = SnapGroundZ(bot, target.x, target.y, target.z);

        float const dist2d = bot->GetDistance2d(target.x, target.y);
        uint64_t const moveElapsedMs = static_cast<uint64_t>(now - state.moveStartedMs);
        if (!TortoiseBots::ShouldFinishCampMove(dist2d, TortoiseBots::CAMP_ARRIVAL_RANGE_YD, moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
        {
            if (!MoveTo(state.mapId, target.x, target.y, target.z))
            {
                ai->AbandonCamp("move to spot failed");
                state.cooldownUntilMs = now + TortoiseBots::CAMP_ROLL_RETRY_COOLDOWN_MS;
                return false;
            }
            ai->SetActionDuration(1000);
            return true;
        }

        if (TortoiseBots::HasCampMoveTimedOut(moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
            sLog.outDetail("TortoiseBots: [Camp] %s: MovingToSpot timed out (dist2d=%.1f), continuing from current position.",
                bot->GetName(), dist2d);

        ai->StopMoving();
        bot->SetFacingTo(target.o);

        if (!state.hasSavedRestBonus)
        {
            state.savedRestBonus = bot->GetRestBonus();
            state.hasSavedRestBonus = true;
        }

        if (state.isJoiner)
        {
            bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            state.phase = PlayerbotAI::CampPhase::Camping;
            state.startedMs = now;
            state.nextEmoteMs = now + 2000;
            state.emoteStep = static_cast<uint8>(TortoiseBots::CampEmoteStep::SitEat);
            sLog.outDetail("TortoiseBots: [Camp] %s: MovingToSpot -> Camping (joiner).", bot->GetName());
            ai->SetActionDuration(2000);
            return true;
        }

        state.phase = PlayerbotAI::CampPhase::SettingUp;
        sLog.outDetail("TortoiseBots: [Camp] %s: MovingToSpot -> SettingUp.", bot->GetName());
    }

    if (state.phase == PlayerbotAI::CampPhase::SettingUp)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId ||
            HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD) ||
            bot->HasSpellCooldown(TortoiseBots::CAMP_TENT_SPELL_ID) ||
            !bot->HasItemCount(TortoiseBots::CAMP_TENT_ITEM_ID, 1))
        {
            ai->AbandonCamp("setting up precondition failed");
            return false;
        }

        if (!state.hasSavedRestBonus)
        {
            state.savedRestBonus = bot->GetRestBonus();
            state.hasSavedRestBonus = true;
        }

        bot->SetFacingTo(state.orientation);
        ai->DoSpecificAction("use", Event("rpg camp", ChatHelper::formatQItem(TortoiseBots::CAMP_TENT_ITEM_ID)), true);

        state.startedMs = now;
        state.phase = PlayerbotAI::CampPhase::WaitingTentCast;
        sLog.outDetail("TortoiseBots: [Camp] %s: SettingUp -> WaitingTentCast.", bot->GetName());
        ai->SetActionDuration(500);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::WaitingTentCast)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId)
        {
            ai->AbandonCamp("ineligible during tent cast");
            return false;
        }

        bool const isCasting = bot->IsNonMeleeSpellCasted(false, true, true);
        bool const tentSpawned = HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD);
        uint64_t const elapsedMs = static_cast<uint64_t>(now - state.startedMs);
        TortoiseBots::CampCastWaitResult const res = TortoiseBots::EvaluateTentCastWait(
            isCasting, tentSpawned, elapsedMs, TortoiseBots::CAMP_TENT_CAST_TIMEOUT_MS);

        if (res == TortoiseBots::CampCastWaitResult::Wait)
        {
            ai->SetActionDuration(500);
            return true;
        }
        if (res == TortoiseBots::CampCastWaitResult::Failed)
        {
            ai->AbandonCamp("tent cast failed");
            state.cooldownUntilMs = now + TortoiseBots::CAMP_ROLL_RETRY_COOLDOWN_MS;
            return false;
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

        state.phase = PlayerbotAI::CampPhase::MovingToFire;
        state.moveStartedMs = now;
        sLog.outDetail("TortoiseBots: [Camp] %s: WaitingTentCast -> MovingToFire.", bot->GetName());
        ai->SetActionDuration(500);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::MovingToFire)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId)
        {
            ai->AbandonCamp("ineligible moving to fire");
            return false;
        }

        TortoiseBots::CampPoint firePt = TortoiseBots::ComputeFireCastPoint(state.x, state.y, state.z, state.orientation);
        firePt.z = SnapGroundZ(bot, firePt.x, firePt.y, firePt.z);

        float const dist2d = bot->GetDistance2d(firePt.x, firePt.y);
        uint64_t const moveElapsedMs = static_cast<uint64_t>(now - state.moveStartedMs);
        if (!TortoiseBots::ShouldFinishCampMove(dist2d, TortoiseBots::CAMP_ARRIVAL_RANGE_YD, moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
        {
            if (!MoveTo(state.mapId, firePt.x, firePt.y, firePt.z))
            {
                ai->AbandonCamp("move to fire failed");
                return false;
            }
            ai->SetActionDuration(800);
            return true;
        }

        if (TortoiseBots::HasCampMoveTimedOut(moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
            sLog.outDetail("TortoiseBots: [Camp] %s: MovingToFire timed out (dist2d=%.1f), continuing from current position.",
                bot->GetName(), dist2d);

        ai->StopMoving();
        bot->SetFacingTo(firePt.o);
        if (sServerFacade.LookupSpellInfo(TortoiseBots::CAMP_FIRE_SPELL_ID))
        {
            bot->CastSpell(bot, TortoiseBots::CAMP_FIRE_SPELL_ID, true);
            state.startedMs = now;
            state.phase = PlayerbotAI::CampPhase::WaitingFireCast;
            sLog.outDetail("TortoiseBots: [Camp] %s: MovingToFire -> WaitingFireCast.", bot->GetName());
            ai->SetActionDuration(500);
            return true;
        }

        state.phase = PlayerbotAI::CampPhase::MovingToSeat;
        state.moveStartedMs = now;
        sLog.outDetail("TortoiseBots: [Camp] %s: MovingToFire -> MovingToSeat.", bot->GetName());
        ai->SetActionDuration(500);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::WaitingFireCast)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId)
        {
            ai->AbandonCamp("ineligible during fire cast");
            return false;
        }

        bool const isCasting = bot->IsNonMeleeSpellCasted(false, true, true);
        TortoiseBots::CampCastWaitResult const res = TortoiseBots::EvaluateSpellCastWait(
            isCasting, static_cast<uint64_t>(now - state.startedMs), TortoiseBots::CAMP_FIRE_CAST_TIMEOUT_MS);
        if (res == TortoiseBots::CampCastWaitResult::Wait)
        {
            ai->SetActionDuration(500);
            return true;
        }

        state.phase = PlayerbotAI::CampPhase::MovingToSeat;
        state.moveStartedMs = now;
        sLog.outDetail("TortoiseBots: [Camp] %s: WaitingFireCast -> MovingToSeat.", bot->GetName());
        ai->SetActionDuration(500);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::MovingToSeat)
    {
        if (!ai->IsAutonomousCampEligible() || bot->GetMapId() != state.mapId)
        {
            ai->AbandonCamp("ineligible moving to seat");
            return false;
        }

        TortoiseBots::CampPoint seatPt = TortoiseBots::ComputeOwnerSeatPoint(state.x, state.y, state.z, state.orientation);
        seatPt.z = SnapGroundZ(bot, seatPt.x, seatPt.y, seatPt.z);

        float const dist2d = bot->GetDistance2d(seatPt.x, seatPt.y);
        uint64_t const moveElapsedMs = static_cast<uint64_t>(now - state.moveStartedMs);
        if (!TortoiseBots::ShouldFinishCampMove(dist2d, TortoiseBots::CAMP_ARRIVAL_RANGE_YD, moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
        {
            if (!MoveTo(state.mapId, seatPt.x, seatPt.y, seatPt.z))
            {
                ai->AbandonCamp("move to seat failed");
                return false;
            }
            ai->SetActionDuration(800);
            return true;
        }

        if (TortoiseBots::HasCampMoveTimedOut(moveElapsedMs, TortoiseBots::CAMP_MOVE_TIMEOUT_MS))
            sLog.outDetail("TortoiseBots: [Camp] %s: MovingToSeat timed out (dist2d=%.1f), continuing from current position.",
                bot->GetName(), dist2d);

        ai->StopMoving();
        bot->SetFacingTo(seatPt.o);

        state.phase = PlayerbotAI::CampPhase::Camping;
        state.startedMs = now;
        state.nextEmoteMs = now + 2000;
        state.emoteStep = static_cast<uint8>(TortoiseBots::CampEmoteStep::SitEat);
        sLog.outDetail("TortoiseBots: [Camp] %s: MovingToSeat -> Camping.", bot->GetName());
        ai->SetActionDuration(2000);
        return true;
    }

    if (state.phase == PlayerbotAI::CampPhase::Camping)
    {
        uint64_t const elapsedMs = static_cast<uint64_t>(now - state.startedMs);
        uint64_t const sitCapMs = state.stayCapMs ? static_cast<uint64_t>(state.stayCapMs)
                                                  : static_cast<uint64_t>(sPlayerbotAIConfig.campSitDurationSec) * 1000ULL;
        bool const pastGrace = elapsedMs >= TortoiseBots::CAMP_TENT_SPAWN_GRACE_MS;
        bool const tentStillStanding = HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD);

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
            char const* reason = "completed";
            if (sServerFacade.IsInCombat(bot))
                reason = "combat";
            else if (!bot->IsAlive())
                reason = "dead";
            else if (!ai->IsAutonomousCampEligible())
                reason = "ineligible";
            else if (bot->GetMapId() != state.mapId)
                reason = "map changed";
            else if (pastGrace && !tentStillStanding)
                reason = "tent despawned";
            else if (elapsedMs >= sitCapMs)
                reason = "duration cap";

            if (state.isJoiner && bot->IsAlive())
                bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            ai->AbandonCamp(reason);
            return true;
        }

        TryGreetNearbyPlayer(ai, bot, context, state, now);

        if (bot->IsNonMeleeSpellCasted(false, true, true))
        {
            ai->SetActionDuration(500);
            return true;
        }

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
