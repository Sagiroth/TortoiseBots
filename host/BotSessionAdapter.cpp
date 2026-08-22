// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:incomplete_member_access,clang:init_conversion_failed,clang:excess_initializers,clang:typecheck_member_reference_struct_union,clang:expected_class_or_namespace,clang:ovl_no_viable_function_in_call,clang:fatal_too_many_errors,clang:unknown_type_name,clang:use_of_undeclared_identifier
#include "BotSessionAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "AccountMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"

namespace TortoiseBots {

WorldSession* BotSessionAdapter::CreateHeadlessSession(uint32 accountId, ObjectGuid characterGuid)
{
    if (!characterGuid.IsPlayer())
    {
        sLog.outError("TortoiseBots: CreateHeadlessSession — invalid guid %s", characterGuid.GetString().c_str());
        return nullptr;
    }

    // Basic validation: account must exist, character must exist and belong to that account.
    // We intentionally do NOT enforce that the character is online/offline here; the normal
    // HandlePlayerLogin alreadyOnline path handles human-reclaim vs duplicate bot login.
    // For MVP we keep validation minimal and let LoadFromDB reject mismatches.

    // Use a synthetic remote address for headless sessions.
    std::string remoteAddr = "127.0.0.1";
    uint32 binaryAddr = 0x7F000001; // 127.0.0.1

    // Use the account's stored security level. Test characters must be valid for
    // that level; the module never elevates an account for a fixture.
    AccountTypes sec = sAccountMgr.GetSecurity(accountId);

    // Transport is established at construction time — no SetHeadless mutation.
    WorldSession* session = new WorldSession(accountId, nullptr, sec, time_t(0), LOCALE_enUS, remoteAddr, binaryAddr, SessionTransport::Headless);
    session->InitHeadlessSession();
    session->SetUsername("TortoiseBot#" + std::to_string(accountId));

    // Queue under the character identity, never in World::m_sessions which is
    // intentionally reserved for the one Network session per account.
    sWorld.AddHeadlessSession(session, characterGuid);

    sLog.outString("TortoiseBots: CreateHeadlessSession acct %u guid %s headless %u ptr %p — queued headless add (login deferred)", accountId, characterGuid.GetString().c_str(), session->IsHeadless(), (void*)session);
    // Do not call LoginPlayer here; BotManager waits until the session is
    // discoverable by character guid through World::FindHeadlessSession.

    return session;
}

bool BotSessionAdapter::LogoutHeadlessSession(WorldSession* session, ObjectGuid characterGuid, bool save)
{
    if (!session || sWorld.FindHeadlessSession(characterGuid) != session)
        return false;
    if (!IsHeadlessSession(session))
    {
        sLog.outError("TortoiseBots: LogoutHeadlessSession called on non-headless session %u", session->GetAccountId());
        return false;
    }

    sLog.outString("TortoiseBots: LogoutHeadlessSession acct %u guid %s player %s save=%u",
        session->GetAccountId(), characterGuid.GetString().c_str(), session->GetPlayerName(), save);
    session->LogoutPlayer(save);
    return sWorld.RemoveHeadlessSession(characterGuid);
}

WorldSession* BotSessionAdapter::RelogHeadlessSession(uint32 accountId, ObjectGuid characterGuid)
{
    // For the Phase 3 spike we want to prove save → logout → re-login.
    // This helper just creates a new session; the caller is responsible for having
    // waited for the previous session to be fully removed (World::UpdateSessions
    // deletes the old session object). Creating a new headless session with the
    // same guid should succeed as long as the old player is no longer in the world.
    return CreateHeadlessSession(accountId, characterGuid);
}

bool BotSessionAdapter::EvictBotForHumanLogin(ObjectGuid characterGuid)
{
    if (Player* existing = sObjectAccessor.FindPlayer(characterGuid))
    {
        WorldSession* sess = existing->GetSession();
        if (sess && IsHeadlessSession(sess))
        {
            sLog.outString("TortoiseBots: EvictBotForHumanLogin guid %s — bot session %u will be transferred",
                characterGuid.GetString().c_str(), sess->GetAccountId());
            // The actual transfer happens in HandlePlayerLogin's alreadyOnline branch;
            // we just log here. Optionally we could pre-logout the bot.
            return true;
        }
    }
    return false;
}

bool BotSessionAdapter::IsHeadlessSession(WorldSession const* session)
{
    if (!session)
        return false;
    return session->IsHeadless();
}

std::string BotSessionAdapter::DescribeSession(WorldSession const* session)
{
    if (!session)
        return "<null>";
    std::string desc = "acct=" + std::to_string(session->GetAccountId())
                     + " transport=" + (session->IsHeadless() ? "Headless" : "Network")
                     + " player=" + std::string(session->GetPlayerName())
                     + " guid=" + (session->GetPlayer() ? session->GetPlayer()->GetObjectGuid().GetString() : "<none>");
    return desc;
}

} // namespace TortoiseBots
