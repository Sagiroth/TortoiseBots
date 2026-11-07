#pragma once

// Per-guild bot rank for guild bank access: the rank every bot in a guild
// with a real player member is moved to, so the bots share one rank the
// master can open bank tabs to. Backed by `tortoise_bots_guild_bot_rank`
// (one row per guild holding a real rank 1..lowest, written when the master
// picks a rank); guilds without a row use the lowest rank. The whole
// feature is an on/off switch (`AiPlayerbot.GuildOrderBotRank`).
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
// guild's lowest rank when nothing was stored. 0 when the switch
// is off (nothing is applied anywhere then).
uint32_t WantedBotRank(uint32_t guildId, uint32_t rankCount);

// Rank a joining member holds: the guild's bot rank for bot characters in
// guilds with a real player member, nullopt for anyone else (the
// core-assigned rank stands). All-bot pool guilds keep their own ranks.
// World thread only (the join hook): recomputes the cached management
// verdict as a side effect.
std::optional<uint32_t> JoinBotRank(uint32_t guildId, uint32_t botGuidLow);
// Move every bot in the guild to the stored rank. World thread only:
// recomputes the cached management verdict as a side effect.
void ApplyBotRankToGuild(uint32_t guildId);
// Recompute the cached management verdict after a member left, ignoring a
// row whose database delete is still queued. World thread only.
void RefreshBotRankManagement(uint32_t guildId, uint32_t excludeGuidLow);
void LoadBotRankManagementAtStartup();
// Open bank tab 1 to the bot rank in the database, for every guild where the
// bot rank applies. The core keeps tab ranks in memory from its load at
// startup, so this takes effect at the next server start. World thread only;
// called at startup and shutdown.
void OpenBotBankTabForNextStart();

// True when bots in the guild must not manage it (promote, demote, kick,
// invite): the guild has a real player member, so every bot holds the same
// bot rank and rank fights would follow. All-bot guilds manage themselves
// as before. Map threads (AI ticks) only: a cache read, never the roster or
// the database; unknown means not blocked.
bool BotRankBlocksManagement(uint32_t guildId);

// Forget the stored choice when the guild is gone.
void OnBotRankDisband(uint32_t guildId);

// ORDER_ACCESS computation for one asker: the bot rank of the guild, the
// bank tabs it reaches, whether it may withdraw gold, and whether the
// asker may change the rank. When the switch is off the line carries no
// rank information (`TBM:ORDER_ACCESS|-|-|-|-|0|0`). World thread only.
std::string FormatAccessLine(uint32_t guildId, uint32_t askerGuidLow);

// Number of unlocked bank tabs of the guild, 0 when the guild never bought
// a bank (no guild_bank_tabs row). Reads the same rows the core loads its
// bank state from. Sync database read: travel requests and arrival checks
// only, never per-tick logic.
uint32_t GuildBankTabCount(uint32_t guildId);

} // namespace GuildOrders
} // namespace TortoiseBots
