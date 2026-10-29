package state

import (
	"encoding/json"
	"net/url"
	"os"
	"reflect"
	"strconv"
	"strings"
	"testing"
	"time"

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
// commander whose window held no report answers tracked true with no frames. The page says "replay kept for the first 4
// raids" for the first and "no positions kept for this fight" for the second.
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
	for i, name := range []string{"a", "b", "c", "d", "e"} {
		if !s.ApplyCommanders(trackPayload(1, int64(100+i), trackBinding(name, bots, nil))) {
			t.Fatalf("payload %s refused", name)
		}
		c.Advance(time.Second)
	}
	if frames, _ := s.Track("a", 0, 1000); len(frames) != 0 {
		t.Errorf("the oldest commander must be dropped, got %d frames", len(frames))
	}
	for _, name := range []string{"b", "c", "d", "e"} {
		if frames, _ := s.Track(name, 0, 1000); len(frames) != 1 {
			t.Errorf("commander %s: %d frames, want 1", name, len(frames))
		}
	}
	c.Advance(31 * time.Minute)
	if !s.ApplyCommanders(trackPayload(1, 200, trackBinding("f", bots, nil))) {
		t.Fatal("payload f refused")
	}
	for _, name := range []string{"a", "b", "c", "d", "e"} {
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
