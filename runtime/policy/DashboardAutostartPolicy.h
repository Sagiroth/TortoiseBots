#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace DashboardAutostartPolicy {

struct DbInfo
{
    std::string host;
    std::string port;
    std::string user;
    std::string password;
    std::string database;
    bool valid = false;
};

struct LaunchEnv
{
    std::string key;
    std::string value;
};

inline std::string TrimCopy(std::string const& s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
        --end;
    return s.substr(start, end - start);
}

inline DbInfo ParseDbInfo(std::string const& raw, std::string const& defaultDb = "")
{
    DbInfo info;
    std::string clean = TrimCopy(raw);
    if (clean.size() >= 2 && clean.front() == '"' && clean.back() == '"')
        clean = clean.substr(1, clean.size() - 2);

    if (clean.empty())
    {
        info.host = "127.0.0.1";
        info.port = "3306";
        info.user = "mangos";
        info.password = "mangos";
        info.database = defaultDb;
        return info;
    }

    std::vector<std::string> fields;
    size_t start = 0;
    while (fields.size() < 4)
    {
        size_t pos = clean.find(';', start);
        if (pos == std::string::npos)
            break;
        fields.push_back(clean.substr(start, pos - start));
        start = pos + 1;
    }
    fields.push_back(clean.substr(start));
    while (fields.size() < 5)
        fields.emplace_back();

    info.host = fields[0].empty() ? "127.0.0.1" : fields[0];
    info.port = fields[1].empty() ? "3306" : fields[1];
    info.user = fields[2].empty() ? "mangos" : fields[2];
    info.password = fields[3]; // Empty password in "host;port;user;;db" is intentional.
    info.database = fields[4].empty() ? defaultDb : fields[4];
    info.valid = true;
    return info;
}

inline std::string DirName(std::string const& path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return path.substr(0, 1);
    if (pos == 2 && path.size() >= 3 && path[1] == ':')
        return path.substr(0, 3);
    return path.substr(0, pos);
}

inline std::string JoinPath(std::string const& dir, std::string const& name, bool isWindows)
{
    if (dir.empty() || dir == ".")
        return name;
    char const sep = isWindows ? '\\' : '/';
    if (dir.back() == '/' || dir.back() == '\\')
        return dir + name;
    return dir + sep + name;
}

inline bool IsAbsolutePath(std::string const& path)
{
    if (path.empty())
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

inline std::vector<std::string> CandidateFilenames(bool isWindows)
{
    if (isWindows)
        return {"tortoise-observability-windows-amd64.exe", "tortoise-observability.exe"};
    return {"tortoise-observability-linux-amd64", "tortoise-observability"};
}

inline std::string LogFilePath(std::string const& binPath, bool isWindows)
{
    return JoinPath(DirName(binPath), "dashboard.log", isWindows);
}

inline std::string IconCacheDir(std::string const& binPath, bool isWindows)
{
    return JoinPath(DirName(binPath), ".icon-cache", isWindows);
}

inline std::string DbcDirFromDataDir(std::string const& dataDir, std::string const& exeDir, bool isWindows)
{
    std::string clean = TrimCopy(dataDir);
    if (clean.size() >= 2 && clean.front() == '"' && clean.back() == '"')
        clean = clean.substr(1, clean.size() - 2);
    while (!clean.empty() && (clean.back() == '/' || clean.back() == '\\'))
        clean.pop_back();
    if (clean.empty())
        return "";
    std::string base = IsAbsolutePath(clean) ? clean : JoinPath(exeDir, clean, isWindows);
    return JoinPath(base, "dbc", isWindows);
}

inline std::vector<LaunchEnv> BuildEnvironment(
    DbInfo const& login,
    DbInfo const& character,
    DbInfo const& world,
    uint32_t udpPort,
    uint16_t httpPort,
    std::string const& dbcDir,
    std::string const& iconCacheDir)
{
    std::vector<LaunchEnv> env;
    env.push_back({"DB_HOST", login.host});
    env.push_back({"DB_PORT", login.port});
    env.push_back({"DB_USER", login.user});
    if (!login.password.empty())
        env.push_back({"DB_PASSWORD", login.password});
    env.push_back({"DB_LOGIN", login.database});
    env.push_back({"DB_CHAR", character.database});
    env.push_back({"DB_WORLD", world.database});
    env.push_back({"UDP_PORT", std::to_string(udpPort ? udpPort : 9195)});
    env.push_back({"UDP_HOST", "127.0.0.1"});
    env.push_back({"HTTP_PORT", std::to_string(httpPort ? httpPort : 8095)});
    if (!iconCacheDir.empty())
        env.push_back({"ICON_CACHE_DIR", iconCacheDir});
    if (!dbcDir.empty())
        env.push_back({"DBC_DIR", dbcDir});
    return env;
}

inline std::vector<std::string> BuildArgs(bool emptyPassword)
{
    if (emptyPassword)
        return {"-db-pass="};
    return {};
}

} // namespace DashboardAutostartPolicy
} // namespace TortoiseBots
