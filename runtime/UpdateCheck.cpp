#include "UpdateCheck.h"
#include "policy/UpdateCheckPolicy.h"
#include "../host/ModuleLog.h"
#include "../host/ModuleVersion.h"
#include "Log.h"

#include <cstdio>
#include <thread>

#ifdef _WIN32
#define TB_POPEN _popen
#define TB_PCLOSE _pclose
#define TB_NULL_DEVICE "NUL"
#else
#define TB_POPEN popen
#define TB_PCLOSE pclose
#define TB_NULL_DEVICE "/dev/null"
#endif

namespace TortoiseBots {

namespace {

// curl ships with Linux distributions and Windows 10+, so the core needs no
// HTTP library; without curl or network the reply is empty.
std::string FetchLatestTag(char const* repo)
{
    std::string const command = std::string("curl -s -m 5 -H \"User-Agent: TortoiseBots\" "
        "https://api.github.com/repos/Sagiroth/") + repo + "/releases/latest 2>" TB_NULL_DEVICE;
    FILE* pipe = TB_POPEN(command.c_str(), "r");
    if (!pipe)
        return "";
    std::string reply;
    char buffer[4096];
    size_t read = 0;
    while (reply.size() < 256 * 1024 && (read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
        reply.append(buffer, read);
    TB_PCLOSE(pipe);
    return UpdateCheckPolicy::ParseReleaseTag(reply);
}

} // namespace

UpdateCheck& UpdateCheck::Instance()
{
    static UpdateCheck instance;
    return instance;
}

void UpdateCheck::Start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_started)
            return;
        m_started = true;
    }
    std::thread([this]()
    {
        std::string const module = FetchLatestTag("TortoiseBots");
        std::string const addon = FetchLatestTag("TortoiseBotsManager");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_module = module;
            m_addon = addon;
        }
        if (UpdateCheckPolicy::IsNewer(module, BuildVersion()))
            TB_LOG_BASIC("TortoiseBots update available: v%s (this server runs %s)", module.c_str(),
                BuildVersion().c_str());
    }).detach();
}

std::string UpdateCheck::LatestModule() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_module;
}

std::string UpdateCheck::LatestAddon() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_addon;
}

} // namespace TortoiseBots
