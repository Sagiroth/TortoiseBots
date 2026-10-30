package model

import (
	"encoding/json"
	"testing"
)

// The emitter writes the bots object with each field in its natural JSON type:
// pool sizes/intervals/budgets as numbers, flags as "0"/"1" strings. Typing
// Bots as map[string]string made encoding/json reject the whole datagram, which
// left the Server panel and the diagnostic report permanently empty.
func TestServerInfoPayloadAcceptsMixedBotsValues(t *testing.T) {
	raw := `{"v":8,"session":1,"seq":2,"ts":3,"type":"SERVER_INFO",
		"module_version":"test","rates":{"xp_kill":1.5},
		"bots":{"min_random":100,"update_interval":120,"group_nearby":"1","auto_do_quests":"0"},
		"diagnostics":{"bot_events":"1"}}`

	var info ServerInfoPayload
	if err := json.Unmarshal([]byte(raw), &info); err != nil {
		t.Fatalf("SERVER_INFO payload must unmarshal: %v", err)
	}
	if info.Bots["min_random"] != float64(100) || info.Bots["group_nearby"] != "1" {
		t.Fatalf("bots values not preserved: %+v", info.Bots)
	}
	if info.ModuleVersion != "test" || info.Rates["xp_kill"] != 1.5 {
		t.Fatalf("scalar fields lost: %+v", info)
	}
}

// Issue #872: pre-v11 modules send no kind; the daemon backfills it from the
// player/alt flags so old and new servers render the same.
func TestKindFromFlags(t *testing.T) {
	if KindFromFlags(true, true) != KindPlayer {
		t.Fatalf("player must win over alt")
	}
	if KindFromFlags(false, true) != KindAlt {
		t.Fatalf("alt flag must map to alt")
	}
	if KindFromFlags(false, false) != KindPool {
		t.Fatalf("no flags must map to pool")
	}
	raw := `{"name":"Old","guid":7,"player":false,"alt":true,"role":"","level":1,"hp":1,"max_hp":1,"power":0,"max_power":0,"state":"idle"}`
	var b BotSnapshot
	if err := json.Unmarshal([]byte(raw), &b); err != nil {
		t.Fatalf("batch row must unmarshal: %v", err)
	}
	if b.Kind != "" || !b.Alt || b.Player {
		t.Fatalf("old row must carry flags and no kind: %+v", b)
	}
	if KindFromFlags(b.Player, b.Alt) != KindAlt {
		t.Fatalf("old alt row must backfill to alt")
	}
}

// Guild rows are additive: a new module sends the name, an older one omits
// it and the row reads guildless.
func TestGuildField(t *testing.T) {
	raw := `{"name":"New","guid":8,"role":"","level":1,"hp":1,"max_hp":1,"power":0,"max_power":0,"guild":"Knights","state":"idle"}`
	var b BotSnapshot
	if err := json.Unmarshal([]byte(raw), &b); err != nil {
		t.Fatalf("batch row must unmarshal: %v", err)
	}
	if b.Guild != "Knights" {
		t.Fatalf("guild name lost: %+v", b)
	}
	old := `{"name":"Old","guid":7,"role":"","level":1,"hp":1,"max_hp":1,"power":0,"max_power":0,"state":"idle"}`
	var o BotSnapshot
	if err := json.Unmarshal([]byte(old), &o); err != nil {
		t.Fatalf("old batch row must unmarshal: %v", err)
	}
	if o.Guild != "" {
		t.Fatalf("old row must read guildless: %+v", o)
	}
}
