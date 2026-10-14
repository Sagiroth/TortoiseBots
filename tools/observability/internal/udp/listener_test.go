package udp

import (
	"encoding/json"
	"strings"
	"testing"
	"time"

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

func newTestListener() (*Listener, *state.Store, *recordingHub) {
	store := state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16))
	hub := &recordingHub{}
	return NewListener("127.0.0.1", 0, store, nil, nil, hub), store, hub
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
