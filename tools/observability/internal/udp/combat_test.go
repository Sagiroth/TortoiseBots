package udp

import (
	"testing"

	"tortoise-observability/internal/combat"
)

// A COMBAT_FIGHT datagram is what the module's CombatMonitor::Book writes
// (runtime/CombatMonitorPolicy.h BuildFightJson): one fight per datagram,
// each fight handed out twice with the same id.
const combatFightDatagram = `{"v":12,"session":77,"seq":6,"ts":1791990020,"type":"COMBAT_FIGHT","fight":{"id":"55-1","commander":"Zfmagea","commander_guid":9,"group":55,"map":409,"plan":"molten_core","leg":"l1","step":3,"kind":"boss","start":1791990000,"dur_ms":30000,"outcome":"kill","boss":11502,"boss_name":"Magmadar","planned":[11502],"engaged":[{"e":11502,"n":"Magmadar","c":1}],"bots":[{"name":"Tanky","guid":700,"class":"warrior","role":"tank","sub":0,"dmg":1200,"heal":0,"over":0,"taken":3000,"casts":6,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":4}]}],"debuffs":{"sampled_s":10,"up_s":{"sunder":9}},"deaths":[]}}`

func TestListenerCombatFight(t *testing.T) {
	l, _, _ := newTestListener()
	store := combat.NewStore(nil)
	l.combat = store
	l.processPacket([]byte(combatFightDatagram))
	if got := store.List("Zfmagea"); len(got) != 1 || got[0].ID != "55-1" {
		t.Fatalf("fight not stored: %+v", got)
	}
	// The module's second take of the same fight: dropped by (session, id).
	l.processPacket([]byte(combatFightDatagram))
	if got := store.List("Zfmagea"); len(got) != 1 {
		t.Fatalf("duplicate take kept: %+v", got)
	}
	// Unknown types still fall through to the anomaly path, not the store.
	l.processPacket([]byte(`{"v":12,"session":77,"seq":7,"ts":1,"type":"NOPE"}`))
	if got := store.List("Zfmagea"); len(got) != 1 {
		t.Fatalf("unknown type touched the combat store: %+v", got)
	}
}
