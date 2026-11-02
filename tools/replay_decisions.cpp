// replay_decisions: cut, inspect, replay and upgrade decision captures.
//
//   g++ -std=c++17 -O2 -Wall -Wextra tools/replay_decisions.cpp -o /tmp/replay_decisions
//
// Exit 0 pass, 1 a fixture failed, 2 usage or load error.

#include "DecisionReplay.h"
#include "DecisionReplayScenario.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace TortoiseBots;
using namespace TortoiseBots::DecisionReplay;

namespace
{

struct Args
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;
};

bool Has(const Args& args, const std::string& flag)
{
    for (const std::string& have : args.flags)
        if (have == flag)
            return true;
    return false;
}

std::string Value(const Args& args, const std::string& name, const std::string& fallback = std::string())
{
    auto it = args.values.find(name);
    return it == args.values.end() ? fallback : it->second;
}

bool ParseArgs(int argc, char** argv, int start, Args& args, std::string& err)
{
    static std::vector<std::string> const withValue = {"--seq",      "--from",      "--to",       "--name",
                                                       "--mode",     "--out-dir",   "--step",     "--phase",
                                                       "--order-contains", "--note-contains"};
    for (int i = start; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-')
        {
            bool needsValue = false;
            for (const std::string& name : withValue)
                if (name == arg)
                    needsValue = true;
            if (needsValue)
            {
                if (i + 1 >= argc)
                {
                    err = arg + " needs a value";
                    return false;
                }
                args.values[arg] = argv[++i];
            }
            else if (arg == "--facts" || arg == "--run" || arg == "--post" || arg == "--verbose")
                args.flags.push_back(arg);
            else
            {
                err = "unknown option " + arg;
                return false;
            }
            continue;
        }
        args.positional.push_back(arg);
    }
    return true;
}

std::string TickLine(const TickRec& tick)
{
    std::string line = "tick " + std::to_string(tick.seq) + " t=" + std::to_string(tick.t) + " step " +
                       std::to_string(tick.out.step) + " phase " + PhaseText(tick.out.phase) + " state " +
                       DungeonRun::StateName(static_cast<DungeonRun::State>(tick.out.state));
    if (!tick.out.order.empty())
        line += " order \"" + Cut(tick.out.order, 100) + "\"";
    if (!tick.out.note.empty())
        line += " note \"" + Cut(tick.out.note, 80) + "\"";
    return line;
}

int LoadCapture(const std::vector<std::string>& files, Capture& capture)
{
    std::string err;
    if (!LoadSegments(files, capture, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    return 0;
}

bool IsFixturePath(const std::string& path)
{
    return std::filesystem::path(path).extension() == ".json";
}

int List(const Args& args, bool find)
{
    if (args.positional.empty())
    {
        std::cerr << "usage: replay_decisions " << (find ? "find" : "list") << " <seg...> [filters]\n";
        return 2;
    }
    Capture capture;
    if (int rc = LoadCapture(args.positional, capture))
        return rc;

    std::string const orderContains = Value(args, "--order-contains");
    std::string const noteContains = Value(args, "--note-contains");
    std::string const phase = Value(args, "--phase");
    bool const haveStep = args.values.count("--step") > 0;
    std::uint64_t const step = haveStep ? std::strtoull(Value(args, "--step").c_str(), nullptr, 10) : 0;

    for (const std::string& note : capture.notes)
        if (!find && note.compare(0, 4, "end ") != 0)
            std::cout << note << "\n";
    for (const TickRec& tick : capture.ticks)
    {
        if (find)
        {
            if (!orderContains.empty() && tick.out.order.find(orderContains) == std::string::npos)
                continue;
            if (!noteContains.empty() && tick.out.note.find(noteContains) == std::string::npos)
                continue;
            if (haveStep && tick.out.step != step)
                continue;
            if (!phase.empty() && PhaseText(tick.out.phase) != phase)
                continue;
        }
        std::cout << TickLine(tick) << "\n";
    }
    if (!find)
        for (const std::string& note : capture.notes)
            if (note.compare(0, 4, "end ") == 0)
                std::cout << note << "\n";
    return 0;
}

void PrintFields(const std::string& prefix, const FieldMap& fields)
{
    for (const auto& field : fields)
        std::cout << prefix << "." << field.first << ": " << field.second << "\n";
}

int Show(const Args& args)
{
    if (args.positional.empty())
    {
        std::cerr << "usage: replay_decisions show <seg...|fixture> --seq N [--facts] [--run] [--post]\n";
        return 2;
    }
    if (!args.values.count("--seq"))
    {
        std::cerr << "show needs --seq N\n";
        return 2;
    }
    std::uint64_t const seq = std::strtoull(Value(args, "--seq").c_str(), nullptr, 10);

    auto showTick = [&](const Capture& capture) -> int
    {
        for (const TickRec& tick : capture.ticks)
        {
            if (tick.seq != seq)
                continue;
            std::cout << TickLine(tick) << "\n";
            if (tick.kind != "tick")
            {
                std::cout << "undecodable record: no keyframe in reach\n";
                return 0;
            }
            if (Has(args, "--facts"))
                PrintFields("facts", tick.facts);
            if (Has(args, "--run"))
                PrintFields("run", tick.preRun);
            if (Has(args, "--post"))
                PrintFields("post", tick.postRun);
            return 0;
        }
        std::cerr << "replay_decisions: no tick with seq " << seq << "\n";
        return 2;
    };

    if (args.positional.size() == 1 && IsFixturePath(args.positional[0]))
    {
        Fixture fixture;
        std::string err;
        if (!LoadFixture(args.positional[0], fixture, err))
        {
            std::cerr << "replay_decisions: " << err << "\n";
            return 2;
        }
        return showTick(fixture.cap);
    }
    Capture capture;
    if (int rc = LoadCapture(args.positional, capture))
        return rc;
    return showTick(capture);
}

int ExtractCommand(const Args& args)
{
    if (args.positional.empty())
    {
        std::cerr << "usage: replay_decisions extract <seg...> --from N --to N --name NAME [--mode M] [--out-dir D]\n";
        return 2;
    }
    if (!args.values.count("--from") || !args.values.count("--to") || !args.values.count("--name"))
    {
        std::cerr << "extract needs --from, --to and --name\n";
        return 2;
    }
    Capture capture;
    if (int rc = LoadCapture(args.positional, capture))
        return rc;

    ExtractOptions options;
    options.fromSeq = std::strtoull(Value(args, "--from").c_str(), nullptr, 10);
    options.toSeq = std::strtoull(Value(args, "--to").c_str(), nullptr, 10);
    options.name = Value(args, "--name");
    options.mode = Value(args, "--mode", "independent");
    options.outDir = Value(args, "--out-dir", "tools/fixtures/decisions");
    options.segment = std::filesystem::path(args.positional.front()).filename().string();

    std::string path, err;
    if (!Extract(capture, options, path, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    std::cout << path << "\n";
    return 0;
}

std::string ReportLine(const Fixture& fixture, const Report& report)
{
    std::string state = report.xpass ? "XPASS" : report.xfail ? "XFAIL" : report.pass ? "pass" : "fail";
    std::size_t ticks = report.ticks.size(), reproduced = 0;
    for (const TickResult& tick : report.ticks)
        if (tick.reproduced)
            ++reproduced;
    std::string line = fixture.name + ": " + state + " (" + std::to_string(reproduced) + "/" +
                       std::to_string(ticks) + " ticks reproduce, " + std::to_string(fixture.expect.size()) +
                       " expectation(s))";
    return line;
}

int Run(const Args& args)
{
    if (args.positional.empty())
    {
        std::cerr << "usage: replay_decisions run <fixture...> [--verbose]\n";
        return 2;
    }
    int rc = 0;
    for (const std::string& path : args.positional)
    {
        Fixture fixture;
        std::string err;
        if (!LoadFixture(path, fixture, err))
        {
            std::cerr << "replay_decisions: " << err << "\n";
            rc = 2;
            continue;
        }
        Report const report = Replay(fixture);
        std::cout << ReportLine(fixture, report) << "\n";
        if (Has(args, "--verbose"))
            for (const TickResult& tick : report.ticks)
                std::cout << "  seq " << tick.seq << (tick.reproduced ? " reproduced" : " differs") << "\n";
        for (const Expect& expect : fixture.expect)
        {
            std::string why;
            bool satisfied = false;
            for (const TickResult& tick : report.ticks)
                if (tick.seq == expect.seq)
                {
                    satisfied = EvaluateExpect(expect, tick.got, tick.post, why);
                    break;
                }
            if (!satisfied && why.empty())
                why = "no such tick";
            std::cout << "  seq " << expect.seq << " expectation: " << (satisfied ? "ok" : "failed") <<
                (why.empty() ? "" : ": " + why) << "\n";
        }
        for (const std::string& failure : report.failures)
            std::cout << "  " << failure << "\n";
        if (!report.pass)
            rc = rc == 2 ? 2 : 1;
    }
    return rc;
}

int WhyCommand(const Args& args)
{
    if (args.positional.size() != 1 || !args.values.count("--seq"))
    {
        std::cerr << "usage: replay_decisions why <fixture> --seq N\n";
        return 2;
    }
    Fixture fixture;
    std::string err;
    if (!LoadFixture(args.positional[0], fixture, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    std::vector<std::string> fields;
    std::uint64_t const seq = std::strtoull(Value(args, "--seq").c_str(), nullptr, 10);
    if (!Why(fixture, seq, fields, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    if (fields.empty())
        std::cout << "seq " << seq << ": no single Facts field changes the decision\n";
    for (const std::string& field : fields)
        std::cout << "seq " << seq << ": " << field << "\n";
    return 0;
}

int UpgradeCommand(const Args& args)
{
    if (args.positional.size() != 1)
    {
        std::cerr << "usage: replay_decisions upgrade <fixture>\n";
        return 2;
    }
    std::string err, summary;
    if (!UpgradeFixture(args.positional[0], err, summary))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    std::cout << summary << "\n";
    return 0;
}

struct MemorySink : DecisionCapture::Sink
{
    std::vector<std::pair<std::string, std::string>> lines;

    bool Push(std::string line) override
    {
        lines.emplace_back(std::move(line), "scenario");
        return true;
    }

    bool NewSegment(std::uint32_t, std::uint64_t) override
    {
        return true;
    }
};

int Synth(const Args& args)
{
    std::string const outDir = Value(args, "--out-dir", "tools/fixtures/decisions");
    MemorySink sink;
    RecordScenario(sink);

    Capture capture;
    std::string err;
    if (!LoadLines(sink.lines, capture, err))
    {
        std::cerr << "replay_decisions: scenario: " << err << "\n";
        return 2;
    }
    ExtractOptions options;
    options.fromSeq = 1;
    options.toSeq = kScenarioTicks;
    options.name = "synthetic-pull";
    options.mode = "independent";
    options.status = "fixed";
    options.outDir = outDir;
    options.segment = "seg-0001.jsonl";
    std::string path;
    if (!Extract(capture, options, path, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    std::cout << path << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: replay_decisions <list|find|show|extract|run|why|upgrade|synth> ...\n";
        return 2;
    }
    std::string const command = argv[1];
    Args args;
    std::string err;
    if (!ParseArgs(argc, argv, 2, args, err))
    {
        std::cerr << "replay_decisions: " << err << "\n";
        return 2;
    }
    if (command == "list")
        return List(args, false);
    if (command == "find")
        return List(args, true);
    if (command == "show")
        return Show(args);
    if (command == "extract")
        return ExtractCommand(args);
    if (command == "run")
        return Run(args);
    if (command == "why")
        return WhyCommand(args);
    if (command == "upgrade")
        return UpgradeCommand(args);
    if (command == "synth")
        return Synth(args);
    std::cerr << "replay_decisions: unknown command " << command << "\n";
    return 2;
}
