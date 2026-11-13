#pragma once

// Pure policy for the summon commands' pet hand-over (issue #1606).
//
// A bot moved by a summon used to leave its pet behind, and the pet then walked
// the direct route to its owner through the instance, pulling whatever it passed
// (the reporter's hunter pet off the Lord Incendius ledge in Blackrock Depths).
// The core already relocates a pet across a teleport for players:
// Player::UnsummonPetTemporaryIfAny caches it, and
// Player::ResummonPetTemporaryUnSummonedIfAny loads it beside its owner when the
// teleport lands (Player::ExecuteTeleportNear, and
// WorldSession::HandleMoveWorldportAckOpcode for a far teleport). Player::TeleportTo
// only does this itself for a same-map pet farther than the grid activation
// distance, which a summon across an instance is not.
//
// So a summon asks this policy whether to hand the pet over before TeleportTo and
// give it back when TeleportTo refuses. Nothing here needs the pet object: the
// call sites pass the facts.
//   hasPet           - GetPet() is not null (a hunter pet or a warlock demon)
//   petAlive         - a dead pet cannot walk or pull, so it stays where it is
//   noPetPending     - no earlier temporary unsummon is still pending: the core
//                      would resummon the older pet and lose this one
//   coreWillResummon - !IsPetNeedBeTemporaryUnsummoned(): mounted, taxiing, dead
//                      or out of the world means the core would not bring it back,
//                      and the pet would be gone until the next teleport
//
// Guardians (temporary summons) stay out of scope: the core has no relocation
// helper for them, and the issue is about the hunter pet and the warlock demon.

namespace ai
{
    inline bool SummonTeleportMovesPet(bool hasPet, bool petAlive, bool noPetPending, bool coreWillResummon)
    {
        if (!hasPet || !petAlive)
            return false;

        return noPetPending && coreWillResummon;
    }
}
