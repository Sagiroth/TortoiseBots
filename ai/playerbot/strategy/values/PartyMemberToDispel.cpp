
#include "playerbot/playerbot.h"
#include "PartyMemberToDispel.h"
#include "playerbot/strategy/RaidCurePolicy.h"
#include "playerbot/strategy/RaidDispelPolicy.h"

#include "playerbot/policy/heal/DispelTargetPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
using namespace ai;

class PartyMemberToDispelPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PartyMemberToDispelPredicate(PlayerbotAI* ai, uint32 dispelType, std::string const& claimKey, bool priorityOnly = false) :
        PlayerbotAIAware(ai), FindPlayerPredicate(), dispelType(dispelType), claimKey(claimKey),
        best(nullptr), bestTier(kDispelTargetRejected), priorityOnly(priorityOnly) {}

public:
    // True only for a best-tier member (in cast range, in sight, not picked
    // by another bot), which ends the role-ordered search at once. Anything
    // worse is remembered as the fallback (DispelTargetPolicy.h).
    virtual bool Check(Unit* unit) override
    {
        if (IsRaidCureInterlocked(unit->GetMapId(), unit->HasAura(MUTATING_INJECTION_SPELL_ID)))
            return false;

        Pet* pet = dynamic_cast<Pet*>(unit);
        if (pet && (pet->getPetType() == MINI_PET || pet->getPetType() == SUMMON_PET))
            return false;

        bool const hasPriorityAura =
            (unit->HasAura(LUCIFRONS_CURSE_SPELL_ID) && IsPriorityRaidDispel(unit->GetMapId(), LUCIFRONS_CURSE_SPELL_ID, dispelType)) ||
            (unit->HasAura(MAGMADAR_PANIC_SPELL_ID) && IsPriorityRaidDispel(unit->GetMapId(), MAGMADAR_PANIC_SPELL_ID, dispelType)) ||
            (unit->HasAura(NEFARIAN_WILD_POLYMORPH_SPELL_ID) && IsPriorityRaidDispel(unit->GetMapId(), NEFARIAN_WILD_POLYMORPH_SPELL_ID, dispelType));
        if (priorityOnly && !hasPriorityAura)
            return false;

        if (!sServerFacade.IsAlive(unit) || !ai->HasAuraToDispel(unit, dispelType))
            return false;

        // A member just outside cast range stays a candidate (the cure
        // action queues its reach prerequisite), but only up to the reach
        // cap and in sight - no crossing the room to a pillar-blocked one.
        Player* bot = ai->GetBot();
        float const distance = sServerFacade.GetDistance2d(bot, unit);
        float const spellDistance = ai->GetRange("spell");
        if (!IsWithinDispelReach(distance, spellDistance))
            return false;

        bool const claimed = bot->GetGroup() &&
            BuffClaimRegistry::IsClaimedByOther(bot->GetObjectGuid(), unit->GetObjectGuid(), claimKey);
        int const tier = DispelTargetTier(distance, spellDistance, true, claimed);
        if (bestTier != kDispelTargetRejected && tier >= bestTier)
            return false;

        if (!sServerFacade.IsWithinLOSInMap(bot, unit))
            return false;

        if (tier == 0)
            return true;

        best = unit;
        bestTier = tier;
        return false;
    }

    Unit* GetFallback() const { return best; }

private:
    uint32 dispelType;
    std::string claimKey;
    Unit* best;
    int bestTier;
    bool priorityOnly;
};

Unit* PartyMemberToDispel::Calculate()
{
    uint32 dispelType = atoi(qualifier.c_str());
    std::string const claimKey = DispelClaimKey(qualifier);

    PartyMemberToDispelPredicate priorityPredicate(ai, dispelType, claimKey, true);
    Unit* target = FindPartyMember(priorityPredicate);
    if (!target)
        target = priorityPredicate.GetFallback();
    if (!target)
    {
        PartyMemberToDispelPredicate predicate(ai, dispelType, claimKey);
        target = FindPartyMember(predicate);
        if (!target)
            target = predicate.GetFallback();
    }

    // Claim the pick so the next dispeller of this type that recalculates
    // (same tick or the next few seconds) prefers somebody else.
    if (target && bot->GetGroup())
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), target->GetObjectGuid(), claimKey);

    return target;
}
