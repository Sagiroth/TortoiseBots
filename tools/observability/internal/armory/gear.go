package armory

import (
	"fmt"

	"tortoise-observability/internal/model"
)

// Equipment slots that carry no gear value: the shirt (EQUIPMENT_SLOT_BODY)
// and the tabard. Every other filled slot 0-18 counts, weapons/offhand/ranged
// included.
const (
	slotShirt  = 3
	slotTabard = 18
)

// GearRollup returns every pool bot's equipped-gear summary in one query over
// character_inventory: the average item level of the filled equipment slots
// plus the quality census of those pieces. Empty slots are not averaged in, so
// a bot in a half-finished outfit is not scored as if it wore ilvl-0 gear. It
// is a diagnostic snapshot the daemon runs on a slow timer (minutes), never
// per tick.
func (s *Service) GearRollup() (map[uint32]model.BotGear, error) {
	query := fmt.Sprintf(`
		SELECT c.guid, it.quality, it.item_level
		FROM character_inventory ci
		JOIN characters c ON c.guid = ci.guid
		JOIN tortoise_bots_pool_account p ON p.account_id = c.account
		JOIN %s.item_template it ON it.entry = ci.item_template
		WHERE c.deleteDate IS NULL
		  AND ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d
		  AND ci.slot NOT IN (%d, %d)
	`, s.cfg.WorldDB, EquipmentSlotEnd, slotShirt, slotTabard)

	rows, err := s.db.Query(query)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	// [0..4] = grey, white, green, blue, epic-or-higher.
	type equipTally struct {
		sumLevel uint64
		pieces   int
		quality  [5]int
	}
	tallies := map[uint32]*equipTally{}
	for rows.Next() {
		var guid uint32
		var quality, itemLevel uint32
		if err := rows.Scan(&guid, &quality, &itemLevel); err != nil {
			return nil, err
		}
		t := tallies[guid]
		if t == nil {
			t = &equipTally{}
			tallies[guid] = t
		}
		t.sumLevel += uint64(itemLevel)
		t.pieces++
		// Legendary/artifact pieces are rare but must still be counted so the
		// per-quality census always sums to Pieces.
		t.quality[min(quality, 4)]++
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	out := make(map[uint32]model.BotGear, len(tallies))
	for guid, t := range tallies {
		gear := model.BotGear{
			Pieces: t.pieces,
			Grey:   t.quality[0],
			White:  t.quality[1],
			Green:  t.quality[2],
			Blue:   t.quality[3],
			Epic:   t.quality[4],
		}
		if t.pieces > 0 {
			gear.ItemLevel = float64(t.sumLevel) / float64(t.pieces)
		}
		out[guid] = gear
	}
	return out, nil
}
