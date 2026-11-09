package meters

import (
	"encoding/json"
	"strconv"
	"strings"
)

// The history file keeps a fight's series in a compact form: per bot and curve the per-bucket increments (not the cumulative values)
// as base-36 numbers joined by commas, a zero as nothing between two commas, a curve that is all zero left out. A dense 40-bot
// 150-bucket series as plain JSON arrays would be hundreds of KiB; this is a few KiB, and Encode keeps a hard cap on it by halving
// the buckets, then by giving the series up: the fight itself is never dropped because of its meters.
const (
	// MaxEncodedBytes is the most one fight's stored series may take in its history line.
	MaxEncodedBytes = 24 << 10
	// StoredV is the stored form's version (the served form, Series, stays V 1).
	StoredV = 2
	// maxStoredBots bounds what Decode accepts (a corrupt line cannot ask for a huge allocation).
	maxStoredBots = 400
)

type storedBot struct {
	GUID uint32 `json:"g"`
	D    string `json:"d,omitempty"`
	H    string `json:"h,omitempty"`
	Flat bool   `json:"flat,omitempty"`
}

type stored struct {
	V      int         `json:"v"`
	StepMs uint32      `json:"step_ms"`
	N      int         `json:"n"`
	Src    string      `json:"src"`
	Bots   []storedBot `json:"bots"`
}

// incs writes a cumulative curve as increments in base 36 ("" for a zero); an all-zero curve is "".
func incs(cum []uint64) string {
	var sb strings.Builder
	var prev uint64
	any := false
	for i, v := range cum {
		if i > 0 {
			sb.WriteByte(',')
		}
		if v > prev {
			sb.WriteString(strconv.FormatUint(v-prev, 36))
			any = true
		}
		prev = v
	}
	if !any {
		return ""
	}
	return sb.String()
}

// cumulative is incs' inverse for n buckets; ok is false for a curve with the wrong number of buckets, a bad digit or an overflow.
func cumulative(s string, n int) ([]uint64, bool) {
	out := make([]uint64, n)
	if s == "" {
		return out, true
	}
	parts := strings.Split(s, ",")
	if len(parts) != n {
		return nil, false
	}
	var run uint64
	for i, p := range parts {
		if p != "" {
			v, err := strconv.ParseUint(p, 36, 64)
			if err != nil || run+v < run {
				return nil, false
			}
			run += v
		}
		out[i] = run
	}
	return out, true
}

func encodeOnce(s Series) ([]byte, error) {
	st := stored{V: StoredV, StepMs: s.StepMs, N: s.N, Src: s.Src, Bots: make([]storedBot, len(s.Bots))}
	for i, b := range s.Bots {
		st.Bots[i] = storedBot{GUID: b.GUID, D: incs(b.Dmg), H: incs(b.Heal), Flat: b.Flat}
	}
	return json.Marshal(st)
}

// Coarsen halves the buckets: bucket i of the result ends where old bucket 2i+1 did (the last one where the old last did), so the
// last value, the exact total, is untouched and the step doubles.
func Coarsen(s Series) Series {
	n := (s.N + 1) / 2
	out := Series{V: s.V, StepMs: s.StepMs * 2, N: n, Src: s.Src, Bots: make([]Bot, len(s.Bots))}
	pick := func(c []uint64) []uint64 {
		o := make([]uint64, n)
		for i := range o {
			j := 2*i + 1
			if j > len(c)-1 {
				j = len(c) - 1
			}
			o[i] = c[j]
		}
		return o
	}
	for i, b := range s.Bots {
		out.Bots[i] = Bot{GUID: b.GUID, Dmg: pick(b.Dmg), Heal: pick(b.Heal), Flat: b.Flat}
	}
	return out
}

// Encode is the series as the history line stores it, at most MaxEncodedBytes: a series that does not fit is coarsened (halving its
// buckets) until it does. ok is false when even one bucket does not fit (or the series is empty): store no meters for that fight.
func Encode(s Series) (raw []byte, ok bool) {
	if s.N <= 0 || len(s.Bots) == 0 {
		return nil, false
	}
	for {
		b, err := encodeOnce(s)
		if err != nil {
			return nil, false
		}
		if len(b) <= MaxEncodedBytes {
			return b, true
		}
		if s.N <= 1 {
			return nil, false
		}
		s = Coarsen(s)
	}
}

// Decode reads what Encode wrote back into the served form (cumulative arrays, V 1). ok is false for anything else: another version,
// a wrong bucket count, a bad number.
func Decode(raw []byte) (Series, bool) {
	var st stored
	if err := json.Unmarshal(raw, &st); err != nil || st.V != StoredV || st.N <= 0 || st.N > MaxBuckets || len(st.Bots) > maxStoredBots || st.StepMs == 0 {
		return Series{}, false
	}
	out := Series{V: 1, StepMs: st.StepMs, N: st.N, Src: st.Src, Bots: make([]Bot, len(st.Bots))}
	for i, b := range st.Bots {
		d, ok1 := cumulative(b.D, st.N)
		h, ok2 := cumulative(b.H, st.N)
		if !ok1 || !ok2 {
			return Series{}, false
		}
		out.Bots[i] = Bot{GUID: b.GUID, Dmg: d, Heal: h, Flat: b.Flat}
	}
	return out, true
}
