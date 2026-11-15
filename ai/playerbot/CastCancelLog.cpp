#include "CastCancelLog.h"
#include "CastCancelPolicy.h"
#include "playerbot/playerbot.h"
#include "ObjectAccessor.h"
#include <cstdio>

namespace
{
    // The row's slot word.
    const char* SlotWord(CurrentSpellTypes slot)
    {
        switch (slot)
        {
            case CURRENT_GENERIC_SPELL: return "generic";
            case CURRENT_CHANNELED_SPELL: return "channeled";
            case CURRENT_AUTOREPEAT_SPELL: return "autorepeat";
            case CURRENT_MELEE_SPELL: return "melee";
        }
        return "generic";
    }

    // The gate, read before any slot: off, a call costs the branch and writes nothing.
    bool CastCancelEnabled()
    {
        return sPlayerbotAIConfig.raidBossDiagnostics && sPlayerbotAIConfig.hasLog("bot_events.csv");
    }
}

void ai::LogCastCancel(PlayerbotAI* ai, const char* site, CurrentSpellTypes slot, float movedYd)
{
    if (!ai || !site || static_cast<int>(slot) >= CURRENT_MAX_SPELL || !CastCancelEnabled())
        return;
    Player* const bot = ai->GetBot();
    if (!bot)
        return;
    Spell* const cast = bot->GetCurrentSpell(slot);
    if (!cast || !cast->m_spellInfo)
        return;
    // A cast bar has its total in m_casttime; a channel's total is the spell's duration.
    int totalMs = cast->GetCastTime();
    if (totalMs <= 0)
        totalMs = static_cast<int>(GetSpellDuration(cast->m_spellInfo));
    const int pct = CastCancelPct(totalMs, cast->GetCastedTime());
    // The cast's cached unit target is refreshed only in the caster's next Spell::update, and the map frees a unit
    // removed in this tick, so a cancel at a dying boss's summons can read freed memory. Resolve the guid the cast
    // holds: a target that left the world reads none.
    ObjectGuid const targetGuid = cast->m_targets.getUnitTargetGuid();
    Unit* const target = targetGuid ? ObjectAccessor::GetUnit(*bot, targetGuid) : nullptr;
    char info1[192];
    snprintf(info1, sizeof(info1), "site=%s,slot=%s,spell=%u,cast_pct=%d,target=%s,moved_yd=%.1f",
             site, SlotWord(slot), cast->m_spellInfo->Id, pct, target ? target->GetName() : "none", movedYd);
    sPlayerbotAIConfig.logEvent(ai, "CastCancel", info1, "");
}

void ai::LogCastCancelStop(PlayerbotAI* ai, const char* site, float movedYd)
{
    if (!ai || !site || !CastCancelEnabled())
        return;
    Player* const bot = ai->GetBot();
    if (!bot)
        return;
    // Unit::CastStop's own set and condition: a preparing cast that has run for a while, one row each.
    for (const CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_AUTOREPEAT_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* const cast = bot->GetCurrentSpell(slot);
        if (cast && cast->m_spellInfo && cast->getState() == SPELL_STATE_PREPARING && cast->GetCastedTime())
            LogCastCancel(ai, site, slot, movedYd);
    }
}

void ai::LogCastCancelNonMelee(PlayerbotAI* ai, const char* site, bool withDelayed, float movedYd)
{
    if (!ai || !site || !CastCancelEnabled())
        return;
    Player* const bot = ai->GetBot();
    if (!bot)
        return;
    for (const CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_AUTOREPEAT_SPELL })
    {
        Spell* const cast = bot->GetCurrentSpell(slot);
        if (cast && cast->m_spellInfo && (withDelayed || cast->getState() != SPELL_STATE_DELAYED))
            LogCastCancel(ai, site, slot, movedYd);
    }
    if (Spell* const channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (channel->m_spellInfo)
            LogCastCancel(ai, site, CURRENT_CHANNELED_SPELL, movedYd);
}
