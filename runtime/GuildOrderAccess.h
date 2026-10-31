#pragma once

// Per-guild bot rank for guild bank access: the rank every bot in a guild
// is moved to, so the bots share one rank the master can open bank tabs to.
// Backed by `tortoise_bots_guild_bot_rank` (one row per guild, written when
// the master picks a rank); guilds without a row use the default rank 1.
#include <cstdint>
#include <optional>

namespace TortoiseBots
{
namespace GuildOrders
{

// Stored choice for the guild, or nullopt when the master never picked one.
std::optional<uint32_t> StoredBotRank(uint32_t guildId);
// Remember the master's choice. World thread only (the `.bot order rank`
// command). Returns false when the write failed.
bool StoreBotRank(uint32_t guildId, uint32_t rank);

// Rank every bot in the guild should hold: the stored choice, or the
// highest rank below the master when nothing was stored. 0 disables the
// feature (config off, or the stored choice was cleared).
uint32_t WantedBotRank(uint32_t guildId, uint32_t rankCount);

// Rank a joining member holds: the guild's bot rank for bot characters in
// guilds with a real player member, nullopt for anyone else (the
// core-assigned rank stands). All-bot pool guilds keep their own ranks.
std::optional<uint32_t> JoinBotRank(uint32_t guildId, uint32_t botGuidLow);
// Move every bot in the guild to the stored rank. World thread only.
void ApplyBotRankToGuild(uint32_t guildId);
void ApplyBotRanksAtStartup();

// True when bots in the guild must not manage it (promote, demote, kick,
// invite): the guild has a real player member, so every bot holds the same
// bot rank and rank fights would follow. All-bot guilds manage themselves
// as before.
bool BotRankBlocksManagement(uint32_t guildId);
// Drop the cached management verdict: the roster changed outside the rank
// paths above (a member left). The next query recomputes it.
void MarkBotRankGuildStale(uint32_t guildId);

// Forget the stored choice when the guild is gone.
void OnBotRankDisband(uint32_t guildId);

// ORDER_ACCESS computation for one asker: the bot rank of the guild, the
// bank tabs it reaches, whether it may withdraw gold, and whether the
// asker may change the rank. World thread only.
std::string FormatAccessLine(uint32_t guildId, uint32_t askerGuidLow);

} // namespace GuildOrders
} // namespace TortoiseBots
