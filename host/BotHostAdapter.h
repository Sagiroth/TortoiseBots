#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "Common.h"
#include "IWorldUpdateListener.h"
#include <string>

// Generic world-tick adapter — the only place that knows "headless session = bot".
// Core sees only IWorldUpdateListener; this adapter is bot-aware.

namespace TortoiseBots {

class BotHostAdapter : public IWorldUpdateListener
{
public:
    static BotHostAdapter& Instance();

    // IWorldUpdateListener
    void OnWorldUpdate(uint32 diff) override;

    // Ensure the pending-factory registration has happened (idempotent).
    void EnsureRegistered();

    // Lifecycle
    void OnStartup();
    void OnShutdown();

    // Diagnostics
    bool IsRegistered() const { return m_registered; }

private:
    BotHostAdapter() = default;
    ~BotHostAdapter() override = default;
    BotHostAdapter(BotHostAdapter const&) = delete;
    BotHostAdapter& operator=(BotHostAdapter const&) = delete;

    bool m_registered = false;
    uint32 m_ticks = 0;
    bool m_startupLogged = false;
};

// Factory function for the pending-listener registry (C linkage not needed).
IWorldUpdateListener* CreateBotHostAdapter();

} // namespace TortoiseBots
