package plans

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// fightPackCase is one row of testdata/fightpack-cases.json, which fightpack_parity_check.js reads too: the same cases
// pin the Go port and web/static/js/planoverlay.js, so a change on either side fails one of the two tests.
type fightPackCase struct {
	Name    string   `json:"name"`
	Plan    string   `json:"plan"`
	Step    uint32   `json:"step"`
	Boss    uint32   `json:"boss"`
	Engaged []uint32 `json:"engaged"`
	Want    int      `json:"want"`
}

func TestFightPack(t *testing.T) {
	raw, err := os.ReadFile(filepath.Join("testdata", "fightpack-cases.json"))
	if err != nil {
		t.Fatal(err)
	}
	var doc struct {
		Cases []fightPackCase `json:"cases"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	if len(doc.Cases) < 10 {
		t.Fatalf("%d cases, want at least 10", len(doc.Cases))
	}
	plans := map[string]*Plan{}
	load := func(name string) *Plan {
		if p, ok := plans[name]; ok {
			return p
		}
		b, err := os.ReadFile(filepath.Join("testdata", name+".plan.json"))
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		var p Plan
		if err := json.Unmarshal(b, &p); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		plans[name] = &p
		return &p
	}
	for _, c := range doc.Cases {
		if got := FightPack(load(c.Plan), c.Step, c.Boss, c.Engaged); got != c.Want {
			t.Errorf("%s: FightPack(step %d, boss %d, engaged %v) = %d, want %d", c.Name, c.Step, c.Boss, c.Engaged, got, c.Want)
		}
	}
}

// TestFightPackNilPlan pins the missing-plan answer: the history writer asks for the pack of a fight whose plan the world
// database does not have, and 0 means "no pack step".
func TestFightPackNilPlan(t *testing.T) {
	if got := FightPack(nil, 3, 10184, []uint32{12129}); got != 0 {
		t.Fatalf("FightPack(nil, ...) = %d, want 0", got)
	}
}
