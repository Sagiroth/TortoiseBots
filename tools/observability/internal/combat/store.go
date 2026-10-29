// The combat fight store: per commander the last fights, newest first,
// deduped by (session, id). The module sends each finished fight in two
// consecutive snapshot cycles (UDP loss); the second take is dropped here.
//
// Fight ids repeat across commanders and sessions (a per-run counter), so
// the store keys every fight by (commander, session, id): the detail route
// takes all three and Get requires all three.
package combat

import (
	"sort"
	"strconv"
	"sync"
	"time"
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
	live    map[string]*liveFight // commander -> the one open fight (live.go)
	now     func() time.Time      // tests inject a clock; nil is time.Now
}

// NewStore builds a store judging with rules (nil rules: store only, every
// summary reads 0 warnings).
func NewStore(rules []Rule) *Store {
	return &Store{
		rules:   rules,
		fights:  make(map[string][]keptFight),
		seenIDs: make(map[string]map[string]bool),
		live:    make(map[string]*liveFight),
	}
}

// SetClock replaces the store's clock (live-fight expiry); nil restores time.Now.
func (s *Store) SetClock(now func() time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.now = now
}

func (s *Store) nowLocked() time.Time {
	if s.now != nil {
		return s.now()
	}
	return time.Now()
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
	_, ok := s.AddSummary(session, f)
	return ok
}

// AddSummary files a fight like Add and returns its list row, which the live
// stream pushes as the "fight" frame. Filing it closes the live row of the
// same (session, id) the COMBAT_EVENTS datagrams opened.
func (s *Store) AddSummary(session uint64, f Fight) (FightSummary, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.addLocked(session, f) {
		return FightSummary{}, false
	}
	return s.summaryLocked(f.Commander, keptFight{session: session, fight: f}), true
}

// addLocked files one fight; called with the lock held.
func (s *Store) addLocked(session uint64, f Fight) bool {
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
	if lf := s.live[f.Commander]; lf != nil && lf.session == session && lf.id == f.ID {
		delete(s.live, f.Commander)
	}
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

// List returns the newest-first summaries of a commander's kept fights, the
// live fight first while one runs.
func (s *Store) List(commander string) []FightSummary {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expireLocked(s.nowLocked())
	out := make([]FightSummary, 0, len(s.fights[commander])+1)
	if lf := s.live[commander]; lf != nil {
		out = append(out, s.liveSummaryLocked(commander, lf))
	}
	for _, k := range s.fights[commander] {
		out = append(out, s.summaryLocked(commander, k))
	}
	return out
}

// summaryLocked builds one list row; called with the lock held.
func (s *Store) summaryLocked(commander string, k keptFight) FightSummary {
	return FightSummary{
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
	}
}

// LiveRows returns the summaries of the open fights, of the given commanders
// or of all of them when names is empty (the SSE snapshot frame).
func (s *Store) LiveRows(names []string) []FightSummary {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expireLocked(s.nowLocked())
	want := make(map[string]bool, len(names))
	for _, n := range names {
		want[n] = true
	}
	out := make([]FightSummary, 0, len(s.live))
	for c, lf := range s.live {
		if len(want) > 0 && !want[c] {
			continue
		}
		out = append(out, s.liveSummaryLocked(c, lf))
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Key < out[j].Key })
	return out
}

// Get returns a fight and its warnings by commander, session and id.
func (s *Store) Get(commander string, session uint64, id string) (Fight, []Warning, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expireLocked(s.nowLocked())
	if lf := s.live[commander]; lf != nil && lf.session == session && lf.id == id {
		return s.liveFightLocked(commander, lf), nil, true
	}
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
	s.expireLocked(s.nowLocked())
	for commander, lf := range s.live {
		if Key(commander, lf.session, lf.id) == key {
			return s.liveFightLocked(commander, lf), nil, true
		}
	}
	for commander, list := range s.fights {
		for _, k := range list {
			if Key(commander, k.session, k.fight.ID) == key {
				return k.fight, Judge(k.fight, s.rules), true
			}
		}
	}
	return Fight{}, nil, false
}
