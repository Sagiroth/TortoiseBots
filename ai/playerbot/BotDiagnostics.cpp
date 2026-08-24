#include "BotDiagnostics.h"
#include "PlayerbotAIConfig.h"

namespace ai { namespace botdiag {
    thread_local const char* gLastPhaseTag     = nullptr;
    thread_local const char* gLastPhaseBotName = nullptr;

    bool IsActionLogEnabled()
    {
        return sPlayerbotAIConfig.enableActionLog;
    }
}}
