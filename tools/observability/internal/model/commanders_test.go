package model

import (
	"encoding/json"
	"strings"
	"testing"
)

// A module that reads the ready check facts sends them on each bot as "rdy"; the wire form is fixed:
// {"b":13,"c":6,"cr":2,"cn":3,"cl":142}, with cl left out when nothing is on cooldown.
func TestCommanderBotReadyRoundTrip(t *testing.T) {
	raw := `{"name":"A","guid":7,"class":"priest","role":"healer","level":60,"hp":1,"max_hp":2,"power":3,"max_power":4,` +
		`"power_type":"mana","alive":true,"lead":false,"map":249,"zone":1,"x":0,"y":0,"z":0,` +
		`"rdy":{"b":13,"c":6,"cr":2,"cn":3,"cl":142}}`
	var b CommanderBot
	if err := json.Unmarshal([]byte(raw), &b); err != nil {
		t.Fatalf("a bot with rdy must unmarshal: %v", err)
	}
	if b.Ready == nil {
		t.Fatal("rdy must land in Ready")
	}
	if b.Ready.B != 13 || b.Ready.C != 6 || b.Ready.CR != 2 || b.Ready.CN != 3 || b.Ready.CL != 142 {
		t.Fatalf("rdy fields not preserved: %+v", *b.Ready)
	}
	out, err := json.Marshal(b)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if !strings.Contains(string(out), `"rdy":{"b":13,"c":6,"cr":2,"cn":3,"cl":142}`) {
		t.Fatalf("the wire form must round trip: %s", out)
	}
}

func TestCommanderBotReadyAbsent(t *testing.T) {
	var b CommanderBot
	if err := json.Unmarshal([]byte(`{"name":"A","guid":7}`), &b); err != nil {
		t.Fatalf("a bot without rdy must unmarshal: %v", err)
	}
	if b.Ready != nil {
		t.Fatalf("absent rdy must stay absent: %+v", *b.Ready)
	}
	out, err := json.Marshal(b)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if strings.Contains(string(out), `"rdy"`) {
		t.Fatalf("a bot without rdy must not write one: %s", out)
	}
	zero, err := json.Marshal(CommanderReady{B: 1})
	if err != nil {
		t.Fatalf("marshal ready: %v", err)
	}
	if string(zero) != `{"b":1,"c":0,"cr":0,"cn":0}` {
		t.Fatalf("cl is left out when 0: %s", zero)
	}
}
