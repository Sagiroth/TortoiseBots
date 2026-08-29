// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:incomplete_member_access,clang:init_conversion_failed,clang:excess_initializers,clang:typecheck_member_reference_struct_union,clang:expected_class_or_namespace,clang:ovl_no_viable_function_in_call,clang:fatal_too_many_errors,clang:unknown_type_name,clang:use_of_undeclared_identifier
#include "BotSessionAdapter.h"
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

namespace TortoiseBots {

bool BotSessionAdapter::StartHeadlessSession(uint32 accountId, ObjectGuid characterGuid)
{
    HeadlessSessionStartResult result = sWorld.StartHeadlessSession(accountId, characterGuid, LOCALE_enUS, "TortoiseBot#" + std::to_string(accountId));
    if (result == HeadlessSessionStartResult::Started)
    {
        sLog.outString("TortoiseBots: StartHeadlessSession acct %u guid %s — queued (Started)", accountId, characterGuid.GetString().c_str());
        return true;
    }
    sLog.outError("TortoiseBots: StartHeadlessSession acct %u guid %s failed %u", accountId, characterGuid.GetString().c_str(), static_cast<uint32>(result));
    return false;
}

bool BotSessionAdapter::StopHeadlessSession(ObjectGuid characterGuid, bool save)
{
    bool ok = sWorld.StopHeadlessSession(characterGuid, save);
    if (ok)
        sLog.outString("TortoiseBots: StopHeadlessSession guid %s save %u", characterGuid.GetString().c_str(), save ? 1u : 0u);
    else
        sLog.outDebug("TortoiseBots: StopHeadlessSession guid %s no session to stop", characterGuid.GetString().c_str());
    return ok;
}

HeadlessSessionState BotSessionAdapter::GetHeadlessSessionState(ObjectGuid characterGuid)
{
    return sWorld.GetHeadlessSessionState(characterGuid);
}


} // namespace TortoiseBots
