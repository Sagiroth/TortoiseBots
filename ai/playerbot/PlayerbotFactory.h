#pragma once

#include "../../runtime/PlayerbotAIStorage.h"

class Player;
class ChatHandler;


class PlayerbotFactory
{
public:
    PlayerbotFactory(Player* bot, uint32 level, uint32 itemQuality = 0) : level(level), itemQuality(itemQuality), bot(bot), ai(PlayerbotAIStorage::Instance().GetAI(bot)) {}
    virtual ~PlayerbotFactory() {}

    void Refresh();
    void InitSkills();
    // Learn the abilities the bot's skills teach, and restore the per-session
    // equip flags those abilities carry (Dual Wield, Parry, Block). Table-driven
    // from skill_line_ability / SkillRaceClassInfo / the spell store: a bot that
    // owns skill 118 learns spell 674, a bot with Bows learns Shoot Bow, and a
    // rogue/warrior/hunter that reached the class level for Dual Wield learns it
    // even before the skill exists. Idempotent; safe on every login and every
    // factory provisioning pass. See the implementation for why a bot cannot use
    // its off-hand, shoot a bow, or block without it.
    static void EnsureSkillRewardedSpells(Player* bot);

    void EnchantEquipment();
    void EquipGear() { InitEquipment(false, false); }
    void EquipGearBest() { return InitEquipment(false, false, false); }
    void EquipGearPartialUpgrade() { return InitEquipment(false, false, true, true); }
    void UpgradeGear(bool syncWithMaster) { return InitEquipment(!syncWithMaster, syncWithMaster); }
    void UpgradeGearBest() { return InitEquipment(true, false, false); }
    // Weapon/armour/riding skills plus two class-appropriate professions and the
    // secondary skills, all bounded by the current level. Public so the runtime can
    // give it to persistent-level bots on first login. Primaries are granted from
    // level PRIMARY_PROFESSION_MIN_LEVEL (5); below that only secondaries land.
    void InitAllSkills();
    // Idempotent primary-pair grant at the level gate (Professions-at-5):
    // rolls the class pair and teaches it when the bot is at/above the gate
    // and holds no primary; never touches a bot that already has one. Public
    // so the level-up spell hook can grant it on ding.
    void EnsurePrimaryProfessions();
    // True when the bot holds any primary profession (never re-roll those).
    bool HasAnyPrimaryProfession() const;
    // Issue #189 Phase 1: idempotent "make complete" for random-pool bots.
    // Talents first, then knob-gated free spells, then skills, then gear —
    // same result whether first run at 1, 10, 40 or 60. Owned bots keep
    // current behavior (caller gates on IsRandomBot).
    void MakeComplete();
    // Issue #192: public so the hire provisioner can provision a companion
    // without reimplementing gear/spell init. Incremental-only callers must
    // use UpgradeGearBest (never the wiping non-incremental path).
    void ProvisionSpellsAndGear();
    // Cheap periodic top-up for hired/owned companions (no item cheat):
    // class reagents, food/drink, potions and level-tier bandages, each
    // topped to a small bounded stack. Safe to run hourly; never duplicates.
    void RestockCompanion();
    void AddTools() { return InitInventorySkill(); }
    void AddReagents() { return InitReagents(); }
    void AddPotions() { return InitPotions(); }
    void AddConsumes() { return AddConsumables(); }
    void AddFood() { return InitFood(); }
    void AddBandages() { return InitBandages(); }
    void InitAmmo();
    void InitThrown();
    // Idempotent #401 starter-set backstop for the live pool (login and
    // level-up): thrown first, then bags, then ammo. Never touches real
    // players - callers gate on the random-pool record.
    void EnsureStarterKit();
    // Turtle mount (quest 40302 equivalent, spell 30174): idempotent grant
    // for TurtleMountAtLevel..39, no-op at 40+ and when disabled. Called from
    // InitMounts (seed path) and the level-up spell hook.
    void InitTurtleMount();
    // Racial/collection mount set at 40 (slow) and 60 (epic), idempotent per
    // tier. Public so the 40/60 level-up spell hook can grant it behind
    // AiPlayerbot.LevelUpMounts; the seed/hire field kit calls it too.
    void InitMounts();
    void InitPet();
    void InitPetSpells();

private:
    void Shuffle(std::vector<uint32>& items);
    void InitEquipment(bool incremental, bool syncWithMaster, bool progressive = sPlayerbotAIConfig.randomGearProgression, bool partialUpgrade = false);
    // One per-quality candidate query with the wearability descent (shared
    // by the main band loop, the epic path and the fallback).
    void QuerySeedCandidates(Player* bot, uint32 specId, uint8 slot, uint32 searchLevel, uint32 maxItemLevel, uint32 q, std::vector<uint32>& ids);
    // Rare world-epic gate (§6): EPIC-only query for the slot, restricted to
    // loot-attested BoE world epics the bot can wear. Returns true with ids
    // filled when the slot has any; false (fall back to the normal band)
    // otherwise.
    bool TrySeedEpicIds(Player* bot, uint32 specId, uint8 slot, uint32 searchLevel, std::vector<uint32>& ids);
    bool CanEquipItem(ItemPrototype const* proto, uint32 desiredQuality);
    void InitTradeSkills();
    void SetRandomSkill(uint16 id);
    void InitAvailableSpells();
    void InitSpecialSpells();
    void InitClassLevelSpells();
    // Assigns the bot a premade talent spec (specNo) by weighted probability, so the
    // "auto talents" action applies the matching premade build. Returns false if the
    // class has no premade specs configured.
    bool SelectPremadeSpecNo();
    // Idempotent kit helpers shared by the fresh-seed and restock paths.
    // InitBags keeps the hunter quiver/pouch logic intact; InitLevelBags
    // upgrades plain container slots to a level-tier vendor bag first,
    // InitStarterBags fills the #401 starter set (3x 14-slot 3914, warlock
    // soul pouch 22243).
    void InitLevelBags();
    void InitStarterBags();
    void SeedFreshMoney();
    void InitBandages();
    void InitPotions();
    void InitFood();
    // Pool-only full-stack ration seed behind InitFood's cheat gate: best
    // vendor food (and water for mana users) for the level, one full stack
    // each, stale lower tiers replaced. The per-tick refill keeps it there.
    void InitPoolRations();
    void InitReagents();
    bool CanEquipArmor(ItemPrototype const* proto);
    bool CanEquipWeapon(ItemPrototype const* proto);
    void EnchantItem(Item* item);
    void PruneDuplicateEquipRows();
    void AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank);
    void AddItemSpellStats(uint32 smod, uint8& sp, uint8& ap, uint8& tank);
    bool CheckItemStats(uint8 sp, uint8 ap, uint8 tank);
    void InitBags();
    void InitInventorySkill();
    Item* StoreItem(uint32 itemId, uint32 count, bool ignoreCount = false);
    // Top up one consumable family (oils/stones/poisons), ordered low to high
    // tier: destroys only strictly lower tiers, tops targetId up to target
    // items, never touches a higher tier the bot owns, never trims target.
    void TopUpConsumableFamily(std::vector<uint32> const& familyLowToHigh, uint32 targetId, uint32 target);
    void AddConsumables();
    // Level-appropriate enchant for one equipped item: best candidate from
    // ai_playerbot_enchant_candidates for the bot's class/spec (see
    // RandomItemMgr::CalculateBestBotEnchantId). Silent skip (mask mismatch,
    // no candidate, below gate) keeps seed logs clean.
    void ApplyBestEnchant(Item* item);

private:
    uint32 level;
    uint32 itemQuality;
    PlayerbotAI* ai;
    Player* bot;
};

enum PriorizedConsumables
{
   CONSUM_ID_ROUGH_WEIGHTSTONE = 3239,
   CONSUM_ID_COARSE_WEIGHTSTONE = 3240,
   CONSUM_ID_HEAVY_WEIGHTSTONE = 3241,
   CONSUM_ID_SOLID_WEIGHTSTONE = 7965,
   CONSUM_ID_DENSE_WEIGHTSTONE = 12643,
   CONSUM_ID_ROUGH_SHARPENING_STONE = 2862,
   CONSUM_ID_COARSE_SHARPENING_STONE = 2863,
   CONSUM_ID_HEAVY_SHARPENING_STONE = 2871,
   CONSUM_ID_SOL_SHARPENING_STONE = 7964,
   CONSUM_ID_DENSE_SHARPENING_STONE = 12404,
   CONSUM_ID_ELEMENTAL_SHARPENING_STONE = 18262,
   CONSUM_ID_CONSECRATED_SHARPENING_STONE = 23122,
   CONSUM_ID_LINEN_BANDAGE = 1251,
   CONSUM_ID_HEAVY_LINEN_BANDAGE = 2581,
   CONSUM_ID_WOOL_BANDAGE = 3530,
   CONSUM_ID_HEAVY_WOOL_BANDAGE = 3531,
   CONSUM_ID_SILK_BANDAGE = 6450,
   CONSUM_ID_HEAVY_SILK_BANDAGE = 6451,
   CONSUM_ID_MAGEWEAVE_BANDAGE = 8544,
   CONSUM_ID_HEAVY_MAGEWEAVE_BANDAGE = 8545,
   CONSUM_ID_RUNECLOTH_BANDAGE = 14529,
   CONSUM_ID_HEAVY_RUNECLOTH_BANDAGE = 14530,
   CONSUM_ID_BRILLIANT_MANA_OIL = 20748,
   CONSUM_ID_MINOR_MANA_OIL = 20745,
   CONSUM_ID_LESSER_MANA_OIL = 20747,
   CONSUM_ID_BRILLIANT_WIZARD_OIL = 20749,
   CONSUM_ID_MINOR_WIZARD_OIL = 20744,
   CONSUM_ID_WIZARD_OIL = 20750,
   CONSUM_ID_LESSER_WIZARD_OIL = 20746,
   CONSUM_ID_INSTANT_POISON = 6947,
   CONSUM_ID_INSTANT_POISON_II = 6949,
   CONSUM_ID_INSTANT_POISON_III = 6950,
   CONSUM_ID_INSTANT_POISON_IV = 8926,
   CONSUM_ID_INSTANT_POISON_V = 8927,
   CONSUM_ID_INSTANT_POISON_VI = 8928,
   CONSUM_ID_INSTANT_POISON_VII = 21927,
   CONSUM_ID_DEADLY_POISON = 2892,
   CONSUM_ID_DEADLY_POISON_II = 2893,
   CONSUM_ID_DEADLY_POISON_III = 8984,
   CONSUM_ID_DEADLY_POISON_IV = 8985,
   CONSUM_ID_DEADLY_POISON_V = 20844,
   CONSUM_ID_DEADLY_POISON_VI = 22053,
   CONSUM_ID_DEADLY_POISON_VII = 22054,
   CONSUM_ID_CRIPPLING_POISON = 3775,
   CONSUM_ID_CRIPPLING_POISON_II = 3776,
   CONSUM_ID_MIND_POISON = 5237,
   CONSUM_ID_MIND_POISON_II = 6951,
   CONSUM_ID_MIND_POISON_III = 9186,
};
