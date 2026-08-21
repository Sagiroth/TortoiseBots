#include "Movement.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "MotionMaster.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

namespace TortoiseBots {
namespace Movement {

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
bool Follow(::Player* bot, ::Player* master, float distance, float angle)
{
    // pi-lens-ignore: clang:incomplete_member_access
    if (!bot || !master)
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    if (!bot->IsInWorld() || !master->IsInWorld())
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    if (bot->GetMap() != master->GetMap())
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    if (master->IsBeingTeleported() || bot->IsBeingTeleported())
        return false;

    // pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
    MotionMaster* mm = bot->GetMotionMaster();
    // pi-lens-ignore: clang:incomplete_member_access
    if (!mm)
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    mm->MoveFollow(master, distance, angle);
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
bool Stop(::Player* bot)
{
    // pi-lens-ignore: clang:incomplete_member_access
    if (!bot)
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    if (!bot->IsInWorld())
        return false;

    // pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
    MotionMaster* mm = bot->GetMotionMaster();
    // pi-lens-ignore: clang:incomplete_member_access
    if (!mm)
        return false;

    // pi-lens-ignore: clang:incomplete_member_access
    mm->Clear();
    // pi-lens-ignore: clang:incomplete_member_access
    bot->StopMoving();
    // pi-lens-ignore: clang:incomplete_member_access,clang:undeclared_var_use
    bot->ClearUnitState(UNIT_STAT_CHASE);
    // pi-lens-ignore: clang:incomplete_member_access,clang:undeclared_var_use
    bot->ClearUnitState(UNIT_STAT_FOLLOW);
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool IsFollowing(::Player* bot, ::ObjectGuid const& targetGuid, float distance, float angle)
{
    (void)targetGuid;
    (void)distance;
    (void)angle;
    // pi-lens-ignore: clang:incomplete_member_access
    if (!bot || targetGuid.IsEmpty())
        return false;

    // pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
    MotionMaster* mm = bot->GetMotionMaster();
    // pi-lens-ignore: clang:incomplete_member_access
    if (!mm)
        return false;

    // pi-lens-ignore: clang:incomplete_member_access,clang:undeclared_var_use
    return mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;
}

} // namespace Movement
} // namespace TortoiseBots
