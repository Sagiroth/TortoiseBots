#include "../ai/playerbot/LogFileRotation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
static void Write(fs::path const& p, char const* text) { std::ofstream(p) << text; }
static std::string Read(fs::path const& p)
{
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static bool Has(std::vector<std::string> const& list, fs::path const& p)
{
    return std::find(list.begin(), list.end(), p.string()) != list.end();
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    auto root = fs::path(argv[1]) / ("bot-log-rotation-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    auto p = (root / "bot_events.csv").string();
    time_t const now = 1791650000;
    std::error_code error;
    std::string piece;

    // Only the first open of a run archives.
    ai::LogFileRotation rotation;
    assert(rotation.FirstOpen(p));
    assert(!rotation.FirstOpen(p));

    // No live file: nothing to archive.
    assert(ai::LogFileRotation::Archive(p, now, piece, error) && !error && piece.empty());

    // The live file moves to a piece named after the close time.
    Write(p, "run one\n");
    assert(ai::LogFileRotation::Archive(p, now, piece, error) && !error);
    assert(piece == ai::LogFileRotation::PieceName(p, now));
    assert(!fs::exists(p) && Read(piece) == "run one\n");

    // Same second again: the earlier piece is never overwritten.
    Write(p, "run two\n");
    std::string second;
    assert(ai::LogFileRotation::Archive(p, now, second, error) && !error);
    assert(second == ai::LogFileRotation::PieceName(p, now, 1));
    assert(Read(piece) == "run one\n" && Read(second) == "run two\n");

    // Plain pieces are left-over packs; .gz pieces are packed; other files are not pieces.
    fs::path const packed = ai::LogFileRotation::PieceName(p, now - 3600) + ".gz";
    Write(packed, "gz");
    Write(root / "bot_events_old.csv", "not a piece");
    Write(root / "deaths_2026-10-10_10-00-00.csv", "another log");
    std::vector<std::string> plain, compressed;
    ai::LogFileRotation::Pieces(p, plain, compressed);
    assert(plain.size() == 2 && Has(plain, piece) && Has(plain, second));
    assert(compressed.size() == 1 && Has(compressed, packed));

    // Pieces past the retention are deleted, recent ones stay.
    fs::last_write_time(packed, fs::file_time_type::clock::now() - std::chrono::hours(24 * 4));
    ai::LogFileRotation::Prune(p, 3);
    assert(!fs::exists(packed) && fs::exists(piece) && fs::exists(second));
    assert(fs::exists(root / "bot_events_old.csv") && fs::exists(root / "deaths_2026-10-10_10-00-00.csv"));

    fs::remove_all(root);
}
