package state

import (
	"errors"
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
)

// commandersState is the latest COMMANDERS payload. It has its own lock so a
// 2 s datagram never waits on the roster lock.
type commandersState struct {
	mu      sync.Mutex
	session uint64
	ts      int64
	at      time.Time
	list    []model.CommanderBinding
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
		if b.Bots == nil {
			b.Bots = []model.CommanderBot{}
		}
		if b.Orders == nil {
			b.Orders = []model.CommanderOrder{}
		}
		out = append(out, b)
	}
	st.session, st.ts, st.at, st.list = p.Session, p.TS, s.now(), out
	return true
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
