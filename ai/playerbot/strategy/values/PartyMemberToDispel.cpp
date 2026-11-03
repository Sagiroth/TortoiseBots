
#include "playerbot/playerbot.h"
#include <limits>
#include "PartyMemberToDispel.h"

#include "playerbot/policy/heal/DispelTargetPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
using namespace ai;

class PartyMemberToDispelPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PartyMemberToDispelPredicate(PlayerbotAI* ai, uint32 dispelType, std::string const& claimKey,
                                 const ActiveBossRows& dispelRows) :
        PlayerbotAIAware(ai), FindPlayerPredicate(), dispelType(dispelType), claimKey(claimKey), rows(dispelRows),
        best(nullptr), bestTier(kDispelTargetRejected), bestRank(0), picked(nullptr), pickAuraId(0),
        pickMode(TacticMode::Default), pickRank(0), pickAllyYd(0.0f) {}

public:
    // True only for a best-tier member (in cast range, in sight, not picked
    // by another bot), which ends the role-ordered search at once. Anything
    // worse is remembered as the fallback (DispelTargetPolicy.h). With a
    // dispel_priority row in the fight the member's aura rank comes first and
    // the target tier second (DispelCandidateBetter), and a member the row
    // excludes is skipped before its aura is even offered.
    virtual bool Check(Unit* unit) override
    {
        Pet* pet = dynamic_cast<Pet*>(unit);
        if (pet && (pet->getPetType() == MINI_PET || pet->getPetType() == SUMMON_PET))
            return false;

        if (!sServerFacade.IsAlive(unit))
            return false;

        Player* bot = ai->GetBot();
        const bool ranked = !rows.empty();
        float allyYd = std::numeric_limits<float>::max();
        DispelVerdict verdict;
        DispelOverride overrideSet;
        if (ranked)
        {
            std::vector<AuraOnMember> auras;
            CollectDispelAuras(unit, dispelType, auras);
            allyYd = DispelNeedsAllyDistance(rows, auras) ? NearestGroupMemberYd(unit)
                                                          : std::numeric_limits<float>::max();
            verdict = DispelMemberVerdict(rows, auras, dispelType, allyYd, sPlayerbotAIConfig.bombRunoutDistance);
            if (verdict.excluded)
            {
                LogDispelExcluded(ai, bot, verdict, unit, allyYd);
                return false;
            }
            // A never-dispel aura a `clear` row releases: the verdict left the member in, so the carrier is clear and
            // the aura may go.
            for (const AuraOnMember& aura : auras)
            {
                if (IsNeverDispelSpellId(aura.spellId))
                    overrideSet.allow.push_back(aura.spellId);
            }
        }

        if (!ai->HasAuraToDispel(unit, dispelType, ranked ? &overrideSet : nullptr))
            return false;

        // A member just outside cast range stays a candidate (the cure
        // action queues its reach prerequisite), but only up to the reach
        // cap and in sight - no crossing the room to a pillar-blocked one.
        float const distance = sServerFacade.GetDistance2d(bot, unit);
        float const spellDistance = ai->GetRange("spell");
        if (!IsWithinDispelReach(distance, spellDistance))
            return false;

        bool const claimed = bot->GetGroup() &&
            BuffClaimRegistry::IsClaimedByOther(bot->GetObjectGuid(), unit->GetObjectGuid(), claimKey);
        int const tier = DispelTargetTier(distance, spellDistance, true, claimed);
        int const rank = ranked ? verdict.rank : 0;
        if (bestTier != kDispelTargetRejected &&
            (ranked ? !DispelCandidateBetter(rank, tier, bestRank, bestTier) : tier >= bestTier))
            return false;

        if (!sServerFacade.IsWithinLOSInMap(bot, unit))
            return false;

        // A rank-0 member in cast range is the best a ranked cure can do: take it without scanning the raid.
        if (ranked && rank == 0 && tier == 0)
        {
            Remember(unit, verdict, rank, allyYd);
            return true;
        }
        if (!ranked && tier == 0)
        {
            Remember(unit, verdict, rank, allyYd);
            return true;
        }

        best = unit;
        bestTier = tier;
        bestRank = rank;
        Remember(unit, verdict, rank, allyYd);
        return false;
    }

    Unit* GetFallback() const { return best; }

    // The candidate the search settled on and the row data behind it (the pick row is logged once per decision).
    Unit* GetPick() const { return picked; }
    uint32 GetPickAuraId() const { return pickAuraId; }
    TacticMode GetPickMode() const { return pickMode; }
    int GetPickRank() const { return pickRank; }
    float GetPickAllyYd() const { return pickAllyYd; }

private:
    void Remember(Unit* unit, const DispelVerdict& verdict, int rank, float allyYd)
    {
        picked = unit;
        pickAuraId = verdict.auraId;
        pickMode = verdict.mode;
        pickRank = rank;
        pickAllyYd = allyYd;
    }

private:
    uint32 dispelType;
    std::string claimKey;
    const ActiveBossRows& rows;
    Unit* best;
    int bestTier;
    int bestRank;
    Unit* picked;
    uint32 pickAuraId;
    TacticMode pickMode;
    int pickRank;
    float pickAllyYd;
};

Unit* PartyMemberToDispel::Calculate()
{
    uint32 dispelType = atoi(qualifier.c_str());
    std::string const claimKey = DispelClaimKey(qualifier);

    // The fight's dispel_priority rows: read only under the key, on a map that has one, and with the strategy on, so a
    // fight without a row keeps the plain predicate (no ranking, no early-return change).
    ActiveBossRows dispelRows;
    if (sPlayerbotAIConfig.dungeonRunAuraTactics && GetBossTactics().HasDispelMap(bot->GetMapId()) &&
        ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
    {
        const BossTacticsFight fight = ScanBossTactics(ai, bot);
        for (const BossTacticRow* row : fight.rows)
        {
            if (row->kind == BossTacticKind::DispelPriority)
                dispelRows.push_back(row);
        }
    }

    PartyMemberToDispelPredicate predicate(ai, dispelType, claimKey, dispelRows);
    Unit* target = FindPartyMember(predicate);
    if (!target)
        target = predicate.GetFallback();

    // The decision row: which aura ranked the carrier, under which mode, and how far it stood from the group when the
    // cure was picked (the Grobbulus release wants the clear distance at pick time).
    if (target && target == predicate.GetPick() && predicate.GetPickAuraId() != 0)
    {
        DispelVerdict verdict;
        verdict.auraId = predicate.GetPickAuraId();
        verdict.mode = predicate.GetPickMode();
        verdict.rank = predicate.GetPickRank();
        LogDispelPick(ai, bot, verdict, target, predicate.GetPickAllyYd());
    }

    // Claim the pick so the next dispeller of this type that recalculates
    // (same tick or the next few seconds) prefers somebody else.
    if (target && bot->GetGroup())
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), target->GetObjectGuid(), claimKey);

    return target;
}
