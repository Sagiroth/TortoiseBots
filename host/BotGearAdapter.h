#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots
{

// World tick driver for the `.bot gear` job queue (runtime/BotGearService).
class BotGearAdapter final : public WorldScript
{
public:
    BotGearAdapter();

    void OnStartup() override;
    void OnUpdate(uint32 diff) override;
    void OnShutdown() override;
};

} // namespace TortoiseBots
