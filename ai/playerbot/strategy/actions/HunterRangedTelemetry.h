#pragma once
//
// Hunter ranged telemetry (diagnosis aid, 2026-09-29).
//
// Hunter ammo stacks cannot answer "do hunters shoot?": the item cheat refills
// the equipped ammo stack to its maximum on every update (PlayerbotAI.cpp, the
// BotCheatMask::item block), so a stack sits at 200 whether or not the bot
// fires. These rows answer the question from the decision side instead:
//
//   AutoShot       - the module (re)started the hunter's ranged auto-attack,
//                    with cast=<0/1> (did the core accept the cast)
//   SwitchToMelee  - the hunter traded its ranged kit for melee (-ranged,+close)
//   SwitchToRanged - the hunter traded melee back for ranged (-close,+ranged)
//
// Every row carries the distance to the current target and the two strategy
// flags that gate the shot, captured BEFORE the switch is applied. Rows go to
// bot_events.csv through sPlayerbotAIConfig.logEvent, so they are a no-op
// unless bot_events.csv is in AiPlayerbot.AllowedLogFiles. Each event name is
// throttled to one row per bot per 5 s through the bot's own "manual time"
// store - action instances survive strategy rebuilds, but the throttle
// bookkeeping belongs to the bot, not to the action object.
//
#include <cstdio>
#include <ctime>
#include <string>

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    // Why-flag for a kit switch, appended to the row's info2 (cheap getters
    // only; rows are throttled to one per bot per 5 s). Names the exact
    // branch that fired: live 1 h showed SwitchToRanged landing at
    // dist < 5 yd in 83% of melee->ranged pairs, which the distance edge
    // alone cannot explain - these flags say which branch did it.
    //   toRanged=false (melee): on=victim on bot, imm=immobilized,
    //     fast=fast-or-finisher, fin=pet-gone finisher exception,
    //     glue=true dist <= 8 yd, na=no ammo.
    //   toRanged=true (ranged): off=victim off bot, imm=immobilized,
    //     slow=too slow net of the finisher exception,
    //     far=true dist > 8 yd.
    // Distance here is bot->GetDistance(target), the same call the switch
    // triggers gate on, so the flags and the logged dist always agree.
    inline std::string HunterSwitchWhy(PlayerbotAI* ai, Unit* target, bool toRanged)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || !target)
            return "why=?";
        float const dist = bot->GetDistance(target);
        bool const finisher = (!bot->GetPet() || bot->GetPet()->IsDead()) &&
            target->IsCreature() && target->GetHealthPercent() < 50.f &&
            target->GetHealth() < bot->GetHealth();
        char buf[96];
        if (toRanged)
        {
            bool const off = target->GetVictim() != bot;
            bool const imm = IsImmobilizedStateCompat(target);
            bool const slow = target->GetSpeed(MOVE_RUN) <= (bot->GetSpeed(MOVE_RUN) / 2) && !finisher;
            snprintf(buf, sizeof(buf), "why=R,off=%d,imm=%d,slow=%d,fin=%d,far=%d",
                (int)off, (int)imm, (int)slow, (int)finisher, (int)(dist > 8.0f));
        }
        else
        {
            bool const fast = (target->GetSpeed(MOVE_RUN) > (bot->GetSpeed(MOVE_RUN) / 2)) || finisher;
            bool const noAmmo = !(ai->HasCheat(BotCheatMask::item) || AI_VALUE2(uint32, "item count", "ammo"));
            snprintf(buf, sizeof(buf), "why=M,on=%d,imm=%d,fast=%d,fin=%d,glue=%d,na=%d",
                (int)(target->GetVictim() == bot), (int)IsImmobilizedStateCompat(target),
                (int)fast, (int)finisher, (int)(dist <= 8.0f), (int)noAmmo);
        }
        return buf;
    }

    inline void LogHunterRangedEvent(PlayerbotAI* ai, const std::string& event, const std::string& extra = "")
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetClass() != CLASS_HUNTER)
            return;

        if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
            return;

        AiObjectContext* context = ai->GetAiObjectContext();
        if (!context)
            return;

        std::string const throttleKey = "hunter ranged telemetry::" + event;
        Value<time_t>* last = context->GetValue<time_t>("manual time", throttleKey);
        if (!last)
            return;

        time_t const now = time(nullptr);
        if (last->Get() && now - last->Get() < 5)
            return;
        last->Set(now);

        Unit* target = context->GetValue<Unit*>("current target")->Get();
        char info1[32];
        snprintf(info1, sizeof(info1), "dist=%.1f", target ? bot->GetDistance(target) : -1.0f);

        std::string info2 = "ranged=" + std::to_string(ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT) ? 1 : 0) +
            ",close=" + std::to_string(ai->HasStrategy("close", BotState::BOT_STATE_COMBAT) ? 1 : 0);
        if (!extra.empty())
            info2 += "," + extra;

        sPlayerbotAIConfig.logEvent(ai, event, info1, info2);
    }
}
