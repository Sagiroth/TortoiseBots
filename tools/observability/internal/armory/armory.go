package armory

import (
	"database/sql"
	"fmt"
	"strings"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

type Config struct {
	DBHost     string
	DBPort     int
	DBUser     string
	DBPassword string
	CharDB     string
	WorldDB    string
	// DBCDir optionally points at the operator's own extracted DBC files
	// (the same dir mangosd reads via DataDir). When Talent.dbc +
	// TalentTab.dbc are present, talents resolve from them; otherwise the
	// backend falls back to the world talent/talenttab mirror tables.
	DBCDir string
}

type Service struct {
	cfg Config
	db  *sql.DB
}

func NewService(cfg Config) (*Service, error) {
	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?timeout=5s&parseTime=false",
		cfg.DBUser, cfg.DBPassword, cfg.DBHost, cfg.DBPort, cfg.CharDB)

	db, err := sql.Open("mysql", dsn)
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	// Diagnostic tool, not a hot path.
	db.SetMaxOpenConns(5)
	db.SetMaxIdleConns(2)
	db.SetConnMaxLifetime(5 * time.Minute)

	return &Service{cfg: cfg, db: db}, nil
}

// likeEscape guards the name search against callers smuggling % _ or \ into
// the match.
func likeEscape(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `%`, `\%`)
	s = strings.ReplaceAll(s, `_`, `\_`)
	return s
}

func (s *Service) ListBots(query string) ([]BotSummary, error) {
	// Bot identity is the module's managed-account registry
	// (tortoise_bots_pool_account), never a username prefix: a personal account
	// whose name merely looks like a bot account is not a bot.
	sqlQuery := `
		SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.money, c.totaltime, c.online
		FROM characters c
		JOIN tortoise_bots_pool_account p ON p.account_id = c.account
		WHERE c.deleteDate IS NULL
	`

	var args []interface{}
	if query != "" {
		sqlQuery += " AND c.name LIKE ? ESCAPE '\\\\'"
		args = append(args, "%"+likeEscape(query)+"%")
	}
	sqlQuery += " ORDER BY c.name LIMIT 1000"

	rows, err := s.db.Query(sqlQuery, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	bots := []BotSummary{}
	for rows.Next() {
		var b BotSummary
		if err := rows.Scan(&b.GUID, &b.Name, &b.Race, &b.Class, &b.Gender, &b.Level, &b.Money, &b.TotalTime, &b.Online); err != nil {
			return nil, err
		}
		bots = append(bots, b)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	s.resolveBotSpecs(bots)
	return bots, nil
}

func (s *Service) GetBotProfile(guid uint32) (*BotProfile, error) {
	var profile BotProfile

	// 1. Identity: same columns the login query holder already loads; a bot is a
	// character on a managed pool account (registry), not a name prefix.
	summaryQuery := `
		SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.money, c.totaltime, c.online
		FROM characters c
		JOIN tortoise_bots_pool_account p ON p.account_id = c.account
		WHERE c.guid = ? AND c.deleteDate IS NULL
	`
	err := s.db.QueryRow(summaryQuery, guid).Scan(
		&profile.Summary.GUID, &profile.Summary.Name, &profile.Summary.Race,
		&profile.Summary.Class, &profile.Summary.Gender, &profile.Summary.Level,
		&profile.Summary.Money, &profile.Summary.TotalTime, &profile.Summary.Online)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, fmt.Errorf("bot %d not found", guid)
		}
		return nil, err
	}

	// 2. Equipment: bag=0 + slot 0-18, mirroring Player::_LoadInventory
	// (IsEquipmentPos(INVENTORY_SLOT_BAG_0, slot)). Stack count comes from
	// item_instance; that JOIN is the same one the core login query uses.
	// detailSelect appends the tooltip columns consumed by scanDetailTail.
	eqQuery := fmt.Sprintf(`
		SELECT ci.slot, ci.item, ci.item_template, COALESCE(ii.`+"`count`"+`, 1),
		       it.name, it.quality, it.item_level, it.inventory_type, it.display_id,
		       COALESCE(idi.icon, '') AS icon,
		       COALESCE(ii.enchantments, '') AS enchantments,
		       COALESCE(ii.randomPropertyId, 0) AS random_property_id, %s
		FROM character_inventory ci
		JOIN %s.item_template it ON ci.item_template = it.entry
		LEFT JOIN %s.item_display_info idi ON it.display_id = idi.ID
		LEFT JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d
		ORDER BY ci.slot
	`, detailSelect, s.cfg.WorldDB, s.cfg.WorldDB, EquipmentSlotEnd)

	if err := s.scanEquipped(eqQuery, guid, &profile); err != nil {
		return nil, err
	}
	bagQuery := fmt.Sprintf(`
		SELECT ci.slot, ci.item, ci.item_template, COALESCE(ii.`+"`count`"+`, 1),
		       it.name, it.quality, it.container_slots, it.display_id,
		       COALESCE(idi.icon, '') AS icon,
		       COALESCE(ii.randomPropertyId, 0) AS random_property_id, %s
		FROM character_inventory ci
		JOIN %s.item_template it ON ci.item_template = it.entry
		LEFT JOIN %s.item_display_info idi ON it.display_id = idi.ID
		LEFT JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= %d AND ci.slot < %d
	`, detailSelect, s.cfg.WorldDB, s.cfg.WorldDB, BagSlotStart, BagSlotEnd)

	if err := s.scanBags(bagQuery, guid, &profile); err != nil {
		return nil, err
	}

	// Bag contents: ci.bag holds the item-instance GUID of the container
	// (Player::_SaveInventory writes container->GetGUIDLow() into bag). The
	// legacy frontend compared against the visible 19-22 number; that never
	// matches, so this joins back through the container row to resolve the
	// instance GUID for each equipped bag.
	for i := range profile.Bags {
		contentQuery := fmt.Sprintf(`
			SELECT ci.slot, ci.item_template, COALESCE(ii.`+"`count`"+`, 1),
			       it.name, it.quality, it.display_id,
			       COALESCE(idi.icon, '') AS icon,
			       COALESCE(ii.enchantments, '') AS enchantments,
			       COALESCE(ii.randomPropertyId, 0) AS random_property_id, %s
			FROM character_inventory ci
			JOIN character_inventory container
			  ON container.guid = ci.guid
			 AND container.bag = 0
			 AND container.slot = ?
			 AND container.item = ci.bag
			JOIN %s.item_template it ON ci.item_template = it.entry
			LEFT JOIN %s.item_display_info idi ON it.display_id = idi.ID
			LEFT JOIN item_instance ii ON ii.guid = ci.item
			WHERE ci.guid = ? AND ci.bag != 0
			ORDER BY ci.slot
		`, detailSelect, s.cfg.WorldDB, s.cfg.WorldDB)
		rows, err := s.db.Query(contentQuery, profile.Bags[i].Slot, guid)
		if err != nil {
			return nil, err
		}
		for rows.Next() {
			var bi BagItem
			var rawEnchants string
			var randPropID uint32
			var st, sv [10]int32
			var sp [5]uint32
			var tr [5]uint8
			base := []interface{}{&bi.Slot, &bi.ItemTemplate, &bi.Count, &bi.Name, &bi.Quality, &bi.DisplayID, &bi.Icon, &rawEnchants, &randPropID}
			if err := rows.Scan(append(base, detailDests(&bi.Detail, &st, &sv, &sp, &tr)...)...); err != nil {
				rows.Close()
				return nil, err
			}
			foldDetail(&bi.Detail, st, sv, sp, tr)
			s.resolveSpellNames(&bi.Detail)
			s.applyRandomProperties(&bi.Name, &bi.Detail, randPropID)
			bi.Enchantments = s.parseEnchantments(rawEnchants)
			profile.Bags[i].Items = append(profile.Bags[i].Items, bi)
		}
		rows.Close()
		if err := rows.Err(); err != nil {
			return nil, err
		}
		if profile.Bags[i].Items == nil {
			profile.Bags[i].Items = []BagItem{}
		}
	}

	bpQuery := fmt.Sprintf(`
		SELECT ci.slot, ci.item_template, COALESCE(ii.`+"`count`"+`, 1),
		       it.name, it.quality, it.display_id,
		       COALESCE(idi.icon, '') AS icon,
		       COALESCE(ii.enchantments, '') AS enchantments,
		       COALESCE(ii.randomPropertyId, 0) AS random_property_id, %s
		FROM character_inventory ci
		JOIN %s.item_template it ON ci.item_template = it.entry
		LEFT JOIN %s.item_display_info idi ON it.display_id = idi.ID
		LEFT JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= %d AND ci.slot < %d
		ORDER BY ci.slot
	`, detailSelect, s.cfg.WorldDB, s.cfg.WorldDB, BackpackStart, BackpackEnd)

	if err := s.scanBackpack(bpQuery, guid, &profile); err != nil {
		return nil, err
	}

	// 4b. Buyback: vendor-sold items still recoverable (slots 69-80).
	bbQuery := fmt.Sprintf(`
		SELECT ci.slot, ci.item_template, COALESCE(ii.`+"`count`"+`, 1),
		       it.name, it.quality, it.display_id,
		       COALESCE(idi.icon, '') AS icon,
		       COALESCE(ii.enchantments, '') AS enchantments,
		       COALESCE(ii.randomPropertyId, 0) AS random_property_id, %s
		FROM character_inventory ci
		JOIN %s.item_template it ON ci.item_template = it.entry
		LEFT JOIN %s.item_display_info idi ON it.display_id = idi.ID
		LEFT JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= %d AND ci.slot < %d
		ORDER BY ci.slot
	`, detailSelect, s.cfg.WorldDB, s.cfg.WorldDB, BuybackStart, BuybackEnd)

	if err := s.scanBuyback(bbQuery, guid, &profile); err != nil {
		return nil, err
	}
	// 5. Stats: exact values the module writes for online bots first
	// (tortoise_bots_armory_stats), then the core armory row, the core
	// character_stats subset, and finally live characters.health/power +
	// player_levelstats base attributes. Both core tables stay empty while a
	// character is online (core writes them only on logout).
	if !s.loadModuleStats(guid, &profile.Stats) {
		statQuery := `SELECT maxhealth, maxpower1, maxpower2, maxpower3, maxpower4, maxpower5,
			strength, agility, stamina, intellect, spirit, armor,
			resHoly, resFire, resNature, resFrost, resShadow, resArcane,
			dmgModNormal, dmgModHoly, dmgModFire, dmgModNature, dmgModFrost, dmgModShadow, dmgModArcane,
			blockPct, dodgePct, parryPct, meleeCritPct, rangedCritPct,
			attackPower, rangedAttackPower, meleeDamage, rangedDamage,
			meleeWeaponSpeed, rangedWeaponSpeed, castSpeed, meleeHit, rangedHit, spellHit
			FROM character_armory_stats WHERE guid = ?`
		err = s.db.QueryRow(statQuery, guid).Scan(
			&profile.Stats.MaxHealth, &profile.Stats.MaxPower1, &profile.Stats.MaxPower2,
			&profile.Stats.MaxPower3, &profile.Stats.MaxPower4, &profile.Stats.MaxPower5,
			&profile.Stats.Strength, &profile.Stats.Agility, &profile.Stats.Stamina,
			&profile.Stats.Intellect, &profile.Stats.Spirit, &profile.Stats.Armor,
			&profile.Stats.ResHoly, &profile.Stats.ResFire, &profile.Stats.ResNature,
			&profile.Stats.ResFrost, &profile.Stats.ResShadow, &profile.Stats.ResArcane,
			&profile.Stats.SpellDamage, &profile.Stats.SpellDmgHoly, &profile.Stats.SpellDmgFire,
			&profile.Stats.SpellDmgNature, &profile.Stats.SpellDmgFrost, &profile.Stats.SpellDmgShadow,
			&profile.Stats.SpellDmgArcane,
			&profile.Stats.BlockPct, &profile.Stats.DodgePct, &profile.Stats.ParryPct,
			&profile.Stats.MeleeCritPct, &profile.Stats.RangedCritPct,
			&profile.Stats.AttackPower, &profile.Stats.RangedAttackPower,
			&profile.Stats.MeleeDamage, &profile.Stats.RangedDamage,
			&profile.Stats.MeleeSpeed, &profile.Stats.RangedSpeed, &profile.Stats.CastSpeed,
			&profile.Stats.MeleeHit, &profile.Stats.RangedHit, &profile.Stats.SpellHit)
		if err == nil {
			if profile.Stats.HealingPower == 0 {
				profile.Stats.HealingPower = profile.Stats.SpellDamage
			}
			profile.Stats.Source = "armory_stats"
		} else {
			fallbackQuery := `SELECT maxhealth, maxpower1, maxpower2, maxpower3, maxpower4,
				strength, agility, stamina, intellect, spirit, armor,
				resHoly, resFire, resNature, resFrost, resShadow, resArcane,
				blockPct, dodgePct, parryPct, critPct, rangedCritPct,
				attackPower, rangedAttackPower
				FROM character_stats WHERE guid = ?`
			if ferr := s.db.QueryRow(fallbackQuery, guid).Scan(
				&profile.Stats.MaxHealth, &profile.Stats.MaxPower1, &profile.Stats.MaxPower2,
				&profile.Stats.MaxPower3, &profile.Stats.MaxPower4,
				&profile.Stats.Strength, &profile.Stats.Agility, &profile.Stats.Stamina,
				&profile.Stats.Intellect, &profile.Stats.Spirit, &profile.Stats.Armor,
				&profile.Stats.ResHoly, &profile.Stats.ResFire, &profile.Stats.ResNature,
				&profile.Stats.ResFrost, &profile.Stats.ResShadow, &profile.Stats.ResArcane,
				&profile.Stats.BlockPct, &profile.Stats.DodgePct, &profile.Stats.ParryPct,
				&profile.Stats.MeleeCritPct, &profile.Stats.RangedCritPct,
				&profile.Stats.AttackPower, &profile.Stats.RangedAttackPower); ferr == nil {
				profile.Stats.Source = "character_stats"
			} else {
				s.loadLiveStats(guid, &profile.Stats)
			}
		}
	}

	// 6. Spells: persisted rows (character_spell) plus the race/class defaults
	// the core gives every character. LearnDefaultSpells adds those as
	// dependent spells and _SaveSpells skips dependent entries, so a DB-only
	// view would otherwise hide the whole starting spellbook. Description
	// disambiguates shared names (pet-teach Survival Instinct vs talent).
	spellMeta := fmt.Sprintf(`COALESCE(st.name, '') AS name,
		       COALESCE(st.nameSubtext, '') AS name_subtext,
		       COALESCE(st.description, '') AS description,
		       COALESCE(st.effectBasePoints1, 0) + COALESCE(st.effectBaseDice1, 0) AS value1,
		       COALESCE(st.effectBasePoints2, 0) + COALESCE(st.effectBaseDice2, 0) AS value2,
		       COALESCE(st.effectBasePoints3, 0) + COALESCE(st.effectBaseDice3, 0) AS value3,
		       COALESCE(st.effectMiscValue1, 0) AS misc1,
		       COALESCE(st.effectMiscValue2, 0) AS misc2,
		       COALESCE(st.effectMiscValue3, 0) AS misc3,
		       COALESCE(st.effectTriggerSpell1, 0) AS trigger1,
		       COALESCE(st.effectTriggerSpell2, 0) AS trigger2,
		       COALESCE(st.effectTriggerSpell3, 0) AS trigger3,
		       COALESCE(st.durationIndex, 0) AS duration_index,
		       COALESCE(st.rangeIndex, 0) AS range_index,
		       COALESCE(st.castingTimeIndex, 0) AS casting_time_index,
		       COALESCE(st.school, 0) AS school,
		       COALESCE(st.spellIconId, 0) AS spell_icon_id,
		       COALESCE(si.Name, '') AS icon,
		       (COALESCE(st.attributes, 0) & %d) <> 0 AS is_passive`, spellAttrPassive)

	classSpells := s.classSpellsFor(profile.Summary.Class)
	startIds, err := s.startingSpellIds(profile.Summary.Race, profile.Summary.Class, classSpells)
	if err != nil {
		return nil, err
	}

	spellQuery := fmt.Sprintf(`
		SELECT * FROM (
			SELECT cs.spell AS spell, cs.active AS active, cs.disabled AS disabled, 0 AS is_starting, %s
			FROM character_spell cs
			LEFT JOIN %s.spell_template st ON st.entry = cs.spell
			LEFT JOIN %s.spellicon si ON si.ID = st.spellIconId
			WHERE cs.guid = ?
	`, spellMeta, s.cfg.WorldDB, s.cfg.WorldDB)
	args := []interface{}{guid}
	if len(startIds) > 0 {
		marks := strings.Repeat(", ?", len(startIds)-1)
		spellQuery += fmt.Sprintf(`
			UNION ALL
			SELECT st.entry, 1, 0, 1, %s
			FROM %s.spell_template st
			LEFT JOIN %s.spellicon si ON si.ID = st.spellIconId
			WHERE st.entry IN (?%s)
			  AND NOT EXISTS (SELECT 1 FROM character_spell known WHERE known.guid = ? AND known.spell = st.entry)
		`, spellMeta, s.cfg.WorldDB, s.cfg.WorldDB, marks)
		for _, id := range startIds {
			args = append(args, id)
		}
		args = append(args, guid)
	}
	spellQuery += `
		) spellbook ORDER BY spellbook.name, spellbook.spell`

	spRows, err := s.db.Query(spellQuery, args...)
	if err != nil {
		return nil, err
	}
	defer spRows.Close()
	profile.Spells = []SpellEntry{}
	for spRows.Next() {
		var se SpellEntry
		var starting, passive uint8
		var v1, v2, v3 int32
		var m1, m2, m3 int32
		var t1, t2, t3 uint32
		var durIdx, rngIdx, castIdx uint32
		if err := spRows.Scan(&se.Spell, &se.Active, &se.Disabled, &starting, &se.Name, &se.Subtext, &se.Description,
			&v1, &v2, &v3, &m1, &m2, &m3, &t1, &t2, &t3, &durIdx, &rngIdx, &castIdx,
			&se.School, &se.IconID, &se.Icon, &passive); err != nil {
			return nil, err
		}
		se.ClassSpell = classSpells[se.Spell]
		se.Passive = passive != 0
		if starting != 0 {
			se.Origin = SpellOriginStarting
		}
		if se.Icon == "" && se.IconID != 0 {
			se.Icon = s.spellIconByID(se.IconID)
		}
		se.Values = []int32{v1, v2, v3}
		se.Misc = []int32{m1, m2, m3}
		se.Triggers = []uint32{t1, t2, t3}
		se.DurationMs, se.RangeYd, se.CastMs = s.spellTiming(durIdx, rngIdx, castIdx)
		profile.Spells = append(profile.Spells, se)
	}
	if err := spRows.Err(); err != nil {
		return nil, err
	}

	// 7. Skills: every row; the frontend filters weapon/armor categories.
	// Dedupe defensively: (guid, skill) is unique, but MAX() keeps the
	// highest value if a duplicate ever slips in, so the Skills tab never
	// shows the same skill twice.
	skillQuery := "SELECT skill, MAX(value), MAX(`max`) FROM character_skills WHERE guid = ? GROUP BY skill ORDER BY skill"
	skRows, err := s.db.Query(skillQuery, guid)
	if err != nil {
		return nil, err
	}
	defer skRows.Close()
	profile.Skills = []SkillEntry{}
	for skRows.Next() {
		var se SkillEntry
		if err := skRows.Scan(&se.Skill, &se.Value, &se.Max); err != nil {
			return nil, err
		}
		profile.Skills = append(profile.Skills, se)
	}
	if err := skRows.Err(); err != nil {
		return nil, err
	}
	// 8. Talents: resolved from the operator's world DB talent/talenttab
	// mirror tables when present. The core itself loads Talent/TalentTab from
	// the operator's own DBC files at startup, so these mirrors are often
	// empty; an empty Talents list then means "no data", not "no talents".
	talents, err := s.loadTalents(guid)
	if err != nil {
		return nil, err
	}
	profile.Talents = talents

	return &profile, nil
}

func (s *Service) scanEquipped(query string, guid uint32, profile *BotProfile) error {
	rows, err := s.db.Query(query, guid)
	if err != nil {
		return err
	}
	defer rows.Close()
	profile.Equipment = []EquippedItem{}
	for rows.Next() {
		var eq EquippedItem
		var itemGUID uint32
		var rawEnchants string
		var randPropID uint32
		var st, sv [10]int32
		var sp [5]uint32
		var tr [5]uint8
		base := []interface{}{&eq.Slot, &itemGUID, &eq.ItemTemplate, &eq.Count, &eq.Name, &eq.Quality, &eq.ItemLevel, &eq.InventoryType, &eq.DisplayID, &eq.Icon, &rawEnchants, &randPropID}
		if err := rows.Scan(append(base, detailDests(&eq.Detail, &st, &sv, &sp, &tr)...)...); err != nil {
			return err
		}
		foldDetail(&eq.Detail, st, sv, sp, tr)
		s.resolveSpellNames(&eq.Detail)
		s.applyRandomProperties(&eq.Name, &eq.Detail, randPropID)
		eq.Enchantments = s.parseEnchantments(rawEnchants)
		profile.Equipment = append(profile.Equipment, eq)
	}
	return rows.Err()
}

func (s *Service) scanBags(query string, guid uint32, profile *BotProfile) error {
	rows, err := s.db.Query(query, guid)
	if err != nil {
		return err
	}
	defer rows.Close()
	profile.Bags = []BagContainer{}
	for rows.Next() {
		var b BagContainer
		var randPropID uint32
		var st, sv [10]int32
		var sp [5]uint32
		var tr [5]uint8
		base := []interface{}{&b.Slot, &b.Item, &b.ItemTemplate, &b.Count, &b.Name, &b.Quality, &b.ContainerSlots, &b.DisplayID, &b.Icon, &randPropID}
		if err := rows.Scan(append(base, detailDests(&b.Detail, &st, &sv, &sp, &tr)...)...); err != nil {
			return err
		}
		foldDetail(&b.Detail, st, sv, sp, tr)
		s.resolveSpellNames(&b.Detail)
		s.applyRandomProperties(&b.Name, &b.Detail, randPropID)
		b.Items = []BagItem{}
		profile.Bags = append(profile.Bags, b)
	}
	return rows.Err()
}

func (s *Service) scanBackpack(query string, guid uint32, profile *BotProfile) error {
	rows, err := s.db.Query(query, guid)
	if err != nil {
		return err
	}
	defer rows.Close()
	profile.Backpack = []BagItem{}
	for rows.Next() {
		var bi BagItem
		var rawEnchants string
		var randPropID uint32
		var st, sv [10]int32
		var sp [5]uint32
		var tr [5]uint8
		base := []interface{}{&bi.Slot, &bi.ItemTemplate, &bi.Count, &bi.Name, &bi.Quality, &bi.DisplayID, &bi.Icon, &rawEnchants, &randPropID}
		if err := rows.Scan(append(base, detailDests(&bi.Detail, &st, &sv, &sp, &tr)...)...); err != nil {
			return err
		}
		foldDetail(&bi.Detail, st, sv, sp, tr)
		s.resolveSpellNames(&bi.Detail)
		s.applyRandomProperties(&bi.Name, &bi.Detail, randPropID)
		bi.Enchantments = s.parseEnchantments(rawEnchants)
		profile.Backpack = append(profile.Backpack, bi)
	}
	return rows.Err()
}

func (s *Service) scanBuyback(query string, guid uint32, profile *BotProfile) error {
	rows, err := s.db.Query(query, guid)
	if err != nil {
		return err
	}
	defer rows.Close()
	profile.Buyback = []BagItem{}
	for rows.Next() {
		var bi BagItem
		var rawEnchants string
		var randPropID uint32
		var st, sv [10]int32
		var sp [5]uint32
		var tr [5]uint8
		base := []interface{}{&bi.Slot, &bi.ItemTemplate, &bi.Count, &bi.Name, &bi.Quality, &bi.DisplayID, &bi.Icon, &rawEnchants, &randPropID}
		if err := rows.Scan(append(base, detailDests(&bi.Detail, &st, &sv, &sp, &tr)...)...); err != nil {
			return err
		}
		foldDetail(&bi.Detail, st, sv, sp, tr)
		s.resolveSpellNames(&bi.Detail)
		s.applyRandomProperties(&bi.Name, &bi.Detail, randPropID)
		bi.Enchantments = s.parseEnchantments(rawEnchants)
		profile.Buyback = append(profile.Buyback, bi)
	}
	return rows.Err()
}

// loadModuleStats reads the exact stats the module's ObservabilityEmitter
// copies from the live Player for online bots (enchants, talents, buffs and
// racials included). Debuffs can push signed columns below zero; they are
// clamped for the unsigned model. Returns false when there is no such row.
func (s *Service) loadModuleStats(guid uint32, st *CharacterStats) bool {
	var meleeMin, meleeMax, rangedMin, rangedMax float64
	err := s.db.QueryRow(`SELECT maxhealth, maxpower1, maxpower2, maxpower3, maxpower4, maxpower5,
		strength, agility, stamina, intellect, spirit, GREATEST(armor, 0),
		GREATEST(resHoly, 0), GREATEST(resFire, 0), GREATEST(resNature, 0), GREATEST(resFrost, 0), GREATEST(resShadow, 0), GREATEST(resArcane, 0),
		GREATEST(spellDamage, 0), GREATEST(spellDmgHoly, 0), GREATEST(spellDmgFire, 0), GREATEST(spellDmgNature, 0), GREATEST(spellDmgFrost, 0), GREATEST(spellDmgShadow, 0), GREATEST(spellDmgArcane, 0),
		GREATEST(healingPower, 0), blockPct, dodgePct, parryPct, meleeCritPct, rangedCritPct, spellCritPct,
		attackPower, rangedAttackPower, meleeDmgMin, meleeDmgMax, rangedDmgMin, rangedDmgMax,
		meleeSpeed, rangedSpeed, meleeHit, rangedHit, spellHit, GREATEST(manaRegen, 0)
		FROM tortoise_bots_armory_stats WHERE guid = ?`, guid).Scan(
		&st.MaxHealth, &st.MaxPower1, &st.MaxPower2, &st.MaxPower3, &st.MaxPower4, &st.MaxPower5,
		&st.Strength, &st.Agility, &st.Stamina, &st.Intellect, &st.Spirit, &st.Armor,
		&st.ResHoly, &st.ResFire, &st.ResNature, &st.ResFrost, &st.ResShadow, &st.ResArcane,
		&st.SpellDamage, &st.SpellDmgHoly, &st.SpellDmgFire, &st.SpellDmgNature, &st.SpellDmgFrost,
		&st.SpellDmgShadow, &st.SpellDmgArcane,
		&st.HealingPower, &st.BlockPct, &st.DodgePct, &st.ParryPct,
		&st.MeleeCritPct, &st.RangedCritPct, &st.SpellCritPct,
		&st.AttackPower, &st.RangedAttackPower, &meleeMin, &meleeMax, &rangedMin, &rangedMax,
		&st.MeleeSpeed, &st.RangedSpeed, &st.MeleeHit, &st.RangedHit, &st.SpellHit, &st.ManaRegen)
	if err != nil {
		return false
	}
	if meleeMax > 0 {
		st.MeleeDamage = fmt.Sprintf("%.0f – %.0f", meleeMin, meleeMax)
	}
	if rangedMax > 0 {
		st.RangedDamage = fmt.Sprintf("%.0f – %.0f", rangedMin, rangedMax)
	}
	st.Source = "module_stats"
	return true
}

// loadLiveStats fills stats from live characters.health/power plus base
// attributes from player_levelstats. Both snapshot tables stay empty while
// a bot is online (core writes them only on logout), so without this every
// online bot renders 0/0%. characters.health can be stale (1 HP corpse row),
// so prefer player_classlevelstats.basehp and only fall back to the live row.
func MathRound(v float64) float64 {
	if v < 0 {
		return float64(int(v - 0.5))
	}
	return float64(int(v + 0.5))
}

// loadLiveStats fills stats from live characters.health/power, base attributes,
// and equipped gear stat bonuses (Str, Agi, Sta, Int, Spi, Armor, Resists).
func (s *Service) loadLiveStats(guid uint32, st *CharacterStats) {
	var race, class, level uint32
	var liveHP, p1, p2, p3, p4, p5 uint32
	if err := s.db.QueryRow(`SELECT race, class, level, health, power1, power2, power3, power4, power5
		FROM characters WHERE guid = ?`, guid).Scan(
		&race, &class, &level, &liveHP, &p1, &p2, &p3, &p4, &p5); err != nil {
		st.Source = "none"
		return
	}
	st.MaxPower1, st.MaxPower2, st.MaxPower3, st.MaxPower4, st.MaxPower5 = p1, p2, p3, p4, p5
	_ = s.db.QueryRow(fmt.Sprintf(`SELECT str, agi, sta, inte, spi
		FROM %s.player_levelstats WHERE race = ? AND class = ? AND level = ?`,
		s.cfg.WorldDB), race, class, level).Scan(
		&st.Strength, &st.Agility, &st.Stamina, &st.Intellect, &st.Spirit)
	_ = s.db.QueryRow(fmt.Sprintf(`SELECT basehp, basemana FROM %s.player_classlevelstats
		WHERE class = ? AND level = ?`, s.cfg.WorldDB), class, level).Scan(&st.MaxHealth, &st.MaxPower1)
	if st.MaxHealth == 0 {
		st.MaxHealth = liveHP
	}

	var totalArmor, resHoly, resFire, resNature, resFrost, resShadow, resArcane uint32
	var bonusStr, bonusAgi, bonusSta, bonusInt, bonusSpi uint32
	var mainDmgMin, mainDmgMax, mainDelay float64
	var rangedDmgMin, rangedDmgMax, rangedDelay float64
	var hasShield bool
	var itemSpells []uint32

	q := fmt.Sprintf(`SELECT ci.slot, it.armor, it.holy_res, it.fire_res, it.nature_res, it.frost_res, it.shadow_res, it.arcane_res,
		it.stat_type1, it.stat_value1, it.stat_type2, it.stat_value2,
		it.stat_type3, it.stat_value3, it.stat_type4, it.stat_value4,
		it.stat_type5, it.stat_value5, it.stat_type6, it.stat_value6,
		it.stat_type7, it.stat_value7, it.stat_type8, it.stat_value8,
		it.stat_type9, it.stat_value9, it.stat_type10, it.stat_value10,
		it.delay, it.dmg_min1, it.dmg_max1, it.subclass, it.inventory_type,
		it.spellid_1, it.spellid_2, it.spellid_3, it.spellid_4, it.spellid_5
		FROM character_inventory ci
		JOIN %s.item_template it ON it.entry = ci.item_template
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d`,
		s.cfg.WorldDB, EquipmentSlotEnd)

	rows, err := s.db.Query(q, guid)
	if err == nil {
		defer rows.Close()
		for rows.Next() {
			var slot uint32
			var armor, rHoly, rFire, rNat, rFrst, rShad, rArc uint32
			var st1, st2, st3, st4, st5, st6, st7, st8, st9, st10 int32
			var sv1, sv2, sv3, sv4, sv5, sv6, sv7, sv8, sv9, sv10 int32
			var delay, dmgMin, dmgMax uint32
			var subClass, invType uint32
			var sp1, sp2, sp3, sp4, sp5 uint32

			if err := rows.Scan(&slot, &armor, &rHoly, &rFire, &rNat, &rFrst, &rShad, &rArc,
				&st1, &sv1, &st2, &sv2, &st3, &sv3, &st4, &sv4, &st5, &sv5,
				&st6, &sv6, &st7, &sv7, &st8, &sv8, &st9, &sv9, &st10, &sv10,
				&delay, &dmgMin, &dmgMax, &subClass, &invType,
				&sp1, &sp2, &sp3, &sp4, &sp5); err == nil {

				totalArmor += armor
				resHoly += rHoly; resFire += rFire; resNature += rNat; resFrost += rFrst; resShadow += rShad; resArcane += rArc
				if slot == 14 && (subClass == 6 || invType == 14) {
					hasShield = true
				}
				if slot == 15 {
					mainDmgMin = float64(dmgMin)
					mainDmgMax = float64(dmgMax)
					mainDelay = float64(delay)
				} else if slot == 17 {
					rangedDmgMin = float64(dmgMin)
					rangedDmgMax = float64(dmgMax)
					rangedDelay = float64(delay)
				}

				stats := [10][2]int32{{st1, sv1}, {st2, sv2}, {st3, sv3}, {st4, sv4}, {st5, sv5}, {st6, sv6}, {st7, sv7}, {st8, sv8}, {st9, sv9}, {st10, sv10}}
				for _, pair := range stats {
					if pair[1] <= 0 {
						continue
					}
					switch pair[0] {
					case 3: bonusAgi += uint32(pair[1])
					case 4: bonusStr += uint32(pair[1])
					case 5: bonusInt += uint32(pair[1])
					case 6: bonusSpi += uint32(pair[1])
					case 7: bonusSta += uint32(pair[1])
					}
				}
				for _, sp := range []uint32{sp1, sp2, sp3, sp4, sp5} {
					if sp > 0 {
						itemSpells = append(itemSpells, sp)
					}
				}
			}
		}
	}

	st.Strength += float64(bonusStr)
	st.Agility += float64(bonusAgi)
	st.Stamina += float64(bonusSta)
	st.Intellect += float64(bonusInt)
	st.Spirit += float64(bonusSpi)
	st.Armor = totalArmor + uint32(st.Agility*2.0)
	st.ResHoly = resHoly; st.ResFire = resFire; st.ResNature = resNature; st.ResFrost = resFrost; st.ResShadow = resShadow; st.ResArcane = resArcane

	var bonusSpellCrit, bonusSpellHit, bonusHit, bonusCrit, bonusAP, bonusRAP float64
	if len(itemSpells) > 0 {
		marks := strings.Repeat(", ?", len(itemSpells)-1)
		args := make([]interface{}, len(itemSpells))
		for i, id := range itemSpells {
			args[i] = id
		}
		qSpells := fmt.Sprintf(`SELECT effectApplyAuraName1, effectMiscValue1, effectBasePoints1,
			effectApplyAuraName2, effectMiscValue2, effectBasePoints2,
			effectApplyAuraName3, effectMiscValue3, effectBasePoints3
			FROM %s.spell_template WHERE entry IN (?%s)`, s.cfg.WorldDB, marks)
		if srows, err := s.db.Query(qSpells, args...); err == nil {
			for srows.Next() {
				var ea1, ea2, ea3 uint32
				var em1, em2, em3 uint32
				var eb1, eb2, eb3 int32
				if err := srows.Scan(&ea1, &em1, &eb1, &ea2, &em2, &eb2, &ea3, &em3, &eb3); err == nil {
					effects := [][3]int64{{int64(ea1), int64(em1), int64(eb1)}, {int64(ea2), int64(em2), int64(eb2)}, {int64(ea3), int64(em3), int64(eb3)}}
					for _, eff := range effects {
						aura, mask, bp := eff[0], eff[1], eff[2]
						pts := uint32(bp + 1)
						switch aura {
						case 13: // SPELL_AURA_MOD_DAMAGE_DONE
							if mask == 0 || (mask&126) == 126 || mask == 126 {
								st.SpellDamage += pts
							} else {
								if mask&2 != 0 { st.SpellDmgHoly += pts }
								if mask&4 != 0 { st.SpellDmgFire += pts }
								if mask&8 != 0 { st.SpellDmgNature += pts }
								if mask&16 != 0 { st.SpellDmgFrost += pts }
								if mask&32 != 0 { st.SpellDmgShadow += pts }
								if mask&64 != 0 { st.SpellDmgArcane += pts }
							}
						case 135: // SPELL_AURA_MOD_HEALING_DONE
							st.HealingPower += pts
						case 71: // SPELL_AURA_MOD_SPELL_CRIT_CHANCE
							bonusSpellCrit += float64(pts)
						case 55: // SPELL_AURA_MOD_SPELL_HIT_CHANCE
							bonusSpellHit += float64(pts)
						case 85: // SPELL_AURA_MOD_POWER_REGEN (MP5)
							st.ManaRegen += pts
						case 99: // SPELL_AURA_MOD_ATTACK_POWER
							bonusAP += float64(pts)
						case 124: // SPELL_AURA_MOD_RANGED_ATTACK_POWER
							bonusRAP += float64(pts)
						case 54: // SPELL_AURA_MOD_HIT_CHANCE
							bonusHit += float64(pts)
						case 52: // SPELL_AURA_MOD_CRIT_PERCENT
							bonusCrit += float64(pts)
						}
					}
				}
			}
			srows.Close()
		}
	}
	if st.HealingPower < st.SpellDamage {
		st.HealingPower = st.SpellDamage
	}
	st.SpellHit += bonusSpellHit

	var baseSpellCrit float64
	switch class {
	case 8:
		baseSpellCrit = 0.91 + st.Intellect/59.5
	case 5:
		baseSpellCrit = 1.24 + st.Intellect/59.2
	case 9:
		baseSpellCrit = 1.70 + st.Intellect/60.6
	case 11:
		baseSpellCrit = 1.85 + st.Intellect/60.0
	case 7:
		baseSpellCrit = 2.20 + st.Intellect/59.5
	case 2:
		baseSpellCrit = 0.70 + st.Intellect/54.0
	default:
		baseSpellCrit = st.Intellect / 60.0
	}
	st.SpellCritPct = MathRound((baseSpellCrit+bonusSpellCrit)*100) / 100

	var ap, rap float64
	lvl := float64(level)
	str := st.Strength
	agi := st.Agility

	switch class {
	case 1, 2:
		if lvl*3.0+str*2.0 > 20.0 { ap = lvl*3.0 + str*2.0 - 20.0 }
	case 3:
		if lvl*2.0+agi*2.0 > 20.0 { ap = lvl*2.0 + agi*2.0 - 20.0 }
		if lvl*2.0+agi*2.0 > 10.0 { rap = lvl*2.0 + agi*2.0 - 10.0 }
	case 4:
		if lvl*2.0+str+agi*2.0 > 20.0 { ap = lvl*2.0 + str + agi*2.0 - 20.0 }
		if lvl*2.0+agi*2.0 > 20.0 { rap = lvl*2.0 + agi*2.0 - 20.0 }
	case 7:
		if lvl*2.0+str*2.0 > 20.0 { ap = lvl*2.0 + str*2.0 - 20.0 }
	default:
		if str*2.0 > 20.0 { ap = str*2.0 - 20.0 }
	}

	st.AttackPower = ap + bonusAP
	st.RangedAttackPower = rap + bonusRAP
	st.MeleeHit += bonusHit
	st.RangedHit += bonusHit

	critBase := 5.0 + agi/20.0 + bonusCrit
	dodgeBase := 3.0 + agi/20.0
	st.MeleeCritPct = MathRound(critBase*100) / 100
	st.RangedCritPct = MathRound(critBase*100) / 100
	st.DodgePct = MathRound(dodgeBase*100) / 100
	if class == 1 || class == 2 || class == 4 || class == 7 {
		st.ParryPct = 5.0
	}
	if hasShield {
		st.BlockPct = 5.0
	}

	if mainDmgMax > 0 {
		apBonus := st.AttackPower / 14.0 * (mainDelay / 1000.0)
		dMin := MathRound(mainDmgMin + apBonus)
		dMax := MathRound(mainDmgMax + apBonus)
		st.MeleeDamage = fmt.Sprintf("%.0f – %.0f", dMin, dMax)
		st.MeleeSpeed = mainDelay / 1000.0
	}
	if rangedDmgMax > 0 {
		rapBonus := st.RangedAttackPower / 14.0 * (rangedDelay / 1000.0)
		dMin := MathRound(rangedDmgMin + rapBonus)
		dMax := MathRound(rangedDmgMax + rapBonus)
		st.RangedDamage = fmt.Sprintf("%.0f – %.0f", dMin, dMax)
		st.RangedSpeed = rangedDelay / 1000.0
	}

	st.Source = "live"
}

// loadTalents resolves allocated talent ranks from known spells. DBC files
// from the operator (same dir mangosd reads) win when configured; the world
// talent/talenttab mirrors are the fallback. A talent node matches when any
// of its rank spells appears in character_spell; the rank is the highest
// matching position (Player::LearnTalent unlearns other ranks).
func (s *Service) loadTalents(guid uint32) ([]TalentTree, error) {
	known := map[uint32]bool{}
	spRows, err := s.db.Query("SELECT spell FROM character_spell WHERE guid = ?", guid)
	if err != nil {
		return nil, err
	}
	for spRows.Next() {
		var spell uint32
		if err := spRows.Scan(&spell); err != nil {
			spRows.Close()
			return nil, err
		}
		known[spell] = true
	}
	if err := spRows.Err(); err != nil {
		return nil, err
	}

	var classID uint32
	if err := s.db.QueryRow("SELECT class FROM characters WHERE guid = ?", guid).Scan(&classID); err != nil {
		return nil, err
	}
	if s.cfg.DBCDir != "" {
		if trees, err := s.talentsFromDBC(guid, classID, known); err == nil {
			return trees, nil
		}
		// Fall through to SQL mirrors on any DBC read error.
	}
	talentQuery := fmt.Sprintf(`
		SELECT t.id, t.talentTabId, t.tierId, t.columnIndex,
		       t.spellRank1, t.spellRank2, t.spellRank3, t.spellRank4, t.spellRank5,
		       tt.Name1, tt.orderIndex,
		       COALESCE(st.name, ''), COALESCE(si.Name, ''), COALESCE(st.spellIconId, 0)
		FROM %s.talent t
		JOIN %s.talenttab tt ON tt.id = t.talentTabId
		LEFT JOIN %s.spell_template st ON st.entry = t.spellRank1
		LEFT JOIN %s.spellicon si ON si.ID = st.spellIconId
		WHERE (tt.classMask & (1 << (? - 1))) != 0
		ORDER BY tt.orderIndex, t.tierId, t.columnIndex, t.id
	`, s.cfg.WorldDB, s.cfg.WorldDB, s.cfg.WorldDB, s.cfg.WorldDB)

	rows, err := s.db.Query(talentQuery, classID)
	if err != nil {
		// Mirror tables missing or empty: not an error, just no talent data.
		if isMissingTable(err) {
			return []TalentTree{}, nil
		}
		return nil, err
	}
	defer rows.Close()

	trees := []TalentTree{}
	byTab := map[uint32]int{}
	for rows.Next() {
		var (
			talentID, tabID, row, col uint32
			r1, r2, r3, r4, r5        uint32
			tabName                   sql.NullString
			page                      uint32
			spellName, spellIcon      sql.NullString
			iconID                    uint32
		)
		if err := rows.Scan(&talentID, &tabID, &row, &col,
			&r1, &r2, &r3, &r4, &r5, &tabName, &page, &spellName, &spellIcon, &iconID); err != nil {
			return nil, err
		}
		ranks := []uint32{r1, r2, r3, r4, r5}
		maxRank := uint32(0)
		activeSpell := uint32(0)
		activeRank := uint32(0)
		activeName, activeIcon := "", ""
		for i, rs := range ranks {
			if rs == 0 {
				continue
			}
			maxRank = uint32(i + 1)
			if known[rs] {
				activeRank = uint32(i + 1)
				activeSpell = rs
				if spellName.Valid {
					activeName = spellName.String
				}
				if spellIcon.Valid {
					activeIcon = spellIcon.String
				}
			}
		}
		if activeName == "" && spellName.Valid {
			activeName = spellName.String
		}
		if activeIcon == "" && spellIcon.Valid {
			activeIcon = spellIcon.String
		}
		if activeIcon == "" && iconID != 0 {
			activeIcon = s.spellIconByID(iconID)
		}
		if maxRank == 0 {
			continue
		}
		idx, ok := byTab[tabID]
		if !ok {
			name := fmt.Sprintf("Tree %d", tabID)
			if tabName.Valid && tabName.String != "" {
				name = tabName.String
			}
			trees = append(trees, TalentTree{TabID: tabID, Name: name, Page: page, Talents: []TalentNode{}})
			idx = len(trees) - 1
			byTab[tabID] = idx
		}
		trees[idx].Talents = append(trees[idx].Talents, TalentNode{
			TalentID: talentID, Row: row, Col: col,
			Rank: activeRank, MaxRank: maxRank, SpellID: activeSpell, Name: activeName, Icon: activeIcon,
		})
		if activeRank > 0 {
			trees[idx].Points += activeRank
		}
	}
	if err := rows.Err(); err != nil {
		if isMissingTable(err) {
			return []TalentTree{}, nil
		}
		return nil, err
	}
	if trees == nil {
		trees = []TalentTree{}
	}
	return trees, nil
}

func isMissingTable(err error) bool {
	if err == nil {
		return false
	}
	msg := err.Error()
	return strings.Contains(msg, "doesn't exist") || strings.Contains(msg, "does not exist") ||
		strings.Contains(msg, "Table") && strings.Contains(msg, "unknown")
}
