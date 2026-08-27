#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots {

// Minimal generic role provider for the LFT queue. The core owns the queue and
// intersects this mask with its class mask; 0 means no opinion (falls back to
// class). The module answers only for its own Headless bots via AiFactory spec.
class BotLftRoleAdapter final : public PlayerScript
{
public:
    BotLftRoleAdapter();

    bool GetAllowedRoles(Player const* player, uint8& roles) override;
};

} // namespace TortoiseBots
