#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    // Opt-out balance fallback. Do not take a feral bot out of form.
    inline bool BalanceManaForm(PlayerbotAI* ai)
    {
        const auto form = ai->GetBot()->GetShapeshiftForm();
        return ai->HasStrategy("oom melee", BotState::BOT_STATE_COMBAT) &&
            ai->HasStrategy("balance", BotState::BOT_STATE_COMBAT) &&
            (form == FORM_NONE || form == FORM_MOONKIN) &&
            !ai->HasStrategy("tank feral", BotState::BOT_STATE_COMBAT) &&
            !ai->HasStrategy("dps feral", BotState::BOT_STATE_COMBAT);
    }

    class BalanceManaTrigger : public Trigger
    {
    public:
        enum Mode { ENTER_MELEE, RETURN_RANGED, KEEP_MELEE };
        BalanceManaTrigger(PlayerbotAI* ai, Mode mode) : Trigger(ai, "balance mana"), mode(mode) {}
        bool IsActive() override
        {
            if (!BalanceManaForm(ai) || bot->IsNonMeleeSpellCasted(true) ||
                !bot->GetMaxPower(POWER_MANA))
                return false;
            const float mana = 100.0f * bot->GetPower(POWER_MANA) / bot->GetMaxPower(POWER_MANA);
            const bool close = ai->HasStrategy("close", BotState::BOT_STATE_COMBAT);
            if (mode == ENTER_MELEE)
                return mana <= 5.0f && ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT);
            if (mode == RETURN_RANGED)
                return close && mana >= 30.0f;
            return close && mana < 30.0f;
        }
    private:
        Mode mode;
    };

    class BalanceManaMultiplier : public Multiplier
    {
    public:
        BalanceManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "balance mana") {}
        float GetValue(Action* action) override
        {
            if (!action || !BalanceManaForm(ai))
                return 1.0f;
            const std::string name = action->getName();
            // Recheck at execution selection: a queued transition must not cancel a cast.
            if (bot->IsNonMeleeSpellCasted(true) &&
                (name == "-ranged,+close" || name == "-close,+ranged" || name == "melee"))
                return 0.0f;
            if (!ai->HasStrategy("close", BotState::BOT_STATE_COMBAT))
                return 1.0f;
            // Save regenerated mana until the return threshold. Healing, dispels,
            // Innervate and emergency fleeing retain their normal priorities.
            if (name == "wrath" || name == "starfire" || name == "moonfire" ||
                name == "insect swarm" || name == "hurricane" || name == "faerie fire")
                return 0.0f;
            return 1.0f;
        }
    };
}
