#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots {

// World tick driver for the default-off bounded LFT fill.
// Delegates to runtime/LftBotFillService (observes GetQueuedPlayers,
// calls QueuePlayer, reconciles). Keeps core queue ownership.
class LftFillAdapter final : public WorldScript
{
public:
    LftFillAdapter();

    void OnStartup() override;
    void OnUpdate(uint32 diff) override;
    void OnShutdown() override;
};

} // namespace TortoiseBots
