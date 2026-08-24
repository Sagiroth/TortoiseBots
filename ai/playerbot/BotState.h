#pragma once

enum class BotState : uint8
{
    BOT_STATE_COMBAT = 0,
    BOT_STATE_NON_COMBAT = 1,
    BOT_STATE_DEAD = 2,
    BOT_STATE_REACTION = 3,
    BOT_STATE_ALL
};

// Compatibility names used by the forward-ported donor strategies.  They
// carry the same strongly typed values as the native enum class.
constexpr BotState BOT_STATE_COMBAT = BotState::BOT_STATE_COMBAT;
constexpr BotState BOT_STATE_NON_COMBAT = BotState::BOT_STATE_NON_COMBAT;
constexpr BotState BOT_STATE_DEAD = BotState::BOT_STATE_DEAD;
constexpr BotState BOT_STATE_REACTION = BotState::BOT_STATE_REACTION;
