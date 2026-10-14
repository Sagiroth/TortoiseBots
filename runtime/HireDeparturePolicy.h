#pragma once

// Issue #378: when does a hired companion end because its master is no longer
// in its group? Like the deletion guard (HireDeletionPolicy.h), the rule is a
// pure function of the facts a caller can see, so it is unit-tested on its own
// (tools/test_hire_departure_policy.cpp).
//
// Facts:
//  - isHire: the guid is in the active-hire registry (HireLifecycle's m_hired).
//    The registry is written only by Hire(); a pool bot or a player's alt is
//    never in it, so neither can ever be dismissed by this rule.
//  - masterOnline: the master has a live, non-headless session AND no logout
//    grace is running. During a logout the master object can still be in world
//    for a moment, so callers pass `MasterOnline(record) && !record.masterOfflineSince`:
//    a logged-out master is offline here and the grace timer decides instead.
//  - everGrouped: the hire was observed sharing its master's group at least
//    once. Before that the hire is still being provisioned (or was never
//    grouped) and must not be dismissed.
//  - inSameGroupWithMaster: the hire still shares its master's group — the
//    same group object (raid membership, not just the same raid subgroup,
//    issue #387). A hire that is not in world (mid-login, a stale session)
//    also counts as "same group": the runtime-record watchdog owns that case,
//    not this rule.
//
// A hire whose master left the party while online has masterOnline true,
// everGrouped true and inSameGroupWithMaster false, so it is dismissed.
//
// Remote commander (issue #884): a hire may be bound by its own hirer
// (`.bot commander bind`), which puts the hire in a bot-only party the hirer is
// not in. HireCommandState says where a hire stands in that: while it is bound
// the "left the hirer's party" end conditions are suspended (the binding is the
// reason it is elsewhere); the hire still ends on dismiss, on the hirer's
// logout grace, and on a removal from the bound party that no bind step made.

#include <cstdint>

namespace TortoiseBots
{

enum class HireCommandState : uint8_t
{
    None,       // an ordinary hire: every end condition applies
    Transition, // the bind or unbind is moving the hire between parties right now
    Bound,      // bound to its own hirer: the hirer is meant to be outside its party
};

inline bool ShouldDismissHireOnGroupDeparture(bool isHire, bool masterOnline, bool everGrouped,
    bool inSameGroupWithMaster, HireCommandState command = HireCommandState::None)
{
    if (!isHire)
        return false; // a pool bot or an alt: nothing here may touch it
    if (command != HireCommandState::None)
        return false; // the hirer's own binding keeps the hire outside the hirer's party
    if (!masterOnline)
        return false; // offline/logged-out master: the disconnect grace period owns the hire
    if (!everGrouped)
        return false; // not grouped yet: provisioning (or a stuck reunite) is in flight
    if (inSameGroupWithMaster)
        return false; // still together
    return true;
}

// The hire itself was removed from its group (kicked, left): the hire ends,
// unless a bind or unbind step took it out. A bound hire that someone else
// removes still ends.
inline bool ShouldDismissHireOnOwnRemoval(HireCommandState command)
{
    return command != HireCommandState::Transition;
}

// The hire's group was disbanded: ends the hire, unless the hirer's binding
// holds it (the binding then ends on its own, GroupChanged, and returns the
// hire to its hirer, see DecideHireBindingEnd).
inline bool ShouldDismissHireOnDisband(HireCommandState command)
{
    return command == HireCommandState::None;
}

// The hire sweep's verdict on a Bound hire, taken only AFTER the hirer's
// binding was settled (a binding whose lead left or whose group changed is
// released, which returns its hires, before the sweep may judge any hire):
//  - no binding stands for the hirer: the hires are returned to the hirer, they
//    are never judged as hires that left their hirer's party;
//  - a binding stands and the hire is in its group, or is not live: nothing;
//  - a binding stands and a live hire is not in its group (it left or was
//    taken out): the bound state is dropped and the ordinary rules apply.
enum class BoundHireVerdict : uint8_t
{
    Keep,
    ReturnToHirer,
    DropState,
};

inline BoundHireVerdict DecideBoundHireSweep(bool bindingStands, bool hireLive, bool inCommandedGroup)
{
    if (!bindingStands)
        return BoundHireVerdict::ReturnToHirer;
    if (!hireLive || inCommandedGroup)
        return BoundHireVerdict::Keep;
    return BoundHireVerdict::DropState;
}

// What happens to a hire when its hirer's binding ends (unbind, or any
// automatic release). The hire is kept: it goes back to its hirer's party.
enum class HireBindingEnd : uint8_t
{
    Leave,        // no such character any more: nothing to do
    Keep,         // already in the hirer's party
    RejoinHirer,  // leave the bound party, accept the hirer's invitation again
    ParkForLogin, // the hirer is offline: the logout grace period owns the hire; it rejoins on return
    AwaitWorld,   // the hire is out of world (far teleport, instance entry): stays in transition, the sweep re-joins it once it is back
};

// hireExists: the character object is there, in the world or not. A hire out of
// world is a hire mid-teleport, not a missing one: it must not be released into
// the ordinary rules, which would read "not in the hirer's party" and delete it.
inline HireBindingEnd DecideHireBindingEnd(bool hireLive, bool hirerOnline, bool inHirerGroup,
    bool hireExists = true)
{
    if (!hireExists)
        return HireBindingEnd::Leave;
    if (!hirerOnline)
        return HireBindingEnd::ParkForLogin;
    if (!hireLive)
        return HireBindingEnd::AwaitWorld;
    return inHirerGroup ? HireBindingEnd::Keep : HireBindingEnd::RejoinHirer;
}

// The sweep's step for a hire held in transition until it is back in world.
enum class AwaitedRegroup : uint8_t
{
    Wait,    // still out of world, within the bound
    Retry,   // back in world and the hirer is online: re-join now
    Park,    // the hirer logged out meanwhile: the logout grace owns the hire
    GiveUp,  // out of world for longer than the bound: the ordinary rules end it
};

inline AwaitedRegroup DecideAwaitedRegroup(bool hirerOnline, bool hireLive, bool deadlinePassed)
{
    if (deadlinePassed)
        return AwaitedRegroup::GiveUp;
    if (!hirerOnline)
        return AwaitedRegroup::Park;
    return hireLive ? AwaitedRegroup::Retry : AwaitedRegroup::Wait;
}

// The return path re-invited a held hire (the binding's end, or the sweep's
// retry) and the invitation did not land (a teleport in flight, a refused
// invite): the hire stays held and is invited again on the next sweep until
// the bound passes, instead of being judged at once as a hire outside its
// hirer's party (zf2-hires-2: the tank was deleted 3 s after an unbind).
inline bool KeepAwaitingRegroup(bool regrouped, bool deadlinePassed)
{
    return !regrouped && !deadlinePassed;
}

// A bind or a hire's return moves a bot between parties while it stands in a
// dungeon. The core starts its 60 s homebind timer for a player who leaves a
// party inside an instance (Group::_homebindIfInstance) and stops it only when
// the player joins a party bound to that same instance; a fresh party is bound
// to nothing, so the bots were teleported to their hearth 60 s after the
// bind (zf2-hires-2, three times). The module moved them, so it carries the
// instance over: a receiving party with no bind for the map is bound to the
// instance the bot stands in, and a bot whose party holds that instance keeps
// it. A party bound to another instance of the map, or an instance already
// waiting for its reset, is left to the core.
enum class PartyMoveInstance : uint8_t
{
    Nothing,
    BindParty, // bind the party to the bot's instance, then KeepValid
    KeepValid, // the party holds the bot's instance: the bot stays inside
};

inline PartyMoveInstance DecidePartyMoveInstance(bool inDungeon, bool instanceResetting,
    bool partyBoundToMap, bool partyBoundToThisInstance)
{
    if (!inDungeon || instanceResetting)
        return PartyMoveInstance::Nothing;
    if (!partyBoundToMap)
        return PartyMoveInstance::BindParty;
    return partyBoundToThisInstance ? PartyMoveInstance::KeepValid : PartyMoveInstance::Nothing;
}

// Once the party holds the bot's instance, a non-permanent solo bind of the
// bot to that same instance is dropped, as the core does on entry
// (DungeonMap::BindPlayerOrGroupOnEnter). Kept, it made the bot the party's
// "last member already bound" at the next disband: Group::Disband skipped the
// hand-over and reset the dungeon (bind -> unbind round trip).
inline bool DropSoloBindAfterPartyMove(PartyMoveInstance decision, bool hasSoloBind,
    bool soloBindPermanent, bool soloBindThisInstance)
{
    return decision != PartyMoveInstance::Nothing && hasSoloBind && !soloBindPermanent &&
        soloBindThisInstance;
}

} // namespace TortoiseBots
