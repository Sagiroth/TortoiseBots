package state

import (
	"encoding/json"
	"net/url"
	"os"
	"reflect"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"
	"unsafe"

	"tortoise-observability/internal/model"
)

// trackBot builds one bound bot with its guid as the reported id.
func trackBot(guid uint32, x, y float64, hp, maxHP uint32) model.CommanderBot {
	return model.CommanderBot{Name: "b", GUID: guid, X: x, Y: y, HP: hp, MaxHP: maxHP, Alive: true, MapID: 249,
		ID: strconv.FormatUint(uint64(guid), 10)}
}

func trackBinding(name string, bots []model.CommanderBot, mobs []model.CommanderMob) model.CommanderBinding {
	return model.CommanderBinding{Commander: name, Online: true, Bots: bots, Mobs: mobs}
}

func trackPayload(session uint64, ts int64, bs ...model.CommanderBinding) *model.CommandersPayload {
	return &model.CommandersPayload{Session: session, TS: ts, Type: "COMMANDERS", Commanders: bs}
}

func TestTrackWalk2MatchesFixture(t *testing.T) {
	raw, err := os.ReadFile("testdata/walk2-commanders-walking.json")
	if err != nil {
		t.Fatal(err)
	}
	var snap struct {
		Commanders []model.CommanderBinding `json:"commanders"`
		UpdatedAt  int64                    `json:"updated_at"`
	}
	if err := json.Unmarshal(raw, &snap); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(trackPayload(1, snap.UpdatedAt, snap.Commanders...)) {
		t.Fatal("payload refused")
	}
	frames, names, tracked := s.TrackWindow("Glacienne", snap.UpdatedAt, snap.UpdatedAt)
	if len(frames) != 1 {
		t.Fatalf("frames = %d, want 1", len(frames))
	}
	if !tracked {
		t.Fatal("tracked = false, want true for a commander that reported")
	}
	f := frames[0]
	if len(f.Bots) != 39 {
		t.Fatalf("bots = %d, want 39", len(f.Bots))
	}
	if len(f.Mobs) != 1 || f.Mobs[0].VictimBot != 814 {
		t.Fatalf("mobs = %+v, want one mob targeting 814", f.Mobs)
	}
	if f.Step != 20 {
		t.Fatalf("step = %d, want 20", f.Step)
	}
	got, err := json.Marshal(TrackJSON("Glacienne", frames, names, true))
	if err != nil {
		t.Fatal(err)
	}
	wantRaw, err := os.ReadFile("testdata/walk2-track.json")
	if err != nil {
		t.Fatal(err)
	}
	var gotAny, wantAny any
	if err := json.Unmarshal(got, &gotAny); err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(wantRaw, &wantAny); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(gotAny, wantAny) {
		gi, _ := json.MarshalIndent(gotAny, "", " ")
		wi, _ := json.MarshalIndent(wantAny, "", " ")
		t.Fatalf("track JSON differs from the fixture:\n got %s\nwant %s", gi, wi)
	}
}

func TestTrackWindowEdges(t *testing.T) {
	s := newTestStore(newClock())
	for _, ts := range []int64{100, 102, 104, 110} {
		if !s.ApplyCommanders(trackPayload(1, ts, trackBinding("c", []model.CommanderBot{trackBot(1, 1, 2, 10, 10)}, nil))) {
			t.Fatalf("payload ts %d refused", ts)
		}
	}
	cases := []struct {
		from, to int64
		want     []int64
	}{
		{101, 103, []int64{100, 102, 104}},
		{105, 106, []int64{104, 110}},
		{200, 300, []int64{}},
		{95, 95, []int64{}},
	}
	for _, c := range cases {
		frames, _ := s.Track("c", c.from, c.to)
		got := []int64{}
		for _, f := range frames {
			got = append(got, f.T)
		}
		if !reflect.DeepEqual(got, c.want) {
			t.Errorf("Track(%d, %d) = %v, want %v", c.from, c.to, got, c.want)
		}
	}
}

func TestTrackSkipsSameTS(t *testing.T) {
	s := newTestStore(newClock())
	p := trackPayload(1, 100, trackBinding("c", []model.CommanderBot{trackBot(1, 1, 2, 10, 10)}, nil))
	if !s.ApplyCommanders(p) || !s.ApplyCommanders(p) {
		t.Fatal("a repeated ts must be accepted")
	}
	frames, _ := s.Track("c", 100, 100)
	if len(frames) != 1 {
		t.Fatalf("frames = %d, want 1 (one frame per ts)", len(frames))
	}
}

func TestTrackCapsFrames(t *testing.T) {
	s := newTestStore(newClock())
	b := trackBinding("c", []model.CommanderBot{trackBot(1, 1, 2, 10, 10)}, nil)
	for ts := int64(1); ts <= 905; ts++ {
		if !s.ApplyCommanders(trackPayload(1, ts, b)) {
			t.Fatalf("payload ts %d refused", ts)
		}
	}
	frames, _ := s.Track("c", 1, 905)
	if len(frames) != TrackMaxFrames {
		t.Fatalf("frames = %d, want %d", len(frames), TrackMaxFrames)
	}
	if frames[0].T != 6 {
		t.Fatalf("oldest frame T = %d, want 6", frames[0].T)
	}
}

func TestTrackVictims(t *testing.T) {
	bots := []model.CommanderBot{
		{Name: "a", GUID: 814, ID: "814", MapID: 249, Alive: true},
		{Name: "b", GUID: 900, MapID: 249, Alive: true}, // no id: its guid string is the key
		{Name: "c", GUID: 901, ID: "77", MapID: 249, Alive: true},
	}
	mobs := []model.CommanderMob{
		{ID: "1", Entry: 1, Victim: "814"},
		{ID: "2", Entry: 1, Victim: "900"},
		{ID: "3", Entry: 1, Victim: "77"},
		{ID: "4", Entry: 1, Victim: "55555"},
		{ID: "5", Entry: 1},
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(trackPayload(1, 100, trackBinding("c", bots, mobs))) {
		t.Fatal("payload refused")
	}
	frames, _ := s.Track("c", 100, 100)
	if len(frames) != 1 || len(frames[0].Mobs) != 5 {
		t.Fatalf("frames = %+v", frames)
	}
	m := frames[0].Mobs
	wants := []struct {
		victim uint32
		other  bool
	}{{814, false}, {900, false}, {901, false}, {0, true}, {0, false}}
	for i, w := range wants {
		if m[i].VictimBot != w.victim || m[i].VictimOther != w.other {
			t.Errorf("mob %d victim = %d/%v, want %d/%v", i, m[i].VictimBot, m[i].VictimOther, w.victim, w.other)
		}
	}
}

func TestTrackHPAndAlive(t *testing.T) {
	bots := []model.CommanderBot{
		{Name: "a", GUID: 1, MapID: 249, HP: 3464, MaxHP: 6929, Alive: true},
		{Name: "b", GUID: 2, MapID: 249, HP: 5, MaxHP: 0, Alive: true},
		{Name: "c", GUID: 3, MapID: 249, HP: 3464, MaxHP: 6929, Alive: false},
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(trackPayload(1, 100, trackBinding("c", bots, nil))) {
		t.Fatal("payload refused")
	}
	frames, _ := s.Track("c", 100, 100)
	if len(frames) != 1 || len(frames[0].Bots) != 3 {
		t.Fatalf("frames = %+v", frames)
	}
	wantHP := []uint8{50, 0, 50}
	wantAlive := []bool{true, true, false}
	for i, b := range frames[0].Bots {
		if b.HP != wantHP[i] || b.Alive != wantAlive[i] {
			t.Errorf("bot %d = hp %d alive %v, want hp %d alive %v", i, b.HP, b.Alive, wantHP[i], wantAlive[i])
		}
	}
}

// TestTrackSaysUntracked: a commander the daemon does not track answers with tracked false and no frames, while a tracked
// commander whose window held no report answers tracked true with no frames. The page says "replay kept for the first 20
// commanders" for the first and "no positions kept for this fight" for the second.
func TestTrackSaysUntracked(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.CommanderBot{trackBot(1, 1, 2, 10, 10)}
	if !s.ApplyCommanders(trackPayload(1, 100, trackBinding("kept", bots, nil))) {
		t.Fatal("payload refused")
	}
	frames, _, tracked := s.TrackWindow("kept", 5000, 6000)
	if !tracked || len(frames) != 0 {
		t.Fatalf("kept commander: tracked %v frames %d, want true and 0", tracked, len(frames))
	}
	frames, _, tracked = s.TrackWindow("gone", 0, 1000)
	if tracked || len(frames) != 0 {
		t.Fatalf("untracked commander: tracked %v frames %d, want false and 0", tracked, len(frames))
	}
	raw, err := json.Marshal(TrackJSON("gone", frames, nil, tracked))
	if err != nil {
		t.Fatal(err)
	}
	var got map[string]any
	if err := json.Unmarshal(raw, &got); err != nil {
		t.Fatal(err)
	}
	if v, ok := got["tracked"]; !ok || v != false {
		t.Fatalf("the track answer says tracked %v (present %v), want false: %s", v, ok, raw)
	}
}

func TestTrackForgetsAndCaps(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.CommanderBot{trackBot(1, 1, 2, 10, 10)}
	names := make([]string, 0, 21)
	for i := 0; i < 21; i++ {
		names = append(names, "c"+strconv.Itoa(i))
	}
	for i, name := range names {
		if !s.ApplyCommanders(trackPayload(1, int64(100+i), trackBinding(name, bots, nil))) {
			t.Fatalf("payload %s refused", name)
		}
		c.Advance(time.Second)
	}
	if TrackMaxCommanders != 20 {
		t.Fatalf("TrackMaxCommanders = %d, want 20", TrackMaxCommanders)
	}
	if frames, _ := s.Track(names[0], 0, 1000); len(frames) != 0 {
		t.Errorf("the oldest commander must be dropped, got %d frames", len(frames))
	}
	for _, name := range names[1:] {
		if frames, _ := s.Track(name, 0, 1000); len(frames) != 1 {
			t.Errorf("commander %s: %d frames, want 1", name, len(frames))
		}
	}
	c.Advance(31 * time.Minute)
	if !s.ApplyCommanders(trackPayload(1, 200, trackBinding("f", bots, nil))) {
		t.Fatal("payload f refused")
	}
	for _, name := range names {
		if frames, _ := s.Track(name, 0, 1000); len(frames) != 0 {
			t.Errorf("the forgotten commander %s still has %d frames", name, len(frames))
		}
	}
	if frames, _ := s.Track("f", 0, 1000); len(frames) != 1 {
		t.Errorf("the reported commander has %d frames, want 1", len(frames))
	}
}

func TestTrackCopies(t *testing.T) {
	bots := []model.CommanderBot{{Name: "a", GUID: 1, X: -98.8, Y: 5, HP: 10, MaxHP: 10, Alive: true, MapID: 249}}
	mobs := []model.CommanderMob{{ID: "1", Entry: 12129, Name: "Onyxian Warder", X: 1, Y: 2}}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(trackPayload(1, 100, trackBinding("c", bots, mobs))) {
		t.Fatal("payload refused")
	}
	f1, n1 := s.Track("c", 100, 100)
	f1[0].Bots[0].X = 999
	f1[0].Mobs[0].X = 999
	n1[12129] = "changed"
	f2, n2 := s.Track("c", 100, 100)
	if f2[0].Bots[0].X != -98.8 || f2[0].Mobs[0].X != 1 || n2[12129] != "Onyxian Warder" {
		t.Fatalf("the returned frame shares state with the buffer: %+v %v", f2[0], n2)
	}
}

func TestParseTrackWindow(t *testing.T) {
	ok := url.Values{"commander": {"Glacienne"}, "from": {"1793210000"}, "to": {"1793211800"}}
	c, from, to, err := ParseTrackWindow(ok)
	if err != nil || c != "Glacienne" || from != 1793210000 || to != 1793211800 {
		t.Fatalf("ok window = %q %d %d %v", c, from, to, err)
	}
	bad := []url.Values{
		{"from": {"1"}, "to": {"2"}},
		{"commander": {"c"}, "from": {"x"}, "to": {"2"}},
		{"commander": {"c"}, "from": {"5"}, "to": {"4"}},
		{"commander": {"c"}, "from": {"1"}, "to": {"1802"}},
		{"commander": {""}, "from": {"1"}, "to": {"2"}},
	}
	for _, q := range bad {
		if _, _, _, err := ParseTrackWindow(q); err == nil {
			t.Errorf("window %v accepted", q)
		}
	}
}

// A frame carries where a boss faced and the cast it ran, and its wire shape says when that cast ends, so a replay can draw
// the facing arrow and the spell's area without arithmetic of its own.
func TestTrackCarriesBossOverlay(t *testing.T) {
	facing := 1.5708
	mobs := []model.CommanderMob{{ID: "5", Entry: 10184, Name: "Onyxia", X: 1, Y: 2, HP: 80, Rank: "b",
		O: &facing, Cast: "Wing Buffet", CastID: 18435, CastLeftMS: 1200, CastTotalMS: 2000, CastTarget: "814",
		Shape: &model.CommanderShape{Kind: "cone", Angle: 120, Length: 45}}}
	s := newTestStore(newClock())
	s.ApplyCommanders(trackPayload(1, 100, trackBinding("c", []model.CommanderBot{trackBot(814, 0, 0, 100, 100)}, mobs)))
	frames, names := s.Track("c", 100, 100)
	if len(frames) != 1 || len(frames[0].Mobs) != 1 {
		t.Fatalf("frames = %+v", frames)
	}
	m := frames[0].Mobs[0]
	if m.O == nil || *m.O != facing || m.CastID != 18435 || m.CastName != "Wing Buffet" || m.CastEnd != 101200 || m.CastTotal != 2000 || m.CastTarget != "814" || m.Channel != 0 || m.Shape == nil || m.Shape.Kind != "cone" {
		t.Fatalf("frame overlay lost: %+v", m)
	}
	raw, err := json.Marshal(TrackJSON("c", frames, names, true))
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{`"o":1.57`, `"cid":18435`, `"cn":"Wing Buffet"`, `"ce":101200`, `"ctot":2000`, `"ctg":"814"`, `"sh":{"k":"cone","a":120,"l":45}`} {
		if !strings.Contains(string(raw), want) {
			t.Fatalf("track JSON lacks %s: %s", want, raw)
		}
	}
	// The cast's name rides with the cast id only: a name without a cast to time is not a cast.
	castOnly := []model.CommanderMob{{ID: "6", Entry: 10184, Name: "Onyxia", X: 1, Y: 2, HP: 80, Rank: "b", Cast: "Wing Buffet"}}
	s2 := newTestStore(newClock())
	s2.ApplyCommanders(trackPayload(1, 100, trackBinding("c", []model.CommanderBot{trackBot(814, 0, 0, 100, 100)}, castOnly)))
	f2, n2 := s2.Track("c", 100, 100)
	raw2, _ := json.Marshal(TrackJSON("c", f2, n2, true))
	if strings.Contains(string(raw2), `"cn"`) {
		t.Fatalf("a cast name without a cast id must not be sent: %s", raw2)
	}
	// A facing of 0 is a direction, not "not reported".
	zero := 0.0
	frames[0].Mobs[0].O = &zero
	raw, _ = json.Marshal(TrackJSON("c", frames, names, true))
	if !strings.Contains(string(raw), `"o":0`) {
		t.Fatalf("a facing of 0 must be reported: %s", raw)
	}
	// The frames the caller got are copies: changing them changes neither the store nor the next read.
	frames[0].Mobs[0].Shape.Kind = "mutated"
	again, _ := s.Track("c", 100, 100)
	if again[0].Mobs[0].Shape == nil || again[0].Mobs[0].Shape.Kind != "cone" {
		t.Fatalf("a frame's shape must be a copy: %+v", again[0].Mobs[0].Shape)
	}
	plain := trackMobJSON{}
	out, _ := json.Marshal(plain)
	if strings.Contains(string(out), `"o"`) || strings.Contains(string(out), `"sh"`) || strings.Contains(string(out), `"cn"`) {
		t.Fatalf("an ordinary mob must not grow the overlay fields: %s", out)
	}
}

// A frame's charge is what it retains, not a fixed guess per row: the rows and slices it holds at capacity, a mob's
// rank, cast target, facing and shape, and a fifth more for the runtime's own bookkeeping.
func TestTrackFrameBytesChargesRetainedState(t *testing.T) {
	plain := TrackFrame{Bots: make([]TrackBot, 1), Mobs: make([]TrackMob, 1)}
	plain.Mobs[0] = TrackMob{ID: "1"}
	facing := 1.0
	rich := TrackFrame{Bots: make([]TrackBot, 1), Mobs: make([]TrackMob, 1)}
	rich.Mobs[0] = TrackMob{ID: "1", Rank: strings.Repeat("r", 40), CastTarget: strings.Repeat("t", 50),
		O: &facing, Shape: &model.CommanderShape{Kind: strings.Repeat("k", 30)}}
	const want = 40 + 50 + 8 + 40 + 30 // rank, cast target, facing, shape record and its kind
	if got := trackFrameBytes(rich) - trackFrameBytes(plain); got < want {
		t.Fatalf("rank, cast target, facing and shape add %d bytes to a frame's charge, want at least %d", got, want)
	}
	// Capacity, not length: the backing array a frame's slices keep stays in memory even when the frame holds no rows.
	spare := TrackFrame{Bots: make([]TrackBot, 0, 40), Mobs: make([]TrackMob, 0, 24)}
	if got, want := trackFrameBytes(spare)-trackFrameBytes(TrackFrame{}), int64(40*unsafe.Sizeof(TrackBot{})+24*unsafe.Sizeof(TrackMob{})); got < want {
		t.Fatalf("spare capacity costs %d, want at least the %d bytes the arrays hold", got, want)
	}
	// And the row structs themselves, not a fixed per-row number.
	rows := TrackFrame{Bots: make([]TrackBot, 8), Mobs: make([]TrackMob, 3)}
	if got, want := trackFrameBytes(rows), int64(unsafe.Sizeof(TrackFrame{}))+8*int64(unsafe.Sizeof(TrackBot{}))+3*int64(unsafe.Sizeof(TrackMob{})); got < want {
		t.Fatalf("a frame's charge is %d, below the %d bytes its rows occupy", got, want)
	}
}

// The 48 MiB budget is spent in retained bytes, so twenty raids of the worst-case report (40 bots, 40 enemies, every
// optional string and overlay set) stay under it with room to spare and no commander's replay is cut below its 5 min.
func TestTrackHeapStaysUnderBudget(t *testing.T) {
	var before, after runtime.MemStats
	runtime.GC()
	runtime.ReadMemStats(&before)

	s := newTestStore(newClock())
	const commanders, bots, mobs = 20, 40, 40
	// Every cycle is decoded afresh, as the listener does: its strings and overlay objects are each frame's own.
	cycle := func(ts int64) *model.CommandersPayload {
		bindings := make([]model.CommanderBinding, 0, commanders)
		for i := 0; i < commanders; i++ {
			bs := make([]model.CommanderBot, 0, bots)
			for g := 1; g <= bots; g++ {
				bs = append(bs, trackBot(uint32(g), float64(g), 2, 10, 10))
			}
			ms := make([]model.CommanderMob, 0, mobs)
			for m := 0; m < mobs; m++ {
				facing := 1.5
				ms = append(ms, model.CommanderMob{ID: "1737939109461" + strconv.Itoa(m), Entry: uint32(1000 + m), Name: "Onyxian Warder",
					X: 1, Y: 2, HP: 50, Rank: "elite", Cast: "Shadow Bolt Volley", CastID: uint32(700 + m), CastLeftMS: 900,
					CastTotalMS: 1500, CastTarget: "1737939109461", O: &facing,
					Shape: &model.CommanderShape{Kind: "back-cone", Angle: 120, Length: 45, Width: 10}})
			}
			bindings = append(bindings, trackBinding("c"+strconv.Itoa(i), bs, ms))
		}
		return trackPayload(1, ts, bindings...)
	}
	for ts := int64(1); ts <= TrackMaxFrames; ts++ {
		if !s.ApplyCommanders(cycle(ts)) {
			t.Fatalf("payload ts %d refused", ts)
		}
	}
	for i := 0; i < commanders; i++ {
		name := "c" + strconv.Itoa(i)
		frames, _, tracked := s.TrackWindow(name, 1, TrackMaxFrames)
		if !tracked || len(frames) < TrackMinFrames {
			t.Fatalf("%s keeps %d frames, want at least the %d floor", name, len(frames), TrackMinFrames)
		}
		if frames[len(frames)-1].T != TrackMaxFrames {
			t.Fatalf("%s lost its newest frame: last T %d", name, frames[len(frames)-1].T)
		}
	}
	runtime.GC()
	runtime.ReadMemStats(&after)
	runtime.KeepAlive(s) // without it the store is dead by the collection above and its tracks are freed
	growth := int64(after.HeapAlloc) - int64(before.HeapAlloc)
	if limit := int64(TrackMemBudget) * 5 / 4; growth > limit {
		t.Fatalf("the tracks grew the heap by %d bytes, over the %d the budget allows", growth, limit)
	}
}

// With every track at the 5 min floor and the budget still passed, whole commanders are forgotten, least recently seen
// first, and the frames of the ones kept are never cut below the floor.
func TestTrackBudgetForgetsAtTheFloor(t *testing.T) {
	st := &commandersState{tracks: map[string]*commanderTrack{}}
	at := time.Unix(1790000000, 0)
	for i := 0; i < TrackMaxCommanders; i++ {
		tr := &commanderTrack{frames: make([]TrackFrame, TrackMinFrames), lastSeen: at.Add(time.Duration(i) * time.Second)}
		tr.bytes = TrackMemBudget / 10 // ten tracks fill the budget: twenty are twice over it
		st.tracks["c"+strconv.Itoa(i)] = tr
	}
	st.trimTrackBudgetLocked()
	kept := make([]string, 0, TrackMaxCommanders)
	var total int64
	for name, tr := range st.tracks {
		kept = append(kept, name)
		total += tr.bytes
		if len(tr.frames) != TrackMinFrames {
			t.Fatalf("%s keeps %d frames, want the %d floor", name, len(tr.frames), TrackMinFrames)
		}
	}
	sort.Strings(kept)
	if want := "c10,c11,c12,c13,c14,c15,c16,c17,c18,c19"; strings.Join(kept, ",") != want {
		t.Fatalf("kept %v, want the ten seen last", kept)
	}
	if total > TrackMemBudget {
		t.Fatalf("the tracks still charge %d bytes, over the %d budget", total, TrackMemBudget)
	}
}

// A line whose damage is a lane between two map points carries those ends: a Deep Breath does not start at the boss's
// feet, so the replay must draw the segment, not a reach from the boss's facing. The segment is copied with the frame.
func TestTrackKeepsALaneSegment(t *testing.T) {
	facing := 3.14
	mobs := []model.CommanderMob{{ID: "5", Entry: 10184, Name: "Onyxia", X: 1, Y: 2, HP: 80, Rank: "b",
		O: &facing, CastID: 17086, CastLeftMS: 1200, CastTotalMS: 5000,
		Shape: &model.CommanderShape{Kind: "line", Length: 151.53, Width: 60,
			Segment: []float64{20.730539, -215.237610, -130.788300, -213.424026}}}}
	s := newTestStore(newClock())
	s.ApplyCommanders(trackPayload(1, 100, trackBinding("c", []model.CommanderBot{trackBot(814, 0, 0, 100, 100)}, mobs)))
	frames, names := s.Track("c", 100, 100)
	if len(frames) != 1 || len(frames[0].Mobs) != 1 || frames[0].Mobs[0].Shape == nil {
		t.Fatalf("frames = %+v", frames)
	}
	seg := frames[0].Mobs[0].Shape.Segment
	if len(seg) != 4 || seg[0] != 20.730539 || seg[1] != -215.237610 || seg[2] != -130.788300 || seg[3] != -213.424026 {
		t.Fatalf("lane segment lost: %v", frames[0].Mobs[0].Shape)
	}
	raw, err := json.Marshal(TrackJSON("c", frames, names))
	if err != nil {
		t.Fatal(err)
	}
	if want := `"sh":{"k":"line","l":151.53,"w":60,"seg":[20.730539,-215.23761,-130.7883,-213.424026]}`; !strings.Contains(string(raw), want) {
		t.Fatalf("track JSON lacks %s: %s", want, raw)
	}
	// The frames are copies, the segment slice too.
	frames[0].Mobs[0].Shape.Segment[0] = 0
	again, _ := s.Track("c", 100, 100)
	if got := again[0].Mobs[0].Shape.Segment[0]; got != 20.730539 {
		t.Fatalf("a frame's lane segment must be a copy, got %v", got)
	}
}
