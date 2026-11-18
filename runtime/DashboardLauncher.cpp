// Winsock must precede windows.h to avoid winsock1/winsock2 header collisions.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "DashboardLauncher.h"
#include "ObservabilityEmitter.h"
#include "policy/DashboardAutostartPolicy.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../host/ModuleLog.h"
#include "Config/Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

namespace TortoiseBots {

namespace {

constexpr uint16_t kDefaultHttpPort = 8095;
constexpr int kPortProbeTimeoutMs = 150;

#ifdef _WIN32
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

bool IsRegularFile(std::string const& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec) && !ec;
}

bool IsDirectory(std::string const& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(path), ec) && !ec;
}

std::string GetExecutableDir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return DashboardAutostartPolicy::DirName(std::string(buf, len));
    return ".";
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
        return DashboardAutostartPolicy::DirName(std::string(buf, static_cast<size_t>(len)));
    return ".";
#endif
}

bool IsPortListening(uint16_t port)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
        return false;

    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool listening = false;
    if (rc == 0)
    {
        listening = true;
    }
    else if (WSAGetLastError() == WSAEWOULDBLOCK)
    {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        timeval tv{};
        tv.tv_usec = kPortProbeTimeoutMs * 1000;
        if (select(0, nullptr, &wfds, nullptr, &tv) > 0)
        {
            int err = 0;
            int errLen = sizeof(err);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &errLen) == 0 && err == 0)
                listening = true;
        }
    }
    closesocket(sock);
    return listening;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool listening = false;
    if (rc == 0)
    {
        listening = true;
    }
    else if (errno == EINPROGRESS)
    {
        pollfd pfd{};
        pfd.fd = sock;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, kPortProbeTimeoutMs) > 0)
        {
            int err = 0;
            socklen_t errLen = sizeof(err);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0 && err == 0)
                listening = true;
        }
    }
    close(sock);
    return listening;
#endif
}

std::string ReadConfigPair(char const* dottedKey, char const* plainKey)
{
    std::string val = sConfig.GetStringDefault(dottedKey, "");
    if (val.empty() && plainKey)
        val = sConfig.GetStringDefault(plainKey, "");
    return val;
}

#ifdef _WIN32
bool EqualsIgnoreCase(std::string const& a, std::string const& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::vector<char> BuildWindowsEnvBlock(std::vector<DashboardAutostartPolicy::LaunchEnv> const& overrides)
{
    std::vector<char> block;
    LPCH rawEnv = GetEnvironmentStringsA();
    if (rawEnv)
    {
        char const* p = rawEnv;
        while (*p != '\0')
        {
            size_t len = std::strlen(p);
            std::string entry(p, len);
            size_t eq = entry.find('=');
            bool overridden = false;
            if (eq != std::string::npos && eq > 0)
            {
                std::string key = entry.substr(0, eq);
                for (auto const& kv : overrides)
                {
                    if (EqualsIgnoreCase(key, kv.key))
                    {
                        overridden = true;
                        break;
                    }
                }
            }
            if (!overridden)
                block.insert(block.end(), entry.begin(), entry.end()), block.push_back('\0');
            p += len + 1;
        }
        FreeEnvironmentStringsA(rawEnv);
    }
    for (auto const& kv : overrides)
    {
        std::string line = kv.key + "=" + kv.value;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}
#endif

} // namespace

DashboardLauncher& DashboardLauncher::Instance()
{
    static DashboardLauncher instance;
    return instance;
}

DashboardLauncher::~DashboardLauncher()
{
    Stop();
}

void DashboardLauncher::Start()
{
    if (!ObservabilityEmitter::Instance().IsEnabled())
        return;

    bool const autoStart = sPlayerbotAIConfig.observabilityAutoStart &&
                           sConfig.GetBoolDefault("AiPlayerbot.ObservabilityAutoStart", true);
    if (!autoStart)
        return;

    if (m_started.exchange(true))
        return;

    LaunchConfig cfg;
    cfg.binOverride = sPlayerbotAIConfig.observabilityBin;
    if (cfg.binOverride.empty())
        cfg.binOverride = sConfig.GetStringDefault("AiPlayerbot.ObservabilityBin", "");
    cfg.udpPort = sPlayerbotAIConfig.observabilityPort ? sPlayerbotAIConfig.observabilityPort : 9195;
    cfg.httpPort = kDefaultHttpPort;
    cfg.loginDbRaw = ReadConfigPair("LoginDatabase.Info", "LoginDatabaseInfo");
    cfg.charDbRaw = ReadConfigPair("CharacterDatabase.Info", "CharacterDatabaseInfo");
    cfg.worldDbRaw = ReadConfigPair("WorldDatabase.Info", "WorldDatabaseInfo");
    cfg.dataDir = sConfig.GetStringDefault("DataDir", "");

    m_stopping.store(false);
    m_workerThread = std::thread(&DashboardLauncher::Worker, this, std::move(cfg));
}

void DashboardLauncher::Stop()
{
    m_stopping.store(true);
    if (m_workerThread.joinable())
        m_workerThread.join();

    std::lock_guard<std::mutex> lock(m_procMutex);
#ifdef _WIN32
    if (m_childProcess)
    {
        HANDLE proc = static_cast<HANDLE>(m_childProcess);
        TerminateProcess(proc, 0);
        WaitForSingleObject(proc, 1000);
        CloseHandle(proc);
        m_childProcess = nullptr;
    }
    if (m_jobObject)
    {
        CloseHandle(static_cast<HANDLE>(m_jobObject));
        m_jobObject = nullptr;
    }
#else
    if (m_childPid > 0)
    {
        pid_t const pid = static_cast<pid_t>(m_childPid);
        if (kill(pid, SIGTERM) == 0)
        {
            int status = 0;
            bool reaped = false;
            for (int i = 0; i < 20; ++i)
            {
                pid_t r = waitpid(pid, &status, WNOHANG);
                if (r == pid || (r == -1 && errno == ECHILD))
                {
                    reaped = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!reaped)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
        }
        m_childPid = -1;
    }
#endif
}

void DashboardLauncher::Worker(LaunchConfig cfg)
{
    std::string const exeDir = GetExecutableDir();
    std::string binPath;

    if (!cfg.binOverride.empty())
    {
        if (IsRegularFile(cfg.binOverride))
            binPath = cfg.binOverride;
        else
        {
            std::string const rel = DashboardAutostartPolicy::JoinPath(exeDir, cfg.binOverride, kIsWindows);
            if (IsRegularFile(rel))
                binPath = rel;
        }
        if (binPath.empty())
        {
            TB_LOG_BASIC("TortoiseBots: observability dashboard binary not found at '%s'; autostart skipped",
                cfg.binOverride.c_str());
            return;
        }
    }
    else
    {
        auto const candidates = DashboardAutostartPolicy::CandidateFilenames(kIsWindows);
        for (auto const& name : candidates)
        {
            std::string const cand = DashboardAutostartPolicy::JoinPath(exeDir, name, kIsWindows);
            if (IsRegularFile(cand))
            {
                binPath = cand;
                break;
            }
        }
#ifdef TORTOISEBOTS_DASHBOARD_BUILD_PATH
        if (binPath.empty() && IsRegularFile(TORTOISEBOTS_DASHBOARD_BUILD_PATH))
            binPath = TORTOISEBOTS_DASHBOARD_BUILD_PATH;
#endif
        if (binPath.empty())
        {
            TB_LOG_BASIC("TortoiseBots: observability dashboard not found next to mangosd (%s); place %s there to enable autostart",
                exeDir.c_str(), candidates.front().c_str());
            return;
        }
    }

    if (m_stopping.load())
        return;

    if (IsPortListening(cfg.httpPort))
    {
        TB_LOG_BASIC("TortoiseBots: observability dashboard already listening on port %u; skipping autostart",
            static_cast<unsigned>(cfg.httpPort));
        return;
    }

    if (m_stopping.load())
        return;

#ifndef _WIN32
    if (access(binPath.c_str(), X_OK) != 0)
    {
        sLog.outError("TortoiseBots: observability dashboard '%s' is not executable (run chmod +x); autostart skipped",
            binPath.c_str());
        return;
    }
#endif

    auto const loginDb = DashboardAutostartPolicy::ParseDbInfo(cfg.loginDbRaw, "tw_logon");
    auto const charDb = DashboardAutostartPolicy::ParseDbInfo(cfg.charDbRaw, "tw_char");
    auto const worldDb = DashboardAutostartPolicy::ParseDbInfo(cfg.worldDbRaw, "tw_world");

    std::string dbcDir = DashboardAutostartPolicy::DbcDirFromDataDir(cfg.dataDir, exeDir, kIsWindows);
    if (!IsDirectory(dbcDir))
        dbcDir.clear();

    std::string const logPath = DashboardAutostartPolicy::LogFilePath(binPath, kIsWindows);
    std::string const iconCacheDir = DashboardAutostartPolicy::IconCacheDir(binPath, kIsWindows);
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(iconCacheDir), ec);
    }

    auto const envVars = DashboardAutostartPolicy::BuildEnvironment(
        loginDb, charDb, worldDb, cfg.udpPort, cfg.httpPort, dbcDir, iconCacheDir);
    auto const args = DashboardAutostartPolicy::BuildArgs(loginDb.password.empty());

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hLog = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hLog == INVALID_HANDLE_VALUE)
        hLog = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    HANDLE hNullIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = (hNullIn != INVALID_HANDLE_VALUE) ? hNullIn : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = (hLog != INVALID_HANDLE_VALUE) ? hLog : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = si.hStdOutput;

    std::string cmdLine = "\"" + binPath + "\"";
    for (auto const& arg : args)
        cmdLine += " " + arg;
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    std::vector<char> envBlock = BuildWindowsEnvBlock(envVars);
    std::string const binDir = DashboardAutostartPolicy::DirName(binPath);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        binPath.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        envBlock.data(),
        binDir.c_str(),
        &si,
        &pi);

    if (hLog != INVALID_HANDLE_VALUE)
        CloseHandle(hLog);
    if (hNullIn != INVALID_HANDLE_VALUE)
        CloseHandle(hNullIn);

    if (!ok)
    {
        sLog.outError("TortoiseBots: failed to start observability dashboard '%s' (error=%lu)",
            binPath.c_str(), static_cast<unsigned long>(GetLastError()));
        return;
    }

    CloseHandle(pi.hThread);

    HANDLE hJob = CreateJobObjectA(nullptr, nullptr);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)) ||
            !AssignProcessToJobObject(hJob, pi.hProcess))
        {
            CloseHandle(hJob);
            hJob = nullptr;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        m_childProcess = pi.hProcess;
        m_jobObject = hJob;
    }
#else
    // Prepare argv and envp before fork() so the child only calls async-signal-safe functions.
    std::vector<std::string> envStorage;
    if (environ)
    {
        for (char** ep = environ; *ep != nullptr; ++ep)
        {
            std::string entry(*ep);
            size_t eq = entry.find('=');
            bool overridden = false;
            if (eq != std::string::npos)
            {
                std::string key = entry.substr(0, eq);
                for (auto const& kv : envVars)
                {
                    if (key == kv.key)
                    {
                        overridden = true;
                        break;
                    }
                }
            }
            if (!overridden)
                envStorage.push_back(std::move(entry));
        }
    }
    for (auto const& kv : envVars)
        envStorage.push_back(kv.key + "=" + kv.value);

    std::vector<char*> envp;
    envp.reserve(envStorage.size() + 1);
    for (auto& s : envStorage)
        envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(binPath.c_str()));
    for (auto const& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    char const* binCStr = binPath.c_str();
    char const* logCStr = logPath.c_str();

    std::lock_guard<std::mutex> lock(m_procMutex);
    if (m_stopping.load())
        return;

    pid_t pid = fork();
    if (pid < 0)
    {
        sLog.outError("TortoiseBots: failed to fork observability dashboard '%s' (errno=%d)", binCStr, errno);
        return;
    }
    if (pid == 0)
    {
        int nullFd = open("/dev/null", O_RDONLY);
        if (nullFd >= 0)
        {
            if (nullFd != STDIN_FILENO)
            {
                dup2(nullFd, STDIN_FILENO);
                close(nullFd);
            }
        }
        int logFd = open(logCStr, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logFd < 0)
            logFd = open("/dev/null", O_WRONLY);
        if (logFd >= 0)
        {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            if (logFd > STDERR_FILENO)
                close(logFd);
        }
        execve(binCStr, argv.data(), envp.data());
        _exit(127);
    }
    m_childPid = static_cast<int>(pid);
#endif

    TB_LOG_BASIC("TortoiseBots: started observability dashboard (%s) -> http://localhost:%u/dashboard (log: %s)",
        binPath.c_str(), static_cast<unsigned>(cfg.httpPort), logPath.c_str());
}

} // namespace TortoiseBots
