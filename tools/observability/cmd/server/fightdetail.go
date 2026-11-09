package main

import (
	"encoding/json"
	"net/http"
	"strings"

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

// metersJSON is metersSeries as the history line stores it (nil: none).
func metersJSON(src trackSource, f combat.Fight) json.RawMessage {
	s := metersSeries(src, f)
	if s == nil {
		return nil
	}
	b, err := json.Marshal(s)
	if err != nil {
		return nil
	}
	return b
}

// combatFightDetailHandler serves GET /api/v1/combat/fight/<key>: the filed fight and its warnings, plus the damage and healing
// series ("meters") for a fight the daemon watched. The detail key is (commander, session, id): fight ids repeat across commanders
// and sessions. The list rows carry the composed key; callers may also pass the three parts as query params. A fight that is no
// longer in memory is re-read from its history file and judged now with the same rules.
func combatFightDetailHandler(combatStore *combat.Store, historyStore *history.Store, src trackSource) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		key := strings.TrimPrefix(r.URL.Path, "/api/v1/combat/fight/")
		f, warnings, ok := combatStore.GetKey(key)
		if !ok {
			if commander, session, id, qok := combatFightKey(q); qok {
				f, warnings, ok = combatStore.Get(commander, session, id)
			}
		}
		if !ok {
			if historyStore != nil {
				if hf, row, ms, hok := historyStore.FightMeters(key); hok {
					writeJSON(w, struct {
						Fight    combat.Fight     `json:"fight"`
						Warnings []combat.Warning `json:"warnings"`
						Hist     bool             `json:"hist,omitempty"`
						PackKey  string           `json:"pack_key,omitempty"`
						Meters   json.RawMessage  `json:"meters,omitempty"`
					}{hf, combat.Judge(hf, combatStore.Rules()), true, row.PackKey, ms})
					return
				}
			}
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusNotFound)
			_, _ = w.Write([]byte(`{"error":"no such fight"}`))
			return
		}
		writeJSON(w, struct {
			Fight    combat.Fight     `json:"fight"`
			Warnings []combat.Warning `json:"warnings"`
			Meters   *meters.Series   `json:"meters,omitempty"`
		}{f, warnings, metersSeries(src, f)})
	}
}
