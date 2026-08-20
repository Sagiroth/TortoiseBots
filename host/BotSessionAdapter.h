#pragma once

#include "Common.h"
#include "ObjectGuid.h"
#include <string>

class WorldSession;
class Player;

// Centralized headless-session factory — module-owned, core stays generic.
// Core only knows SessionTransport::Headless / IsHeadless(). This adapter
// knows that a particular headless session is a TortoiseBot.

namespace TortoiseBots {

class BotSessionAdapter
{
public:
    // Create a headless WorldSession for the given account/character and
    // immediately start the normal character-loading flow (LoginQueryHolder).
    // Returns the new session on success, nullptr on failure.
    // The session is added to World::m_sessions and will appear in the world
    // after the async DB load completes (same path as a normal login).
    static WorldSession* CreateHeadlessSession(uint32 accountId, ObjectGuid characterGuid);

    // Explicit logout for a headless session. Safe to call from the world thread.
    static bool LogoutHeadlessSession(WorldSession* session, bool save = true);

    // Re-login helper for the Phase 3 acceptance test: logout then create a new
    // headless session for the same character after a short delay is handled by
    // the caller (e.g. BotManager). This just wraps CreateHeadlessSession.
    static WorldSession* RelogHeadlessSession(uint32 accountId, ObjectGuid characterGuid);

    // Human-reclaim check: if a normal network session tries to login the same
    // character guid that is currently a headless bot, the normal
    // HandlePlayerLogin already handles the transfer (see WorldSession::HandlePlayerLogin
    // alreadyOnline branch). This helper just documents the expected behavior and
    // can be used to proactively evict a bot before human login if needed.
    static bool EvictBotForHumanLogin(ObjectGuid characterGuid);

    // Diagnostic helpers
    static bool IsHeadlessSession(WorldSession const* session);
    static std::string DescribeSession(WorldSession const* session);
};

} // namespace TortoiseBots
