#pragma once

// Minimal module lifecycle for Phase 2.
// Logs load/unload; Phase 3 adds headless session handling.

namespace TortoiseBots {

void Initialize();
void Shutdown();

// Called by the generic pending-listener factory (see BotHostAdapter).
void RegisterWorldListener();

} // namespace TortoiseBots
