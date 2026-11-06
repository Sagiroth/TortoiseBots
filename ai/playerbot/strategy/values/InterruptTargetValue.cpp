
#include "playerbot/playerbot.h"
#include "InterruptTargetValue.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/policy/combat/InterruptTargetPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "AttackersValue.h"

using namespace ai;

Unit* InterruptTargetValue::Calculate()
{
    std::string const interruptSpell = qualifier;
    if (interruptSpell.empty())
        return NULL;

    // A bot whose interrupt is on cooldown must neither pick nor claim a
    // cast: the claim would sit for seconds and suppress the ready
    // teammate's priority pick.
    uint32 const interruptId = AI_VALUE2(uint32, "spell id", interruptSpell);
    if (!interruptId || !sServerFacade.IsSpellReady(bot, interruptId))
        return NULL;

    // Interrupt reach: the interrupt spell's own range, floored at melee
    // distance - combat-reach spells (Kick, Shield Bash, Pummel, Bash)
    // report a bare reach here, and their actions are melee ones at
    // ATTACK_DISTANCE. A self-cast AoE (Arcane Torrent) reports no range:
    // reach as far as its widest effect radius instead.
    float range = ATTACK_DISTANCE;
    float spellRange;
    if (ai->GetSpellRange(interruptSpell, &spellRange))
    {
        if (spellRange > range)
            range = spellRange;
        else if (spellRange <= 0)
        {
            if (SpellEntry const* info = sServerFacade.LookupSpellInfo(interruptId))
            {
                for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (SpellRadiusEntry const* radiusEntry = sSpellRadiusStore.LookupEntry(info->EffectRadiusIndex[i]))
                        if (radiusEntry->Radius > range)
                            range = radiusEntry->Radius;
                }
            }
        }
    }

    CcGuard ccGuard(ai);
    Unit* best = NULL;
    int bestTier = 0;

    std::list<ObjectGuid> const& attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::const_iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !unit->IsAlive())
            continue;

        if (sServerFacade.getDistance2d(bot, unit) > range)
            continue;

        if (!ai->IsInterruptableSpellCasting(unit, interruptSpell, true))
            continue;

        // A held CC mark (Moon or "rti cc", in its hold window or under a
        // CC cast bar) is the CC bot's: Kick and Shield Bash deal damage,
        // and the walk to it drags the bot onto the mark.
        if (!CcEnemyHealerPickAllowed(ccGuard.IsHeldCcMark(unit)))
            continue;

        // A cast that sleeps, fears, sheep-shackles or heals someone
        // outranks a plain damage cast.
        Spell* cast = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!cast || !cast->m_spellInfo)
            cast = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        bool const priority = cast && cast->m_spellInfo &&
            (PlayerbotAI::IsHealSpell(cast->m_spellInfo) ||
             FindNonCcTargetStrategy::IsCcSpell(cast->m_spellInfo));

        bool const claimed = bot->GetGroup() &&
            BuffClaimRegistry::IsClaimedByOther(bot->GetObjectGuid(), unit->GetObjectGuid(), InterruptClaimKey());
        // AiPlayerbot.DungeonRunCharmYield: a charmed group member's cast is never interrupted (Counterspell, Shield
        // Bash, Pummel on the ally are what killed four damage dealers in 109 s).
        bool const offLimits = AttackersValue::CharmedAllyOffLimits(unit, bot);
        int const tier = InterruptTargetTier(priority, claimed, offLimits);
        if (tier == kInterruptTargetRejected)
            continue;
        if (!best || tier < bestTier)
        {
            best = unit;
            bestTier = tier;
        }
        if (bestTier == 0)
            break; // a free priority cast cannot be beaten
    }

    // One interrupter per cast: claim the pick so the next interrupter that
    // recalculates (same tick or the next few seconds) skips this cast for
    // another one. Only written after the ready check above.
    if (best && bot->GetGroup())
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), best->GetObjectGuid(), InterruptClaimKey());

    return best;
}
