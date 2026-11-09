package main

import (
	"encoding/json"
	"net/http"
	"strings"
	"sync"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
	"tortoise-observability/internal/meters"
	"tortoise-observability/internal/state"
)

// trackSource is the part of the state store the fight detail reads: a commander's kept position track.
type trackSource interface {
	Track(commander string, from, to int64) ([]state.TrackFrame, map[uint32]string)
}

// metersSeries is the fight's damage and healing series over the kept track, or nil without one (no reports inside the fight, or
// a fight the daemon did not see run). The window reaches TrackEdgeSec before the start and a little past the end: the report
// that carries the last seconds of the window arrives up to half a window after the fight.
func metersSeries(src trackSource, f combat.Fight) *meters.Series {
	if src == nil {
		return nil
	}
	frames, _ := src.Track(f.Commander, f.Start-state.TrackEdgeSec, f.Start+int64(f.DurMS)/1000+state.TrackEdgeSec+5)
	s, ok := meters.Build(frames, f)
	if !ok {
		return nil
	}
	return &s
}

// metersJSON is the series as the history line stores it (compact, capped: meters.Encode); nil when there is none or it does not fit.
func metersJSON(src trackSource, f combat.Fight) json.RawMessage {
	s := metersSeries(src, f)
	if s == nil {
		return nil
	}
	raw, ok := meters.Encode(*s)
	if !ok {
		return nil
	}
	return raw
}

// meterCache keeps the series a fight's history line stored, decoded, so a detail request does not read the line from disk again.
// A line is never rewritten, so an entry never goes stale; the cache is bounded (oldest entry out) and "no series" is cached too.
type meterCache struct {
	mu    sync.Mutex
	m     map[string]*meters.Series
	order []string
}

const meterCacheMax = 64

func (c *meterCache) get(key string) (*meters.Series, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	s, ok := c.m[key]
	return s, ok
}

func (c *meterCache) put(key string, s *meters.Series) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.m == nil {
		c.m = map[string]*meters.Series{}
	}
	if _, ok := c.m[key]; !ok {
		c.order = append(c.order, key)
		if len(c.order) > meterCacheMax {
			delete(c.m, c.order[0])
			c.order = c.order[1:]
		}
	}
	c.m[key] = s
}

// histDetail is a past night's fight as the detail answers it: the fight, its pack key and its decoded series.
type histDetail struct {
	fight   combat.Fight
	packKey string
	meters  *meters.Series
}

// histCache keeps the last few past-night details, so reopening one (the navigation of the night picker) does not read its line and
// decode its series again. A line is never rewritten, so an entry never goes stale. Small on purpose: a fight is a few hundred KiB.
type histCache struct {
	mu    sync.Mutex
	m     map[string]*histDetail
	order []string
}

const histCacheMax = 16

func (c *histCache) get(key string) (*histDetail, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	d, ok := c.m[key]
	return d, ok
}

func (c *histCache) put(key string, d *histDetail) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.m == nil {
		c.m = map[string]*histDetail{}
	}
	if _, ok := c.m[key]; !ok {
		c.order = append(c.order, key)
		if len(c.order) > histCacheMax {
			delete(c.m, c.order[0])
			c.order = c.order[1:]
		}
	}
	c.m[key] = d
}

// historyDetail is the stored fight of a past night through the cache: read from its day file and decoded once.
func historyDetail(hs *history.Store, cache *histCache, key string) (*histDetail, bool) {
	if hs == nil {
		return nil, false
	}
	if d, ok := cache.get(key); ok {
		return d, true
	}
	hf, row, raw, ok := hs.FightMeters(key)
	if !ok {
		return nil, false
	}
	d := &histDetail{fight: hf, packKey: row.PackKey, meters: decodeMeters(raw)}
	cache.put(key, d)
	return d, true
}

// storedMeters is the series the history wrote for this fight: found false while the fight is not written yet (still in the writer's
// six-second wait), else the series or nil (the line carries none: no reports were kept, or it did not fit).
func storedMeters(hs *history.Store, cache *meterCache, key string) (s *meters.Series, found bool) {
	if hs == nil {
		return nil, false
	}
	if s, ok := cache.get(key); ok {
		return s, true
	}
	_, _, raw, ok := hs.FightMeters(key)
	if !ok {
		return nil, false
	}
	out := decodeMeters(raw)
	cache.put(key, out)
	return out, true
}

// decodeMeters is the stored series back in its served form; nil for a line with none or one that does not read.
func decodeMeters(raw json.RawMessage) *meters.Series {
	if len(raw) == 0 {
		return nil
	}
	d, ok := meters.Decode(raw)
	if !ok {
		return nil
	}
	return &d
}

// combatFightDetailHandler serves GET /api/v1/combat/fight/<key>: the filed fight and its warnings, plus the damage and healing
// series ("meters") for a fight the daemon watched. The series is built once, when the history writes the fight (the writer waits for
// the last reports), and every later request answers that stored one, so a track trimmed since changes nothing; only a fight not yet
// written is built from the track, per request. The detail key is (commander, session, id): fight ids repeat across commanders and
// sessions. The list rows carry the composed key; callers may also pass the three parts as query params. A fight that is no longer in
// memory is re-read from its history file and judged now with the same rules.
func combatFightDetailHandler(combatStore *combat.Store, historyStore *history.Store, src trackSource) http.HandlerFunc {
	cache := &meterCache{}
	past := &histCache{}
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		key := strings.TrimPrefix(r.URL.Path, "/api/v1/combat/fight/")
		f, warnings, ok := combatStore.GetKey(key)
		if !ok {
			if commander, session, id, qok := combatFightKey(q); qok {
				f, warnings, ok = combatStore.Get(commander, session, id)
				key = combat.Key(commander, session, id)
			}
		}
		if !ok {
			if d, hok := historyDetail(historyStore, past, key); hok {
				writeJSON(w, struct {
					Fight    combat.Fight     `json:"fight"`
					Warnings []combat.Warning `json:"warnings"`
					Hist     bool             `json:"hist,omitempty"`
					PackKey  string           `json:"pack_key,omitempty"`
					Meters   *meters.Series   `json:"meters,omitempty"`
				}{d.fight, combat.Judge(d.fight, combatStore.Rules()), true, d.packKey, d.meters})
				return
			}
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusNotFound)
			_, _ = w.Write([]byte(`{"error":"no such fight"}`))
			return
		}
		ms, found := storedMeters(historyStore, cache, key)
		if !found {
			ms = metersSeries(src, f)
		}
		writeJSON(w, struct {
			Fight    combat.Fight     `json:"fight"`
			Warnings []combat.Warning `json:"warnings"`
			Meters   *meters.Series   `json:"meters,omitempty"`
		}{f, warnings, ms})
	}
}
