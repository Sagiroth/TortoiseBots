package armory

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// GuildOrderNeed is one material the order asks the player to bring.
type GuildOrderNeed struct {
	ItemID uint32 `json:"item_id"`
	Name   string `json:"name"`
	Count  uint32 `json:"count"`
}

// GuildOrderStep is one observed change of an order row: the dashboard polls
// the table and records every change, so the page can show what the bot did
// and in which order. The module keeps its own step history in memory only.
type GuildOrderStep struct {
	At         int64  `json:"at"`
	State      string `json:"state"`
	Reason     string `json:"reason"`
	Step       string `json:"step"`
	ItemID     uint32 `json:"item_id"`
	ItemName   string `json:"item_name"`
	Count      uint32 `json:"count"`
	Delivered  uint32 `json:"delivered"`
	Carried    uint32 `json:"carried"`
	FarmZone   uint32 `json:"farm_zone"`
	FarmItem   string `json:"farm_item"`
	Need       string `json:"need"`
	DeathsZone uint32 `json:"deaths_in_zone"`
}

// GuildOrder is one order row as the dashboard shows it.
type GuildOrder struct {
	BotGUID     uint32           `json:"bot_guid"`
	Bot         string           `json:"bot"`
	Class       uint8            `json:"class"`
	Level       uint8            `json:"level"`
	Online      bool             `json:"online"`
	GuildID     uint32           `json:"guild_id"`
	Guild       string           `json:"guild"`
	Giver       string           `json:"giver"`
	ParentGUID  uint32           `json:"parent_guid"`
	Parent      string           `json:"parent"`
	Type        string           `json:"type"`
	ItemID      uint32           `json:"item_id"`
	ItemName    string           `json:"item_name"`
	Quality     uint8            `json:"quality"`
	Amount      uint32           `json:"amount"`
	Hours       uint32           `json:"hours"`
	Keep        bool             `json:"keep"`
	Delivered   uint32           `json:"delivered"`
	Carried     uint32           `json:"carried"`
	State       string           `json:"state"`
	Reason      string           `json:"reason"`
	Step        string           `json:"step"`
	SupplyID    uint32           `json:"supply_item"`
	SupplyName  string           `json:"supply_name"`
	SupplyCount uint32           `json:"supply_count"`
	SupplyVend  bool             `json:"supply_vendor"`
	FarmZone    uint32           `json:"farm_zone"`
	FarmItem    string           `json:"farm_item"`
	FarmChance  uint8            `json:"farm_chance"`
	DeathsZone  uint32           `json:"deaths_in_zone"`
	WorkedSec   uint32           `json:"worked_sec"`
	NoGainSec   uint32           `json:"nogain_sec"`
	GoldCap     uint32           `json:"gold_cap"`
	GoldUsed    uint32           `json:"guild_gold_used"`
	Need        []GuildOrderNeed `json:"need"`
	Cancelled   bool             `json:"cancelled"`
	CreatedAt   int64            `json:"created_at"`
	UpdatedAt   int64            `json:"updated_at"`
	Steps       []GuildOrderStep `json:"steps"`

	giverGUID uint32
}

// GuildRelist is one auction-house surplus the guild still has to get back.
type GuildRelist struct {
	Bot       string `json:"bot"`
	GuildID   uint32 `json:"guild_id"`
	ItemName  string `json:"item_name"`
	Count     uint32 `json:"count"`
	Buyout    uint32 `json:"buyout"`
	PostedAt  int64  `json:"posted_at"`
	State     uint8  `json:"state"` // 0 listed, 1 mail claimed, 2 value received
	HeldGold  uint32 `json:"held_gold"`
	HeldCount uint32 `json:"held_count"`
}

type GuildOrdersView struct {
	Orders  []GuildOrder  `json:"orders"`
	Relists []GuildRelist `json:"relists"`
	Polled  int64         `json:"polled"`
	Error   string        `json:"error,omitempty"`
}

const (
	guildOrderStepCap = 200
	guildOrderKeep    = 24 * time.Hour
)

// GuildOrderTracker polls the module's guild order table and remembers every
// change of each order. The table is small (one row per ordered bot, InnoDB),
// so a poll every few seconds is cheap.
type GuildOrderTracker struct {
	svc *Service

	mu      sync.RWMutex
	view    GuildOrdersView
	steps   map[string][]GuildOrderStep // key: bot guid + created_at
	seen    map[string]time.Time
	names   map[uint32]itemName
	carried bool // the carried column exists (newer module)
	file    string
	dirty   bool
}

type itemName struct {
	name    string
	quality uint8
	vendor  bool // sold by a vendor without a stock limit
}

// NewGuildOrderTracker keeps the step history in file (empty = memory only),
// so a dashboard restart does not lose it.
func NewGuildOrderTracker(svc *Service, file string) *GuildOrderTracker {
	t := &GuildOrderTracker{svc: svc, steps: map[string][]GuildOrderStep{}, seen: map[string]time.Time{},
		names: map[uint32]itemName{}, carried: true, file: file}
	if file != "" {
		if raw, err := os.ReadFile(file); err == nil {
			if err := json.Unmarshal(raw, &t.steps); err != nil {
				log.Printf("[GuildOrders] ignoring unreadable %s: %v", file, err)
				t.steps = map[string][]GuildOrderStep{}
			}
			now := time.Now()
			for key := range t.steps {
				t.seen[key] = now
			}
		}
	}
	return t
}

func (t *GuildOrderTracker) save() {
	t.mu.RLock()
	if t.file == "" || !t.dirty {
		t.mu.RUnlock()
		return
	}
	raw, err := json.Marshal(t.steps)
	t.mu.RUnlock()
	if err != nil {
		return
	}
	tmp := t.file + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o600); err != nil {
		log.Printf("[GuildOrders] saving history failed: %v", err)
		return
	}
	if err := os.Rename(tmp, t.file); err == nil {
		t.mu.Lock()
		t.dirty = false
		t.mu.Unlock()
	}
}

// Run polls forever; call it in its own goroutine.
func (t *GuildOrderTracker) Run(every time.Duration) {
	for {
		t.Poll()
		t.save()
		time.Sleep(every)
	}
}

func (t *GuildOrderTracker) View() GuildOrdersView {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.view
}

func orderKey(guid uint32, created int64) string {
	return strconv.FormatUint(uint64(guid), 10) + ":" + strconv.FormatInt(created, 10)
}

// Poll reads the table once and records the changes.
func (t *GuildOrderTracker) Poll() {
	orders, err := t.load()
	now := time.Now()
	if err != nil {
		t.mu.Lock()
		t.view.Error = err.Error()
		t.view.Polled = now.Unix()
		t.mu.Unlock()
		return
	}
	relists, _ := t.loadRelists()

	t.mu.Lock()
	defer t.mu.Unlock()
	for i := range orders {
		o := &orders[i]
		key := orderKey(o.BotGUID, o.CreatedAt)
		cur := GuildOrderStep{At: now.Unix(), State: o.State, Reason: o.Reason, Step: o.Step, ItemID: o.SupplyID,
			ItemName: o.SupplyName, Count: o.SupplyCount, Delivered: o.Delivered, Carried: o.Carried,
			FarmZone: o.FarmZone, FarmItem: o.FarmItem, Need: needText(o.Need), DeathsZone: o.DeathsZone}
		hist := t.steps[key]
		if len(hist) == 0 {
			// First sight: date the entry by the row's own clock so a running
			// order does not look like it just started.
			if o.UpdatedAt > 0 {
				cur.At = o.UpdatedAt
			}
			hist = append(hist, cur)
			t.dirty = true
		} else if last := hist[len(hist)-1]; !sameStep(last, cur) {
			hist = append(hist, cur)
			t.dirty = true
			if len(hist) > guildOrderStepCap {
				hist = hist[len(hist)-guildOrderStepCap:]
			}
		}
		t.steps[key] = hist
		t.seen[key] = now
		o.Steps = append([]GuildOrderStep(nil), hist...)
	}
	for key, at := range t.seen {
		if now.Sub(at) > guildOrderKeep {
			delete(t.seen, key)
			delete(t.steps, key)
			t.dirty = true
		}
	}
	t.view = GuildOrdersView{Orders: orders, Relists: relists, Polled: now.Unix()}
}

func sameStep(a, b GuildOrderStep) bool {
	a.At, b.At = 0, 0
	return a == b
}

func needText(need []GuildOrderNeed) string {
	parts := make([]string, 0, len(need))
	for _, n := range need {
		parts = append(parts, fmt.Sprintf("%dx %s", n.Count, n.Name))
	}
	return strings.Join(parts, ", ")
}

// parseNeed reads the module's "itemId:count;itemId:count" list.
func parseNeed(s string) []GuildOrderNeed {
	var out []GuildOrderNeed
	for _, part := range strings.Split(s, ";") {
		id, count, ok := strings.Cut(strings.TrimSpace(part), ":")
		if !ok {
			continue
		}
		i, err1 := strconv.ParseUint(id, 10, 32)
		c, err2 := strconv.ParseUint(count, 10, 32)
		if err1 != nil || err2 != nil || i == 0 {
			continue
		}
		out = append(out, GuildOrderNeed{ItemID: uint32(i), Count: uint32(c)})
	}
	return out
}

func (t *GuildOrderTracker) load() ([]GuildOrder, error) {
	db := t.svc.db
	carried := "0"
	t.mu.RLock()
	if t.carried {
		carried = "o.carried"
	}
	t.mu.RUnlock()
	q := `SELECT o.bot_guid, o.guild_id, o.giver_guid, o.parent_bot, o.type, o.item_id, o.amount, o.hours, o.keep,
		o.delivered, ` + carried + `, o.state, o.reason, o.supply_step, o.supply_item, o.supply_count, o.farm_zone,
		o.farm_item, o.farm_chance, o.deaths_in_zone, o.worked_total, o.worked_nogain, o.gold_cap,
		o.guild_gold_withdrawn, o.need_items, o.cancelled,
		UNIX_TIMESTAMP(o.created_at), UNIX_TIMESTAMP(o.updated_at)
		FROM tortoise_bots_guild_order o`
	rows, err := db.Query(q)
	if err != nil && t.carried && strings.Contains(err.Error(), "carried") {
		t.mu.Lock()
		t.carried = false
		t.mu.Unlock()
		return t.load()
	}
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var orders []GuildOrder
	var farmItems []uint32
	for rows.Next() {
		var o GuildOrder
		var keep, cancelled uint8
		var needRaw string
		var farmItem uint32
		var created, updated int64
		if err := rows.Scan(&o.BotGUID, &o.GuildID, &o.giverGUID, &o.ParentGUID, &o.Type, &o.ItemID, &o.Amount, &o.Hours, &keep,
			&o.Delivered, &o.Carried, &o.State, &o.Reason, &o.Step, &o.SupplyID, &o.SupplyCount, &o.FarmZone,
			&farmItem, &o.FarmChance, &o.DeathsZone, &o.WorkedSec, &o.NoGainSec, &o.GoldCap,
			&o.GoldUsed, &needRaw, &cancelled, &created, &updated); err != nil {
			return nil, err
		}
		o.Keep, o.Cancelled = keep != 0, cancelled != 0
		o.CreatedAt, o.UpdatedAt = created, updated
		o.Need = parseNeed(needRaw)
		if farmItem != 0 {
			o.FarmItem = strconv.FormatUint(uint64(farmItem), 10)
			farmItems = append(farmItems, farmItem)
		}
		orders = append(orders, o)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	if len(orders) == 0 {
		return orders, nil
	}

	// Names in two short keyed reads: characters is MyISAM and large, so it
	// is never joined to anything, only looked up by primary key.
	guids := map[uint32]bool{}
	guilds := map[uint32]bool{}
	items := map[uint32]bool{}
	for _, o := range orders {
		guids[o.BotGUID] = true
		if o.ParentGUID != 0 {
			guids[o.ParentGUID] = true
		}
		guids[o.giverGUID] = true
		guilds[o.GuildID] = true
		items[o.ItemID] = true
		if o.SupplyID != 0 {
			items[o.SupplyID] = true
		}
		for _, n := range o.Need {
			items[n.ItemID] = true
		}
	}
	for _, id := range farmItems {
		items[id] = true
	}
	chars, err := t.svc.charactersByGUID(keysOf(guids))
	if err != nil {
		return nil, err
	}
	guildNames, _ := t.svc.guildNames(keysOf(guilds))
	names := t.itemNames(keysOf(items))

	for i := range orders {
		o := &orders[i]
		c := chars[o.BotGUID]
		o.Bot, o.Class, o.Level, o.Online = c.name, c.class, c.level, c.online
		o.Parent = chars[o.ParentGUID].name
		o.Giver = chars[o.giverGUID].name
		o.Guild = guildNames[o.GuildID]
		o.ItemName, o.Quality = names[o.ItemID].name, names[o.ItemID].quality
		o.SupplyName, o.SupplyVend = names[o.SupplyID].name, names[o.SupplyID].vendor
		if o.FarmItem != "" {
			id, _ := strconv.ParseUint(o.FarmItem, 10, 32)
			o.FarmItem = names[uint32(id)].name
		}
		for j := range o.Need {
			o.Need[j].Name = names[o.Need[j].ItemID].name
		}
	}
	sort.SliceStable(orders, func(i, j int) bool { return orders[i].CreatedAt > orders[j].CreatedAt })
	return orders, nil
}

type charInfo struct {
	name   string
	class  uint8
	level  uint8
	online bool
}

func keysOf(m map[uint32]bool) []uint32 {
	out := make([]uint32, 0, len(m))
	for k := range m {
		if k != 0 {
			out = append(out, k)
		}
	}
	return out
}

func inList(ids []uint32) (string, []interface{}) {
	args := make([]interface{}, len(ids))
	for i, id := range ids {
		args[i] = id
	}
	return "?" + strings.Repeat(",?", len(ids)-1), args
}

func (s *Service) charactersByGUID(ids []uint32) (map[uint32]charInfo, error) {
	out := map[uint32]charInfo{}
	if len(ids) == 0 {
		return out, nil
	}
	ph, args := inList(ids)
	rows, err := s.db.Query("SELECT guid, name, class, level, online FROM characters WHERE guid IN ("+ph+")", args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var guid uint32
		var c charInfo
		var online uint8
		if err := rows.Scan(&guid, &c.name, &c.class, &c.level, &online); err != nil {
			return nil, err
		}
		c.online = online != 0
		out[guid] = c
	}
	return out, rows.Err()
}

func (s *Service) guildNames(ids []uint32) (map[uint32]string, error) {
	out := map[uint32]string{}
	if len(ids) == 0 {
		return out, nil
	}
	ph, args := inList(ids)
	rows, err := s.db.Query("SELECT guildid, name FROM guild WHERE guildid IN ("+ph+")", args...)
	if err != nil {
		return out, err
	}
	defer rows.Close()
	for rows.Next() {
		var id uint32
		var name string
		if rows.Scan(&id, &name) == nil {
			out[id] = name
		}
	}
	return out, rows.Err()
}

// itemNames resolves item ids once and caches them (world data never changes
// while the dashboard runs).
func (t *GuildOrderTracker) itemNames(ids []uint32) map[uint32]itemName {
	t.mu.RLock()
	var missing []uint32
	for _, id := range ids {
		if _, ok := t.names[id]; !ok {
			missing = append(missing, id)
		}
	}
	t.mu.RUnlock()
	if len(missing) > 0 {
		ph, args := inList(missing)
		rows, err := t.svc.db.Query(fmt.Sprintf("SELECT entry, name, quality FROM %s.item_template WHERE entry IN (%s)",
			t.svc.cfg.WorldDB, ph), args...)
		if err == nil {
			t.mu.Lock()
			for rows.Next() {
				var id uint32
				var n itemName
				if rows.Scan(&id, &n.name, &n.quality) == nil {
					t.names[id] = n
				}
			}
			t.mu.Unlock()
			rows.Close()
			vrows, err := t.svc.db.Query(fmt.Sprintf("SELECT DISTINCT item FROM %s.npc_vendor WHERE maxcount = 0 AND item IN (%s)",
				t.svc.cfg.WorldDB, ph), args...)
			if err == nil {
				t.mu.Lock()
				for vrows.Next() {
					var id uint32
					if vrows.Scan(&id) == nil {
						n := t.names[id]
						n.vendor = true
						t.names[id] = n
					}
				}
				t.mu.Unlock()
				vrows.Close()
			}
		}
	}
	t.mu.RLock()
	defer t.mu.RUnlock()
	out := make(map[uint32]itemName, len(ids))
	for _, id := range ids {
		out[id] = t.names[id]
	}
	return out
}

func (t *GuildOrderTracker) loadRelists() ([]GuildRelist, error) {
	// state: 0 posted, 1 mail claimed, 2 value received. Modules before the
	// claim step only had a received flag (1 = received).
	rows, err := t.svc.db.Query(`SELECT bot_guid, guild_id, item_id, count, buyout, posted_at, state, held_gold, held_count
		FROM tortoise_bots_guild_relist ORDER BY posted_at DESC`)
	if err != nil && strings.Contains(err.Error(), "state") {
		rows, err = t.svc.db.Query(`SELECT bot_guid, guild_id, item_id, count, buyout, posted_at, received * 2, held_gold, held_count
			FROM tortoise_bots_guild_relist ORDER BY posted_at DESC`)
	}
	if err != nil {
		return nil, err // older module: no ledger table
	}
	defer rows.Close()
	type raw struct {
		GuildRelist
		guid, item uint32
	}
	var list []raw
	guids, items := map[uint32]bool{}, map[uint32]bool{}
	for rows.Next() {
		var r raw
		if err := rows.Scan(&r.guid, &r.GuildID, &r.item, &r.Count, &r.Buyout, &r.PostedAt, &r.State, &r.HeldGold, &r.HeldCount); err != nil {
			return nil, err
		}
		guids[r.guid], items[r.item] = true, true
		list = append(list, r)
	}
	if len(list) == 0 {
		return nil, rows.Err()
	}
	chars, _ := t.svc.charactersByGUID(keysOf(guids))
	names := t.itemNames(keysOf(items))
	out := make([]GuildRelist, len(list))
	for i, r := range list {
		r.Bot = chars[r.guid].name
		r.ItemName = names[r.item].name
		out[i] = r.GuildRelist
	}
	return out, nil
}
