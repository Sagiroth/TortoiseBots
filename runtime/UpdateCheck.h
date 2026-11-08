#pragma once

// One background look at GitHub at server start for the newest TortoiseBots
// and TortoiseBotsManager releases (AiPlayerbot.UpdateCheck). Offline or
// failed lookups leave both empty and change nothing.

#include <mutex>
#include <string>

namespace TortoiseBots {

class UpdateCheck
{
public:
    static UpdateCheck& Instance();

    void Start();
    std::string LatestModule() const;
    std::string LatestAddon() const;

private:
    mutable std::mutex m_mutex;
    std::string m_module;
    std::string m_addon;
    bool m_started = false;
};

} // namespace TortoiseBots
