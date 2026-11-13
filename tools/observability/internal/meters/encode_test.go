package meters

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"

	"tortoise-observability/internal/combat"
)

// lcg is a deterministic noise source (no math/rand state shared with other tests).
type lcg uint64

func (l *lcg) next(n uint64) uint64 {
	*l = *l*6364136223846793005 + 1442695040888963407
	return (uint64(*l) >> 33) % n
}

// denseSeries is the worst case: bots x 150 buckets with totals around 10^7 and a noisy curve (every bucket a different amount).
func denseSeries(bots int, total uint64) Series {
	r := lcg(7)
	s := Series{V: 1, StepMs: 12000, N: 150, Src: "window"}
	for g := 1; g <= bots; g++ {
		b := Bot{GUID: uint32(g), Dmg: make([]uint64, 150), Heal: make([]uint64, 150)}
		var d, h uint64
		for i := 0; i < 150; i++ {
			d += total/150/2 + r.next(total/150)
			h += total/300/2 + r.next(total/300)
			b.Dmg[i], b.Heal[i] = d, h
		}
		// Scale to the exact totals, as Build does.
		for i := range b.Dmg {
			b.Dmg[i] = b.Dmg[i] * total / d
			b.Heal[i] = b.Heal[i] * (total / 2) / h
		}
		s.Bots = append(s.Bots, b)
	}
	return s
}

// The encoding round-trips: every cumulative value comes back, healing left out when it is all zero, the flag kept.
func TestEncodeRoundTrip(t *testing.T) {
	f := fightOf(1000, 20000,
		combat.FightBot{GUID: 7, Dmg: 2000, Heal: 1000},
		combat.FightBot{GUID: 8, Dmg: 500},
		combat.FightBot{GUID: 9, Dmg: 0, Heal: 0},
	)
	s, ok := Build(steadyFrames(1000, 7), f)
	if !ok {
		t.Fatal("no series")
	}
	raw, ok := Encode(s)
	if !ok {
		t.Fatal("did not encode")
	}
	got, ok := Decode(raw)
	if !ok {
		t.Fatalf("did not decode: %s", raw)
	}
	if !reflect.DeepEqual(got, s) {
		t.Fatalf("round trip differs:\n%+v\n%+v\n%s", got, s, raw)
	}
	var st struct {
		V    int
		Bots []map[string]any
	}
	if err := json.Unmarshal(raw, &st); err != nil || st.V != StoredV {
		t.Fatalf("stored form: %s", raw)
	}
	if _, has := st.Bots[1]["h"]; has {
		t.Fatalf("a bot with no healing stores no h: %s", raw)
	}
	if _, has := st.Bots[2]["d"]; has {
		t.Fatalf("a bot with no damage stores no d: %s", raw)
	}
	if strings.Contains(string(raw), `"d":[`) || strings.Contains(string(raw), `"h":[`) {
		t.Fatalf("increments are strings, not arrays: %s", raw)
	}
}

// The worst case (40 bots, 150 buckets, 7-digit totals) stays under the cap: it is coarsened to fit and its totals stay exact.
func TestEncodeWorstCaseUnderCap(t *testing.T) {
	s := denseSeries(40, 9_999_999)
	full, _ := encodeOnce(s)
	raw, ok := Encode(s)
	if !ok {
		t.Fatal("the worst case did not encode")
	}
	t.Logf("worst case: 40 bots x 150 buckets, 7-digit totals: %d bytes uncapped, %d bytes stored (cap %d)", len(full), len(raw), MaxEncodedBytes)
	if len(raw) > MaxEncodedBytes {
		t.Fatalf("%d bytes is over the cap %d", len(raw), MaxEncodedBytes)
	}
	got, ok := Decode(raw)
	if !ok {
		t.Fatal("did not decode")
	}
	if got.N >= 150 && len(full) > MaxEncodedBytes {
		t.Fatalf("over the cap at 150 buckets but not coarsened: n=%d", got.N)
	}
	for i, b := range got.Bots {
		if b.Dmg[got.N-1] != 9_999_999 || b.Heal[got.N-1] != 9_999_999/2 {
			t.Fatalf("bot %d totals %d / %d are not exact", i, b.Dmg[got.N-1], b.Heal[got.N-1])
		}
		if len(b.Dmg) != got.N || len(b.Heal) != got.N {
			t.Fatalf("bot %d has %d / %d buckets, header says %d", i, len(b.Dmg), len(b.Heal), got.N)
		}
	}
	if got.StepMs%s.StepMs != 0 || got.StepMs < s.StepMs {
		t.Fatalf("the step %d is not a multiple of %d", got.StepMs, s.StepMs)
	}
}

// Coarsening keeps the last value, doubles the step and halves the buckets (an odd count rounds up); a series that does not fit even
// as one bucket is given up (ok false), never partly stored.
func TestEncodeCoarsensThenDrops(t *testing.T) {
	c := Coarsen(Series{V: 1, StepMs: 2000, N: 5, Src: "window", Bots: []Bot{{GUID: 1, Dmg: []uint64{1, 3, 6, 10, 15}, Heal: []uint64{0, 0, 0, 0, 0}}}})
	if c.N != 3 || c.StepMs != 4000 || !reflect.DeepEqual(c.Bots[0].Dmg, []uint64{3, 10, 15}) {
		t.Fatalf("coarsened: %+v", c)
	}
	// A series that fits at 150 buckets is left as it is.
	small := denseSeries(2, 100000)
	raw, ok := Encode(small)
	if !ok {
		t.Fatal("small series")
	}
	if got, _ := Decode(raw); got.N != 150 {
		t.Fatalf("a series under the cap is not coarsened: n=%d", got.N)
	}
	// Over the cap at 150 buckets: coarsened until it fits.
	big := denseSeries(40, 9_999_999)
	raw, ok = Encode(big)
	if got, dok := Decode(raw); !ok || !dok || got.N >= 150 || got.N < 1 {
		t.Fatalf("an over-cap series coarsens: ok=%v n=%d", ok, got.N)
	}
	// Too many bots for even one bucket: no meters.
	huge := Series{V: 1, StepMs: 2000, N: 1, Src: "window"}
	for g := 1; g <= 2000; g++ {
		huge.Bots = append(huge.Bots, Bot{GUID: uint32(g), Dmg: []uint64{123456789}, Heal: []uint64{98765432}})
	}
	if raw, ok := Encode(huge); ok || raw != nil {
		t.Fatalf("a series that cannot fit is given up, got %d bytes", len(raw))
	}
	if _, ok := Encode(Series{}); ok {
		t.Fatal("an empty series")
	}
}

// Decode refuses what it did not write: another version, a wrong bucket count, a bad digit, too many buckets, garbage.
func TestDecodeRejectsBadInput(t *testing.T) {
	for name, raw := range map[string]string{
		"version":  `{"v":1,"step_ms":2000,"n":2,"src":"window","bots":[{"g":1,"d":"1,2"}]}`,
		"count":    `{"v":2,"step_ms":2000,"n":3,"src":"window","bots":[{"g":1,"d":"1,2"}]}`,
		"digit":    `{"v":2,"step_ms":2000,"n":2,"src":"window","bots":[{"g":1,"d":"1,!"}]}`,
		"buckets":  `{"v":2,"step_ms":2000,"n":151,"src":"window","bots":[]}`,
		"no step":  `{"v":2,"step_ms":0,"n":2,"src":"window","bots":[]}`,
		"overflow": `{"v":2,"step_ms":2000,"n":2,"src":"window","bots":[{"g":1,"d":"zzzzzzzzzzzzz,zzzzzzzzzzzzz"}]}`,
		"garbage":  `not json`,
	} {
		if _, ok := Decode([]byte(raw)); ok {
			t.Errorf("%s: decoded %s", name, raw)
		}
	}
	got, ok := Decode([]byte(`{"v":2,"step_ms":2000,"n":3,"src":"window","bots":[{"g":1,"d":",a,","h":"1,,2"}]}`))
	if !ok || !reflect.DeepEqual(got.Bots[0].Dmg, []uint64{0, 10, 10}) || !reflect.DeepEqual(got.Bots[0].Heal, []uint64{1, 1, 3}) {
		t.Fatalf("empty tokens are zeros: %+v ok=%v", got, ok)
	}
}
