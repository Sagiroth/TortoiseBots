// The live fight: the COMBAT_EVENTS datagrams the module sends while a bound
// group fights, kept as one open row per commander. The finished fight still
// arrives as COMBAT_FIGHT (twice); Add closes the live row, so the list never
// shows a fight twice. A live row that goes quiet for LiveExpire is filed as
// "unfinished": the fight happened, and the debugger can still open it.
package combat

import (
	"encoding/json"
	"strconv"
	"time"
)

// LiveEventsMax bounds the events one live fight keeps (and the deaths the
// detail builds from them). The module queues at most this many per book.
const LiveEventsMax = 200

// LiveLogMax bounds the events of the kinds a fight log keeps (cast,
// interrupt, taunt and death): those are what a long fight is replayed from,
// so they get their own, higher ring. The fight the row becomes keeps at most
// as many.
const LiveLogMax = 1000

// isLogKind reports whether a finished fight keeps an event of this kind in its
// log: the casts, interrupts, taunts and deaths the debugger replays.
func isLogKind(k string) bool {
	switch k {
	case "cast", "interrupt", "taunt", "death":
		return true
	}
	return false
}

// LiveExpire is how long a live fight may go without a datagram before it is
// filed as unfinished (the module's keepalive is 5 s).
const LiveExpire = 15 * time.Minute

// maxLiveCommanders bounds the open fights; the oldest is filed when it is
// passed.
const maxLiveCommanders = 32

// LiveHeader is the fight object of a COMBAT_EVENTS datagram (keys as the
// finished COMBAT_FIGHT's, so the live row and the fight it becomes line up).
type LiveHeader struct {
	ID       string `json:"id"`
	Start    int64  `json:"start"`
	StartMs  int64  `json:"start_ms"`
	Plan     string `json:"plan"`
	Leg      string `json:"leg"`
	Step     uint32 `json:"step"`
	Kind     string `json:"kind"`
	Boss     uint32 `json:"boss"`
	BossName string `json:"boss_name"`
	FightMs  uint32 `json:"fight_ms"`
}

// EventsDatagram is the COMBAT_EVENTS envelope (wire format 2.1). Fight is
// null only in the datagram that carries the end of a fight and opens none.
type EventsDatagram struct {
	V             int               `json:"v"`
	FMT           int               `json:"fmt"`
	Session       uint64            `json:"session"`
	Seq           uint64            `json:"seq"`
	TS            int64             `json:"ts"`
	TSMs          int64             `json:"ts_ms"`
	Type          string            `json:"type"`
	Commander     string            `json:"commander"`
	CommanderGUID uint32            `json:"commander_guid"`
	Group         uint32            `json:"group"`
	Map           uint32            `json:"map"`
	Fight         *LiveHeader       `json:"fight"`
	Dropped       uint32            `json:"dropped"`
	Events        []json.RawMessage `json:"events"`
}

// liveEvent is the part of one event this daemon reads.
type liveEvent struct {
	K    string  `json:"k"`
	T    uint32  `json:"t"`
	N    string  `json:"n"`
	By   string  `json:"by"`
	X    float64 `json:"x"`
	Y    float64 `json:"y"`
	O    string  `json:"o"`
	Dur  uint32  `json:"dur"`
	Kept *bool   `json:"kept"`
}

type liveFight struct {
	session  uint64
	seq      uint64
	id       string
	group    uint32
	mapID    uint32
	header   LiveHeader
	events   []json.RawMessage
	kinds    []bool // kinds[i]: events[i] is of a kind the fight log keeps
	logged   int    // events of the log kinds held in events
	dropped  int    // log-kind events trimmed past LiveLogMax, so the filed fight can report them
	other    int    // events of every other kind held in events
	deaths   []FightDeath
	lastSeen time.Time
	outcome  string
	ended    bool
	endDur   uint32
}

// ApplyEvents ingests one COMBAT_EVENTS datagram and reports the live row it
// left, whether the row was created or changed (the fight_live frame) and
// whether ingestion accepted the datagram at all. Datagrams are ordered by
// (session, seq), the module's per-session counter: a repeated or an older
// one is dropped before it can touch the header or the events, so UDP
// duplication cannot double a death and reordering cannot roll the fight
// back. A newer session starts over with that datagram's seq. A rejected
// datagram must not reach the live stream either: its events are the same
// ones the row already holds, or belong to a fight that is over.
func (s *Store) ApplyEvents(dg EventsDatagram, now time.Time) (FightSummary, bool, bool) {
	if dg.FMT != 1 || dg.Commander == "" {
		return FightSummary{}, false, false
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.live == nil {
		s.live = make(map[string]*liveFight)
	}
	s.expireLocked(now)

	row := s.live[dg.Commander]
	if row != nil {
		if dg.Session < row.session {
			return FightSummary{}, false, false // a datagram of an older game session
		}
		if dg.Session == row.session && dg.Seq <= row.seq {
			return FightSummary{}, false, false // a duplicate or a reordered datagram
		}
	}
	changed := false
	if h := dg.Fight; h != nil && h.ID != "" {
		switch {
		case row != nil && row.session == dg.Session && row.id == h.ID:
			row.header = *h
			row.seq = dg.Seq
		case s.seenLocked(dg.Commander, dg.Session, h.ID):
			// The finished fight is already filed: a late datagram of it.
			return FightSummary{}, false, false
		default:
			if row != nil {
				// The new fight replaces the live row, whose own end may
				// never arrive: file it the way the expiry path files a
				// quiet row, so neither the fight nor its log is lost.
				s.fileLiveLocked(dg.Commander, row, "unfinished")
			}
			row = &liveFight{session: dg.Session, seq: dg.Seq, id: h.ID, group: dg.Group, mapID: dg.Map, header: *h}
			s.live[dg.Commander] = row
			changed = true
			s.evictLiveLocked()
		}
	} else {
		// A datagram that opens no fight only continues the live row of its
		// own session.
		if row == nil || row.session != dg.Session {
			return FightSummary{}, false, false
		}
		row.seq = dg.Seq
	}
	row.lastSeen = now

	for _, raw := range dg.Events {
		var ev liveEvent
		if err := json.Unmarshal(raw, &ev); err != nil {
			continue
		}
		if ev.K == "end" {
			kept := ev.Kept != nil && *ev.Kept
			if !kept {
				// Shorter than the module's minimum: no COMBAT_FIGHT follows.
				sum := s.liveSummaryLocked(dg.Commander, row)
				sum.Outcome, sum.Live = ev.O, false
				delete(s.live, dg.Commander)
				return sum, true, true
			}
			row.ended, row.outcome, row.endDur = true, ev.O, ev.Dur
			changed = true
		}
		row.noteEvent(raw, ev.K)
		if ev.K == "death" {
			row.deaths = append(row.deaths, FightDeath{Name: ev.N, TMs: ev.T, X: ev.X, Y: ev.Y, Killer: ev.By})
			if len(row.deaths) > LiveEventsMax {
				row.deaths = append(row.deaths[:0], row.deaths[len(row.deaths)-LiveEventsMax:]...)
			}
		}
	}
	return s.liveSummaryLocked(dg.Commander, row), changed, true
}

// noteEvent adds one event to the row's ring, oldest first. Each class has its own cap: the kinds a fight log keeps up to
// LiveLogMax, every other kind LiveEventsMax. Past a cap, the oldest event of that class goes, so a busy fight keeps its
// newest casts without losing the events that only the live view reads.
func (lf *liveFight) noteEvent(raw json.RawMessage, kind string) {
	logged := isLogKind(kind)
	lf.events = append(lf.events, raw)
	lf.kinds = append(lf.kinds, logged)
	if logged {
		lf.logged++
		if lf.logged > LiveLogMax {
			lf.dropOldest(true)
			lf.dropped++
		}
		return
	}
	lf.other++
	if lf.other > LiveEventsMax {
		lf.dropOldest(false)
	}
}

// dropOldest removes the oldest event of one class from the ring; true is the log kinds, false the others.
func (lf *liveFight) dropOldest(log bool) {
	for i, isLog := range lf.kinds {
		if isLog != log {
			continue
		}
		n := len(lf.events)
		copy(lf.events[i:], lf.events[i+1:])
		lf.events[n-1] = nil // the copy left the old last event referenced past the slice
		lf.events = lf.events[:n-1]
		copy(lf.kinds[i:], lf.kinds[i+1:])
		lf.kinds = lf.kinds[:n-1]
		if log {
			lf.logged--
		} else {
			lf.other--
		}
		return
	}
}

// logForFile returns the events a filed fight keeps: the log kinds in order, their newest LiveLogMax of them, and how many
// were dropped for the cap.
func (lf *liveFight) logForFile() ([]json.RawMessage, int) {
	out := make([]json.RawMessage, 0, lf.logged)
	for i, raw := range lf.events {
		if lf.kinds[i] {
			out = append(out, raw)
		}
	}
	dropped := lf.dropped
	if len(out) > LiveLogMax {
		extra := len(out) - LiveLogMax
		dropped += extra
		out = out[extra:]
	}
	if len(out) == 0 {
		return nil, 0
	}
	return out, dropped
}

// seenLocked reports whether a finished fight of this (session, id) is filed.
func (s *Store) seenLocked(commander string, session uint64, id string) bool {
	seen := s.seenIDs[commander]
	if seen == nil {
		return false
	}
	return seen[strconv.FormatUint(session, 10)+"\x00"+id]
}

// expireLocked files the live rows that went quiet as unfinished.
func (s *Store) expireLocked(now time.Time) {
	for c, lf := range s.live {
		if now.Sub(lf.lastSeen) > LiveExpire {
			s.fileLiveLocked(c, lf, "unfinished")
			delete(s.live, c)
		}
	}
}

// evictLiveLocked files the oldest live rows past maxLiveCommanders.
func (s *Store) evictLiveLocked() {
	for len(s.live) > maxLiveCommanders {
		var oldest string
		var at time.Time
		for c, lf := range s.live {
			if oldest == "" || lf.lastSeen.Before(at) {
				oldest, at = c, lf.lastSeen
			}
		}
		s.fileLiveLocked(oldest, s.live[oldest], "unfinished")
		delete(s.live, oldest)
	}
}

// fileLiveLocked files a live row as a finished, summary-only fight so it
// stays in the debugger. The filing is provisional: the fight's own
// COMBAT_FIGHT still replaces it when it arrives late. Its (session, id) is
// marked seen: the row and the fight it describes must not both appear.
func (s *Store) fileLiveLocked(commander string, lf *liveFight, outcome string) {
	h := lf.header
	dur := h.FightMs
	if lf.ended && lf.endDur > 0 {
		dur = lf.endDur
	}
	f := Fight{
		ID: lf.id, Commander: commander, Group: lf.group, Map: lf.mapID,
		Plan: h.Plan, Leg: h.Leg, Step: h.Step, Kind: h.Kind,
		Start: h.Start, DurMS: dur, Outcome: outcome,
		Boss: h.Boss, BossName: h.BossName,
		Bots:   []FightBot{},
		Deaths: append([]FightDeath(nil), lf.deaths...),
	}
	// addLocked prepends: the row is the newest by the time it was filed.
	s.addLocked(lf.session, f, true)
}

// liveSummaryLocked builds the list row of a live fight.
func (s *Store) liveSummaryLocked(commander string, lf *liveFight) FightSummary {
	h := lf.header
	dur := h.FightMs
	outcome := "live"
	if lf.ended {
		outcome = lf.outcome
		if lf.endDur > 0 {
			dur = lf.endDur
		}
	}
	return FightSummary{
		ID: lf.id, Key: Key(commander, lf.session, lf.id), Session: lf.session,
		Start: h.Start, Dur: dur, Outcome: outcome, BossName: h.BossName,
		Leg: h.Leg, Step: h.Step, Deaths: len(lf.deaths), Live: true,
	}
}

// liveFightLocked builds the detail of a live fight: the header, the outcome
// and the deaths so far. It has no per-bot rows (nothing finished to judge).
func (s *Store) liveFightLocked(commander string, lf *liveFight) Fight {
	h := lf.header
	dur := h.FightMs
	outcome := "live"
	if lf.ended {
		outcome = lf.outcome
		if lf.endDur > 0 {
			dur = lf.endDur
		}
	}
	return Fight{
		ID: lf.id, Commander: commander, Group: lf.group, Map: lf.mapID,
		Plan: h.Plan, Leg: h.Leg, Step: h.Step, Kind: h.Kind,
		Start: h.Start, DurMS: dur, Outcome: outcome,
		Boss: h.Boss, BossName: h.BossName,
		Bots:   []FightBot{},
		Deaths: append([]FightDeath(nil), lf.deaths...),
		Debuffs: FightDebuffs{
			UpS: map[string]uint64{},
		},
	}
}
