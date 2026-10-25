#include "../../runtime/PlayerbotAIStorage.h"
#include "../../runtime/policy/class/ShamanTankDetectionPolicy.h"
#include "../../runtime/BotManager.h"
#include "../../runtime/HireLifecycle.h"
#include "../../runtime/AltBotService.h"
#include "../../runtime/policy/class/ShamanTankKitPolicy.h"
#include "../../runtime/policy/class/ShamanTankLevelingPolicy.h"
#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "../../runtime/RoleConsumableService.h"
#include "playerbot/AiContextAugment.h"
#include <vector>
#include "strategy/AiObjectContext.h"
#include "strategy/ReactionEngine.h"

#include "strategy/priest/PriestAiObjectContext.h"
#include "strategy/mage/MageAiObjectContext.h"
#include "strategy/warlock/WarlockAiObjectContext.h"
#include "strategy/warrior/WarriorAiObjectContext.h"
#include "strategy/shaman/ShamanAiObjectContext.h"
#include "strategy/paladin/PaladinAiObjectContext.h"
#include "strategy/druid/DruidAiObjectContext.h"
#include "strategy/hunter/HunterAiObjectContext.h"
#include "strategy/rogue/RogueAiObjectContext.h"
#include "Objects/Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCEnums.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Objects/Item.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Objects/ItemPrototype.h"
#include "playerbot/GroupMembers.h"
#include "../../runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/policy/dungeon/InstanceGuardPolicy.h"
#include "playerbot/SurvivePolicy.h"
#include "../../runtime/CommandedGroupQuery.h"
#include "../../runtime/RemoteCommanderPolicy.h"
#include "../../runtime/policy/pvp/WorldPvpSeekPolicy.h"
#include "playerbot/strategy/values/WorldPvpSeekLog.h"
#include "Battlegrounds/BattleGroundMgr.h"

// The class context of a bot; the augmenters of other modules are applied on top in
// createAiObjectContext below (AiContextAugment.h).
static AiObjectContext* CreateClassAiObjectContext(Player* player, PlayerbotAI* ai)
{
    switch (player->GetClass())
    {
        case CLASS_PRIEST:
        {
            return new ai::PriestAiObjectContext(ai);
            break;
        }

        case CLASS_MAGE:
        {
            return new ai::MageAiObjectContext(ai);
            break;
        }

        case CLASS_WARLOCK:
        {
            return new ai::WarlockAiObjectContext(ai);
            break;
        }

        case CLASS_WARRIOR:
        {
            return new ai::WarriorAiObjectContext(ai);
            break;
        }

        case CLASS_SHAMAN:
        {
            return new ai::ShamanAiObjectContext(ai);
            break;
        }

        case CLASS_PALADIN:
        {
            return new ai::PaladinAiObjectContext(ai);
            break;
        }

        case CLASS_DRUID:
        {
            return new ai::DruidAiObjectContext(ai);
            break;
        }

        case CLASS_HUNTER:
        {
            return new ai::HunterAiObjectContext(ai);
            break;
        }

        case CLASS_ROGUE:
        {
            return new ai::RogueAiObjectContext(ai);
            break;
        }

    }
    return new AiObjectContext(ai);
}

namespace
{
    std::vector<AiContextAugmenter>& Augmenters()
    {
        static std::vector<AiContextAugmenter> augmenters;
        return augmenters;
    }
}

void RegisterAiContextAugmenter(AiContextAugmenter augmenter)
{
    if (!augmenter)
        return;
    Augmenters().push_back(augmenter);
    // The bots that are already in the world get it now.
    for (Player* bot : TortoiseBots::BotManager::Instance().GetAllBots())
        if (bot)
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
                if (ai->GetAiObjectContext())
                    augmenter(ai, ai->GetAiObjectContext());
}

void ApplyAiContextAugmenters(PlayerbotAI* ai, ai::AiObjectContext* context)
{
    if (!ai || !context)
        return;
    for (AiContextAugmenter augmenter : Augmenters())
        augmenter(ai, context);
}

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    AiObjectContext* context = CreateClassAiObjectContext(player, ai);
    ApplyAiContextAugmenters(ai, context);
    return context;
}

int AiFactory::GetPlayerSpecTab(const Player* bot)
{
    std::map<uint32, int32> tabs = GetPlayerSpecTabs(bot);

    if (bot->GetLevel() >= 10 && ((tabs[0] + tabs[1] + tabs[2]) > 0))
    {
        int tab = -1, max = 0;
        for (uint32 i = 0; i < uint32(3); i++)
        {
            if (tab == -1 || max < tabs[i])
            {
                tab = i;
                max = tabs[i];
            }
        }
        return tab;
    }
    else
    {
        int tab = 0;

        switch (bot->GetClass())
        {
        case CLASS_MAGE:
            tab = 1;
            break;
        case CLASS_PALADIN:
            tab = 2;
            break;
        case CLASS_PRIEST:
            tab = 1;
            break;
        case CLASS_WARRIOR:
            tab = 2;
            break;
        case CLASS_ROGUE:
            // Combat has the baseline Sinister Strike rotation. Assassination
            // needs the Backstab-only path and is not useful before talents.
            tab = 1;
            break;
        case CLASS_SHAMAN:
            tab = 1;
            break;
        }
        return tab;
    }
}

int AiFactory::GetKitSpecTab(const Player* player, PlayerbotAI* const facade)
{
    int const tab = GetPlayerSpecTab(player);
    uint8 const role = facade ? facade->GetForcedRole() : static_cast<uint8>(BOT_ROLE_NONE);
    if (!role || (role & BOT_ROLE_TANK))
        return tab;

    // `.bot role <bot> dps` on a protection warrior kept "protection",
    // "tank assist" and "pull" from his talent tab, and he taunted the group
    // tank's target off him. Only the Feral druid read the forced role.
    if (player->GetClass() == CLASS_WARRIOR && tab == 2)
        return 0; // arms
    if (player->GetClass() == CLASS_PALADIN && tab == 1)
        return (role & BOT_ROLE_HEALER) ? 0 : 2; // holy or retribution
    return tab;
}

std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(const Player* bot)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;

    uint32 classMask = bot->GetClassMask();
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const *talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
            continue;

        TalentTabEntry const *talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        if ((classMask & talentTabInfo->ClassMask) == 0)
            continue;

        int maxRank = 0;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (!talentInfo->RankID[rank])
                continue;

            uint32 spellid = talentInfo->RankID[rank];
            if (spellid && bot->HasSpell(spellid))
                maxRank = rank + 1;

        }
        tabs[talentTabInfo->tabpage] += maxRank;
    }

    return tabs;
}

BotRoles AiFactory::GetPlayerRoles(uint8 cls, uint8 tab)
{
    BotRoles role = BOT_ROLE_NONE;
    switch (cls)
    {
        case CLASS_PRIEST: {
            if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }
            else
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

        case CLASS_SHAMAN: {
            if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_WARRIOR: {
            if (tab == 2)
            {
                role = BOT_ROLE_TANK;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_PALADIN: {
            if (tab == 1)
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 0)
            {
                role = BOT_ROLE_HEALER;
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_DRUID: {
            if (tab == 0)
            {
                role = BOT_ROLE_DPS;
            }
            else if (tab == 1)
            {
                // Feral covers both Bear (tank) and Cat (melee DPS) on the
                // single shared 11.1 talent path. Report both roles so
                // role-filtered premade lookups can match it either way;
                // the forced role (or Cat default) decides Bear vs Cat.
                role = BotRoles(BOT_ROLE_TANK | BOT_ROLE_DPS);
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

        default: {
            role = BOT_ROLE_DPS;
            break;
        }
    }

    return role;
}

static BotRoles GetTalentRoles(const Player* player)
{
    return AiFactory::GetPlayerRoles(player->GetClass(),
        static_cast<uint8>(AiFactory::GetPlayerSpecTab(player)));
}

static bool HasShieldEquipped(const Player* player)
{
    Item const* offhand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    ItemPrototype const* prototype = offhand ? offhand->GetProto() : nullptr;
    return prototype && prototype->Class == ITEM_CLASS_ARMOR &&
        prototype->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
}

static bool HasRockbiterWeapon(const Player* player)
{
    Item const* mainHand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    uint32 enchantId = mainHand ? mainHand->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) : 0;
    SpellItemEnchantmentEntry const* enchant = enchantId
        ? sSpellItemEnchantmentStore.LookupEntry(enchantId) : nullptr;
    if (!enchant)
        return false;

    for (uint8 effect = 0; effect < 3; ++effect)
        if (enchant->type[effect] == ITEM_ENCHANTMENT_TYPE_TOTEM)
            return true;

    return false;
}

// Key tank talents as HasSpell flags (top rank first; rank 2 in the spell
// map does not imply rank 1, so both ranks are checked).
static bool HasTotemicAlignmentTalent(const Player* player)
{
    return player->HasSpell(TortoiseBots::kShamanTankTalentTotemicAlignmentR2) ||
        player->HasSpell(TortoiseBots::kShamanTankTalentTotemicAlignmentR1);
}

static bool HasSpiritArmorTalent(const Player* player)
{
    return player->HasSpell(TortoiseBots::kShamanTankTalentSpiritArmorR2) ||
        player->HasSpell(TortoiseBots::kShamanTankTalentSpiritArmorR1);
}

// Spent ranks of a talent spec, by talent id.
static TortoiseBots::ShamanTalentRanks TalentRanks(TalentSpec const& spec)
{
    TortoiseBots::ShamanTalentRanks ranks;
    for (TalentSpec::TalentListEntry const& entry : spec.talents)
        ranks[entry.entry] = entry.rank;
    return ranks;
}

// The premade entry the appliers would use for this bot's level: the first
// entry it can afford (ChangeTalentsAction::GetBestPremadeSpec) cropped to its
// level (CropTalents), so the ranks are exactly what a bot following that path
// carries.
static TalentSpec BestPremadeEntryForLevel(TalentPath const& path, Player* bot)
{
    TalentSpec best = path.talentSpec.back();
    uint32 const available = GetTotalTalentPoints_TB(bot);
    for (TalentSpec const& entry : path.talentSpec)
        if (entry.points >= available)
        {
            best = entry;
            break;
        }
    best.CropTalents(bot);
    return best;
}

// Issue #1125: below the first key tank talent the 7.3 levelling layout is the
// durable half of the identity - talents live in the character, the forced
// role does not. The tank entry for the bot's level has to match the spent
// ranks on its own: at 10-14 the 7.3 and 7.1 links are identical, so a match
// shared with another premade proves nothing and the read stays on the DPS
// spec (runtime/ShamanTankLevelingPolicy.h).
static bool HasShamanTankLevelingBuild(const Player* player)
{
    if (!player || player->GetClass() != CLASS_SHAMAN)
        return false;
    // At or above the first key talent the key-talent read is authoritative
    // and free; the talent walk stays off that path.
    if (player->GetLevel() >= TortoiseBots::kShamanTankFirstTalentLevel)
        return false;

    std::vector<TalentPath>& paths = sPlayerbotAIConfig.classSpecs[CLASS_SHAMAN].talentPath;
    if (paths.empty())
        return false;

    Player* bot = const_cast<Player*>(player);
    TortoiseBots::ShamanTalentRanks const current = TalentRanks(TalentSpec(bot));
    bool tankEntryMatches = false;
    int otherPremadeMatches = 0;
    for (TalentPath& path : paths)
    {
        if (path.talentSpec.empty())
            continue;
        if (!TortoiseBots::ShamanTalentRanksEqual(current, TalentRanks(BestPremadeEntryForLevel(path, bot))))
            continue;
        if (path.id == TortoiseBots::kShamanTankPremadeSpec)
            tankEntryMatches = true;
        else
            ++otherPremadeMatches;
    }

    return TortoiseBots::ShamanOnTankLevelingPath(tankEntryMatches, otherPremadeMatches);
}

// The gear-free build read behind the shaman kit decision. Only the 7.3 tank
// premade takes Totemic Alignment or Spirit Armor, so no enhancement,
// elemental or restoration shaman ever matches (issue #1125). Talents are
// persisted, unlike the forced role - and below the first key talent the
// levelling layout is read instead, so the identity survives a restart there
// too.
static bool HasShamanTankBuild(const Player* player)
{
    if (TortoiseBots::IsShamanTankBuild(HasTotemicAlignmentTalent(player), HasSpiritArmorTalent(player)))
        return true;
    return HasShamanTankLevelingBuild(player);
}

// Which shaman kit this bot plays (issue #1125): the explicit forced role
// (`.bot role`, hire, dungeon finder) decides outright, else the bot's own
// build does — a pool shaman that rolled the tank spec kept its tank talents
// but came back from every restart and group leave with the DPS kit, because
// the forced role that made it a tank lived in memory only.
static bool PlaysShamanTankKit(const Player* player, PlayerbotAI* const facade)
{
    if (!player || player->GetClass() != CLASS_SHAMAN)
        return false;
    uint8 const role = facade ? facade->GetForcedRole() : static_cast<uint8>(BOT_ROLE_NONE);
    return TortoiseBots::ShamanPlaysTankKit(role, HasShamanTankBuild(player));
}

// Shared gear+talent read behind GetRoleFromStanceOrGear and HasOtherTank:
// shield equipped, Rockbiter on the main hand, and one key tank talent
// (Totemic Alignment or Spirit Armor, either rank). Cheap Player::HasSpell
// map lookups (top rank first); never GetPlayerSpecTabs here — it rewalks
// the whole talent store per call and this runs per group member.
// Forced roles are handled by the callers (player storage / bot AI) before
// this is reached, so this is the pure detection signal only.
static bool IsDetectedShamanTank(const Player* player)
{
    TortoiseBots::ShamanTankDetectionInputs inputs;
    inputs.shieldEquipped = HasShieldEquipped(player);
    inputs.rockbiterOnMainHand = HasRockbiterWeapon(player);
    inputs.totemicAlignment = HasTotemicAlignmentTalent(player);
    inputs.spiritArmor = HasSpiritArmorTalent(player);
    return TortoiseBots::IsShamanTank(inputs);
}

static BotRoles GetRoleFromStanceOrGear(const Player* player)
{
    switch (player->GetClass())
    {
        case CLASS_DRUID:
            if (player->GetShapeshiftForm() == FORM_BEAR ||
                player->GetShapeshiftForm() == FORM_DIREBEAR)
                return BOT_ROLE_TANK;
            if (player->GetShapeshiftForm() == FORM_CAT)
                return BOT_ROLE_DPS;
            break;
        case CLASS_WARRIOR:
            if (player->GetShapeshiftForm() == FORM_DEFENSIVESTANCE || HasShieldEquipped(player))
                return BOT_ROLE_TANK;
            break;
        case CLASS_PALADIN:
            if (player->HasAura(25780) && HasShieldEquipped(player))
                return BOT_ROLE_TANK;
            break;
        case CLASS_SHAMAN:
            if (IsDetectedShamanTank(player))
                return BOT_ROLE_TANK;
            break;
    }

    return BOT_ROLE_NONE;
}


static bool HasOtherTank(const Player* player)
{
    Group* group = const_cast<Player*>(player)->GetGroup();
    if (!group)
        return false;

    ObjectGuid playerGuid = player->GetObjectGuid();
    bool playerSeen = false;
    bool earlierHeuristicTank = false;
    PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
    for (Player* member : LiveGroupMembers(group))
    {
        ObjectGuid memberGuid = member->GetObjectGuid();
        if (memberGuid == playerGuid)
        {
            playerSeen = true;
            continue;
        }

        uint8 forcedRole = storage.GetPlayerForcedRole(memberGuid);
        if (forcedRole != static_cast<uint8>(BOT_ROLE_NONE))
        {
            if (forcedRole == static_cast<uint8>(BOT_ROLE_TANK))
                return true;
            continue;
        }

        PlayerbotAI* memberAI = storage.GetAI(member);
        if (memberAI)
        {
            uint8 botForcedRole = memberAI->GetForcedRole();
            if (botForcedRole == static_cast<uint8>(BOT_ROLE_TANK) ||
                memberAI->ContainsStrategy(STRATEGY_TYPE_TANK))
                return true;
            if (botForcedRole != static_cast<uint8>(BOT_ROLE_NONE) && memberAI->HasActivePlayerMaster())
                continue;
        }

        uint8 memberClass = member->GetClass();
        if ((memberClass == CLASS_WARRIOR || memberClass == CLASS_PALADIN || memberClass == CLASS_DRUID) &&
            (GetTalentRoles(member) & BOT_ROLE_TANK) != 0)
            return true;

        // A detected shaman tank is a real tank for the members scanned
        // after it: gear+talent is as explicit as a talent tab here, and
        // the shaman co-tanks instead of being suppressed.
        if (memberClass == CLASS_SHAMAN && IsDetectedShamanTank(member))
            return true;

        // If no explicit or talent tank exists, the first group-slot member
        // with a tank stance/form/gear signal owns the automatic tank role.
        // Counting later candidates would make two stance-shifters suppress
        // each other instead of selecting one tank.
        if (!playerSeen && GetRoleFromStanceOrGear(member) == BOT_ROLE_TANK)
            earlierHeuristicTank = true;
    }

    return earlierHeuristicTank;
}

BotRoles AiFactory::GetPlayerRoles(const Player* player)
{
    if (!player)
        return BOT_ROLE_NONE;

    PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
    uint8 playerForcedRole = storage.GetPlayerForcedRole(player->GetObjectGuid());
    if (playerForcedRole != static_cast<uint8>(BOT_ROLE_NONE))
        return static_cast<BotRoles>(playerForcedRole);

    PlayerbotAI* botAi = storage.GetAI(const_cast<Player*>(player));
    if (botAi && botAi->GetForcedRole() != BOT_ROLE_NONE && botAi->HasActivePlayerMaster())
        return static_cast<BotRoles>(botAi->GetForcedRole());

    // A bot that has the tank build counts as a tank on its own (issue
    // #1125): the build lives in its talents, so it still reads tank after
    // the restart or group leave that dropped the in-memory forced role.
    // The forced-role returns above keep an explicit order in charge; real
    // players keep the gear-gated read below (docs/classes/shaman.md).
    if (botAi && botAi->GetForcedRole() == BOT_ROLE_NONE && PlaysShamanTankKit(player, botAi))
        return BOT_ROLE_TANK;

    // Only scan the group when a current stance/form/gear signal could change
    // the talent role; without one, both paths return the talent role.
    BotRoles stateRole = GetRoleFromStanceOrGear(player);
    if (stateRole != BOT_ROLE_NONE)
    {
        // A detected shaman tank co-tanks: another tank in the group never
        // demotes it to its talent role (Enhancement DPS).
        if (player->GetClass() == CLASS_SHAMAN && stateRole == BOT_ROLE_TANK)
            return stateRole;
        if (HasOtherTank(player))
            return GetTalentRoles(player);
        return stateRole;
    }

    return GetTalentRoles(player);
}

bool AiFactory::HasShamanTankTalents(const Player* player)
{
    if (!player || player->GetClass() != CLASS_SHAMAN)
        return false;
    return HasTotemicAlignmentTalent(player) || HasSpiritArmorTalent(player);
}

// Party back-off (threat): non-tank DPS/healer bots grouped with a real player
// ride the shared tank's threat, so a full burn pulls the mob off the tank.
// Masterless pool bots keep the old behavior: the multiplier is group-gated
// anyway, and threat vetoes would stall their solo leveling burn. ThreatValue
// returns 0 with no tank, so solo/no-tank parties are unaffected even when the
// veto is present. A party a remote commander leads has no real-player master
// (the commander stands outside it) and backs off the same way.
bool AiFactory::KeepsPartyThreatBackoff(Player* player, PlayerbotAI* const facade)
{
    if (!player || !facade || player->InBattleGround())
        return false;
    bool const isTank = PlayerbotAI::IsTank(player);
    if (facade->HasRealPlayerMaster())
        return !isTank;
    return TortoiseBots::RemoteCommander::KeepsCommandedThreatBackoff(
        TortoiseBots::BotCommands::IsInCommandedGroup(player), isTank);
}

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    int tab = GetKitSpecTab(player, facade);

    // Raid/dungeon transition engine: the "dungeon" strategy owns the
    // enter/leave <raid> triggers that swap in MC/Ony/BWL/Naxx tactics.
    // Without it bots zone into raids and keep fighting with outdoor AI.
    combatEngine->addStrategies("dungeon", "mount", NULL);
    // The pinned Tortoise PathInfo exposes no area-cost/avoidance filter. Do
    // not install the mature AvoidMobs strategy whose setArea() calls would
    // otherwise report success while changing no path state.

    // A battleground is precisely where "pvp" is needed: it is the strategy
    // whose "enemy player near" trigger lets the bot take an enemy player as a
    // target at all, and ChooseTargetAction asks for it by name. Excluding it
    // here left bots standing on their spawn with no way to acquire anyone, and
    // took "default" - the baseline combat behaviour - with it. Measured before
    // this change: ten bots held identical coordinates for six minutes inside a
    // running match, and no [BATTLEGROUND] honor had been recorded since 08-10.
    // "duel" is the one that genuinely has no business in a battleground.
    if (player->InBattleGround())
    {
        combatEngine->addStrategies("racials", "default", "pvp", NULL);
    }
    else
    {
        combatEngine->addStrategies("racials", "default", "duel", "pvp", "cast time", NULL);
    }

    switch (player->GetClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("discipline");
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    // mod-playerbots parity (PRI-2): disc healer bots deal
                    // off-spec damage by default; the player can `-offdps`.
                    combatEngine->addStrategy("offdps");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("holy");
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    // mod-playerbots parity (PRI-2): holy healer bots deal
                    // off-spec damage by default; the player can `-offdps`.
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategy("shadow");
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("dps assist", "flee", "cure", "ranged", "cc", "buff", "aoe", "boost", NULL);
            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("fire");
            }
            else
            {
                combatEngine->addStrategy("frost");
            }

            combatEngine->addStrategies("dps assist", "flee", "cure", "ranged", "cc", "buff", "aoe", "boost", NULL);
            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "pull back", "close", "tank face", NULL);
            }
            else if (player->GetLevel() < 30 || tab == 0)
            {
                combatEngine->addStrategies("arms", "dps assist", "close", "behind", NULL);
            }
            else
            {
                combatEngine->addStrategies("fury", "dps assist", "close", "behind", NULL);
            }

            combatEngine->addStrategies("aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_SHAMAN:
        {
            // A shaman with a tank role plays the tank kit (issue #1125):
            // the "tank shaman" spec strategy plus the shared tank-framework
            // pieces (warrior `:575` / bear `:667` pattern), then the shared
            // shaman pieces that still apply. Deliberately no "dps assist"
            // (threat race), no "behind" (tanks face the mob).
            if (tab == 1 && PlaysShamanTankKit(player, facade))
            {
                for (std::string const& name : TortoiseBots::ShamanTankCombatKit())
                    combatEngine->addStrategy(name);
                break;
            }

            if (tab == 0)
            {
                combatEngine->addStrategies("elemental", "aoe", "cc", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategies("restoration", "aoe", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                // Melee DPS works the mob's rear via "behind" (set behind):
                // without it enhancement never sidesteps off the mob's front.
                combatEngine->addStrategies("enhancement", "aoe", "cc", "close", "behind", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("dps assist", "cure", "totems", "buff", "boost", NULL);
            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "pull back", "close", "tank face", NULL);
			}
            else if(tab == 0)
            {
                combatEngine->addStrategies("holy", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                // Melee DPS works the mob's rear via "behind" (set behind):
                // without it retribution never sidesteps off the mob's front.
                combatEngine->addStrategies("retribution", "dps assist", "close", "behind", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", "aura", "blessing", NULL);
            break;
        }

        case CLASS_DRUID:
        {
            // Below level 10, no shapeshift forms exist yet (Bear at 10, Cat at 20), so
            // neither Feral spec is usable. Dedicated leveling kit instead: burst Wrath/
            // Moonfire at range, melee once the mob closes, self-heal as needed. Deliberately
            // no "flee"/"ranged" here - see the CLASS_DRUID guard in OutNumberedTrigger.
            if (player->GetLevel() < 10)
            {
                combatEngine->addStrategies("leveling", "dps assist", "cure", NULL);
                break;
            }

            if (tab == 1)
            {
                // Bear or cat. An assigned role decides it outright: a druid the
                // dungeon finder put on the tank slot should be a bear whether or
                // not it has reached Primal Fury, and one sent as dps should stay
                // a cat even if it has. Unassigned wandering Feral defaults to
                // Cat DPS; Bear is opt-in via role, never a talent guess.
                uint8 const role = facade->GetForcedRole();

                bool const tanking = role ? (role & BOT_ROLE_TANK) != 0 : false;

                if (tanking)
                {
                    combatEngine->addStrategies("tank feral", "tank assist", "pull", "pull back", "close", "tank face", NULL);
                }
                else
                {
                    combatEngine->addStrategies("dps feral", "dps assist", "close", "behind", "stealth", NULL);
                    if (sPlayerbotAIConfig.enableOffSpecStrategies)
                        combatEngine->addStrategy("offheal");
                }
            }
            else if (tab == 2)
            {
                combatEngine->addStrategies("restoration", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategies("balance", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategy("survival");
            }
            else
            {
                combatEngine->addStrategy("marksmanship");
            }

            combatEngine->addStrategies("dps assist", "ranged", "cc", "aoe", "buff", "boost", "aspect", "sting", "pet", NULL);
            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategy("subtlety");
            }
            else
            {
                combatEngine->addStrategy("combat");
            }

            combatEngine->addStrategies("dps assist", "aoe", "close", "cc", "behind", "stealth", "poisons", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("demonology");
            }
            else
            {
                combatEngine->addStrategy("destruction");
            }

            combatEngine->addStrategies("dps assist", "flee", "ranged", "cc", "pet", "aoe", "buff", "boost", "curse", NULL);
            break;
        }

    }

    if (facade->IsRealPlayer() || sRandomBotFacade.IsFreeBot(player))
	{
        combatEngine->addStrategy("roll");

        // The pool-only "critical health -> flee" node lives in the flee
        // strategy, so grouped pool bots need it too: free bots group with
        // each other while levelling, and those groups wipe the same way a
        // solo bot does. Ungrouped grant is unchanged (plus boost and the
        // druid/shaman/paladin kit below). Real-player masters are safe: the
        // trigger itself refuses to fire while one answers for the bot, and
        // dungeons/raids with a real master stay flee-free either way.
        // AiPlayerbot.PoolMeleeHoldGround (opt-in) leaves warriors and rogues
        // without it: they fight on instead of breaking off (SurvivePolicy.h).
        bool const rageOrEnergyClass = player->GetClass() == CLASS_WARRIOR ||
            player->GetClass() == CLASS_ROGUE;
        if ((!player->GetGroup() || !facade->HasRealPlayerMaster()) &&
            ai::ShouldGrantPoolFlee(sPlayerbotAIConfig.poolMeleeHoldGround, rageOrEnergyClass))
            combatEngine->addStrategy("flee");

        if (!player->GetGroup())
        {
            combatEngine->addStrategy("boost");

            if (player->GetClass() == CLASS_DRUID && tab == 2)
            {
                combatEngine->addStrategies("balance", "ranged", NULL);
                combatEngine->removeStrategy("close");
            }

            if (player->GetClass() == CLASS_DRUID && tab == 1 && urand(0, 100) > 50 && player->GetLevel() >= 20 && !facade->GetForcedRole())
            {
                if (player->HasSpell(16961) || player->HasSpell(16958))
                {
                    combatEngine->addStrategies("dps feral", "close", "stealth", "behind", NULL);
                    combatEngine->removeStrategy("tank feral");
                    combatEngine->removeStrategy("tank assist");
                }
                else
                {
                    combatEngine->addStrategies("tank feral", "tank assist", "close", "tank face", NULL);
                    combatEngine->removeStrategy("dps feral");
                    combatEngine->removeStrategy("dps assist");
                    combatEngine->removeStrategy("behind");
                }
            }

            if (player->GetClass() == CLASS_PRIEST && tab < 2)
            {
                combatEngine->addStrategy("offdps");
            }

            if (player->GetClass() == CLASS_SHAMAN && tab == 2)
            {
                combatEngine->addStrategies("elemental", "aoe", "cc", NULL);
            }

            if (player->GetClass() == CLASS_PALADIN && tab == 0)
            {
                combatEngine->addStrategies("retribution", "close", NULL);
                combatEngine->removeStrategy("ranged");
            }
        }

        if ((facade->HasRealPlayerMaster() && sPlayerbotAIConfig.jumpWithPlayer) ||
            (player->InBattleGround() && sPlayerbotAIConfig.jumpInBg) ||
            (!player->InBattleGround() && !facade->HasRealPlayerMaster()))
        {
            if (combatEngine->HasStrategy("close") && frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpMeleeInCombatChance)
            {
                combatEngine->addStrategy("rpg jump");
            }
        }

        if (sPlayerbotAIConfig.jumpChase)
            combatEngine->addStrategy("chase jump");

        // "dps assist" and "tank assist" are siblings, so the list's
        // "+dps assist" swapped "tank assist" out on every pool tank
        // (Protection warrior/paladin, bear) and it picked targets like dps
        // instead of collecting loose mobs. The spec/role defaults above
        // decide the assist strategy; the list must not undo it.
        bool const specTankAssist = combatEngine->HasStrategy("tank assist");
        combatEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies);
        if (specTankAssist && !combatEngine->HasStrategy("tank assist") && combatEngine->HasStrategy("dps assist"))
            combatEngine->addStrategy("tank assist");
    }
    else
    {
        combatEngine->ChangeStrategy(sPlayerbotAIConfig.combatStrategies);
    }

    // Applied after the configured strategy strings: the shipped default
    // random-bot string carries -threat and would strip it again.
    if (KeepsPartyThreatBackoff(player, facade) && !combatEngine->HasStrategy("threat"))
    {
        combatEngine->addStrategy("threat");
        // Given for a commander's binding (a bot that joined the bound party
        // after the bind, or a reset while bound): the binding owns it.
        if (!facade->HasRealPlayerMaster())
            TortoiseBots::BotCommands::NoteCommanderThreat(player);
    }

    // Healers cancel a cast-time heal whose target another healer already
    // topped up (HealingCastPolicy). The strategy existed but nothing added
    // it: Patchwerk with 17 healers measured ~63% overheal on the tanks.
    if (facade->IsHeal(player) && !combatEngine->HasStrategy("heal interrupt"))
        combatEngine->addStrategy("heal interrupt");

    if (TortoiseBots::RoleConsumablesEnabled())
        combatEngine->addStrategy("role consumables");

    // Battleground switch
    if (player->InBattleGround())
    {
        BattleGroundTypeId bgType = player->GetBattleGroundTypeId();


        if (bgType == BATTLEGROUND_WS)
        {
            combatEngine->addStrategy("warsong");
        }

        if (bgType == BATTLEGROUND_AB)
        {
            combatEngine->addStrategy("arathi");
        }

        if(bgType == BATTLEGROUND_AV)
        {
            combatEngine->addStrategy("alterac");
        }


        combatEngine->addStrategies("boost", "racials", "default", "aoe", "dps assist", "pvp", NULL);
        combatEngine->removeStrategy("custom::say");
        combatEngine->removeStrategy("flee");
        combatEngine->removeStrategy("threat");
        combatEngine->removeStrategy("follow");
        combatEngine->removeStrategy("wander");
        combatEngine->removeStrategy("conserve mana");
        combatEngine->removeStrategy("cast time");

        // A healing spec stays a healer in a battleground, as in mod-playerbots:
        // its BG switch adds objectives and pvp on top but keeps the talent
        // spec. Re-adding the spec strategy drops its sibling, so a "+dps"
        // (the priest Shadow placeholder) in the combat strategy list or the
        // ungrouped pool kit (elemental/balance/retribution) cannot turn the
        // team's healers into damage dealers. Stored strategies and a forced
        // role are applied after this, so a player's explicit order still wins.
        if (player->GetClass() == CLASS_PRIEST && tab == 0)
        {
            combatEngine->addStrategy("discipline");
        }

        if (player->GetClass() == CLASS_PRIEST && tab == 1)
        {
            combatEngine->addStrategy("holy");
        }

        // Below level 10 a druid runs the "leveling" kit instead of a spec.
        if ((player->GetClass() == CLASS_SHAMAN ||
             (player->GetClass() == CLASS_DRUID && player->GetLevel() >= 10)) && tab == 2)
        {
            combatEngine->addStrategies("restoration", "ranged", NULL);
            combatEngine->removeStrategy("close");
        }

        if (player->GetClass() == CLASS_PALADIN && tab == 0)
        {
            combatEngine->addStrategies("holy", "ranged", NULL);
            combatEngine->removeStrategy("close");
        }

        if (player->GetClass() == CLASS_DRUID && tab == 1)
        {
            uint8 const bgRole = facade->GetForcedRole();
            bool const bgTanking = bgRole ? (bgRole & BOT_ROLE_TANK) != 0
                : (player->HasSpell(16961) || player->HasSpell(16958));
            if (bgTanking)
            {
                combatEngine->addStrategies("tank feral", "close", "tank face", NULL);
            }
            else
            {
                combatEngine->addStrategies("behind", "dps feral", "stealth", NULL);
            }
        }

        if (player->GetClass() == CLASS_ROGUE)
        {
            combatEngine->addStrategies("behind", "stealth", "poisons", "buff", NULL);
        }
    }

}

Engine* AiFactory::createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
	Engine* engine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_COMBAT, engine);
    AddDefaultCombatStrategies(player, facade, engine);
    return engine;
}

namespace
{
    // Issue #218: the open-world non-combat strategies the instance guard holds
    // back in a dungeon or raid. Bit i of PlayerbotAI::instanceGuardWithheld
    // stands for entry i. Main's RealMasterSafeNonCombatStrategies strips the
    // same entry set via StripOpenWorldNonCombatStrategies; keep both lists
    // identical.
    char const* const kInstanceGuardStrategies[] = { "grind", "travel", "tfish", "rpg", "rpg craft", "wander" };
    uint8 const kInstanceGuardStrategyCount = sizeof(kInstanceGuardStrategies) / sizeof(kInstanceGuardStrategies[0]);
    uint8 const kInstanceGuardWander = 5;

    inline uint8 InstanceGuardBit(uint8 index) { return uint8(1u << index); }
}

std::string AiFactory::HireSafeNonCombatStrategies(PlayerbotAI* ai, std::string const& strategies)
{
    // Ledger-based: holds from the first strategy build (before any group
    // forms) and through master-offline grace. Owned alts are not hires.
    if (!ai || !ai->GetBot() || !TortoiseBots::HireLifecycle::Instance().IsHired(ai->GetBot()->GetObjectGuid()))
        return strategies;

    return StripOpenWorldNonCombatStrategies(strategies);
}

std::string AiFactory::RealMasterSafeNonCombatStrategies(PlayerbotAI* ai, std::string const& strategies)
{
    // A pool bot in a real player's party follows that player. "+wander" is a
    // sibling of "follow" and replaces it, and the open-world adds re-arm what
    // the player may have switched off, so the list skips them for such a bot.
    if (!ai || !ai->HasRealPlayerMaster())
        return strategies;

    return StripOpenWorldNonCombatStrategies(strategies);
}

std::string AiFactory::CommandedSafeNonCombatStrategies(Player* player, std::string const& strategies, uint32* held)
{
    // A bound bot holds or follows on its commander's orders: the list's
    // "+wander" would replace the binding's stay/follow, and its open-world
    // adds would start walks of its own. held: the dropped adds the binding
    // gives back on release ("+wander" is not one: a movement mode).
    if (!TortoiseBots::BotCommands::IsInCommandedGroup(player))
        return strategies;

    if (held)
        *held |= TortoiseBots::RemoteCommander::CommanderAutonomyAdds(strategies);

    return StripOpenWorldNonCombatStrategies(strategies);
}

std::string AiFactory::StripOpenWorldNonCombatStrategies(std::string const& strategies)
{
    std::string kept;
    for (std::string const& entry : split(strategies, ','))
    {
        bool drop = false;
        if (!entry.empty() && entry[0] == '+')
        {
            for (uint8 i = 0; i < kInstanceGuardStrategyCount; ++i)
            {
                if (entry.compare(1, std::string::npos, kInstanceGuardStrategies[i]) == 0)
                {
                    drop = true;
                    break;
                }
            }
        }

        if (drop)
            continue;

        if (!kept.empty())
            kept += ",";
        kept += entry;
    }
    return kept;
}

bool AiFactory::IsWorldPvpSeekEligible(Player* player, PlayerbotAI* const facade)
{
    if (!player || !facade)
        return false;

    TortoiseBots::WorldPvpSeekInputs inputs;
    inputs.seekEnabled = sPlayerbotAIConfig.worldPvpSeek != 0;
    inputs.freePoolBot = sRandomBotFacade.IsFreeBot(player);
    inputs.ownedBot = facade->IsOwnedBot();
    inputs.hiredBot = TortoiseBots::HireLifecycle::Instance().IsHired(player->GetObjectGuid());
    inputs.inBattleground = player->InBattleGround();

    if (Group* group = player->GetGroup())
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = sObjectAccessor.FindPlayer(slot.guid);
            if (member && member != player && member->IsInWorld() && facade->IsRealPlayer(member))
            {
                inputs.groupedWithRealPlayer = true;
                break;
            }
        }
    }

    return TortoiseBots::WorldPvpSeekEligible(inputs);
}

void AiFactory::RollWorldPvpSeek(Player* player, PlayerbotAI* const facade)
{
    if (!player || !facade)
        return;

    if (!IsWorldPvpSeekEligible(player, facade))
    {
        facade->ChangeStrategy("-world pvp", BotState::BOT_STATE_NON_COMBAT);
        facade->ChangeStrategy("-world pvp", BotState::BOT_STATE_COMBAT);
        return;
    }

    uint32 percent = sPlayerbotAIConfig.worldPvpSeekBotPercent;
    if (percent > 100)
        percent = 100;
    bool const grant = percent > 0 && urand(0, 99) < percent;
    bool const held = facade->HasStrategy("world pvp", BotState::BOT_STATE_NON_COMBAT);
    if (grant && !held)
    {
        facade->ChangeStrategy("+world pvp", BotState::BOT_STATE_NON_COMBAT);
        facade->ChangeStrategy("+world pvp", BotState::BOT_STATE_COMBAT);
        ai::WorldPvpSeekLog::LogGrant(facade, player);
    }
    else if (!grant && held)
    {
        facade->ChangeStrategy("-world pvp", BotState::BOT_STATE_NON_COMBAT);
        facade->ChangeStrategy("-world pvp", BotState::BOT_STATE_COMBAT);
    }
    facade->worldPvpSeekRollMs_ = WorldTimer::getMSTime();
}

void AiFactory::AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    // What the config lists below would have given a bound bot of the
    // open-world kit (CommandedSafeNonCombatStrategies): noted for its release.
    uint32 commandedListHeld = 0;
    const int tab = GetKitSpecTab(player, facade);
    switch (player->GetClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("holy");
            }
            else
            {
                nonCombatEngine->addStrategy("shadow");
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "buff", "boost", NULL);
            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                nonCombatEngine->addStrategies("protection", "tank assist", NULL);
            }
            else if (tab == 0)
            {
                nonCombatEngine->addStrategies("holy", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("retribution", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("cure", "buff", "boost", "blessing", "aura", NULL);
            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategy("survival");
            }
            else
            {
                nonCombatEngine->addStrategy("marksmanship");
            }

            nonCombatEngine->addStrategies("dps assist", "cc", "aoe", "buff", "boost", "aspect", "pet", NULL);
            break;
        }

        case CLASS_SHAMAN:
        {
            // Tank-role shaman keeps the tank kit outside combat too (druid
            // `:1180-1192` pattern): spec strategy + "tank assist" on top of
            // the shared shaman noncombat pieces, never "dps assist".
            if (tab == 1 && PlaysShamanTankKit(player, facade))
            {
                for (std::string const& name : TortoiseBots::ShamanTankNonCombatKit())
                    nonCombatEngine->addStrategy(name);
                break;
            }

            if (tab == 0)
            {
                nonCombatEngine->addStrategies("elemental", "aoe", "cc", NULL);
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategies("restoration", "aoe", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("enhancement", "aoe", "cc", NULL);
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "totems", "buff", "boost", NULL);
            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategy("subtlety");
            }
            else
            {
                nonCombatEngine->addStrategy("combat");
            }

            nonCombatEngine->addStrategies("dps assist", "poisons", "stealth", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("demonology");
            }
            else
            {
                nonCombatEngine->addStrategy("destruction");
            }

            nonCombatEngine->addStrategies("dps assist", "pet", "buff", "boost", NULL);
            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("fire");
            }
            else
            {
                nonCombatEngine->addStrategy("frost");
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "buff", "boost", NULL);
            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                nonCombatEngine->addStrategies("leveling", "dps assist", "cure", NULL);
                break;
            }

            if (tab == 1)
            {
                uint8 const ncRole = facade->GetForcedRole();
                bool const ncTanking = ncRole ? (ncRole & BOT_ROLE_TANK) != 0 : false;
                if (ncTanking)
                {
                    nonCombatEngine->addStrategies("tank feral", "tank assist", NULL);
                }
                else
                {
                    nonCombatEngine->addStrategies("dps feral", "dps assist", NULL);
                }
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategies("restoration", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("balance", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                nonCombatEngine->addStrategies("protection", "tank assist", NULL);
            }
            else if(player->GetLevel() < 30 || tab == 0)
            {
                nonCombatEngine->addStrategies("arms", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("fury", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("buff", "boost", NULL);
            break;
        }


        default:
        {
            nonCombatEngine->addStrategy("dps assist");
            break;
        }
    }

    nonCombatEngine->addStrategies("wbuff", "dungeon", NULL);
    // "dungeon" owns the enter/leave <raid> transition triggers; without it
    // zone-ins never swap into raid tactics.
    // Mob avoidance remains disabled until the core supplies a real path
    // filter; see SetAvoidAreaAction and the audit's core follow-up.

    if(sPlayerbotAIConfig.llmEnabled == 2)
        nonCombatEngine->addStrategy("ai chat");

    // A hire follows its master and never talks to quest NPCs on its own:
    // the DefaultQuestStrategy gossip-hello triggers stay off the hire
    // (explicit orders still run through the action). Owned alts keep it.
    bool const hireNoQuestTalk = TortoiseBots::HireLifecycle::Instance().IsHired(player->GetObjectGuid());

    if (!player->InBattleGround())
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player);
        Player* master = ai ? ai->GetMaster() : nullptr;

        if (master && master->GetSession() && master->GetSession()->HasNetworkTransport())
        {
            // A real player's bot rebuilds with "follow" (GetDefaultMovementStrategy,
            // mod-playerbots parity): a role change or regroup reset must not swap
            // the follow it got on invite for UseWanderAsDefaultFollowStrategy's wander.
            nonCombatEngine->addStrategies("racials", "nc", "food", "follow", "default", hireNoQuestTalk ? "loot" : "quest", "loot", "gather", "duel", "emote", "buff", "mount", "force rebuff", NULL);
        }
        else
        {
            const char* wanderFollow = hireNoQuestTalk ? "follow" : (sPlayerbotAIConfig.useWanderAsDefaultFollowStrategy ? "wander" : "follow");
            nonCombatEngine->addStrategies("racials", "nc", "food", wanderFollow, "default", hireNoQuestTalk ? "loot" : "quest", "loot", "gather", "duel", "emote", "buff", "mount", "force rebuff", NULL);
        }
    }

    if ((facade->HasRealPlayerMaster() && sPlayerbotAIConfig.jumpWithPlayer) ||
        (player->InBattleGround() && sPlayerbotAIConfig.jumpInBg) ||
        (!player->InBattleGround() && !facade->HasRealPlayerMaster()))
    {
        if (frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpNoCombatChance)
        {
            nonCombatEngine->addStrategy("rpg jump");
        }
    }

    if (!player->InBattleGround() && sPlayerbotAIConfig.jumpFollow)
        nonCombatEngine->addStrategy("follow jump");

    // A rebuild starts with nothing held back; the guarded branch below
    // records what it skips inside an instance (Issue #218).
    facade->instanceGuardWithheld = 0;

    if ((facade->IsRealPlayer() || sRandomBotFacade.IsFreeBot(player)) && !player->InBattleGround())
    {
        Player* master = facade->GetMaster();

        nonCombatEngine->addStrategy("roll");

        // let 25% of free bots start duels (never an alt-bot, issue #872:
        // it is the player's own character).
        if (!urand(0, 3) && !TortoiseBots::AltBotService::Instance().IsAutonomous(player->GetObjectGuid()))
        {
            nonCombatEngine->addStrategy("start duel");
        }

        RollWorldPvpSeek(player, facade);

        if (!player->GetGroup() || facade->IsGroupLeader())
        {
            // let 25% of random not grouped (or group leader) bots help other players
            if (!urand(0, 3))
            {
                nonCombatEngine->addStrategy("attack tagged");
            }

            // Hired companions follow their master and never run their own
            // questing/travel: the config list below runs through the same
            // master guard as the grouped branch, so a hire gets only the
            // follower kit (no grind/travel/tfish/rpg/wander) while owned
            // alts keep their explicit orders (ledger check, not master
            // presence). The direct adds above stay for pool bots.
            bool const isHire = TortoiseBots::HireLifecycle::Instance().IsHired(player->GetObjectGuid());
            nonCombatEngine->addStrategy("collision");
            if (!isHire)
                nonCombatEngine->addStrategy("grind");
            nonCombatEngine->addStrategy("group");
            nonCombatEngine->addStrategy("guild");

            if (sPlayerbotAIConfig.autoDoQuests && !isHire)
            {
                nonCombatEngine->addStrategy("travel");
                nonCombatEngine->addStrategy("tfish");
                nonCombatEngine->addStrategy("rpg");
            }

            if(!master || (master->GetSession() && !master->GetSession()->HasNetworkTransport()))
            {
                nonCombatEngine->addStrategy("maintenance");
            }

            nonCombatEngine->ChangeStrategy(CommandedSafeNonCombatStrategies(player, HireSafeNonCombatStrategies(facade, sPlayerbotAIConfig.randomBotNonCombatStrategies), &commandedListHeld));
        }
        else
        {
            if (facade)
            {
                if (master)
                {
                    if (PlayerbotAIStorage::Instance().GetAI(player) || sRandomBotFacade.IsFreeBot(player))
                    {
                        // Dungeon groups run the LFT role kit, not the open
                        // world: grind/travel/tfish/rpg wander inside the
                        // instance otherwise (Issue #218).
                        bool const inInstance = player->GetMap() && (player->GetMap()->IsDungeon() || player->GetMap()->IsRaid());
                        // What the guard drops from the list below is what
                        // leaving the instance gives back
                        // (ApplyInstanceGuardOnMapChange): the out-param records
                        // only entries actually present in the list, so a list
                        // without +grind never restores grind.
                        uint8 withheld = 0;
                        // A hire never carries the autonomy kit evenGrouped with an
                        // offline master (grace-period guard duty): the ledger
                        // strip, not just the live-master guard.
                        bool const isHire = TortoiseBots::HireLifecycle::Instance().IsHired(player->GetObjectGuid());
                        nonCombatEngine->addStrategy("collision");
                        if (!inInstance && !isHire)
                            nonCombatEngine->addStrategy("grind");
                        nonCombatEngine->addStrategy("group");
                        nonCombatEngine->addStrategy("guild");

                        if (sPlayerbotAIConfig.autoDoQuests && !inInstance && !isHire)
                        {
                            nonCombatEngine->addStrategy("travel");
                            nonCombatEngine->addStrategy("tfish");
                            nonCombatEngine->addStrategy("rpg");
                        }

                        if ((!master || (master->GetSession() && !master->GetSession()->HasNetworkTransport())) && !isHire)
                        {
                            nonCombatEngine->addStrategy("maintenance");
                        }

                        // The config list runs last, so it must honour the instance guard
                        // too, and its "+wander" must not replace a player's bot's follow.
                        // What the guard drops here is recorded in withheld, so leaving
                        // the instance gives back only those entries
                        // (ApplyInstanceGuardOnMapChange).
                        nonCombatEngine->ChangeStrategy(CommandedSafeNonCombatStrategies(player, HireSafeNonCombatStrategies(facade, RealMasterSafeNonCombatStrategies(facade,
                            InstanceSafeNonCombatStrategies(player, sPlayerbotAIConfig.randomBotNonCombatStrategies, &withheld))), &commandedListHeld));
                        facade->instanceGuardWithheld = withheld;
                    }
                    else
                    {
                        nonCombatEngine->ChangeStrategy(sPlayerbotAIConfig.nonCombatStrategies);
                    }
                }
            }
        }
    }
    else
    {
        nonCombatEngine->ChangeStrategy(sPlayerbotAIConfig.nonCombatStrategies);
    }

    if (TortoiseBots::RoleConsumablesEnabled())
        nonCombatEngine->addStrategy("role consumables");

    // Battleground switch
    if (player->InBattleGround())
    {
        nonCombatEngine->addStrategies("racials", "nc", "default", "buff", "food", "mount", "collision", "dps assist", "attack tagged", "emote", NULL);
        nonCombatEngine->removeStrategy("custom::say");
        nonCombatEngine->removeStrategy("travel");
        nonCombatEngine->removeStrategy("tfish");
        nonCombatEngine->removeStrategy("rpg");
        nonCombatEngine->removeStrategy("follow");
        nonCombatEngine->removeStrategy("wander");

        nonCombatEngine->removeStrategy("grind");

        BattleGroundTypeId bgType = player->GetBattleGroundTypeId();

        if (bgType <= BATTLEGROUND_AB) // Vanilla WSG, AV, and AB tactics
        {
            nonCombatEngine->addStrategy("battleground");

            if (bgType == BATTLEGROUND_WS)
                nonCombatEngine->addStrategy("warsong");

            if (bgType == BATTLEGROUND_AV)
                nonCombatEngine->addStrategy("alterac");

            if (bgType == BATTLEGROUND_AB)
                nonCombatEngine->addStrategy("arathi");
        }
    }

    // A bot a remote commander has bound moves only on its orders: whatever
    // open-world kit the branches above gave it (a group leader's grind,
    // travel and rpg, also inside an instance) comes off until the release.
    TortoiseBots::BotCommands::HoldCommanderAutonomy(player, facade, true, commandedListHeld);
}

Engine* AiFactory::createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
	Engine* nonCombatEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_NON_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_NON_COMBAT, nonCombatEngine);
    AddDefaultNonCombatStrategies(player, facade, nonCombatEngine);
	return nonCombatEngine;
}

std::string AiFactory::InstanceSafeNonCombatStrategies(Player* player, std::string const& strategies, uint8* withheld)
{
    if (!player || !player->GetMap() || !(player->GetMap()->IsDungeon() || player->GetMap()->IsRaid()))
        return strategies;

    // Issue #218: inside an instance the party kit stays in charge. Drop the
    // list's open-world adds, and its "+wander" so the movement mode the bot
    // already has (follow for a player's bot) keeps its leash.
    std::string kept;
    for (std::string const& entry : split(strategies, ','))
    {
        bool drop = false;
        if (!entry.empty() && entry[0] == '+')
        {
            for (uint8 i = 0; i < kInstanceGuardStrategyCount; ++i)
            {
                if (entry.compare(1, std::string::npos, kInstanceGuardStrategies[i]) == 0)
                {
                    drop = true;
                    if (withheld)
                        *withheld |= InstanceGuardBit(i);
                    break;
                }
            }
        }

        if (drop)
            continue;

        if (!kept.empty())
            kept += ",";
        kept += entry;
    }
    return kept;
}

void AiFactory::ApplyInstanceGuardOnMapChange(Player* player, PlayerbotAI* ai)
{
    if (!player || !ai || !player->GetMap() || player->InBattleGround())
        return;

    // A commander's release inside a dungeon kept the bot's open-world kit
    // back: out in the open world it comes back (any bot, group leaders too).
    TortoiseBots::BotCommands::CommanderAutonomyMapChange(player, ai);

    // The bots the guarded branch of AddDefaultNonCombatStrategies builds: a
    // pool bot in a group it does not lead, with a master. Without this the
    // kit from the open world rode along through the instance portal until
    // something rebuilt the strategies (Issue #218).
    if (!sRandomBotFacade.IsFreeBot(player) || !player->GetGroup() || ai->IsGroupLeader() || !ai->GetMaster())
        return;

    bool const hasFollow = ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);
    bool const inInstance = player->GetMap()->IsDungeon() || player->GetMap()->IsRaid();
    if (inInstance)
    {
        // Strip only what the bot has and remember exactly that, so leaving
        // gives back nothing the player had turned off. "-rpg" also drops its
        // children (rpg quest/vendor/explore/maintenance). "wander" goes only
        // next to "follow", so a bot whose movement mode is wander keeps one.
        std::string strip;
        uint8 removed = 0;
        for (uint8 i = 0; i < kInstanceGuardStrategyCount; ++i)
        {
            if (i == kInstanceGuardWander && !hasFollow)
                continue;
            if (!ai->HasStrategy(kInstanceGuardStrategies[i], BotState::BOT_STATE_NON_COMBAT))
                continue;
            removed |= InstanceGuardBit(i);
            strip += strip.empty() ? "-" : ",-";
            strip += kInstanceGuardStrategies[i];
        }
        if (!strip.empty())
            ai->ChangeStrategy(strip, BotState::BOT_STATE_NON_COMBAT);
        // OR, not assign: a move between two instance maps keeps what the
        // first one withheld.
        ai->instanceGuardWithheld |= removed;
        return;
    }

    // Back in the open world: give back only what the guard held, so neither
    // a boat ride nor a dungeon run undoes a player's own "-grind" or "-rpg".
    // A bot with a real-player master never gets the open-world kit back
    // (RealMasterSafeNonCombatStrategies, #741): it keeps following its
    // player. Only masterless pool bots get the restore.
    uint8 const withheld = ai->instanceGuardWithheld;
    ai->instanceGuardWithheld = 0;
    std::string const restore =
        ai::InstanceGuardRestore(withheld, ai->HasRealPlayerMaster(), hasFollow);
    if (!restore.empty())
    {
        ai->ChangeStrategy(restore, BotState::BOT_STATE_NON_COMBAT);
        // A bound bot leaving the instance (a wipe's graveyard) gets no own
        // walks back: the binding holds them until its release.
        TortoiseBots::BotCommands::HoldCommanderAutonomy(player, ai, false);
    }
}

void AiFactory::AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine)
{
    deadEngine->addStrategies("dead", "stay", "default", "follow", "group", NULL);
    if (sRandomBotFacade.IsFreeBot(player) && !player->GetGroup())
    {
        deadEngine->removeStrategy("follow");
    }

    const int tab = GetKitSpecTab(player, facade);
    switch (player->GetClass())
    {
        case CLASS_SHAMAN:
        {
            if (tab == 1 && PlaysShamanTankKit(player, facade))
            {
                deadEngine->addStrategy("tank shaman");
                break;
            }
            if (tab == 0)
            {
                deadEngine->addStrategy("elemental");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("restoration");
            }
            else
            {
                deadEngine->addStrategy("enhancement");
            }

            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("subtlety");
            }
            else
            {
                deadEngine->addStrategy("combat");
            }

            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("demonology");
            }
            else
            {
                deadEngine->addStrategy("destruction");
            }

            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("fire");
            }
            else
            {
                deadEngine->addStrategy("frost");
            }

            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                deadEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                deadEngine->addStrategy("holy");
            }
            else
            {
                deadEngine->addStrategy("retribution");
            }

            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                deadEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                deadEngine->addStrategy("arms");
            }
            else
            {
                deadEngine->addStrategy("fury");
            }

            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                deadEngine->addStrategy("leveling");
                break;
            }

            if (tab == 1)
            {
                uint8 const deadRole = facade->GetForcedRole();
                if (deadRole ? (deadRole & BOT_ROLE_TANK) != 0 : false)
                {
                    deadEngine->addStrategy("tank feral");
                }
                else
                {
                    deadEngine->addStrategy("dps feral");
                }
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("restoration");
            }
            else
            {
                deadEngine->addStrategy("balance");
            }

            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("survival");
            }
            else
            {
                deadEngine->addStrategy("marksmanship");
            }

            break;
        }

        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("holy");
            }
            else
            {
                deadEngine->addStrategy("shadow");
            }

            break;
        }
    }

    if (facade->IsRealPlayer() || sRandomBotFacade.IsFreeBot(player))
    {
        deadEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotDeadStrategies);
    }
    else
    {
        deadEngine->ChangeStrategy(sPlayerbotAIConfig.deadStrategies);
    }
}

Engine* AiFactory::createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* deadEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_DEAD);
    facade->SetEngine(BotState::BOT_STATE_DEAD, deadEngine);
    AddDefaultDeadStrategies(player, facade, deadEngine);
    return deadEngine;
}

void AiFactory::AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine)
{
    reactionEngine->addStrategies("react", "chat", "avoid aoe", "potions", NULL);

    const int tab = GetKitSpecTab(player, facade);
    switch (player->GetClass())
    {
        case CLASS_SHAMAN:
        {
            if (tab == 1 && PlaysShamanTankKit(player, facade))
            {
                reactionEngine->addStrategy("tank shaman");
                break;
            }
            if (tab == 0)
            {
                reactionEngine->addStrategy("elemental");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("restoration");
            }
            else
            {
                reactionEngine->addStrategy("enhancement");
            }

            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("subtlety");
            }
            else
            {
                reactionEngine->addStrategy("combat");
            }

            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("demonology");
            }
            else
            {
                reactionEngine->addStrategy("destruction");
            }

            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("fire");
            }
            else
            {
                reactionEngine->addStrategy("frost");
            }

            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                reactionEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                reactionEngine->addStrategy("holy");
            }
            else
            {
                reactionEngine->addStrategy("retribution");
            }

            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                reactionEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                reactionEngine->addStrategy("arms");
            }
            else
            {
                reactionEngine->addStrategy("fury");
            }

            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                reactionEngine->addStrategy("leveling");
                break;
            }

            if (tab == 1)
            {
                uint8 const reactRole = facade->GetForcedRole();
                if (reactRole ? (reactRole & BOT_ROLE_TANK) != 0 : false)
                {
                    reactionEngine->addStrategy("tank feral");
                }
                else
                {
                    reactionEngine->addStrategy("dps feral");
                }
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("restoration");
            }
            else
            {
                reactionEngine->addStrategy("balance");
            }

            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("survival");
            }
            else
            {
                reactionEngine->addStrategy("marksmanship");
            }

            break;
        }

        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("holy");
            }
            else
            {
                reactionEngine->addStrategy("shadow");
            }

            break;
        }
    }

    if (facade->IsRealPlayer() || sRandomBotFacade.IsFreeBot(player))
    {
        reactionEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotReactStrategies);
    }
    else
    {
        reactionEngine->ChangeStrategy(sPlayerbotAIConfig.reactStrategies);
    }
}

ReactionEngine* AiFactory::createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    ReactionEngine* reactionEngine = new ReactionEngine(facade, AiObjectContext, BotState::BOT_STATE_REACTION);
    facade->SetEngine(BotState::BOT_STATE_REACTION, reactionEngine);
    AddDefaultReactionStrategies(player, facade, reactionEngine);
    return reactionEngine;
}
