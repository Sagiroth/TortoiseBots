#pragma once

// Pure policy for alt-bots (Issue #872): a player's own alt that keeps playing
// on its own after it is kicked from the owner's party.
//
// Header-only and free of core/database types on purpose: the rules are
// exercised standalone in tools/test_alt_bot_policy.cpp. AltBotService owns
// the live state and feeds these functions plain values.

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{

// One state machine per owned character. Offline and Companion are what
// exists today (logged out / `.bot add` follower); Autonomous is the alt-bot.
enum class AltBotState : uint8_t
{
    Offline,
    Companion,
    Autonomous,
};

enum class AltBotStartRefusal : uint8_t
{
    None,
    FeatureOff,
    NotOwnedAlt,
    Hardcore,
    NotInWorld,
};

// May a companion that just left the owner's party become an alt-bot?
// ownedAlt: a character on a player account (not a pool account, not hired).
// Hardcore is refused: a bot death would end the player's challenge run.
inline AltBotStartRefusal CheckAltBotStart(bool featureEnabled, bool ownedAlt, bool hardcore, bool inWorld)
{
    if (!featureEnabled)
        return AltBotStartRefusal::FeatureOff;
    if (!ownedAlt)
        return AltBotStartRefusal::NotOwnedAlt;
    if (!inWorld)
        return AltBotStartRefusal::NotInWorld;
    if (hardcore)
        return AltBotStartRefusal::Hardcore;
    return AltBotStartRefusal::None;
}

// Why an alt-bot session ended; picks the summary wording.
enum class AltBotEnd : uint8_t
{
    Removed,      // `.bot remove`, GM stop or the owner logging out (LogoutWithOwner)
    TakenBack,    // `.bot add` on the autonomous alt: it is a companion again
    PlayerLogin,  // the player logged in on the alt itself
};

// In-memory session counters for the end-of-session summary (no DB).
struct AltBotSessionCounters
{
    uint32_t startLevel = 0;
    uint32_t endLevel = 0;
    uint32_t quests = 0;
    uint32_t deaths = 0;
    uint32_t startMoney = 0; // copper
    uint32_t endMoney = 0;
};

inline std::string FormatAltBotMoney(int64_t copper)
{
    bool const negative = copper < 0;
    uint64_t const value = static_cast<uint64_t>(negative ? -copper : copper);
    std::string out = negative ? "-" : "+";
    uint64_t const gold = value / 10000, silver = (value / 100) % 100, cop = value % 100;
    if (gold)
        out += std::to_string(gold) + "g ";
    if (gold || silver)
        out += std::to_string(silver) + "s ";
    out += std::to_string(cop) + "c";
    return out;
}

inline std::string FormatAltBotSummary(std::string const& name, AltBotSessionCounters const& c)
{
    std::string out = name + " played on its own: ";
    if (c.endLevel > c.startLevel)
        out += "level " + std::to_string(c.startLevel) + " -> " + std::to_string(c.endLevel);
    else
        out += "level " + std::to_string(c.endLevel);
    out += ", " + std::to_string(c.quests) + (c.quests == 1 ? " quest" : " quests");
    out += ", " + std::to_string(c.deaths) + (c.deaths == 1 ? " death" : " deaths");
    out += ", gold " + FormatAltBotMoney(static_cast<int64_t>(c.endMoney) - static_cast<int64_t>(c.startMoney)) + ".";
    return out;
}

// Away summary: one line per autonomous alt since the owner's last logout.
// The window is per owner account, not per session: the owner's logout
// snapshots each alt (level, money, quest and death totals, equipped item
// entries per slot); an alt whose session starts later uses its session start
// as the baseline instead. Baselines live only in memory: alts do not survive
// a restart anyway. The service feeds plain values; the diff and the line
// stay here for the standalone test.
constexpr uint32_t kAltBotAwayEquipSlots = 19; // core EQUIPMENT_SLOT_END

struct AltBotAwayPoint
{
    uint32_t level = 0;
    uint32_t money = 0; // copper
    uint32_t quests = 0;
    uint32_t deaths = 0;
    std::vector<uint32_t> equipped; // item entry per equipment slot, 0 = empty
};

// What changed across the window; the line skips every part that is zero.
struct AltBotAwayDiff
{
    uint32_t fromLevel = 0;
    uint32_t toLevel = 0;
    int64_t money = 0; // copper, signed
    uint32_t quests = 0;
    uint32_t deaths = 0;
};

inline AltBotAwayDiff DiffAltBotAway(AltBotAwayPoint const& from, AltBotAwayPoint const& to)
{
    AltBotAwayDiff diff;
    diff.fromLevel = from.level;
    diff.toLevel = to.level;
    diff.money = static_cast<int64_t>(to.money) - static_cast<int64_t>(from.money);
    diff.quests = to.quests >= from.quests ? to.quests - from.quests : 0;
    diff.deaths = to.deaths >= from.deaths ? to.deaths - from.deaths : 0;
    return diff;
}

// Activity word for the line tail. Priority: dead, fighting, questing (on a
// quest errand), travelling, in town, resting, else grinding.
inline char const* AltBotAwayActivityWord(bool dead, bool fighting, bool questTravel, bool anyTravel,
    bool inTown, bool resting)
{
    if (dead)
        return "dead";
    if (fighting)
        return "fighting";
    if (questTravel)
        return "questing";
    if (anyTravel)
        return "travelling";
    if (inTown)
        return "in town";
    if (resting)
        return "resting";
    return "grinding";
}

inline std::string FormatAltBotAwayLine(std::string const& name, AltBotAwayDiff const& diff,
    std::vector<std::string> const& equippedLinks, std::string const& zone, std::string const& activity)
{
    std::vector<std::string> parts;
    if (diff.toLevel > diff.fromLevel)
        parts.push_back("level " + std::to_string(diff.fromLevel) + " -> " + std::to_string(diff.toLevel));
    if (diff.money != 0)
        parts.push_back(FormatAltBotMoney(diff.money));
    if (diff.quests == 1)
        parts.push_back("1 quest");
    else if (diff.quests > 1)
        parts.push_back(std::to_string(diff.quests) + " quests");
    if (diff.deaths == 1)
        parts.push_back("died once");
    else if (diff.deaths == 2)
        parts.push_back("died twice");
    else if (diff.deaths > 2)
        parts.push_back("died " + std::to_string(diff.deaths) + " times");
    if (!equippedLinks.empty())
    {
        std::string equipped = "equipped";
        for (std::string const& link : equippedLinks)
            equipped += " " + link;
        parts.push_back(equipped);
    }

    std::string out = name + ": ";
    if (parts.empty())
        out += "no change";
    else
    {
        out += parts[0];
        for (size_t i = 1; i < parts.size(); ++i)
            out += ", " + parts[i];
    }
    if (!zone.empty() && !activity.empty())
        out += ", now in " + zone + ", " + activity;
    else if (!zone.empty())
        out += ", now in " + zone;
    else if (!activity.empty())
        out += ", now " + activity;
    return out + ".";
}

// F37: each active alt displaces one pool bot. Only a bot that nobody is
// using can go: not grouped, not in combat, not in an instance, no master.
struct PoolDisplaceCandidate
{
    uint32_t guidLow = 0;
    uint32_t level = 0;
    bool grouped = false;
    bool inCombat = false;
    bool inInstance = false;
    bool hasMaster = false;
};

// Returns the guid of a random bot among the lowest-level eligible ones, or 0.
// `roll` is any random number; it picks among the lowest-level ties.
inline uint32_t PickPoolBotToDisplace(std::vector<PoolDisplaceCandidate> const& candidates, uint32_t roll)
{
    uint32_t lowest = UINT32_MAX;
    uint32_t ties = 0;
    for (PoolDisplaceCandidate const& c : candidates)
    {
        if (c.grouped || c.inCombat || c.inInstance || c.hasMaster || !c.guidLow)
            continue;
        if (c.level < lowest)
        {
            lowest = c.level;
            ties = 1;
        }
        else if (c.level == lowest)
            ++ties;
    }
    if (!ties)
        return 0;
    uint32_t pick = roll % ties;
    for (PoolDisplaceCandidate const& c : candidates)
    {
        if (c.grouped || c.inCombat || c.inInstance || c.hasMaster || !c.guidLow || c.level != lowest)
            continue;
        if (!pick--)
            return c.guidLow;
    }
    return 0;
}

// The pool's online target while alts are active: each alt takes one slot.
inline uint32_t PoolTargetWithAlts(uint32_t target, uint32_t activeAlts)
{
    return activeAlts >= target ? 0 : target - activeAlts;
}
// Location safety (Issue #872, instances worker): an alt that becomes
// autonomous inside an instance/BG or on a transport is out of place alone,
// and an autonomous alt left alone inside an instance should leave.
// The module can only teleport it out; the core owns graveyards and portals.
enum class AltBotExitTarget : uint8_t
{
    None,            // no relocation needed
    InstanceExit,    // dungeon/raid map portal out (or BG entry point): preferred
    Graveyard,       // BG or no usable exit portal: nearest graveyard
    LevelZone,       // ordinary map: the C19 level-fitting scatter
};

struct AltBotLocation
{
    bool inWorld = false;
    bool alive = false;
    bool inBattleGround = false;
    bool onTransport = false;   // boat/zeppelin: outside the world routing
    bool instanceableMap = false; // dungeon, raid or BG map
    bool dungeonMap = false;    // dungeon or raid only (has an exit portal)
    bool haveExit = false;      // GetGoBackTrigger(map) returns a portal out
    bool haveGrave = false;     // nearest graveyard lookup returned a row
    bool graveSameMap = false;
};

// Where should an autonomous alt start out? Ungrouped and misplaced (in an
// instance/BG or on a transport) it leaves via the exit portal when one
// exists, else falls to the nearest graveyard (BGs always do: the BG exit
// teleports nowhere useful mid-match). Ordinary maps take the C19 scatter
// (None: the caller still runs TeleportToLevelFittingZone, it already
// fail-closes). Grouped with someone else it stays put (F42, handled by
// TryStart's early return, not here).
inline AltBotExitTarget PickAltBotStartExit(AltBotLocation const& where, bool grouped)
{
    if (!where.inWorld || grouped)
        return AltBotExitTarget::None;
    if (!where.inBattleGround && !where.onTransport && !where.instanceableMap)
        return AltBotExitTarget::None;
    if (where.inBattleGround || !where.dungeonMap || !where.haveExit)
        return where.haveGrave ? AltBotExitTarget::Graveyard : AltBotExitTarget::None;
    return AltBotExitTarget::InstanceExit;
}

// Throttle guard for the alone-in-instance sweep (AltBotService::Update runs
// every world tick; scanning the alt sessions is cheap but pointless per
// tick). Elapsed `nowMs - lastMs` ms with 32-bit wraparound.
constexpr uint32_t kAltBotSweepIntervalMs = 5000;
inline bool AltBotSweepDue(uint32_t nowMs, uint32_t lastMs)
{
    return nowMs - lastMs >= kAltBotSweepIntervalMs;
}

// An autonomous alt in a dungeon/raid map with no other real players in its
// group on that map leaves for the entrance (Issue #872). Cheap inputs only:
// the caller walks its own session list, never scans the world.
inline bool ShouldAltBotLeaveInstance(bool autonomous, bool inWorld, bool alive, bool teleporting,
    bool dungeonMap, bool grouped, bool groupHasOthersOnMap)
{
    if (!autonomous || !inWorld || !alive || teleporting || !dungeonMap)
        return false;
    return !grouped || !groupHasOthersOnMap;
}

// ---- Protection rules (Issue #872, B9-B16 + footprint) ----
//
// An autonomous alt keeps the pool's leisure strategy set (AiFactory gates on
// IsFreeAltBot, which is true for it), so every protection below is an extra
// gate at the action/value site. The rules stay core-free: callers feed plain
// values, and each rule documents the gate it pairs with.
//
// Record random=false on purpose (pool identity paths - pool reset, gear
// randomization, AH, hire deletion - must never see an alt), so gates keyed
// on sRandomBotFacade.IsRandomBot(bot) never fire for alts. Gates below key
// on IsAutonomous instead: it is the only identity an alt-bot has.

// B9: no AH, no mail, no trade, no guild bank. The command choke point
// (AuthTierForCommand OwnerOrGm + IsBotOwnerOrGm) already forces every one of
// these through the owner's account: a stranger's `.bot` order is denied.
// Autonomous economy trips additionally refuse an alt at the value/trigger
// site (RpgAHSell/RpgAHBuy/RpgGetMail/RpgTradeUseful refuse when autonomous;
// GuildBankAction::MoveFromCharToBank is a no-op for everyone).
inline bool AltBotMayUseEconomy(bool isAutonomous)
{
    return !isAutonomous;
}

// B10: vendoring sells only greys (poor) and unneeded whites (common), NEVER
// soulbound or equipped items. Gate in SellAction: the parse resolves to a
// quality-filtered list and Sell() vetoes soulbound/equipped pieces.
// Qualities: 0 poor, 1 common, 2 uncommon, 3 rare, 4 epic, 5 legendary.
inline bool AltBotMayVendorItem(uint32_t quality, bool isSoulbound, bool isEquipped)
{
    if (isSoulbound || isEquipped)
        return false;
    return quality <= 1;
}

// B10 second half: repairs from its own gold. The repair path is
// RepairAllAction with no cheat: alts carry only Bank cheats repair/breath
// (BotCheats), so HasCheat(gold) is false and the gold is really spent.
inline bool AltBotRepairIsPaid(bool hasGoldCheat)
{
    return !hasGoldCheat;
}

// B11: gold goes on trainer spells, repair and flight paths only. Never
// mount, bags, respec or other vendor buys. The vendor buy loop's per-usage
// budgets (BuyAction) already key off these flags: mount/bag/respec usages
// refuse for autonomous alts while spells/repair/travel pass.
enum class AltBotBuyNeed : uint8_t
{
    TrainerSpell,
    Repair,
    FlightPath,
    Mount,
    Bag,
    Respec,
    OtherVendorBuy,
};
inline bool AltBotMaySpendGoldOn(AltBotBuyNeed need)
{
    switch (need)
    {
        case AltBotBuyNeed::TrainerSpell:
        case AltBotBuyNeed::Repair:
        case AltBotBuyNeed::FlightPath:
            return true;
        default:
            return false;
    }
}

// B12: no auto talent spending. The level-up packet path (SMSG_LEVELUP_INFO ->
// AutoSetTalentsAction) and the AI-build hook (PlayerbotAI init) both refuse
// when IsAutonomous: points stay unspent for the player. Explicit `.bot
// talents` orders from the owner account still work (they are commands, not
// automation).
inline bool AltBotMayAutoSpendTalents(bool isAutonomous)
{
    return !isAutonomous;
}

// B13: quest reward choice is the best item for the class (item score). The
// auto path (TalkToQuestGiverAction::RewardMultipleItem) already scores usage
// EQUIP-first with a live-stat-weight tiebreak; this documents the pick rule
// for the standalone test: the highest score wins, earliest choice on ties.
// `scores` holds (choice index, score) pairs.
inline uint32_t AltBotQuestRewardChoice(std::vector<std::pair<uint32_t, uint32_t>> const& scores)
{
    uint32_t best = 0;
    bool have = false;
    uint32_t bestScore = 0;
    for (auto const& scored : scores)
    {
        if (!have || scored.second > bestScore)
        {
            have = true;
            best = scored.first;
            bestScore = scored.second;
        }
    }
    return best;
}

// B14: auto-equip upgrades only if the old piece fits in the bags. The
// EquipItem path moves the displaced piece to the bags via core SwapItem;
// this gate runs first: with no free slot the equip is skipped and the bot
// keeps playing. Companion `.bot add` with zero free slots warns the owner
// instead (TryStart FreeBagSlots check).
inline bool AltBotMayAutoEquip(bool oldPieceGoesSomewhere)
{
    return oldPieceGoesSomewhere;
}

// B16: no drop boost. ApplyBotLootBonus already refuses non-random bots and
// alts carry record.random=false, so no gate change is needed; this pins it.
inline bool AltBotMayGetDropBoost(bool isRandomBot)
{
    return isRandomBot;
}

// C (group loot): an alt in any group rolls Greed or Pass, never Need.
// Gate in RollAction::CalculateRollVote: NEED demotes to GREED for alts.
// 0 = Pass, 1 = Greed, 2 = Need (matches ROLL_PASS/GREED/NEED order).
inline uint32_t AltBotGroupLootVote(uint32_t vote, bool isAutonomous)
{
    if (isAutonomous && vote == 2)
        return 1;
    return vote;
}

// B15: the item cheat (ammo/reagents) applies as for the pool. The cheat
// mask is never set per-bot, so an alt reads HasCheat(item) from the Bank
// list (BotCheats) while the pool reads the random list (RndBotCheats).
// Enabled at the HasCheat choke point, not by editing the lists: the Bank
// list serves owned alts too, which must never get the item cheat.
inline bool AltBotItemCheatCovered(bool altHasItemCheat)
{
    return altHasItemCheat;
}

// Footprint: never rebinds its hearthstone. RpgHomeBindTrigger refuses when
// autonomous; owned non-autonomous alts keep today's behaviour.
inline bool AltBotMayRebindHearth(bool isAutonomous)
{
    return !isAutonomous;
}

// Footprint: quests. An autonomous alt only abandons quests it took itself
// (AltBotService::IsTakenQuest, recorded at accept, cleared on session end)
// and keeps free quest-log slots for the player. MAX_QUEST_LOG_SIZE is 25 on
// this core. Accepts stay allowed until only the reserve is left.
constexpr uint32_t kAltBotFreeQuestSlots = 10;
constexpr uint32_t kMaxQuestLogSize = 25;
inline bool AltBotMayAcceptQuest(uint32_t questsHeld, uint32_t freeSlotsReserved = kAltBotFreeQuestSlots)
{
    uint32_t capacity = kMaxQuestLogSize > freeSlotsReserved ? kMaxQuestLogSize - freeSlotsReserved : 0;
    return questsHeld < capacity;
}
inline bool AltBotMayAbandonQuest(bool isAutonomous, bool takenByAlt)
{
    if (!isAutonomous)
        return true;
    return takenByAlt;
}

// Footprint: never PICKS targets that lower reputation on its own. Killing
// `entry` lowers some standing when the core holds a ReputationOnKill row
// for it with a negative value. Rep lost inside a group fight is accepted
// (minor, documented).
inline bool AltBotMayPickTarget(bool killLowersRep)
{
    return !killLowersRep;
}

// B8/F41/G43: towards other players an autonomous alt behaves like a pool
// bot: strangers (anyone not on the owner's account and not a GM) may give
// non-destructive, combat-related orders. Only the owner has full control.
// The gate below keys on the chat command's first word, exactly like
// PlayerbotAI::AuthTierForCommand; HandleCommand and the `.bot` commander
// paths feed it plain values so the rule stays unit-testable (Issue #872).
enum class AltBotStrangerVerdict : uint8_t
{
    Allowed,    // combat movement or a read-only/info command
    OwnerOnly,  // economy, inventory, guild, AI configuration, removal
};

// First word of a chat command, lowercased, leading blanks skipped.
inline std::string AltBotFirstCommandWord(std::string const& command)
{
    size_t begin = command.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    size_t end = command.find_first_of(" \t", begin);
    std::string word = command.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return word;
}

// May a stranger (not the owner's account, not a GM) give this chat command
// to an autonomous alt-bot? Allowed: follow/stay/guard/free/wander, combat
// orders (attack/pull/pullback/tank/rti/formation/stance), movement queries
// and read-only info (who/where/wts/stats/c/items/spells), emotes and the
// ready check. Everything else is owner-only: trade, selling, destroying,
// equipping, talents, quest abandon, logout, guild commands, hearth bind
// (home), hire, strategy/AI edits (co/nc/de/all/react/save/max/possible/
// attackers/position/queue/los/cast/pet/buff), and any manipulation outside
// combat. `grind` stays owner-only even though a pool bot takes it from
// anyone: on an alt it rewrites the whole leisure mixer (B7), not one
// fight. Unknown commands fail closed to OwnerOnly.
inline AltBotStrangerVerdict CheckAltBotStrangerCommand(std::string const& command)
{
    std::string const word = AltBotFirstCommandWord(command);
    // Follow/stay/guard/free/wander/flee/runaway double as the first words
    // of the CombatStrategy-side two-word movement shortcuts
    // (`stay ...`, `flee ...`), so the first-word key covers both.
    if (word == "follow" || word == "stay" || word == "guard" || word == "free" ||
        word == "wander" || word == "flee" || word == "runaway" ||
        word == "attack" || word == "pull" || word == "pullback" ||
        word == "tank" || word == "rti" ||
        word == "formation" || word == "stance" ||
        word == "ready" || word == "rebuff" ||
        word == "who" || word == "where" || word == "wts" || word == "stats" ||
        word == "c" || word == "items" || word == "inv" || word == "inventory" ||
        word == "spells" || word == "emote" || word == "help" ||
        word == "wait" || word == "jump")
        return AltBotStrangerVerdict::Allowed;
    return AltBotStrangerVerdict::OwnerOnly;
}

// F40: the alt accepts a guild invite only from the owner's account (or a
// GM, who may do anything). A bot already in a guild never joins another.
// `sameAccount`: inviter's account == the alt's owner account. The guild-id
// and hired checks stay at the call site; this is only the account gate.
inline bool AltBotMayAcceptGuildInvite(bool sameAccount, bool isGm)
{
    return sameAccount || isGm;
}
// Group invites need no alt gate: a stranger invites the autonomous alt like
// a pool bot (AcceptInvitationAction). An ungrouped bot with no master
// passes the INVITE security level for anyone, and joining the stranger's
// group does not end the session: AltBotService::OnMasterBound ends it only
// for the owner's account. Verified, not wired (Issue #872).
inline bool AltBotMayAcceptGroupInvite() { return true; }

// B8: strangers invite the alt like a pool bot and give it the combat orders
// above. `.bot remove`/logout-style removal stays owner-or-GM only
// (CanControlBot already enforces that); this mirrors the chat gate for the
// `.bot` commander paths, whose tactical verbs match the chat list.
inline AltBotStrangerVerdict CheckAltBotStrangerOrder(std::string const& verb)
{
    return CheckAltBotStrangerCommand(verb);
}

// G49: the pool reset can never touch an owned alt. Targets come only from
// the managed-account registry (SnapshotTargets selects characters whose
// account is registered in tortoise_bots_pool_account; a player account is
// never registered there), and deletion revalidates the snapshotted account
// (DeleteNextTarget refuses a character that moved accounts). An alt is a
// character on a player account (TryStart requires a non-registered
// account), so it can never be a target: the account sets are disjoint by
// construction. Policy-level form of that disjointness (Issue #872).
inline bool AltBotExcludedFromPoolReset(bool accountIsRegisteredPool) { return !accountIsRegisteredPool; }

// G50: "party leave deletes hired" can never touch an owned alt. Deletion
// needs a tortoise_bots_hire ledger row written once by Hire() for a
// module-created character (DecideHireDeletion refuses without one), and the
// group hooks only dismiss entries already in the hire registry
// (OnGroupMemberRemoved/OnGroupDisband iterate m_hired). An alt never has a
// row and never enters the registry, so it never reaches deletion.
inline bool AltBotExcludedFromHireDeletion(bool hasHireLedgerRow) { return !hasHireLedgerRow; }

} // namespace TortoiseBots
