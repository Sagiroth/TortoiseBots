#pragma once

#include "Common.h"
#include "ScriptObjects.h"

namespace TortoiseBots {

// Native world lifecycle adapter. The core only sees a normal WorldScript;
// BotManager owns the meaning of a Headless session and all bot state.
class BotHostAdapter final : public WorldScript
{
public:
    BotHostAdapter();

    void OnStartup() override;
    void OnUpdate(uint32 diff) override;
    void OnShutdown() override;

private:
    uint32 m_ticks = 0;
};

} // namespace TortoiseBots
