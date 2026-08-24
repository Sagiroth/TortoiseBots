#include "BotController.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

namespace TortoiseBots {

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
BotController::BotController(ObjectGuid botGuid, ObjectGuid masterGuid)
    : m_botGuid(botGuid)
    , m_masterGuid(masterGuid)
    , m_intent(BotIntent::Follow)
{
}

// pi-lens-ignore: clang:incomplete_member_access
void BotController::SetMaster(ObjectGuid masterGuid)
{
    if (m_masterGuid == masterGuid)
        return;
    m_masterGuid = masterGuid;
}

// pi-lens-ignore: clang:incomplete_member_access
void BotController::SetIntent(BotIntent intent)
{
    if (m_intent == intent)
        return;
    m_intent = intent;
    if (intent == BotIntent::Follow)
        sLog.outDebug("TortoiseBots: Bot %s intent -> Follow master %s",
            m_botGuid.GetString().c_str(), m_masterGuid.GetString().c_str());
    else if (intent == BotIntent::None)
        sLog.outDebug("TortoiseBots: Bot %s intent -> None", m_botGuid.GetString().c_str());
}

// pi-lens-ignore: clang:incomplete_member_access
const char* BotController::GetIntentName() const
{
    switch (m_intent)
    {
        case BotIntent::Follow: return "Follow";
        case BotIntent::None: return "None";
        default: return "Unknown";
    }
}

} // namespace TortoiseBots
