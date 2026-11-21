#include "../runtime/policy/DashboardAutostartPolicy.h"

#include <cassert>
#include <iostream>

using namespace TortoiseBots::DashboardAutostartPolicy;

namespace {

void TestParseDbInfo()
{
    {
        DbInfo info = ParseDbInfo("\"127.0.0.1;3306;mangos;secret;tw_logon\"", "tw_logon");
        assert(info.valid);
        assert(info.host == "127.0.0.1");
        assert(info.port == "3306");
        assert(info.user == "mangos");
        assert(info.password == "secret");
        assert(info.database == "tw_logon");
    }
    {
        // Empty password must stay empty, not fall back to "mangos".
        DbInfo info = ParseDbInfo("10.0.0.5;3307;botuser;;custom_char", "tw_char");
        assert(info.valid);
        assert(info.host == "10.0.0.5");
        assert(info.port == "3307");
        assert(info.user == "botuser");
        assert(info.password.empty());
        assert(info.database == "custom_char");
    }
    {
        DbInfo info = ParseDbInfo("", "tw_world");
        assert(!info.valid);
        assert(info.host == "127.0.0.1");
        assert(info.port == "3306");
        assert(info.user == "mangos");
        assert(info.password == "mangos");
        assert(info.database == "tw_world");
    }
}

void TestPaths()
{
    assert(DirName("/opt/turtle/bin/mangosd") == "/opt/turtle/bin");
    assert(DirName("C:\\Turtle\\mangosd.exe") == "C:\\Turtle");
    assert(DirName("mangosd") == ".");

    assert(JoinPath("/opt/turtle/bin", "tortoise-observability-linux-amd64", false) ==
           "/opt/turtle/bin/tortoise-observability-linux-amd64");
    assert(JoinPath("C:\\Turtle\\", "tortoise-observability-windows-amd64.exe", true) ==
           "C:\\Turtle\\tortoise-observability-windows-amd64.exe");

    auto winCands = CandidateFilenames(true);
    assert(winCands.size() == 2);
    assert(winCands[0] == "tortoise-observability-windows-amd64.exe");

    auto linCands = CandidateFilenames(false);
    assert(linCands.size() == 2);
    assert(linCands[0] == "tortoise-observability-linux-amd64");

    assert(LogFilePath("/opt/turtle/bin/tortoise-observability-linux-amd64", false) ==
           "/opt/turtle/bin/dashboard.log");
    assert(LogFilePath("C:\\Turtle\\tortoise-observability-windows-amd64.exe", true) ==
           "C:\\Turtle\\dashboard.log");

    assert(DbcDirFromDataDir("./data/", "/opt/turtle/bin", false) == "/opt/turtle/bin/./data/dbc");
    assert(DbcDirFromDataDir("/opt/turtle/data/", "/opt/turtle/bin", false) == "/opt/turtle/data/dbc");
    assert(DbcDirFromDataDir("C:\\Turtle\\Data\\", "C:\\Turtle", true) == "C:\\Turtle\\Data\\dbc");
    assert(DbcDirFromDataDir("", "/opt/turtle/bin", false).empty());
}

void TestEnvironmentAndArgs()
{
    DbInfo login = ParseDbInfo("127.0.0.1;3306;mangos;;tw_logon", "tw_logon");
    DbInfo ch = ParseDbInfo("127.0.0.1;3306;mangos;;tw_char", "tw_char");
    DbInfo world = ParseDbInfo("127.0.0.1;3306;mangos;;tw_world", "tw_world");

    auto env = BuildEnvironment(login, ch, world, 9195, 8095, "/opt/dbc", "/opt/.icon-cache");
    bool hasPass = false;
    bool hasLogin = false;
    bool hasChar = false;
    bool hasWorld = false;
    for (auto const& kv : env)
    {
        if (kv.key == "DB_PASSWORD")
            hasPass = true;
        if (kv.key == "DB_LOGIN")
            hasLogin = (kv.value == "tw_logon");
        if (kv.key == "DB_CHAR")
            hasChar = (kv.value == "tw_char");
        if (kv.key == "DB_WORLD")
            hasWorld = (kv.value == "tw_world");
    }
    assert(!hasPass);
    assert(hasLogin && hasChar && hasWorld);

    auto findEnv = [](std::vector<LaunchEnv> const& v, std::string const& k) -> std::string {
        for (auto const& kv : v)
            if (kv.key == k)
                return kv.value;
        return "";
    };

    // Bare host keeps the 127.0.0.1 default (HTTP_HOST not overridden).
    assert(findEnv(env, "HTTP_HOST").empty());
    assert(EffectiveHttpHost(false, "") == "127.0.0.1");

    // Container binds 0.0.0.0 so published ports are reachable from the host.
    auto containerEnv = BuildEnvironment(login, ch, world, 9195, 8095, "/opt/dbc", "/opt/.icon-cache", true, "");
    assert(findEnv(containerEnv, "HTTP_HOST") == "0.0.0.0");
    assert(EffectiveHttpHost(true, "") == "0.0.0.0");

    // Explicit HTTP_HOST in the environment is preserved even inside a container.
    auto presetEnv = BuildEnvironment(login, ch, world, 9195, 8095, "/opt/dbc", "/opt/.icon-cache", true, "127.0.0.1");
    assert(findEnv(presetEnv, "HTTP_HOST").empty());
    assert(EffectiveHttpHost(true, "127.0.0.1") == "127.0.0.1");

    auto argsEmptyPass = BuildArgs(true);
    assert(argsEmptyPass.size() == 1 && argsEmptyPass[0] == "-db-pass=");
    assert(BuildArgs(false).empty());
}

void TestChooseHttpPort()
{
    {
        // Configured port free: use it.
        PortChoice c = ChooseHttpPort(8095, [](uint16_t) { return PortState::Free; });
        assert(c.port == 8095 && !c.alreadyRunning);
    }
    {
        // Our dashboard already on the configured port: reuse, do not start another.
        PortChoice c = ChooseHttpPort(8095, [](uint16_t p) { return p == 8095 ? PortState::Dashboard : PortState::Free; });
        assert(c.port == 8095 && c.alreadyRunning);
    }
    {
        // Another program (e.g. Docker) on 8095: take the next free port.
        PortChoice c = ChooseHttpPort(8095, [](uint16_t p) { return p == 8095 ? PortState::Foreign : PortState::Free; });
        assert(c.port == 8096 && !c.alreadyRunning);
    }
    {
        // A dashboard left on a fallback port wins over a free configured port.
        PortChoice c = ChooseHttpPort(8095, [](uint16_t p) { return p == 8097 ? PortState::Dashboard : PortState::Free; });
        assert(c.port == 8097 && c.alreadyRunning);
    }
    {
        // Every port in the span taken by others: no port.
        int probes = 0;
        PortChoice c = ChooseHttpPort(8095, [&probes](uint16_t) { ++probes; return PortState::Foreign; });
        assert(c.port == 0 && !c.alreadyRunning);
        assert(probes == kHttpPortSpan);
    }
    {
        // The span stops at the top of the port range.
        PortChoice c = ChooseHttpPort(65535, [](uint16_t) { return PortState::Foreign; });
        assert(c.port == 0);
    }
    {
        // 0 means the default port.
        PortChoice c = ChooseHttpPort(0, [](uint16_t) { return PortState::Free; });
        assert(c.port == 8095);
    }
    assert(LooksLikeDashboardMetrics("# TYPE tortoisebots_snapshots_total counter\ntortoisebots_snapshots_total 0\n"));
    assert(!LooksLikeDashboardMetrics("HTTP/1.1 200 OK\r\n\r\n<html>Docker</html>"));
    assert(!LooksLikeDashboardMetrics(""));
}

} // namespace

int main()
{
    TestParseDbInfo();
    TestPaths();
    TestEnvironmentAndArgs();
    TestChooseHttpPort();
    std::cout << "test_dashboard_autostart_policy: ALL PASSED\n";
    return 0;
}
