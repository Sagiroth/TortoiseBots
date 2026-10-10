#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>

namespace ai
{
// Keep one previous run. Repeated dumps during this run must not replace it.
class LogFileRotation
{
public:
    bool Prepare(std::string const& path, char const* mode, std::error_code& error)
    {
        error.clear();
        if (mode[0] != 'w' || prepared.count(path))
            return true;

        bool const exists = std::filesystem::exists(path, error);
        if (error)
            return false;
        if (exists)
        {
            std::filesystem::rename(path, path + ".1", error);
            if (error)
                return false; // Never truncate the current file after failed rotation.
        }
        prepared.insert(path);
        return true;
    }

    // Size cap: the full file replaces the previous copy, and the next open
    // starts a new one.
    bool RotateFull(std::string const& path, std::error_code& error)
    {
        error.clear();
        std::filesystem::rename(path, path + ".1", error);
        return !error;
    }

private:
    std::unordered_set<std::string> prepared;
};
}
