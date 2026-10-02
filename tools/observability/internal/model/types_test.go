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
