// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:incomplete_member_access,clang:unknown_type_name,clang:use_of_undeclared_identifier,clang:unknown_type_name
#pragma once

#include "Common.h"
#include "ObjectGuid.h"
#include "HeadlessSessionMgr.h"


// Thin module adapter over core HeadlessSessionMgr. Core owns allocation,
// queueing, LoginPlayer dispatch, logout, deletion and reclaim. Module never
// constructs, inits, queues or deletes a Headless WorldSession directly.

namespace TortoiseBots {

class BotSessionAdapter
{
public:
    // Start a headless session via World::StartHeadlessSession. Returns true
    // only when core reports Started (queued or loading).
    static bool StartHeadlessSession(uint32 accountId, ObjectGuid characterGuid);

    // Stop a headless session via World::StopHeadlessSession.
    static bool StopHeadlessSession(ObjectGuid characterGuid, bool save = true);

    // Query core state via World::GetHeadlessSessionState.
    static HeadlessSessionState GetHeadlessSessionState(ObjectGuid characterGuid);

};

} // namespace TortoiseBots
