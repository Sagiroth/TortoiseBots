#pragma once

#include "PlayerbotAI.h"

class Player;

namespace ai
{
    class AiObjectContext;
    class ReactionEngine;
    class Engine;
}

using namespace ai;

class AiFactory
{
public:
    static AiObjectContext* createAiObjectContext(Player* player, PlayerbotAI* ai);
	static Engine* createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
	static Engine* createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static Engine* createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static ReactionEngine* createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static void AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine);
    static void AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine);
    static void AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* engine);
    static void AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while the player is in a dungeon or raid.
    // When withheld is given, the bits of the entries it dropped are OR-ed in.
    static std::string InstanceSafeNonCombatStrategies(Player* player, std::string const& strategies, uint8* withheld = nullptr);
    // Map-change half of the same guard, run once per map change: a grouped
    // pool bot that enters a dungeon or raid drops that open-world kit, and
    // gets back only what the guard held when it leaves for the open world.
    static void ApplyInstanceGuardOnMapChange(Player* player, PlayerbotAI* ai);

public:
    static int GetPlayerSpecTab(const Player* player);
    // The tab the default strategy kits are built from: the talent tab, except
    // that a warrior's or paladin's protection tab gives way to a damage (or,
    // for a paladin, healing) tab when the bot's forced role leaves out tanking.
    static int GetKitSpecTab(const Player* player, PlayerbotAI* const facade);
    static std::map<uint32, int32> GetPlayerSpecTabs(const Player* player);
    static BotRoles GetPlayerRoles(uint8 cls, uint8 specTab);
    static BotRoles GetPlayerRoles(const Player* player);

    // Party back-off rule: a non-tank bot grouped with a real-player master,
    // or in a party a remote commander leads, outside battlegrounds, keeps the
    // "threat" combat strategy. Every path
    // that re-applies the configured combat strategy string checks this after
    // it, since the shipped random-bot string carries -threat.
    static bool KeepsPartyThreatBackoff(Player* player, PlayerbotAI* const facade);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while the bot is a hired companion.
    // Ledger-based, so it holds before the group forms and while the master
    // is offline; owned alts are untouched.
    static std::string HireSafeNonCombatStrategies(PlayerbotAI* ai, std::string const& strategies);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while the bot's master is a real player.
    static std::string RealMasterSafeNonCombatStrategies(PlayerbotAI* ai, std::string const& strategies);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while a remote commander has bound the
    // bot: it moves only on the commander's orders.
    static std::string CommandedSafeNonCombatStrategies(Player* player, std::string const& strategies, uint32* held = nullptr);
    // The list with every "+grind/+travel/+tfish/+rpg/+rpg craft/+wander" entry removed.
    static std::string StripOpenWorldNonCombatStrategies(std::string const& strategies);
};
