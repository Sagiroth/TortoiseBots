package udp

import (
	"encoding/json"
	"strconv"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/live"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/ringbuf"
	"tortoise-observability/internal/state"
)

type recordingHub struct {
	events []string
	last   interface{}
}

func (h *recordingHub) Broadcast(event string, payload interface{}) {
	h.events = append(h.events, event)
	h.last = payload
}

// testMetrics is one registry for every listener test: metrics.New registers
// on the process-wide Prometheus registry, so a second call would panic.
var testMetrics = metrics.New()

func newTestListener() (*Listener, *state.Store, *recordingHub) {
	store := state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16))
	zp, err := zoneproject.New([]byte(`{}`))
	if err != nil {
		panic(err)
	}
	hub := &recordingHub{}
	return NewListener("127.0.0.1", 0, store, testMetrics, zp, hub), store, hub
}

// The datagram text is what the module's CommanderTelemetry::BuildPayload writes
// (tools/test_commander_telemetry.cpp pins its fields).
const commandersDatagram = `{"v":9,"session":77,"seq":5,"ts":1791990010,"type":"COMMANDERS","commanders":[
 {"commander":"Zfmagea","online":true,"view":true,"group":42,"lead":"Forthur","bots":[
  {"name":"Forthur","guid":700,"class":"warrior","role":"tank","level":46,"hp":2706,"max_hp":2706,"power":40,"max_power":100,"power_type":"rage","alive":true,"lead":true,"map":209,"zone":1176,"x":1212.7,"y":842.0,"z":8.9},
  {"name":"Puvur","guid":701,"class":"priest","role":"healer","level":46,"hp":0,"max_hp":1000,"power":500,"max_power":1000,"power_type":"mana","alive":false,"lead":false,"map":209,"zone":1176,"x":1200.0,"y":830.5,"z":9.0}],
  "orders":[{"at":1791990000,"verb":"move","detail":"1517 833 9","ok":true,"result":"party 5"},
            {"at":1791990005,"verb":"attack","detail":"0xF13000","ok":false,"result":"invalid-target: No unit"}]}]}`

func TestCommandersDatagramReachesTheStoreAndTheStream(t *testing.T) {
	l, store, hub := newTestListener()
	l.processPacket([]byte(commandersDatagram))

	v := store.Commanders()
	if !v.Fresh || len(v.Commanders) != 1 {
		t.Fatalf("view: %+v", v)
	}
	c := v.Commanders[0]
	if c.Commander != "Zfmagea" || !c.Online || !c.View || c.Group != 42 || c.Lead != "Forthur" || len(c.Bots) != 2 || len(c.Orders) != 2 {
		t.Fatalf("binding: %+v", c)
	}
	b := c.Bots[1]
	if b.Name != "Puvur" || b.Alive || b.MapID != 209 || b.X != 1200.0 || b.Y != 830.5 || b.PowerType != "mana" || b.HP != 0 || b.MaxHP != 1000 {
		t.Fatalf("bot: %+v", b)
	}
	if o := c.Orders[1]; o.OK || o.Verb != "attack" || o.Result != "invalid-target: No unit" {
		t.Fatalf("order: %+v", o)
	}
	if len(hub.events) != 1 || hub.events[0] != "commanders" {
		t.Fatalf("stream events: %v", hub.events)
	}
	if _, ok := hub.last.(model.CommandersView); !ok {
		t.Fatalf("stream payload %T", hub.last)
	}
	if _, err := json.Marshal(hub.last); err != nil {
		t.Fatal(err)
	}
}

func TestCommandersDatagramGarbageIsIgnored(t *testing.T) {
	l, store, hub := newTestListener()
	l.processPacket([]byte(`{"v":9,"type":"COMMANDERS","commanders":"nope"}`))
	l.processPacket([]byte(`{"v":9,"type":"COMMANDERS","commanders":[{"bots":5}]}`))
	l.processPacket([]byte(`not json`))
	if len(hub.events) != 0 || store.Commanders().UpdatedAt != 0 {
		t.Fatalf("garbage changed state: %v %+v", hub.events, store.Commanders())
	}
}

func TestEmptyCommandersListEndsTheBindings(t *testing.T) {
	l, store, hub := newTestListener()
	l.processPacket([]byte(commandersDatagram))
	l.processPacket([]byte(`{"v":9,"session":77,"seq":6,"ts":1791990020,"type":"COMMANDERS","commanders":[]}`))
	if v := store.Commanders(); !v.Fresh || len(v.Commanders) != 0 {
		t.Fatalf("%+v", v)
	}
	if len(hub.events) != 2 {
		t.Fatalf("both datagrams must reach the stream: %v", hub.events)
	}
}

// The fields CommanderTelemetry::BuildPayload adds for the map layers (tools/test_commander_telemetry.cpp, TestMapData). The guids are
// decimal strings: a creature guid does not fit a JSON number.
const commandersMapDatagram = `{"v":9,"session":77,"seq":7,"ts":1791990030,"type":"COMMANDERS","commanders":[
 {"commander":"Zfmagea","commander_guid":4242,"online":true,"view":true,"group":42,"lead":"Forthur","bots":[
  {"name":"Forthur","guid":700,"class":"warrior","role":"tank","level":46,"hp":2706,"max_hp":2706,"power":40,"max_power":100,"power_type":"rage","alive":true,"lead":true,"map":209,"zone":1176,"x":1212.7,"y":842.0,"z":8.9,"id":"10844","target":"17379391094613082370"}],
  "commander_pos":{"map":209,"x":1100.5,"y":-50.5},
  "mobs_map":209,"mobs":[
   {"id":"17379391094613082370","entry":4863,"name":"Stonevault Rockchewer","hp":85,"x":1230.5,"y":850.0,"victim":"10844","reaction":"h"},
   {"id":"17379391094613082371","entry":4000,"name":"Rat","hp":100,"x":1240.0,"y":851.0,"reaction":"n","kind":"k"}],
  "orders":[]}]}`

func TestCommandersMapDataReachesTheStore(t *testing.T) {
	l, store, _ := newTestListener()
	l.processPacket([]byte(commandersMapDatagram))
	c := store.Commanders().Commanders[0]
	if c.CommanderPos == nil || c.CommanderPos.MapID != 209 || c.CommanderPos.X != 1100.5 || c.CommanderPos.Y != -50.5 {
		t.Fatalf("commander position: %+v", c.CommanderPos)
	}
	if c.Bots[0].ID != "10844" || c.Bots[0].Target != "17379391094613082370" {
		t.Fatalf("bot id/target: %+v", c.Bots[0])
	}
	if c.MobsMap != 209 || len(c.Mobs) != 2 {
		t.Fatalf("mobs: %+v", c.Mobs)
	}
	m := c.Mobs[0]
	if m.ID != "17379391094613082370" || m.Entry != 4863 || m.Name != "Stonevault Rockchewer" || m.HP != 85 || m.Victim != "10844" || m.Reaction != "h" || m.Kind != "" {
		t.Fatalf("mob: %+v", m)
	}
	if r := c.Mobs[1]; r.Reaction != "n" || r.Kind != "k" || r.Victim != "" {
		t.Fatalf("critter: %+v", r)
	}
	out, err := json.Marshal(store.Commanders())
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(out), `"id":"17379391094613082370"`) {
		t.Fatalf("a guid must stay a string in the REST/stream output: %s", out)
	}
}

// An older module sends none of it: the page sees mobs as null (not reported), not as an empty list.
func TestCommandersWithoutMapDataStayAbsent(t *testing.T) {
	l, store, _ := newTestListener()
	l.processPacket([]byte(commandersDatagram))
	c := store.Commanders().Commanders[0]
	if c.CommanderPos != nil || c.Mobs != nil || c.Bots[0].ID != "" || c.Bots[0].Target != "" {
		t.Fatalf("map data appeared from nowhere: %+v", c)
	}
	out, _ := json.Marshal(c)
	if strings.Contains(string(out), "commander_pos") || !strings.Contains(string(out), `"mobs":null`) {
		t.Fatalf("shape: %s", out)
	}
}

// An empty list that the module read is kept as an empty list: the layers are then available.
func TestCommandersEmptyMobListIsReported(t *testing.T) {
	l, store, _ := newTestListener()
	l.processPacket([]byte(`{"v":9,"session":77,"seq":8,"ts":1791990040,"type":"COMMANDERS","commanders":[{"commander":"Z","bots":[],"mobs_map":209,"mobs":[],"orders":[]}]}`))
	out, _ := json.Marshal(store.Commanders().Commanders[0])
	if !strings.Contains(string(out), `"mobs":[]`) {
		t.Fatalf("shape: %s", out)
	}
}

// DUNGEON_MOBS (issue #1101): the type reaches the store and joins the run in
// /api/v1/dungeons, and the datagram is not pushed on the stream (the browser
// refetches the view on the snapshot broadcast). An unknown type still goes
// nowhere.
func TestDungeonMobsDatagramJoinsTheDungeonsView(t *testing.T) {
	l, store, hub := newTestListener()
	l.processPacket([]byte(`{"v":12,"session":77,"seq":1,"ts":1791990000,"type":"BOT_BATCH","batch_index":0,"total_batches":1,"bots":[
		{"name":"Tank","guid":1,"class":"warrior","role":"tank","level":46,"hp":100,"max_hp":100,"map":409,"instance":13,"zone":2437,"x":1,"y":2,"z":3,"state":"combat"}]}`))
	if view := store.Dungeons(); len(view.Runs) != 1 || view.Runs[0].Key != "409:13" {
		t.Fatalf("roster run: %+v", view.Runs)
	}
	events := len(hub.events)

	l.processPacket([]byte(`{"v":12,"session":77,"seq":2,"ts":1791990002,"type":"DUNGEON_MOBS","part":0,"parts":1,"runs":[
		{"map":409,"instance":13,"raid":false,"members":[{"guid":1,"id":"10844","target":"17379391094613082370"}],
		 "mobs_map":409,"mobs_total":57,"mobs":[
			{"id":"17379391094613082370","entry":4863,"name":"Stonevault Rockchewer","hp":85,"x":1230.5,"y":850,"victim":"10844","reaction":"h"},
			{"id":"17379391094613082371","entry":4000,"name":"Rat","hp":100,"x":1240,"y":851,"reaction":"n","kind":"k"}]}]}`))
	if len(hub.events) != events {
		t.Fatalf("DUNGEON_MOBS must not push a stream event: %v", hub.events)
	}
	view := store.Dungeons()
	run := view.Runs[0]
	if run.Mobs == nil || len(*run.Mobs) != 2 || run.MobsMap == nil || *run.MobsMap != 409 || run.MobsTotal != 57 {
		t.Fatalf("mobs not joined: %+v", run)
	}
	if run.Members[0].ID != "10844" || run.Members[0].Target != "17379391094613082370" {
		t.Fatalf("member id/target: %+v", run.Members[0])
	}
	if m := (*run.Mobs)[0]; m.ID != "17379391094613082370" || m.Victim != "10844" || m.Reaction != "h" {
		t.Fatalf("mob: %+v", m)
	}

	// Garbage and an unknown type change nothing and push nothing.
	l.processPacket([]byte(`{"v":12,"type":"DUNGEON_MOBS","runs":"nope"}`))
	l.processPacket([]byte(`{"v":12,"session":77,"seq":3,"type":"DUNGEON_MOBS_OLD","runs":[]}`))
	if len(hub.events) != events {
		t.Fatalf("unknown/garbage pushed: %v", hub.events)
	}
	if got := store.Dungeons().Runs[0]; got.Mobs == nil || len(*got.Mobs) != 2 {
		t.Fatalf("last good datagram lost: %+v", got)
	}
}

// ---- realtime types (S1): COMMANDER_POS and COMBAT_EVENTS reach the live sink and never the WebSocket.

type fakeLiveSink struct {
	got []string
}

func (s *fakeLiveSink) Publish(commander, event string, _ interface{}) {
	s.got = append(s.got, commander+":"+event)
}

func (s *fakeLiveSink) has(want string) bool {
	for _, g := range s.got {
		if g == want {
			return true
		}
	}
	return false
}

const commanderPosDatagram = `{"v":12,"fmt":1,"session":77,"seq":8,"ts":1791990030,"ts_ms":1791990030500,"type":"COMMANDER_POS",
 "commander":"Zfmagea","commander_guid":12,"combat":true,
 "bots":[{"g":700,"m":209,"x":1213.5,"y":842.0,"z":8.9,"o":3.14,"hp":2706,"mhp":2706,"a":1}],
 "mobs":[{"id":"17370386927380123","x":-20.1,"y":40.2,"hp":87,"v":"700"}]}`

const combatEventsDatagram = `{"v":12,"fmt":1,"session":77,"seq":9,"ts":1791990031,"ts_ms":1791990031250,"type":"COMBAT_EVENTS",
 "commander":"Zfmagea","commander_guid":12,"group":55,"map":249,
 "fight":{"id":"55-6","start":1791990000,"start_ms":1791990000120,"plan":"onyxias_lair","leg":"2","step":24,"kind":"boss","boss":10184,"boss_name":"Onyxia","fight_ms":53130},
 "dropped":0,
 "events":[{"k":"start","t":0},{"k":"death","t":53000,"g":4021,"n":"Bot","by":"Onyxia","x":-12.3,"y":45.6}]}`

func TestRealtimeTypesReachLiveSink(t *testing.T) {
	l, store, _ := newTestListener()
	combatStore := combat.NewStore(nil)
	l.combat = combatStore
	sink := &fakeLiveSink{}
	l.SetLiveSink(sink)

	l.processPacket([]byte(commandersDatagram))
	if !sink.has("Zfmagea:commander") {
		t.Fatalf("COMMANDERS did not reach the live sink: %v", sink.got)
	}
	l.processPacket([]byte(commanderPosDatagram))
	if !sink.has("Zfmagea:pos") {
		t.Fatalf("COMMANDER_POS did not reach the live sink: %v", sink.got)
	}
	if v := store.Commanders(); v.Commanders[0].Bots[0].X != 1213.5 {
		t.Fatalf("pos not merged into the store: %+v", v.Commanders[0].Bots[0])
	}
	l.processPacket([]byte(combatEventsDatagram))
	if !sink.has("Zfmagea:events") || !sink.has("Zfmagea:fight_live") {
		t.Fatalf("COMBAT_EVENTS did not reach the live sink: %v", sink.got)
	}
	l.processPacket([]byte(strings.Replace(combatFightDatagram, `"id":"55-1"`, `"id":"55-6"`, 1)))
	if !sink.has("Zfmagea:fight") {
		t.Fatalf("COMBAT_FIGHT did not reach the live sink: %v", sink.got)
	}
	if rows := combatStore.List("Zfmagea"); len(rows) != 1 || rows[0].Live {
		t.Fatalf("the finished fight must replace the live row: %+v", rows)
	}
}

func TestNewTypesNoWebSocketBroadcast(t *testing.T) {
	l, _, hub := newTestListener()
	l.combat = combat.NewStore(nil)
	l.SetLiveSink(&fakeLiveSink{})
	before := len(hub.events)
	l.processPacket([]byte(commanderPosDatagram))
	l.processPacket([]byte(combatEventsDatagram))
	for _, e := range hub.events[before:] {
		if e == "pos" || e == "events" || e == "fight_live" {
			t.Fatalf("a realtime type reached the WebSocket: %v", hub.events)
		}
	}
}

// Without a live sink nothing panics and the store still takes the data.
func TestRealtimeTypesWithoutSink(t *testing.T) {
	l, store, _ := newTestListener()
	l.processPacket([]byte(commanderPosDatagram))
	l.processPacket([]byte(combatEventsDatagram))
	if v := store.Commanders(); len(v.Commanders) != 0 {
		t.Fatalf("a pos for no standing binding must be ignored: %+v", v)
	}
}

// drainLive takes every frame the hub queued for the client so far. Publish
// is called on this goroutine, so nothing is in flight.
func drainLive(c *live.Client) []live.Frame {
	var frames []live.Frame
	for {
		select {
		case f := <-c.Frames():
			frames = append(frames, f)
		default:
			return frames
		}
	}
}

// The live stream must carry only the datagrams ingestion accepted: a
// duplicate, a reordered one and one of an older game session are dropped by
// ApplyEvents, and the browser appends whatever an events frame carries.
func TestRejectedCombatEventsNeverReachTheStream(t *testing.T) {
	l, _, _ := newTestListener()
	l.combat = combat.NewStore(nil)
	hub := live.NewHub()
	c, err := hub.Subscribe([]string{"Zfmagea"})
	if err != nil {
		t.Fatal(err)
	}
	l.SetLiveSink(hub)

	l.processPacket([]byte(combatEventsDatagram))
	if frames := drainLive(c); len(frames) != 2 || frames[0].Event != "events" || frames[1].Event != "fight_live" {
		t.Fatalf("the first datagram must publish its events and the live row: %+v", frames)
	}

	l.processPacket([]byte(combatEventsDatagram))
	if frames := drainLive(c); len(frames) != 0 {
		t.Fatalf("a duplicate datagram reached the stream: %+v", frames)
	}

	reordered := strings.Replace(combatEventsDatagram, `"seq":9`, `"seq":8`, 1)
	l.processPacket([]byte(reordered))
	if frames := drainLive(c); len(frames) != 0 {
		t.Fatalf("a reordered datagram reached the stream: %+v", frames)
	}

	stale := strings.Replace(combatEventsDatagram, `"session":77`, `"session":76`, 1)
	l.processPacket([]byte(stale))
	if frames := drainLive(c); len(frames) != 0 {
		t.Fatalf("a stale-session datagram reached the stream: %+v", frames)
	}

	// The guard drops only what ingestion rejected: the next datagram of the
	// live session still reaches the stream.
	fresh := strings.Replace(combatEventsDatagram, `"seq":9`, `"seq":10`, 1)
	l.processPacket([]byte(fresh))
	if frames := drainLive(c); len(frames) != 1 || frames[0].Event != "events" {
		t.Fatalf("an accepted update must publish its events: %+v", frames)
	}
}

// R8: a COMMANDERS cycle split into parts reaches the store and the stream once, when the last part is in.
func TestCommandersPartsOneBroadcast(t *testing.T) {
	l, store, hub := newTestListener()
	part := func(idx int, name string) []byte {
		return []byte(`{"v":12,"session":77,"seq":30,"ts":1791990100,"type":"COMMANDERS","part":` + strconv.Itoa(idx) +
			`,"parts":2,"commanders":[{"commander":"` + name + `","online":true,"group":1,"lead":"L","bots":[],"orders":[]}]}`)
	}
	l.processPacket(part(1, "B"))
	if len(hub.events) != 0 {
		t.Fatalf("a part alone must not broadcast: %v", hub.events)
	}
	if v := store.Commanders(); len(v.Commanders) != 0 {
		t.Fatalf("a part alone changed the view: %+v", v.Commanders)
	}
	l.processPacket(part(0, "A"))
	if len(hub.events) != 1 || hub.events[0] != "commanders" {
		t.Fatalf("one part completed the cycle; events %v", hub.events)
	}
	v := store.Commanders()
	if len(v.Commanders) != 2 || v.Commanders[0].Commander != "A" || v.Commanders[1].Commander != "B" {
		t.Fatalf("assembled view: %+v", v.Commanders)
	}
}
