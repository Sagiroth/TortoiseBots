#pragma once

class Player;
class Unit;
class ObjectGuid;

namespace TortoiseBots {
namespace Movement {

// Thin, typed movement boundary — keeps MotionMaster calls in mature AI actions.
// All methods are pure helpers; they do not decide *when* to move.
//
// Reimplemented behavior: cmangos FollowAction dead-zone + restart guard
// (playerbot/strategy/actions/FollowActions.cpp:36-90, MovementActions.cpp Follow).

// Try to follow master at distance/angle. Returns true if a new movement was
// issued, false if already following same target/params or inside dead-zone.
bool Follow(Player* bot, Player* master, float distance, float angle);

// Stop any current movement. Safe to call when not moving.
bool Stop(Player* bot);

// Query helpers
bool IsFollowing(Player* bot, ObjectGuid const& targetGuid, float distance, float angle);

} // namespace Movement
} // namespace TortoiseBots
