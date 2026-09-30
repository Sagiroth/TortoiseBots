package armory

// Talent layout from the operator's own DBC files (the same files mangosd
// reads via DataDir). No DBC data is shipped: paths resolve at runtime from
// Config.DBCDir, and everything degrades to the world talent/talenttab
// mirror tables when the dir is unset or the files are absent.

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
)

type dbcTalent struct {
	id    uint32
	tabID uint32
	row   uint32
	col   uint32
	ranks []uint32
}

type dbcTalentTab struct {
	id        uint32
	name      string
	classMask uint32
	page      uint32
}

type dbcCache struct {
	talents []dbcTalent
	tabs    []dbcTalentTab
	err     error
}

var (
	dbcMu           sync.Mutex
	dbcByDir        = map[string]*dbcCache{}
	dbcSpellIcon    = map[string]map[uint32]string{}
	dbcEnchantments = map[string]map[uint32]string{}
	dbcDurations    = map[string]map[uint32]int32{}
	dbcRandomProps  = map[string]map[uint32]DBCRandomProperty{}
)

func loadDBCSpellIcons(dir string) (map[uint32]string, error) {
	dbcMu.Lock()
	defer dbcMu.Unlock()
	if icons, ok := dbcSpellIcon[dir]; ok {
		return icons, nil
	}
	raw, err := os.ReadFile(filepath.Join(dir, "SpellIcon.dbc"))
	if err != nil {
		return nil, err
	}
	if len(raw) < 20 {
		return nil, fmt.Errorf("SpellIcon.dbc too short")
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	if recSize < 8 || uint32(len(raw)) < 20+nRec*recSize {
		return nil, fmt.Errorf("SpellIcon.dbc truncated")
	}
	strBlock := raw[20+nRec*recSize:]
	icons := make(map[uint32]string, nRec)
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		id := binary.LittleEndian.Uint32(raw[off : off+4])
		strOff := binary.LittleEndian.Uint32(raw[off+4 : off+8])
		fullStr := dbcString(strBlock, strOff)
		base := filepath.Base(strings.ReplaceAll(fullStr, "\\", "/"))
		base = strings.TrimSuffix(base, filepath.Ext(base))
		base = strings.ToLower(strings.TrimSpace(base))
		if base != "" {
			icons[id] = base
		}
	}
	dbcSpellIcon[dir] = icons
	return icons, nil
}

func (s *Service) spellIconByID(id uint32) string {
	if id == 0 || s.cfg.DBCDir == "" {
		return ""
	}
	icons, err := loadDBCSpellIcons(s.cfg.DBCDir)
	if err != nil {
		return ""
	}
	return icons[id]
}

func loadDBCTalents(dir string) ([]dbcTalent, []dbcTalentTab, error) {
	dbcMu.Lock()
	defer dbcMu.Unlock()
	if c, ok := dbcByDir[dir]; ok {
		return c.talents, c.tabs, c.err
	}
	c := &dbcCache{}
	c.talents, c.tabs, c.err = readTalentDBC(dir)
	dbcByDir[dir] = c
	return c.talents, c.tabs, c.err
}

func readDBCRecords(path string) (records [][]uint32, strings []byte, err error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, nil, err
	}
	if len(raw) < 20 {
		return nil, nil, fmt.Errorf("dbc %s too short", path)
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	nFields := binary.LittleEndian.Uint32(raw[8:12])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	if uint32(len(raw)) < 20+nRec*recSize {
		return nil, nil, fmt.Errorf("dbc %s truncated", path)
	}
	// Talent.dbc: 21 int32 fields (id, tab, row, col, 5 ranks, padding, deps...).
	// TalentTab.dbc: 15 int32 fields (id, name offsets, masks, page).
	if nFields < 15 {
		return nil, nil, fmt.Errorf("dbc %s unexpected field count %d", path, nFields)
	}
	records = make([][]uint32, 0, nRec)
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		row := make([]uint32, nFields)
		for f := uint32(0); f < nFields; f++ {
			row[f] = binary.LittleEndian.Uint32(raw[off+f*4 : off+f*4+4])
		}
		records = append(records, row)
	}
	return records, raw[20+nRec*recSize:], nil
}

func dbcString(block []byte, off uint32) string {
	if int(off) >= len(block) {
		return ""
	}
	end := int(off)
	for end < len(block) && block[end] != 0 {
		end++
	}
	return string(block[off:end])
}

// dbcFirstInt reads one int32 field (default column 1) of a single-ID DBC row.
func dbcFirstInt(dir, file string, id uint32) (int32, bool) {
	raw, err := os.ReadFile(filepath.Join(dir, file))
	if err != nil || len(raw) < 20 {
		return 0, false
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		if int(off)+8 > len(raw) {
			break
		}
		if binary.LittleEndian.Uint32(raw[off:off+4]) == id {
			return int32(binary.LittleEndian.Uint32(raw[off+4 : off+8])), true
		}
	}
	return 0, false
}

// spellTiming resolves $d (duration ms), $r handled via range index miles
// conversion below, $c (cast ms) from the operator DBCs. Missing files or
// rows yield zeros; the frontend then hides those tokens.
func (s *Service) spellTiming(durIdx, rngIdx, castIdx uint32) (durMs, rngYd, castMs int32) {
	if s.cfg.DBCDir != "" {
		if v, ok := dbcFirstInt(s.cfg.DBCDir, "SpellDuration.dbc", durIdx); ok {
			durMs = v
		}
		if v, ok := dbcFirstInt(s.cfg.DBCDir, "SpellCastTimes.dbc", castIdx); ok {
			castMs = v
		}
		if rngIdx != 0 {
			if raw, err := os.ReadFile(filepath.Join(s.cfg.DBCDir, "SpellRange.dbc")); err == nil && len(raw) >= 20 {
				nRec := binary.LittleEndian.Uint32(raw[4:8])
				recSize := binary.LittleEndian.Uint32(raw[12:16])
				for i := uint32(0); i < nRec; i++ {
					off := 20 + i*recSize
					if int(off)+12 > len(raw) {
						break
					}
					if binary.LittleEndian.Uint32(raw[off:off+4]) == rngIdx {
						// maxRange is the 3rd field (offset +8): float yards.
						rngYd = int32(math.Float32frombits(binary.LittleEndian.Uint32(raw[off+8 : off+12])))
						break
					}
				}
			}
		}
	}
	return durMs, rngYd, castMs
}
func readTalentDBC(dir string) ([]dbcTalent, []dbcTalentTab, error) {
	tRecs, _, err := readDBCRecords(filepath.Join(dir, "Talent.dbc"))
	if err != nil {
		return nil, nil, err
	}
	tabRecs, tabStr, err := readDBCRecords(filepath.Join(dir, "TalentTab.dbc"))
	if err != nil {
		return nil, nil, err
	}
	tabs := make([]dbcTalentTab, 0, len(tabRecs))
	for _, r := range tabRecs {
		// Layout per core DBCStructure.h TalentTabEntry: id, name[8
		// offsets], nameFlags(9), spellIcon(10), raceMask(11), classMask(12),
		// tabpage/orderIndex(13), internalname(14). Page orders the three
		// trees of a class left-to-right like the 1.12 talent frame.
		if len(r) < 14 {
			continue
		}
		tabs = append(tabs, dbcTalentTab{
			id:        r[0],
			name:      dbcString(tabStr, r[1]),
			classMask: r[12],
			page:      r[13],
		})
	}
	talents := make([]dbcTalent, 0, len(tRecs))
	for _, r := range tRecs {
		// Layout per TalentEntry: id, tab, row, col, rankID[5], padding,
		// dependsOn, ..., dependsOnRank, ..., dependsOnSpell.
		if len(r) < 9 {
			continue
		}
		var ranks []uint32
		for k := 4; k < 9 && k < len(r); k++ {
			if r[k] != 0 {
				ranks = append(ranks, r[k])
			} else {
				break
			}
		}
		if len(ranks) == 0 {
			continue
		}
		talents = append(talents, dbcTalent{
			id: r[0], tabID: r[1], row: r[2], col: r[3], ranks: ranks,
		})
	}
	return talents, tabs, nil
}

// talentsFromDBC builds class talent trees from the operator's DBC files.
// Only ranks the bot actually knows (character_spell) count as allocated.
func (s *Service) talentsFromDBC(guid uint32, classID uint32, known map[uint32]bool) ([]TalentTree, error) {
	talents, tabs, err := loadDBCTalents(s.cfg.DBCDir)
	if err != nil {
		return nil, err
	}
	mask := uint32(1) << (classID - 1)
	tabByID := map[uint32]dbcTalentTab{}
	for _, t := range tabs {
		if t.classMask&mask != 0 {
			tabByID[t.id] = t
		}
	}
	if len(tabByID) == 0 {
		return []TalentTree{}, nil
	}
	trees := []TalentTree{}
	byTab := map[uint32]int{}
	// Batch rank-1 spell names and icons: one query instead of ~54 round-trips.
	infos := s.spellInfos(firstRanks(talents, tabByID))
	for _, t := range talents {
		tab, ok := tabByID[t.tabID]
		if !ok {
			continue
		}
		rank, active := uint32(0), uint32(0)
		for i, rs := range t.ranks {
			if known[rs] {
				rank = uint32(i + 1)
				active = rs
			}
		}
		idx, ok := byTab[t.tabID]
		if !ok {
			name := tab.name
			if name == "" {
				name = fmt.Sprintf("Tree %d", t.tabID)
			}
			trees = append(trees, TalentTree{TabID: t.tabID, Name: name, Page: tab.page, Talents: []TalentNode{}})
			idx = len(trees) - 1
			byTab[t.tabID] = idx
		}
		targetSpell := t.ranks[0]
		if active != 0 {
			targetSpell = active
		}
		spInfo := infos[targetSpell]
		if spInfo.Description == "" {
			spInfo = infos[t.ranks[0]]
		}
		icon := spInfo.Icon
		if icon == "" && infos[t.ranks[0]].Icon != "" {
			icon = infos[t.ranks[0]].Icon
		}
		trees[idx].Talents = append(trees[idx].Talents, TalentNode{
			TalentID: t.id, Row: t.row, Col: t.col,
			Rank: rank, MaxRank: uint32(len(t.ranks)), SpellID: active, Name: spInfo.Name, Icon: icon,
			Description: spInfo.Description,
		})
		if rank > 0 {
			trees[idx].Points += rank
		}
	}
	// Order trees by DBC page so Arms/Fury/Protection stay stable.
	for i := range trees {
		for j := i + 1; j < len(trees); j++ {
			if trees[j].Page < trees[i].Page {
				trees[i], trees[j] = trees[j], trees[i]
			}
		}
	}
	if trees == nil {
		trees = []TalentTree{}
	}
	return trees, nil
}

// resolveBotSpecs populates the Spec field for a slice of bots by reading their
// learned talent ranks from character_spell against Talent.dbc / TalentTab.dbc.
func (s *Service) resolveBotSpecs(bots []BotSummary) {
	if len(bots) == 0 || s.cfg.DBCDir == "" {
		for i := range bots {
			if bots[i].Spec == "" {
				bots[i].Spec = "Unspecified"
			}
		}
		return
	}
	talents, tabs, err := loadDBCTalents(s.cfg.DBCDir)
	if err != nil {
		for i := range bots {
			if bots[i].Spec == "" {
				bots[i].Spec = "Unspecified"
			}
		}
		return
	}
	tabNames := make(map[uint32]string, len(tabs))
	for _, t := range tabs {
		tabNames[t.id] = t.name
	}

	spellToTab := make(map[uint32]uint32, len(talents)*3)
	spellToRank := make(map[uint32]uint32, len(talents)*3)
	for _, t := range talents {
		for i, r := range t.ranks {
			if r != 0 {
				spellToTab[r] = t.tabID
				spellToRank[r] = uint32(i + 1)
			}
		}
	}
	if len(spellToTab) == 0 {
		return
	}

	guids := make([]interface{}, len(bots))
	guidMarks := make([]string, len(bots))
	guidIdx := make(map[uint32]int, len(bots))
	for i, b := range bots {
		guids[i] = b.GUID
		guidMarks[i] = "?"
		guidIdx[b.GUID] = i
	}

	q := fmt.Sprintf(`SELECT guid, spell FROM character_spell WHERE guid IN (%s)`, strings.Join(guidMarks, ","))
	rows, err := s.db.Query(q, guids...)
	if err != nil {
		return
	}
	defer rows.Close()

	points := make(map[uint32]map[uint32]uint32, len(bots))
	for rows.Next() {
		var guid, spell uint32
		if err := rows.Scan(&guid, &spell); err != nil {
			continue
		}
		tabID, ok := spellToTab[spell]
		if !ok {
			continue
		}
		bp, ok := points[guid]
		if !ok {
			bp = make(map[uint32]uint32)
			points[guid] = bp
		}
		bp[tabID] += spellToRank[spell]
	}

	for guid, bp := range points {
		idx, ok := guidIdx[guid]
		if !ok {
			continue
		}
		var maxPts uint32
		var maxTab uint32
		var tie bool
		for tabID, pts := range bp {
			if pts > maxPts {
				maxPts = pts
				maxTab = tabID
				tie = false
			} else if pts == maxPts && pts > 0 {
				tie = true
			}
		}
		if maxPts == 0 {
			bots[idx].Spec = "Unspecified"
		} else if tie {
			bots[idx].Spec = "Hybrid"
		} else {
			name := tabNames[maxTab]
			if name == "" {
				name = "Hybrid"
			}
			bots[idx].Spec = name
		}
	}
	for i := range bots {
		if bots[i].Spec == "" {
			bots[i].Spec = "Unspecified"
		}
	}
}

func loadDBCEnchantments(dir string) (map[uint32]string, error) {
	dbcMu.Lock()
	defer dbcMu.Unlock()
	if ench, ok := dbcEnchantments[dir]; ok {
		return ench, nil
	}
	raw, err := os.ReadFile(filepath.Join(dir, "SpellItemEnchantment.dbc"))
	if err != nil {
		return nil, err
	}
	if len(raw) < 20 {
		return nil, fmt.Errorf("SpellItemEnchantment.dbc too short")
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	if recSize < 56 || uint32(len(raw)) < 20+nRec*recSize {
		return nil, fmt.Errorf("SpellItemEnchantment.dbc truncated")
	}
	strBlock := raw[20+nRec*recSize:]
	ench := make(map[uint32]string, nRec)
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		id := binary.LittleEndian.Uint32(raw[off : off+4])
		strOff := binary.LittleEndian.Uint32(raw[off+52 : off+56])
		s := strings.TrimSpace(dbcString(strBlock, strOff))
		if s != "" {
			ench[id] = s
		}
	}
	dbcEnchantments[dir] = ench
	return ench, nil
}

type DBCRandomProperty struct {
	ID         uint32
	Suffix     string
	EnchantIDs [3]uint32
}

func loadDBCSpellDuration(dir string) (map[uint32]int32, error) {
	dbcMu.Lock()
	defer dbcMu.Unlock()
	if durs, ok := dbcDurations[dir]; ok {
		return durs, nil
	}
	raw, err := os.ReadFile(filepath.Join(dir, "SpellDuration.dbc"))
	if err != nil {
		return nil, err
	}
	if len(raw) < 20 {
		return nil, fmt.Errorf("SpellDuration.dbc too short")
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	if recSize < 8 || uint32(len(raw)) < 20+nRec*recSize {
		return nil, fmt.Errorf("SpellDuration.dbc truncated")
	}
	durs := make(map[uint32]int32, nRec)
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		id := binary.LittleEndian.Uint32(raw[off : off+4])
		dur := int32(binary.LittleEndian.Uint32(raw[off+4 : off+8]))
		durs[id] = dur
	}
	dbcDurations[dir] = durs
	return durs, nil
}

func loadDBCRandomProperties(dir string) (map[uint32]DBCRandomProperty, error) {
	dbcMu.Lock()
	defer dbcMu.Unlock()
	if props, ok := dbcRandomProps[dir]; ok {
		return props, nil
	}
	raw, err := os.ReadFile(filepath.Join(dir, "ItemRandomProperties.dbc"))
	if err != nil {
		return nil, err
	}
	if len(raw) < 20 {
		return nil, fmt.Errorf("ItemRandomProperties.dbc too short")
	}
	nRec := binary.LittleEndian.Uint32(raw[4:8])
	recSize := binary.LittleEndian.Uint32(raw[12:16])
	if recSize < 64 || uint32(len(raw)) < 20+nRec*recSize {
		return nil, fmt.Errorf("ItemRandomProperties.dbc truncated")
	}
	strBlock := raw[20+nRec*recSize:]
	props := make(map[uint32]DBCRandomProperty, nRec)
	for i := uint32(0); i < nRec; i++ {
		off := 20 + i*recSize
		id := binary.LittleEndian.Uint32(raw[off : off+4])
		e1 := binary.LittleEndian.Uint32(raw[off+8 : off+12])
		e2 := binary.LittleEndian.Uint32(raw[off+12 : off+16])
		e3 := binary.LittleEndian.Uint32(raw[off+16 : off+20])
		nameOff := binary.LittleEndian.Uint32(raw[off+28 : off+32])
		suffix := strings.TrimSpace(dbcString(strBlock, nameOff))
		props[id] = DBCRandomProperty{
			ID:         id,
			Suffix:     suffix,
			EnchantIDs: [3]uint32{e1, e2, e3},
		}
	}
	dbcRandomProps[dir] = props
	return props, nil
}

func (s *Service) applyRandomProperties(name *string, detail *ItemDetail, randPropID uint32) {
	if randPropID == 0 || s.cfg.DBCDir == "" {
		return
	}
	props, err := loadDBCRandomProperties(s.cfg.DBCDir)
	if err != nil {
		return
	}
	prop, ok := props[randPropID]
	if !ok {
		return
	}
	if prop.Suffix != "" && name != nil && !strings.Contains(*name, prop.Suffix) {
		*name = strings.TrimSpace(*name + " " + prop.Suffix)
	}
	enchMap, _ := loadDBCEnchantments(s.cfg.DBCDir)
	if enchMap != nil {
		for _, eID := range prop.EnchantIDs {
			if eID == 0 {
				continue
			}
			if desc, ok := enchMap[eID]; ok && desc != "" {
				detail.RandomStats = append(detail.RandomStats, desc)
			}
		}
	}
}

func (s *Service) parseEnchantments(raw string) []ItemEnchantmentInfo {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil
	}
	tokens := strings.Fields(raw)
	if len(tokens) < 3 {
		return nil
	}

	var enchMap map[uint32]string
	if s.cfg.DBCDir != "" {
		enchMap, _ = loadDBCEnchantments(s.cfg.DBCDir)
	}

	var result []ItemEnchantmentInfo

	// Slot 0: Permanent enchantment / armor kit (tokens 0, 1, 2)
	permID, _ := strconv.ParseUint(tokens[0], 10, 32)
	if permID > 0 {
		desc := ""
		if enchMap != nil {
			desc = enchMap[uint32(permID)]
		}
		if desc == "" {
			desc = fmt.Sprintf("Enchantment #%d", permID)
		}
		result = append(result, ItemEnchantmentInfo{
			ID:          uint32(permID),
			Description: desc,
			Slot:        0,
		})
	}

	// Slot 1: Temporary enchantment / poison / stone / oil (tokens 3, 4, 5)
	if len(tokens) >= 6 {
		tempID, _ := strconv.ParseUint(tokens[3], 10, 32)
		if tempID > 0 {
			durationMs, _ := strconv.ParseUint(tokens[4], 10, 32)
			charges, _ := strconv.ParseUint(tokens[5], 10, 32)
			desc := ""
			if enchMap != nil {
				desc = enchMap[uint32(tempID)]
			}
			if desc == "" {
				desc = fmt.Sprintf("Enhancement #%d", tempID)
			}
			result = append(result, ItemEnchantmentInfo{
				ID:          uint32(tempID),
				Description: desc,
				Slot:        1,
				Duration:    uint32(durationMs / 1000),
				Charges:     uint32(charges),
			})
		}
	}

	return result
}

