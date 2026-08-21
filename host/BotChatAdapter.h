#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "IChatCommandInterceptor.h"
#include <string>

// Thin chat-command adapter — the only place that knows "IChatCommandInterceptor + 'bot' = TortoiseBots".
// Core sees only IChatCommandInterceptor; this adapter is bot-aware and delegates to
// the reusable BotCommands/BotManager APIs (no movement/behavior logic here).
// Replaceable later by a Penqle CommandScript: that migration only re-registers the
// same BotCommands handlers via the new script system, no change to BotCommands/BotController.

namespace TortoiseBots {

// pi-lens-ignore: clang:unknown_typename,clang:expected_class_name
class BotChatAdapter : public IChatCommandInterceptor
{
public:
    static BotChatAdapter& Instance();

    // IChatCommandInterceptor
    // pi-lens-ignore: clang:unknown_typename,clang:unknown_type
    bool TryHandleCommand(ChatHandler* handler, char const* text) override;

    // Explicit lifecycle — called from Module/BotHostAdapter EnsureRegistered.
    void EnsureRegistered();
    void EnsureUnregistered();

private:
    BotChatAdapter() = default;
    ~BotChatAdapter() = default;
    BotChatAdapter(BotChatAdapter const&) = delete;
    BotChatAdapter& operator=(BotChatAdapter const&) = delete;

    bool m_registered = false;
};

} // namespace TortoiseBots
