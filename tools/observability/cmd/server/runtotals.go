package main

import (
	"net/http"
	"strconv"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
)

// runTotalsHandler serves GET /api/v1/combat/run-totals?commander=&since=:
// every finished fight of that commander started at or after since (unix
// milliseconds), summed per bot from the memory store plus the history files
// (de-duplicated by fight key, the same readers the list routes use). At most
// combat.RunTotalsCap fights are summed; a cut answers X-More: 1. A missing
// or bad param is a 400.
func runTotalsHandler(combatStore *combat.Store, historyStore *history.Store) http.HandlerFunc {
	return runTotalsHandlerCap(combatStore, historyStore, combat.RunTotalsCap)
}

func runTotalsHandlerCap(combatStore *combat.Store, historyStore *history.Store, cap int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		commander := q.Get("commander")
		ms, err := strconv.ParseInt(q.Get("since"), 10, 64)
		if commander == "" || err != nil || ms <= 0 {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			_, _ = w.Write([]byte(`{"error":"bad commander or since"}`))
			return
		}
		sinceS := ms / 1000
		var in []combat.RunInput
		if combatStore != nil {
			for _, row := range combatStore.List(commander) {
				f, _, ok := combatStore.GetKey(row.Key)
				if !ok {
					continue
				}
				in = append(in, combat.RunInput{Key: row.Key, Fight: f})
			}
		}
		if historyStore != nil {
			for _, row := range historyStore.ListAll(commander) {
				f, _, ok := historyStore.Fight(row.Key)
				if !ok {
					continue
				}
				in = append(in, combat.RunInput{Key: row.Key, Fight: f})
			}
		}
		totals, more := combat.SumRun(in, sinceS, cap)
		totals.Since = sinceS
		if more {
			w.Header().Set("X-More", "1")
		}
		writeJSON(w, totals)
	}
}
