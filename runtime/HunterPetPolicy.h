#pragma once

#include <cstdint>

// Server data, not a guess: in tw_world.spell_template Tame Beast (1515) and
// the pet kit it unlocks - Call Pet (883), Revive Pet (982), Feed Pet (6991) -
// all carry baseLevel = spellLevel = 10, so a hunter below level 10 can neither
// tame nor summon nor revive a pet.
//
// The pool used to hand one out anyway: the hunter pet strategy's "initialize
// pet" action ran PlayerbotFactory::InitPet with no level gate, so a level-1
// hunter logged in with a level-1 pet. The threshold lives here once, shared by
// the pet-creation paths (ai/playerbot/PlayerbotFactory.cpp and the action) and
// by the login cleanup below.
//
// The login cleanup is deliberately narrow, because dropping a pet is
// destructive. It is a pure function of durable facts and is unit-tested on its
// own (tools/test_hunter_pet_policy.cpp). Only a bot that is all of
//   - an unclaimed pool bot (registered RNDBOT pool account, no live real-player
//     master), and
//   - not a hired companion (a hire outlives its master's session),
// may lose a below-threshold pet. Owner-account characters, hired companions
// and adopted party bots keep whatever they have.

namespace TortoiseBots
{

inline constexpr uint32_t HUNTER_PET_MIN_LEVEL = 10;

struct HunterPetLoginInputs
{
    bool isPoolBot = false;        // unclaimed random-pool bot (see above)
    bool isHiredCompanion = false; // live hire ledger row
    bool isHunter = false;
    uint32_t level = 1;
    bool hasPet = false;
};

enum class HunterPetLoginDecision
{
    LeaveAlone,
    DropPetBelowThreshold,
};

inline HunterPetLoginDecision DecideHunterPetOnLogin(HunterPetLoginInputs const& inputs)
{
    if (!inputs.isPoolBot || !inputs.isHunter || inputs.level >= HUNTER_PET_MIN_LEVEL)
        return HunterPetLoginDecision::LeaveAlone;
    if (inputs.isHiredCompanion)
        return HunterPetLoginDecision::LeaveAlone;
    return inputs.hasPet ? HunterPetLoginDecision::DropPetBelowThreshold : HunterPetLoginDecision::LeaveAlone;
}

} // namespace TortoiseBots
