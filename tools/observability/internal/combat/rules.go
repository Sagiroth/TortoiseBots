// The v1 combat rules (raid recon L13): what the dashboard flags on a fight.
// More rows later, not code: a rule with an unknown condition fails LoadRules
// so a typo in rules.json cannot silently judge with the wrong test.
package combat

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"strings"
)

//go:embed rules.json
var defaultRules []byte

func DefaultRules() []byte { return defaultRules }

// LoadRules parses the rule table. Unknown conditions fail: a rule whose
// test the daemon cannot run must not load as judging nothing.
func LoadRules(b []byte) ([]Rule, error) {
	var rules []Rule
	if err := json.Unmarshal(b, &rules); err != nil {
		return nil, err
	}
	for _, r := range rules {
		if _, err := parseCondition(r.Condition); err != nil {
			return nil, fmt.Errorf("rule %q: %w", r.ID, err)
		}
		if r.ID == "" || r.Level == "" {
			return nil, fmt.Errorf("rule %q: id and level are required", r.ID)
		}
	}
	return rules, nil
}

type conditionKind int

const (
	condSpellCountZero conditionKind = iota
	condSpellsCountZero
	condNoHealing
	condNoDamage
)

type condition struct {
	kind   conditionKind
	spells []string
}

func parseCondition(s string) (condition, error) {
	if name, ok := strings.CutPrefix(s, "spell_count_zero:"); ok {
		name = strings.TrimSpace(name)
		if name == "" {
			return condition{}, fmt.Errorf("bad condition %q", s)
		}
		return condition{kind: condSpellCountZero, spells: []string{name}}, nil
	}
	if names, ok := strings.CutPrefix(s, "spells_count_zero:"); ok {
		parts := strings.Split(names, ",")
		var spells []string
		for _, p := range parts {
			if p = strings.TrimSpace(p); p != "" {
				spells = append(spells, p)
			}
		}
		if len(spells) == 0 {
			return condition{}, fmt.Errorf("bad condition %q", s)
		}
		return condition{kind: condSpellsCountZero, spells: spells}, nil
	}
	switch s {
	case "no_healing":
		return condition{kind: condNoHealing}, nil
	case "no_damage":
		return condition{kind: condNoDamage}, nil
	}
	return condition{}, fmt.Errorf("unknown condition %q", s)
}

// spellCasts counts casts of one spell on a bot (the module lists every cast
// spell on the bot line, so quiet means zero).
func spellCasts(b FightBot, name string) uint32 {
	for _, sp := range b.Spells {
		if sp.N == name {
			return sp.C
		}
	}
	return 0
}

func (c condition) violatedBy(b FightBot) bool {
	switch c.kind {
	case condSpellCountZero:
		return spellCasts(b, c.spells[0]) == 0
	case condSpellsCountZero:
		for _, name := range c.spells {
			if spellCasts(b, name) > 0 {
				return false
			}
		}
		return true
	case condNoHealing:
		return b.HealCasts == 0 && b.Heal == 0
	case condNoDamage:
		return b.Dmg == 0
	}
	return false
}

// deadWholeFight skips a bot that died before it could act (a death at
// t_ms < 5000): the rules judge what it did, not that it died.
func deadWholeFight(b FightBot, deaths []FightDeath) bool {
	if b.Deaths == 0 {
		return false
	}
	for _, d := range deaths {
		if d.Name == b.Name && d.TMs < 5000 {
			return true
		}
	}
	return false
}

// Judge runs the rule table over one fight. Duration is seconds (dur_ms /
// 1000): a short fight warns nothing. Class "" matches any class.
func Judge(f Fight, rules []Rule) []Warning {
	var out []Warning
	durS := float64(f.DurMS) / 1000
	for _, r := range rules {
		cond, err := parseCondition(r.Condition)
		if err != nil {
			continue
		}
		if durS < float64(r.MinFightS) {
			continue
		}
		for _, b := range f.Bots {
			if r.Role != "" && b.Role != r.Role {
				continue
			}
			if r.Class != "" && !strings.EqualFold(b.Class, r.Class) {
				continue
			}
			if deadWholeFight(b, f.Deaths) {
				continue
			}
			if cond.violatedBy(b) {
				out = append(out, Warning{
					Rule:  r.ID,
					Level: r.Level,
					Bot:   b.Name,
					Role:  b.Role,
					Text:  strings.ReplaceAll(r.Text, "{bot}", b.Name),
				})
			}
		}
	}
	return out
}
