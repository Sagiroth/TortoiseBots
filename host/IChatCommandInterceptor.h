/*
 * Lens/build fallback for the generic chat command interceptor seam.
 * The authoritative core header lives at src/game/Chat/IChatCommandInterceptor.h
 * (tortoise-docker-penqle/source). This stub keeps the module buildable and
 * pi-lens clean when the core checkout is not on the include path. The two
 * headers are intentionally identical (generic, no bot naming) so either may be
 * found first via -I src/game/Chat vs -I host.
 */

#pragma once

class ChatHandler;

class IChatCommandInterceptor
{
public:
    virtual ~IChatCommandInterceptor() = default;
    virtual bool TryHandleCommand(ChatHandler* handler, char const* text) = 0;
};

void RegisterChatCommandInterceptor(IChatCommandInterceptor* interceptor);
void UnregisterChatCommandInterceptor(IChatCommandInterceptor* interceptor);
bool DispatchChatCommandInterceptors(ChatHandler* handler, char const* text);
