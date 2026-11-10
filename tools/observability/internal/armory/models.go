package armory

// Inventory slot layout mirrors the core (tortoise-wow src/game/Objects/Player.h):
//
//	EquipmentSlots:    0-18  (EQUIPMENT_SLOT_START..EQUIPMENT_SLOT_TABARD)
//	InventorySlots:    19-22 (INVENTORY_SLOT_BAG_START..INVENTORY_SLOT_BAG_END)
//	InventoryPackSlots: 23-38 (INVENTORY_SLOT_ITEM_START..INVENTORY_SLOT_ITEM_END)
//	BuyBackSlots:      69-80 (BUYBACK_SLOT_START..BUYBACK_SLOT_END)
//
// character_inventory rows with bag=0 sit directly in one of those slots.
// Rows with bag!=0 hold the item-instance GUID of the bag container and live
// inside that bag (see Player::_SaveInventory / _LoadInventory).
const (
	EquipmentSlotEnd = 19
	BagSlotStart     = 19
	BagSlotEnd       = 23
	BackpackStart    = 23
	BackpackEnd      = 39
	// BuyBackSlots 69-81 (core Player.h): vendor-sold items awaiting buyback.
	BuybackStart = 69
	BuybackEnd   = 81
)

type BotSummary struct {
	GUID      uint32 `json:"guid"`
	Name      string `json:"name"`
	Race      uint8  `json:"race"`
	Class     uint8  `json:"class"`
	Gender    uint8  `json:"gender"`
	Level     uint32 `json:"level"`
	Money     uint32 `json:"money"`
	TotalTime uint32 `json:"totaltime"`
	Online    uint8  `json:"online"`
	Spec      string `json:"spec,omitempty"`
	// Kind is the dashboard kind label (Issue #872): pool, hired, claimed,
	// player, or own (a player-account character offline or unclassified).
	// Autonomous alts and companions are live roster state: the daemon joins
	// them from telemetry by guid, the DB list cannot see them.
	Kind string `json:"kind,omitempty"`
	// Guild is the guild name from the database (empty when guildless).
	Guild string `json:"guild,omitempty"`
	// Professions are the trade and secondary skills the character knows.
	Professions []SkillEntry `json:"professions,omitempty"`
}

// ItemDetail carries the tooltip-relevant fields from world.item_template:
// combat/display attributes only, no assets, no icon files.
type ItemDetail struct {
	Class          uint8    `json:"class"`
	SubClass       uint8    `json:"subclass"`
	Description    string   `json:"description,omitempty"`
	Bonding        uint8    `json:"bonding"`
	RequiredLevel  uint8    `json:"required_level"`
	RequiredSkill  uint32   `json:"required_skill,omitempty"`
	AllowableClass int32    `json:"allowable_class"`
	AllowableRace  int32    `json:"allowable_race"`
	MaxCount       uint32   `json:"max_count,omitempty"`
	Stackable      uint32   `json:"stackable,omitempty"`
	ContainerSlots uint32   `json:"container_slots,omitempty"`
	Delay          uint32   `json:"delay,omitempty"`
	AmmoType       uint8    `json:"ammo_type,omitempty"`
	DmgMin1        float64  `json:"dmg_min1,omitempty"`
	DmgMax1        float64  `json:"dmg_max1,omitempty"`
	DmgType1       uint8    `json:"dmg_type1,omitempty"`
	DmgMin2        float64  `json:"dmg_min2,omitempty"`
	DmgMax2        float64  `json:"dmg_max2,omitempty"`
	DmgMin3        float64  `json:"dmg_min3,omitempty"`
	DmgMax3        float64  `json:"dmg_max3,omitempty"`
	Block          uint32   `json:"block,omitempty"`
	Armor          int32    `json:"armor,omitempty"`
	ResHoly        int32    `json:"res_holy,omitempty"`
	ResFire        int32    `json:"res_fire,omitempty"`
	ResNature      int32    `json:"res_nature,omitempty"`
	ResFrost       int32    `json:"res_frost,omitempty"`
	ResShadow      int32    `json:"res_shadow,omitempty"`
	ResArcane      int32    `json:"res_arcane,omitempty"`
	StatTypes      []uint32 `json:"stat_types,omitempty"`
	StatValues     []int32  `json:"stat_values,omitempty"`
	SpellIDs       []uint32 `json:"spell_ids,omitempty"`
	SpellTriggers  []uint32 `json:"spell_triggers,omitempty"`
	SpellNames     []string `json:"spell_names,omitempty"`
	SpellDescs     []string `json:"spell_descs,omitempty"`
	RandomStats    []string `json:"random_stats,omitempty"`
	MaxDurability  uint32   `json:"max_durability,omitempty"`
	SellPrice      uint32   `json:"sell_price,omitempty"`
}

type ItemEnchantmentInfo struct {
	ID          uint32 `json:"id"`
	Description string `json:"description"`
	Slot        uint8  `json:"slot"` // 0 = permanent / armor kit, 1 = temporary / poison / stone
	Duration    uint32 `json:"duration,omitempty"`
	Charges     uint32 `json:"charges,omitempty"`
}

type EquippedItem struct {
	Slot          uint8                 `json:"slot"`
	ItemTemplate  uint32                `json:"item_template"`
	Count         uint32                `json:"count"`
	Name          string                `json:"name"`
	Quality       uint8                 `json:"quality"`
	ItemLevel     uint32                `json:"item_level"`
	InventoryType uint32                `json:"inventory_type"`
	DisplayID     uint32                `json:"display_id"`
	// Icon is the icon *name* from world.item_display_info (e.g. INV_Sword_04).
	// The dashboard renders no image files; names are informational only.
	Icon         string                `json:"icon"`
	Enchantments []ItemEnchantmentInfo `json:"enchantments,omitempty"`
	Detail       ItemDetail            `json:"detail"`
}

type BagContainer struct {
	Slot           uint8      `json:"slot"`
	Item           uint32     `json:"item"`
	ItemTemplate   uint32     `json:"item_template"`
	Count          uint32     `json:"count"`
	Name           string     `json:"name"`
	Quality        uint8      `json:"quality"`
	ContainerSlots uint32     `json:"container_slots"`
	DisplayID      uint32     `json:"display_id"`
	Icon           string     `json:"icon"`
	Detail         ItemDetail `json:"detail"`
	Items          []BagItem  `json:"items"`
}

type BagItem struct {
	Slot         uint8                 `json:"slot"`
	ItemTemplate uint32                `json:"item_template"`
	Count        uint32                `json:"count"`
	Name         string                `json:"name"`
	Quality      uint8                 `json:"quality"`
	DisplayID    uint32                `json:"display_id"`
	Icon         string                `json:"icon"`
	Enchantments []ItemEnchantmentInfo `json:"enchantments,omitempty"`
	Detail       ItemDetail            `json:"detail"`
}

// CharacterStats mirrors tw_char.character_armory_stats with a fallback to
// tw_char.character_stats (subset) when no armory row exists.
// Source reports which layer produced the values: "armory_stats",
// "character_stats", "live" (characters + player_levelstats base), or "none".
// Both stat tables are empty until a character logs out
// (Player::_SaveStats, SaveOnlyOnLogout), so online bots report "live".
type CharacterStats struct {
	Source            string  `json:"source"`
	MaxHealth         uint32  `json:"maxhealth"`
	MaxPower1         uint32  `json:"maxpower1"`
	MaxPower2         uint32  `json:"maxpower2"`
	MaxPower3         uint32  `json:"maxpower3"`
	MaxPower4         uint32  `json:"maxpower4"`
	MaxPower5         uint32  `json:"maxpower5"`
	Strength          float64 `json:"strength"`
	Agility           float64 `json:"agility"`
	Stamina           float64 `json:"stamina"`
	Intellect         float64 `json:"intellect"`
	Spirit            float64 `json:"spirit"`
	Armor             uint32  `json:"armor"`
	ResHoly           uint32  `json:"res_holy"`
	ResFire           uint32  `json:"res_fire"`
	ResNature         uint32  `json:"res_nature"`
	ResFrost          uint32  `json:"res_frost"`
	ResShadow         uint32  `json:"res_shadow"`
	ResArcane         uint32  `json:"res_arcane"`
	BlockPct          float64 `json:"block_pct"`
	DodgePct          float64 `json:"dodge_pct"`
	ParryPct          float64 `json:"parry_pct"`
	MeleeCritPct      float64 `json:"melee_crit_pct"`
	RangedCritPct     float64 `json:"ranged_crit_pct"`
	AttackPower       float64 `json:"attack_power"`
	RangedAttackPower float64 `json:"ranged_attack_power"`
	MeleeDamage       string  `json:"melee_damage"`
	RangedDamage      string  `json:"ranged_damage"`
	MeleeSpeed        float64 `json:"melee_speed"`
	RangedSpeed       float64 `json:"ranged_speed"`
	CastSpeed         float64 `json:"cast_speed"`
	MeleeHit          float64 `json:"melee_hit"`
	RangedHit         float64 `json:"ranged_hit"`
	SpellHit          float64 `json:"spell_hit"`
	SpellDamage       uint32  `json:"spell_damage"`
	HealingPower      uint32  `json:"healing_power"`
	SpellCritPct      float64 `json:"spell_crit_pct"`
	ManaRegen         uint32  `json:"mana_regen"`
	SpellDmgFire      uint32  `json:"spell_dmg_fire,omitempty"`
	SpellDmgFrost     uint32  `json:"spell_dmg_frost,omitempty"`
	SpellDmgShadow    uint32  `json:"spell_dmg_shadow,omitempty"`
	SpellDmgNature    uint32  `json:"spell_dmg_nature,omitempty"`
	SpellDmgArcane    uint32  `json:"spell_dmg_arcane,omitempty"`
	SpellDmgHoly      uint32  `json:"spell_dmg_holy,omitempty"`
}

type SpellEntry struct {
	Spell uint32 `json:"spell"`
	Name  string `json:"name"`
	// Subtext is the rank text from spell_template.nameSubtext.
	Subtext string `json:"subtext"`
	// Description is the spell tooltip text from spell_template.description.
	// It disambiguates same-name spells (e.g. pet-teaching "Survival
	// Instinct (Cat)" vs the druid talent of the same name).
	Description string `json:"description,omitempty"`
	// Values resolves the client $sN/$oN/$aN/$tN template tokens:
	// simple value = effectBasePointsN + effectBaseDiceN per core
	// SpellEntry::CalculateSimpleValue. Index 0-2 maps to $s1/$s2/$s3
	// ($oN = over-time tick of the same effect, $aN = radius, $tN = amplitude).
	Values []int32 `json:"values,omitempty"`
	// Misc/Triggers resolve $aN (radius/misc) and triggered-spell links.
	// Misc is signed: spell_template.effectMiscValueN can be -1 (unspecified).
	Misc     []int32  `json:"misc,omitempty"`
	Triggers []uint32 `json:"triggers,omitempty"`
	// DurationMs/RangeYd/CastMs resolve $d/$r/$c from the operator DBCs.
	DurationMs int32  `json:"duration_ms,omitempty"`
	RangeYd    int32  `json:"range_yd,omitempty"`
	CastMs     int32  `json:"cast_ms,omitempty"`
	School     uint32 `json:"school"`
	IconID     uint32 `json:"icon_id"`
	// Icon is the icon *name* from world.spellicon when that mirror table is
	// populated, otherwise empty. No image files are served.
	Icon     string `json:"icon"`
	Active   uint8  `json:"active"`
	Disabled uint8  `json:"disabled"`
	// ClassSpell marks a member of the character's class spellbook (SkillLine
	// "Class Skills" + SkillLineAbility class mask). False when the operator
	// DBCs are unavailable.
	ClassSpell bool `json:"class_spell,omitempty"`
	// Passive marks SPELL_ATTR_PASSIVE spells: talents and other effects the
	// character never casts (Malice, Convection). A few legacy talent dummies
	// carry empty attributes and therefore read as active, exactly as the
	// client flags them.
	Passive bool `json:"passive,omitempty"`
	// Origin is SpellOriginStarting for race/class default spells that the
	// core re-derives on login and never writes to character_spell.
	Origin string `json:"origin,omitempty"`
}

// SpellOriginStarting marks a spell the core grants from playercreateinfo_spell
// (Player::LearnDefaultSpells). It has no character_spell row on purpose: the
// spell is re-learned on every login.
const SpellOriginStarting = "starting"

type SkillEntry struct {
	Skill uint32 `json:"skill"`
	Value uint32 `json:"value"`
	Max   uint32 `json:"max"`
}

// TalentNode is one talent box. Rank is derived from the bot's known spells:
// the highest learned rank spell in RankID order.
type TalentNode struct {
	TalentID uint32 `json:"talent_id"`
	Row      uint32 `json:"row"`
	Col      uint32 `json:"col"`
	Rank     uint32 `json:"rank"`
	MaxRank  uint32 `json:"max_rank"`
	SpellID  uint32 `json:"spell_id"`
	Name        string `json:"name"`
	Icon        string `json:"icon,omitempty"`
	Description string `json:"description,omitempty"`
}

type TalentTree struct {
	TabID   uint32       `json:"tab_id"`
	Name    string       `json:"name"`
	Page    uint32       `json:"page"`
	Points  uint32       `json:"points"`
	Talents []TalentNode `json:"talents"`
}

type BotProfile struct {
	Summary   BotSummary     `json:"summary"`
	Equipment []EquippedItem `json:"equipment"`
	Bags      []BagContainer `json:"bags"`
	Backpack  []BagItem      `json:"backpack"`
	// Buyback holds vendor-sold items still recoverable (slots 69-80).
	Buyback []BagItem      `json:"buyback"`
	Stats   CharacterStats `json:"stats"`
	Spells  []SpellEntry   `json:"spells"`
	Skills  []SkillEntry   `json:"skills"`
	// Talents is empty when the world DB talent mirror tables carry no rows
	// (core loads Talent/TalentTab from the operator's own DBC files at
	// server startup, not from SQL).
	Talents []TalentTree `json:"talents"`
	// Pet is the current hunter pet or warlock demon (character_pet, not stabled);
	// nil when the character has none. Values are as last saved.
	Pet *PetInfo `json:"pet,omitempty"`
}

type PetInfo struct {
	Name        string     `json:"name"`
	Entry       uint32     `json:"entry"`
	Family      uint32     `json:"family"`
	Hunter      bool       `json:"hunter"`
	Level       uint32     `json:"level"`
	Loyalty     uint32     `json:"loyalty"`
	TrainPoints int32      `json:"train_points"`
	Health      uint32     `json:"health"`
	Happiness   uint32     `json:"happiness"`
	Spells      []PetSpell `json:"spells"`
}

type PetSpell struct {
	Spell    uint32 `json:"spell"`
	Name     string `json:"name"`
	Subtext  string `json:"subtext"`
	Passive  bool   `json:"passive,omitempty"`
	Autocast bool   `json:"autocast,omitempty"`
}
