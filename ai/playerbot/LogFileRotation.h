#pragma once

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace ai
{
// Bot logs keep a plain live file. At the first open of a run and every hour
// the live file moves to a piece named after the time it was closed,
// <stem>_YYYY-MM-DD_HH-MM-SS<ext>, which the caller compresses to <piece>.gz;
// pieces older than the retention are deleted.
class LogFileRotation
{
public:
    // True only the first time a path is opened in this run.
    bool FirstOpen(std::string const& path) { return prepared.insert(path).second; }

    // Moves the live file to a piece; piece stays empty when there was no
    // file. A failed rename leaves the live file where it was.
    static bool Archive(std::string const& path, time_t now, std::string& piece, std::error_code& error)
    {
        error.clear();
        piece.clear();
        bool const exists = std::filesystem::exists(path, error);
        if (error || !exists)
            return !error;

        std::string const name = PieceName(path, now);
        std::string candidate = name;
        for (int n = 1; std::filesystem::exists(candidate, error) || std::filesystem::exists(candidate + ".gz", error); ++n)
            candidate = PieceName(path, now, n);
        if (error)
            return false;

        std::filesystem::rename(path, candidate, error);
        if (error)
            return false;
        piece = candidate;
        return true;
    }

    static std::string PieceName(std::string const& path, time_t closedAt, int n = 0)
    {
        std::filesystem::path const p(path);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", std::localtime(&closedAt));
        std::string name = p.stem().string() + "_" + stamp;
        if (n)
            name += "-" + std::to_string(n);
        return (p.parent_path() / (name + p.extension().string())).string();
    }

    // Pieces of this log in its directory: plain ones (a compression that did
    // not finish) and compressed ones.
    static void Pieces(std::string const& path, std::vector<std::string>& plain, std::vector<std::string>& compressed)
    {
        std::filesystem::path const p(path);
        std::string const prefix = p.stem().string() + "_";
        std::string const ext = p.extension().string();
        std::error_code error;
        std::filesystem::path const dir = p.parent_path().empty() ? std::filesystem::path(".") : p.parent_path();
        for (auto const& entry : std::filesystem::directory_iterator(dir, error))
        {
            std::string const file = entry.path().filename().string();
            // stem_ + "YYYY-MM-DD_HH-MM-SS" (19 chars) starting with a digit.
            if (file.size() < prefix.size() + 19 || file.compare(0, prefix.size(), prefix) != 0 ||
                !std::isdigit(static_cast<unsigned char>(file[prefix.size()])))
                continue;
            if (EndsWith(file, ext + ".gz"))
                compressed.push_back(entry.path().string());
            else if (EndsWith(file, ext))
                plain.push_back(entry.path().string());
        }
    }

    // Deletes pieces last written more than retentionDays ago.
    static void Prune(std::string const& path, uint32_t retentionDays)
    {
        std::vector<std::string> plain, compressed;
        Pieces(path, plain, compressed);
        auto const cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * retentionDays);
        std::error_code error;
        for (auto const* list : { &plain, &compressed })
            for (std::string const& piece : *list)
                if (std::filesystem::last_write_time(piece, error) < cutoff && !error)
                    std::filesystem::remove(piece, error);
    }

private:
    static bool EndsWith(std::string const& s, std::string const& suffix)
    {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::unordered_set<std::string> prepared;
};
}
