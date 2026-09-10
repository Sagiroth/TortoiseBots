package state

import (
	"math"
	"sort"
	"sync"
	"time"

	"tortoise-observability/internal/model"
)

// Episode thresholds. Snapshot-derived conditions (stuck, dead) are detected
// here; anomaly-derived ones (action loop, unreachable) are refreshed by the
// emitter and expire if it stops reporting.
const (
	issueWatchAfter      = 60 * time.Second
	issuePersistentAfter = 10 * time.Minute
	issueStuckAfter      = 60 * time.Second
	issueDeadAfter       = 2 * time.Minute
	issueAnomalyTTL      = 2 * time.Minute
	issueResolvedMax     = 200
)

type issueKey struct {
	guid uint32
	typ  string
}

// botDetector holds the per-bot state needed to time a condition.
type botDetector struct {
	seen            bool
	lastX, lastY    float64
	stationarySince time.Time
	deadSince       time.Time
}

type activeIssue struct {
	issue     model.Issue
	startedAt time.Time
	expiresAt time.Time // zero for snapshot-derived episodes
}

// issueTracker keeps persistent bot problems as open/closed episodes. It is
// bounded: only currently-active episodes plus a small resolved history.
type issueTracker struct {
	mu        sync.Mutex
	active    map[issueKey]*activeIssue
	detectors map[uint32]*botDetector
	resolved  []model.Issue
}

func newIssueTracker() *issueTracker {
	return &issueTracker{
		active:    make(map[issueKey]*activeIssue),
		detectors: make(map[uint32]*botDetector),
	}
}

func issueSeverity(d time.Duration) string {
	if d >= issuePersistentAfter {
		return "persistent"
	}
	return "watch"
}

// Observe reconciles snapshot-derived conditions (stuck, dead) with the active
// episodes. It is authoritative: a condition missing from this snapshot closes
// its episode.
func (t *issueTracker) Observe(bots []model.BotSnapshot, now time.Time) {
	t.mu.Lock()
	defer t.mu.Unlock()

	type cond struct {
		start   time.Time
		action  string
		trigger string
		target  string
	}
	present := make(map[issueKey]cond)
	alive := make(map[uint32]bool, len(bots))
	meta := make(map[uint32]model.BotSnapshot, len(bots))

	for i := range bots {
		b := &bots[i]
		alive[b.GUID] = true
		meta[b.GUID] = *b

		d := t.detectors[b.GUID]
		if d == nil {
			d = &botDetector{}
			t.detectors[b.GUID] = d
		}

		moved := 0.0
		if d.seen {
			moved = math.Hypot(b.X-d.lastX, b.Y-d.lastY)
		}
		d.seen = true
		d.lastX, d.lastY = b.X, b.Y

		// Stuck: the movement generator is active but the bot is not moving.
		if b.State == "moving" && moved < 0.5 {
			if d.stationarySince.IsZero() {
				d.stationarySince = now
			}
			if now.Sub(d.stationarySince) >= issueStuckAfter {
				present[issueKey{b.GUID, "STUCK"}] = cond{d.stationarySince, b.LastAction, b.LastTrigger, b.Target}
			}
		} else {
			d.stationarySince = time.Time{}
		}

		// Dead long enough to be a problem rather than a normal corpse run.
		if b.State == "dead" {
			if d.deadSince.IsZero() {
				d.deadSince = now
			}
			if now.Sub(d.deadSince) >= issueDeadAfter {
				present[issueKey{b.GUID, "DEAD_LONG"}] = cond{start: d.deadSince}
			}
		} else {
			d.deadSince = time.Time{}
		}
	}

	for guid := range t.detectors {
		if !alive[guid] {
			delete(t.detectors, guid)
		}
	}

	for key, c := range present {
		ai := t.active[key]
		if ai == nil {
			ai = &activeIssue{startedAt: c.start}
			t.active[key] = ai
		}
		b := meta[key.guid]
		ai.expiresAt = time.Time{}
		ai.issue = model.Issue{
			GUID: key.guid, Bot: b.Name, Class: b.Class, Level: b.Level,
			Type: key.typ, Action: c.action, Trigger: c.trigger, Target: c.target,
			MapID: b.MapID, ZoneID: b.ZoneID,
		}
	}

	// Close anything no longer present, or anomaly-driven episodes whose TTL
	// lapsed without a refresh.
	for key, ai := range t.active {
		if _, ok := present[key]; ok {
			continue
		}
		if !ai.expiresAt.IsZero() && now.Before(ai.expiresAt) {
			continue
		}
		t.closeLocked(key, ai, now)
	}

	for _, ai := range t.active {
		d := now.Sub(ai.startedAt)
		ai.issue.DurationSec = d.Seconds()
		ai.issue.Severity = issueSeverity(d)
	}
}

// TouchAnomaly opens or refreshes an episode for anomaly types that cannot be
// derived from a snapshot.
func (t *issueTracker) TouchAnomaly(a model.AnomalyPayload, now time.Time) {
	var typ string
	switch a.Type {
	case "ACTION_LOOP", "UNREACHABLE_TARGET":
		typ = a.Type
	default:
		return
	}

	t.mu.Lock()
	defer t.mu.Unlock()

	key := issueKey{a.GUID, typ}
	ai := t.active[key]
	if ai == nil {
		ai = &activeIssue{startedAt: now}
		t.active[key] = ai
	}
	ai.expiresAt = now.Add(issueAnomalyTTL)
	ai.issue = model.Issue{
		GUID: a.GUID, Bot: a.Bot, Class: a.Class, Level: a.Level, Type: typ,
		Action: a.LastAction, Target: a.Target, Details: a.Details,
		MapID: a.MapID, ZoneID: a.ZoneID,
		DurationSec: now.Sub(ai.startedAt).Seconds(),
		Severity:    issueSeverity(now.Sub(ai.startedAt)),
	}
}

func (t *issueTracker) closeLocked(key issueKey, ai *activeIssue, now time.Time) {
	issue := ai.issue
	issue.DurationSec = now.Sub(ai.startedAt).Seconds()
	issue.Severity = issueSeverity(now.Sub(ai.startedAt))
	delete(t.active, key)
	t.resolved = append(t.resolved, issue)
	if len(t.resolved) > issueResolvedMax {
		t.resolved = t.resolved[len(t.resolved)-issueResolvedMax:]
	}
}

// Snapshot returns active issues (longest first), recently resolved ones
// (newest first), and per-type active counts.
func (t *issueTracker) Snapshot() model.IssueSnapshot {
	t.mu.Lock()
	defer t.mu.Unlock()

	active := make([]model.Issue, 0, len(t.active))
	counts := make(map[string]int)
	for key, ai := range t.active {
		active = append(active, ai.issue)
		counts[key.typ]++
	}
	sort.Slice(active, func(i, j int) bool { return active[i].DurationSec > active[j].DurationSec })

	resolved := make([]model.Issue, len(t.resolved))
	copy(resolved, t.resolved)
	for i, j := 0, len(resolved)-1; i < j; i, j = i+1, j-1 {
		resolved[i], resolved[j] = resolved[j], resolved[i]
	}

	return model.IssueSnapshot{Active: active, Resolved: resolved, CountsByType: counts}
}

func (t *issueTracker) Reset() {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.active = make(map[issueKey]*activeIssue)
	t.detectors = make(map[uint32]*botDetector)
	t.resolved = nil
}
