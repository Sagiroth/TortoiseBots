package udp

import (
	"encoding/json"
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
