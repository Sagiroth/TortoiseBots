#include "playerbot/playerbot.h"
#include "RpgCampAction.h"
#include "playerbot/CampMgr.h"
#include "playerbot/ChatHelper.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/rpg/CampPolicy.h"
#include "Timer.h"
#include <algorithm>
#include <vector>

using namespace ai;

namespace
{
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
            HasTentNearPoint(bot, state.x, state.y, state.z, TortoiseBots::CAMP_TENT_EXCLUSION_RANGE_YD))
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
        uint64_t const sitCapMs = static_cast<uint64_t>(sPlayerbotAIConfig.campSitDurationSec) * 1000ULL;
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
            ai->AbandonCamp();
            return true;
        }

        if (now >= state.nextEmoteMs)
        {
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
                case TortoiseBots::CampEmoteStep::Dance:
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    bot->HandleEmoteCommand(EMOTE_STATE_DANCE);
                    break;
            }
            bool const canCook = HasCookableFood(ai, context);
            state.emoteStep = static_cast<uint8>(TortoiseBots::NextCampEmoteStep(step, canCook));
            state.nextEmoteMs = now + TortoiseBots::CAMP_EMOTE_ROTATE_MS;
        }

        ai->SetActionDuration(2000);
        return true;
    }

    return false;
}
