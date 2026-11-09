// Package meters builds the Fight debugger's damage and healing series: how each bot's damage and healing grew over a finished
// fight. A fight record carries only per-bot totals; the daemon's position track keeps, per COMMANDERS report (about every 2 s),
// each bot's rate over the report's rolling window (state.TrackBot.DPS/HPS, state.TrackFrame.WinS). Integrating those rates gives
// the shape of the curve, and scaling it to the exact totals keeps the end numbers right. The shape is approximate (about +-5 s,
// the window's length): the page says so.
//
// web/static/js/fightmeters.js FromSamples is the live twin: the same rules over the browser's samples, without the scaling (a
// running fight has no exact totals). Keep the two in step; both fixtures are a steady 100 dps over 20 s.
package meters

import (
	"math"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/state"
)

// Limits and defaults of the series.
const (
	// MaxBuckets bounds a series: a 30 min fight (the track's reach) fits in 150 buckets, so the answer stays small.
	MaxBuckets = 150
	// MinStepMs is the bucket length of a short fight (the report interval).
	MinStepMs = 2000
	// MaxGapS caps the time one report stands for: a hole in the reports is not made up for with a long product.
	MaxGapS = 4
	// FirstGapS is what the first used report stands for.
	FirstGapS = 2
	// DefaultWinS is the window half-shift when a frame names no window.
	DefaultWinS = 10
	// MaxDurMs bounds the fight Build will describe: six hours. A longer (or garbled) duration is no series, so the bucket arithmetic
	// can never wrap and a telemetry value cannot make the daemon allocate or index out of range.
	MaxDurMs = 6 * 60 * 60 * 1000
	// MaxBots bounds the bots of one series (a fight record carries at most a raid's worth; Decode takes the same limit).
	MaxBots = 400
)

// Bot is one bot's cumulative damage and healing at the end of each bucket (index i: the amount up to (i+1)*StepMs).
type Bot struct {
	GUID uint32   `json:"g"`
	Dmg  []uint64 `json:"d"`
	Heal []uint64 `json:"h"`
	// Flat: a ramp stands in for the shape of at least one of the two curves (the track had no rate for it, the total is known).
	Flat bool `json:"flat,omitempty"`
}

// Series is the answer: N buckets of StepMs each, in the order of the fight's bots.
type Series struct {
	V      int    `json:"v"`
	StepMs uint32 `json:"step_ms"`
	N      int    `json:"n"`
	Src    string `json:"src"`
	Bots   []Bot  `json:"bots"`
}

// StepFor is the bucket length for a fight of durMs: at least MinStepMs, whole seconds, and never more than MaxBuckets buckets.
// The arithmetic is 64-bit: no duration wraps it (a result beyond uint32 is clamped, which Build never reaches: see MaxDurMs).
func StepFor(durMs uint32) uint32 {
	s := (uint64(durMs) + MaxBuckets*1000 - 1) / (MaxBuckets * 1000) // ceil(durMs / 150 / 1000) seconds
	ms := s * 1000
	if ms > math.MaxUint32 {
		return math.MaxUint32
	}
	if ms > MinStepMs {
		return uint32(ms)
	}
	return MinStepMs
}

// Build integrates the track's window rates over the fight and scales each bot's curves to the fight's exact totals. ok is false
// without a fight to describe (no duration or no bots) and when no report inside the fight was in combat.
//
// Rules: a report at T seconds with window W stands for the W seconds before it, so its rate belongs to T - W/2 (W/2 = 5 s when
// the report names none); only reports whose shifted time lies in [0, DurMS] count; each adds rate x dt, dt being the gap to the
// previous counted report (at most MaxGapS, FirstGapS for the first); the amount lands in bucket floor(t / StepMs), clamped to the
// last; the buckets are summed cumulatively.
func Build(frames []state.TrackFrame, f combat.Fight) (Series, bool) {
	if f.DurMS == 0 || f.DurMS > MaxDurMs || len(f.Bots) == 0 || len(f.Bots) > MaxBots {
		return Series{}, false
	}
	step := StepFor(f.DurMS)
	n := int((uint64(f.DurMS) + uint64(step) - 1) / uint64(step))
	if n < 1 || n > MaxBuckets {
		return Series{}, false
	}
	index := make(map[uint32]int, len(f.Bots))
	for i, b := range f.Bots {
		index[b.GUID] = i
	}
	dmg := make([][]float64, len(f.Bots))
	heal := make([][]float64, len(f.Bots))
	for i := range dmg {
		dmg[i] = make([]float64, n)
		heal[i] = make([]float64, n)
	}
	startMs := f.Start * 1000
	used := 0
	var prevMs int64
	for i := range frames {
		fr := &frames[i]
		if !fr.Fight {
			continue
		}
		win := fr.WinS
		if win == 0 {
			win = DefaultWinS
		}
		t := fr.T*1000 - startMs - int64(win)*500
		if t < 0 || t > int64(f.DurMS) {
			continue
		}
		dt := float64(FirstGapS)
		if used > 0 {
			dt = float64(t-prevMs) / 1000
			if dt > MaxGapS {
				dt = MaxGapS
			}
			if dt < 0 {
				dt = 0
			}
		}
		prevMs = t
		used++
		b := int(t / int64(step))
		if b < 0 {
			b = 0
		}
		if b >= n {
			b = n - 1
		}
		for _, tb := range fr.Bots {
			if k, ok := index[tb.GUID]; ok {
				dmg[k][b] += float64(tb.DPS) * dt
				heal[k][b] += float64(tb.HPS) * dt
			}
		}
	}
	if used == 0 {
		return Series{}, false
	}
	out := Series{V: 1, StepMs: step, N: n, Src: "window", Bots: make([]Bot, len(f.Bots))}
	for i, fb := range f.Bots {
		d, flatD := scale(dmg[i], fb.Dmg)
		h, flatH := scale(heal[i], fb.Heal)
		out.Bots[i] = Bot{GUID: fb.GUID, Dmg: d, Heal: h, Flat: flatD || flatH}
	}
	return out, true
}

// scale turns per-bucket amounts into cumulative integers whose last value is exactly total. An integral of zero against a
// positive total becomes a linear ramp (flat true); a total of zero is all zeros.
func scale(buckets []float64, total uint64) ([]uint64, bool) {
	n := len(buckets)
	out := make([]uint64, n)
	if total == 0 {
		return out, false
	}
	cum := make([]float64, n)
	run := 0.0
	for i, v := range buckets {
		run += v
		cum[i] = run
	}
	if run <= 0 {
		for i := range out {
			out[i] = uint64(math.Round(float64(total) * float64(i+1) / float64(n)))
		}
		out[n-1] = total
		return out, true
	}
	var prev uint64
	for i := range out {
		v := uint64(math.Round(cum[i] / run * float64(total)))
		if v < prev {
			v = prev
		}
		if v > total {
			v = total
		}
		out[i] = v
		prev = v
	}
	out[n-1] = total
	return out, false
}
