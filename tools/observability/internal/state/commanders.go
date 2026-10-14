package state

import (
	"errors"
	"math"
	"net/url"
	"strings"
	"sync"
	"time"

	"tortoise-observability/internal/model"
)

// CommandersStaleAfter is how long the last COMMANDERS datagram stays valid.
// The module sends one every 2 s snapshot cycle while a binding stands, so a
// silence of this long means the server stopped, restarted or lost telemetry,
// and the tab must not keep showing bindings that may be gone.
const CommandersStaleAfter = 12 * time.Second

// Bounds on what one datagram may put in the store (the emitter sends far
// less); a datagram past them is trimmed, not rejected.
const (
	maxCommanderBindings = 16
	maxCommanderBots     = 8
	maxCommanderMobs     = 24
	maxCommanderOrders   = 16
	maxCommanderObjects  = 24
	maxCommanderSteps    = 24
)

// Short server-side history of where the bound bots have been, so a browser that opens the page mid-run starts with the paths already
// drawn instead of an empty map. One point per TrailMinStepYd yards of movement (the dashboard draws the same way), at most TrailMaxPoints
// per bot; a bot that changes map starts a new trail. Memory only, bounded by bindings x bots x points (16 x 8 x 240 x 24 bytes).
const (
	TrailMaxPoints = 240
	TrailMinStepYd = 2.0
)

// TrailPoint is a place a bot was at: game x, y and the unix second of the report.
type TrailPoint struct {
	X float32
	Y float32
	T int64
}

// BotTrail is one bot's remembered path on one map.
type BotTrail struct {
	Map    uint32
	Points []TrailPoint
}

// commandersState is the latest COMMANDERS payload. It has its own lock so a
// 2 s datagram never waits on the roster lock.
type commandersState struct {
	mu      sync.Mutex
	session uint64
	ts      int64
	at      time.Time
	list    []model.CommanderBinding
	trails  map[string]map[uint32]*BotTrail // commander -> bot guid -> trail
}

// ApplyCommanders replaces the standing bindings with the datagram's list. A
// datagram of an older server session, or older than the one held in the same
// session (UDP may reorder), is ignored. It reports whether the view changed.
func (s *Store) ApplyCommanders(p *model.CommandersPayload) bool {
	if p == nil {
		return false
	}
	st := &s.commanders
	st.mu.Lock()
	defer st.mu.Unlock()
	if p.Session < st.session {
		return false
	}
	if p.Session == st.session && p.TS < st.ts {
		return false
	}
	list := p.Commanders
	if len(list) > maxCommanderBindings {
		list = list[:maxCommanderBindings]
	}
	out := make([]model.CommanderBinding, 0, len(list))
	for _, b := range list {
		if len(b.Bots) > maxCommanderBots {
			b.Bots = b.Bots[:maxCommanderBots]
		}
		if len(b.Orders) > maxCommanderOrders {
			b.Orders = b.Orders[len(b.Orders)-maxCommanderOrders:]
		}
		if len(b.Mobs) > maxCommanderMobs {
			b.Mobs = b.Mobs[:maxCommanderMobs]
		}
		if len(b.Objects) > maxCommanderObjects {
			b.Objects = b.Objects[:maxCommanderObjects]
		}
		if b.Run != nil && len(b.Run.PlanSteps) > maxCommanderSteps {
			run := *b.Run // a copy: the payload's own struct is not changed
			run.PlanSteps = run.PlanSteps[:maxCommanderSteps]
			b.Run = &run
		}
		if b.Bots == nil {
			b.Bots = []model.CommanderBot{}
		}
		if b.Orders == nil {
			b.Orders = []model.CommanderOrder{}
		}
		out = append(out, b)
	}
	st.session, st.ts, st.at, st.list = p.Session, p.TS, s.now(), out
	st.noteTrails(out, p.TS)
	return true
}

// noteTrails adds the bots' places to their trails and forgets the commanders (and bots) that are no longer reported. Called with the lock held.
func (st *commandersState) noteTrails(list []model.CommanderBinding, ts int64) {
	if st.trails == nil {
		st.trails = map[string]map[uint32]*BotTrail{}
	}
	live := map[string]map[uint32]bool{}
	for _, b := range list {
		bots := st.trails[b.Commander]
		if bots == nil {
			bots = map[uint32]*BotTrail{}
			st.trails[b.Commander] = bots
		}
		seen := live[b.Commander]
		if seen == nil {
			seen = map[uint32]bool{}
			live[b.Commander] = seen
		}
		for _, bot := range b.Bots {
			if bot.GUID == 0 {
				continue
			}
			seen[bot.GUID] = true
			t := bots[bot.GUID]
			if t == nil || t.Map != bot.MapID {
				t = &BotTrail{Map: bot.MapID}
				bots[bot.GUID] = t
			}
			if n := len(t.Points); n > 0 {
				last := t.Points[n-1]
				if math.Hypot(float64(last.X)-bot.X, float64(last.Y)-bot.Y) < TrailMinStepYd {
					continue
				}
			}
			t.Points = append(t.Points, TrailPoint{X: float32(bot.X), Y: float32(bot.Y), T: ts})
			if len(t.Points) > TrailMaxPoints {
				t.Points = append(t.Points[:0], t.Points[len(t.Points)-TrailMaxPoints:]...)
			}
		}
	}
	for c, bots := range st.trails {
		if live[c] == nil {
			delete(st.trails, c)
			continue
		}
		for g := range bots {
			if !live[c][g] {
				delete(bots, g)
			}
		}
	}
}

// Trails returns copies of the remembered paths, commander -> bot guid -> trail; empty once the last datagram is stale (the paths are
// then from a server that may have restarted).
func (s *Store) Trails() map[string]map[uint32]BotTrail {
	st := &s.commanders
	st.mu.Lock()
	defer st.mu.Unlock()
	out := map[string]map[uint32]BotTrail{}
	if st.at.IsZero() || s.now().Sub(st.at) > CommandersStaleAfter {
		return out
	}
	for c, bots := range st.trails {
		m := make(map[uint32]BotTrail, len(bots))
		for g, t := range bots {
			m[g] = BotTrail{Map: t.Map, Points: append([]TrailPoint(nil), t.Points...)}
		}
		out[c] = m
	}
	return out
}

// Commanders returns the standing bindings, empty once the last datagram is
// older than CommandersStaleAfter.
func (s *Store) Commanders() model.CommandersView {
	st := &s.commanders
	st.mu.Lock()
	defer st.mu.Unlock()
	view := model.CommandersView{Commanders: []model.CommanderBinding{}}
	if st.at.IsZero() {
		return view
	}
	age := s.now().Sub(st.at)
	view.UpdatedAt = st.at.Unix()
	view.AgeSec = age.Seconds()
	if age <= CommandersStaleAfter {
		view.Fresh = true
		view.Commanders = append(view.Commanders, st.list...)
	}
	return view
}

// NormalizeCommanderURL checks the address of an external commander dashboard
// (TBDASH_COMMANDER_URL): an http or https URL with a host, no credentials.
// It returns the URL without a trailing slash, or "" for an empty input.
func NormalizeCommanderURL(raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return "", nil
	}
	u, err := url.Parse(raw)
	if err != nil {
		return "", err
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return "", errors.New("must start with http:// or https://")
	}
	if u.Hostname() == "" {
		return "", errors.New("has no host")
	}
	if u.User != nil {
		return "", errors.New("must not carry credentials")
	}
	if u.Fragment != "" {
		u.Fragment = ""
	}
	return strings.TrimRight(u.String(), "/"), nil
}
