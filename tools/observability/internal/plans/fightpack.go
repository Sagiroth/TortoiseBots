package plans

// The pack a fight was about: the first pull or boss step at or after the fight's step that holds one of its engaged entries,
// or whose boss entry is the fight's boss. A fight known only by its step (no entries, no boss) marks the next pack, the one
// the run was walking to. 0 means the plan has no such step. This is the Go half of web/static/js/planoverlay.js's fightPack:
// testdata/fightpack-cases.json pins both sides on the same cases.
func FightPack(p *Plan, step, boss uint32, engaged []uint32) int {
	if p == nil {
		return 0
	}
	known := boss > 0 || len(engaged) > 0
	for _, s := range p.Steps {
		if s.K != "pull" && s.K != "boss" {
			continue
		}
		if s.I < int(step) {
			continue
		}
		if !known {
			return s.I
		}
		if boss > 0 && s.K == "boss" && s.E == boss {
			return s.I
		}
		if len(engaged) > 0 && holdsEntry(s, engaged) {
			return s.I
		}
	}
	return 0
}

// holdsEntry reports whether a step's mob entries hold one of the fight's engaged entries.
func holdsEntry(s Step, engaged []uint32) bool {
	holds := func(e uint32) bool {
		for _, x := range engaged {
			if x == e {
				return true
			}
		}
		return false
	}
	if s.K == "boss" && s.E > 0 && holds(s.E) {
		return true
	}
	for _, e := range s.Entries {
		if e > 0 && holds(e) {
			return true
		}
	}
	return false
}
