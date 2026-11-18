#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace TortoiseBots {

// Launches and stops the standalone tortoise-observability dashboard process
// alongside mangosd when observability and autostart are enabled.
class DashboardLauncher
{
public:
    static DashboardLauncher& Instance();

    void Start();
    void Stop();

private:
    DashboardLauncher() = default;
    ~DashboardLauncher();
    DashboardLauncher(DashboardLauncher const&) = delete;
    DashboardLauncher& operator=(DashboardLauncher const&) = delete;

    struct LaunchConfig
    {
        std::string binOverride;
        std::string loginDbRaw;
        std::string charDbRaw;
        std::string worldDbRaw;
        std::string dataDir;
        uint32_t udpPort = 9195;
        uint16_t httpPort = 8095;
    };

    void Worker(LaunchConfig cfg);

    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopping{false};
    std::thread m_workerThread;
    std::mutex m_procMutex;
#ifdef _WIN32
    void* m_childProcess = nullptr;
    void* m_jobObject = nullptr;
#else
    int m_childPid = -1;
#endif
};

} // namespace TortoiseBots
