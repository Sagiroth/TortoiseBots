// The combat fight store: per commander the last fights, newest first,
// deduped by (session, id). The module sends each finished fight in two
// consecutive snapshot cycles (UDP loss); the second take is dropped here.
//
// Fight ids repeat across commanders and sessions (a per-run counter), so
// the store keys every fight by (commander, session, id): the detail route
// takes all three and Get requires all three.
package combat

import (
	"strconv"
	"sync"
)

// maxFightsPerCommander bounds memory: a raid night is a handful of fights,
// and the daemon never pages this to disk.
const maxFightsPerCommander = 50

type keptFight struct {
	session uint64
	fight   Fight
}

// Store keeps finished fights per commander, newest first. Reads served by
// the HTTP routes; Add from the UDP listener. One mutex: UDP ingestion never
// blocks on a client.
type Store struct {
	mu      sync.Mutex
	rules   []Rule
	fights  map[string][]keptFight // commander -> newest first
	seenIDs map[string]map[string]bool
}

// NewStore builds a store judging with rules (nil rules: store only, every
// summary reads 0 warnings).
func NewStore(rules []Rule) *Store {
	return &Store{
		rules:   rules,
		fights:  make(map[string][]keptFight),
		seenIDs: make(map[string]map[string]bool),
	}
}

// Rules returns the table the store judges with.
func (s *Store) Rules() []Rule {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]Rule(nil), s.rules...)
}

// Key composes the public fight key: commander, session and id. The detail
// route passes it through opaquely; the ids it joins may repeat across
// commanders and sessions.
func Key(commander string, session uint64, id string) string {
	return commander + "\x00" + strconv.FormatUint(session, 10) + "\x00" + id
}

// Add files a fight under its commander. False when the (session, id) was
// seen before: the module's second take of the same fight.
func (s *Store) Add(session uint64, f Fight) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	seen := s.seenIDs[f.Commander]
	if seen == nil {
		seen = make(map[string]bool)
		s.seenIDs[f.Commander] = seen
	}
	key := strconv.FormatUint(session, 10) + "\x00" + f.ID
	if seen[key] {
		return false
	}
	seen[key] = true
	list := append([]keptFight{{session: session, fight: f}}, s.fights[f.Commander]...)
	if len(list) > maxFightsPerCommander {
		for _, dropped := range list[maxFightsPerCommander:] {
			delete(seen, strconv.FormatUint(dropped.session, 10)+"\x00"+dropped.fight.ID)
		}
		list = list[:maxFightsPerCommander]
	}
	s.fights[f.Commander] = list
	return true
}

// List returns the newest-first summaries of a commander's kept fights.
func (s *Store) List(commander string) []FightSummary {
	s.mu.Lock()
	defer s.mu.Unlock()
	var out []FightSummary
	for _, k := range s.fights[commander] {
		out = append(out, FightSummary{
			ID:       k.fight.ID,
			Key:      Key(commander, k.session, k.fight.ID),
			Session:  k.session,
			Start:    k.fight.Start,
			Dur:      k.fight.DurMS,
			Outcome:  k.fight.Outcome,
			BossName: k.fight.BossName,
			Leg:      k.fight.Leg,
			Step:     k.fight.Step,
			Deaths:   len(k.fight.Deaths),
			Warnings: len(Judge(k.fight, s.rules)),
		})
	}
	return out
}

// Get returns a fight and its warnings by commander, session and id.
func (s *Store) Get(commander string, session uint64, id string) (Fight, []Warning, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, k := range s.fights[commander] {
		if k.session == session && k.fight.ID == id {
			return k.fight, Judge(k.fight, s.rules), true
		}
	}
	return Fight{}, nil, false
}

// GetKey returns a fight and its warnings by the composed public key.
func (s *Store) GetKey(key string) (Fight, []Warning, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for commander, list := range s.fights {
		for _, k := range list {
			if Key(commander, k.session, k.fight.ID) == key {
				return k.fight, Judge(k.fight, s.rules), true
			}
		}
	}
	return Fight{}, nil, false
}
