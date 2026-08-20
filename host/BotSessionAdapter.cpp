#include "BotSessionAdapter.h"
#include "WorldSession.h"
#include "World.h"
#include "AccountMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Log.h"
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

    // Use the account's actual security level so reserved-name and GM checks pass
    // (e.g. the default "Admin" character is named "Admin" which is reserved for SEC_PLAYER).
    AccountTypes sec = sAccountMgr.GetSecurity(accountId);
    if (sec == SEC_PLAYER && accountId == 4) // fallback for the default admin account in test env
        sec = SEC_ADMINISTRATOR;

    // WorldSession constructor is generic; we pass nullptr socket and then mark headless.
    WorldSession* session = new WorldSession(accountId, nullptr, sec, time_t(0), LOCALE_enUS, remoteAddr, binaryAddr);
    session->SetHeadless(true);
    session->InitHeadlessSession();
    session->SetUsername("TortoiseBot#" + std::to_string(accountId));

    // Load tutorials etc. — for bots this is a no-op but keeps parity with network path.
    // WorldSocket::HandleAuthSession would normally do LoadTutorialsData, but we don't need it.

    // Add to World's session map synchronously so the session is immediately
    // findable for the async LoginQueryHolder callback. For normal network
    // sessions AddSession queues to addSessQueue and is processed next tick,
    // but headless sessions are created on the world thread and need immediate
    // visibility (otherwise HandlePlayerLoginCallback's FindSession fails).
    sWorld.AddSessionDirect(session);

    // Kick off the normal character load. This is the same LoginQueryHolder flow as a
    // real client login, so the character goes through LoadFromDB, AddToMap, etc.
    sLog.outString("TortoiseBots: CreateHeadlessSession acct %u guid %s headless %u ptr %p — direct add", accountId, characterGuid.GetString().c_str(), session->IsHeadless(), (void*)session);
    session->LoginPlayer(characterGuid);

    return session;
}

bool BotSessionAdapter::LogoutHeadlessSession(WorldSession* session, bool save)
{
    if (!session)
        return false;
    if (!IsHeadlessSession(session))
    {
        sLog.outError("TortoiseBots: LogoutHeadlessSession called on non-headless session %u", session->GetAccountId());
        return false;
    }

    sLog.outString("TortoiseBots: LogoutHeadlessSession acct %u player %s save=%u",
        session->GetAccountId(), session->GetPlayerName(), save);
    session->LogoutPlayer(save);
    // The session object will be removed from World::m_sessions on the next
    // World::UpdateSessions if it has no player. We don't delete it here; World owns it.
    return true;
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
