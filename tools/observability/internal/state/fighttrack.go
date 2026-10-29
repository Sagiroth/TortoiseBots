package state

import (
	"errors"
	"math"
	"net/url"
	"strconv"
	"time"

	"tortoise-observability/internal/model"
)

// The fight debugger's replay and aggro lanes need where every bound bot and every enemy near the lead was over the fight, which
// the trails do not hold (one point per 2 yd, no hp, no mobs) and COMMANDERS history had no place for. One frame per accepted
// datagram per commander, memory only: 900 frames is 30 min at the 2 s reports, four commanders at once, and a commander that
// stops reporting is forgotten after 30 min. Frames survive the binding ending, so a finished fight can still be replayed.
const (
	TrackMaxFrames     = 900
	TrackMaxCommanders = 4
	TrackKeep          = 30 * time.Minute
	TrackEdgeSec       = 4
	TrackMaxWindowSec  = 1800
	trackMaxNames      = 256
)

// TrackBot is one bound bot in a frame: where it stood, its health and the map, as reported.
type TrackBot struct {
	GUID  uint32
	X, Y  float32
	HP    uint8 // percent, round(hp * 100 / max_hp)
	Alive bool
	Map   uint32
}

// TrackMob is one enemy near the lead in a frame. VictimBot is the bound bot it targeted (0: none); VictimOther means it
// targeted a unit that is not a bound bot. A boss or elite also carries where it faced and the cast it ran (the fight
// debugger's replay draws the facing arrow and the spell's area), as the module reported them.
type TrackMob struct {
	ID          string
	Entry       uint32
	X, Y        float32
	HP          uint8
	VictimBot   uint32
	VictimOther bool
	Rank        string
	O           *float64              // facing, radians; nil when not reported
	CastID      uint32                // the spell being cast or channelled, 0 = none
	CastEnd     int64                 // when it ends, unix ms
	CastTotal   uint32                // its own time, or the channel's duration, ms
	CastTarget  string                // guid of the unit it is aimed at
	Channel     int                   // 1 when channelled
	Shape       *model.CommanderShape // the area it covers; nil when unknown
}

// TrackFrame is the state of one commander's binding at one report: every bound bot's place and health, every reported enemy,
// and the run's step.
type TrackFrame struct {
	T     int64
	Map   uint32
	Step  uint32
	Fight bool
	Bots  []TrackBot
	Mobs  []TrackMob
}

// commanderTrack is one commander's frames plus the entry names seen in them (the frames carry entries, not names, to stay small).
type commanderTrack struct {
	frames   []TrackFrame
	names    map[uint32]string
	lastSeen time.Time
}

// noteTracks appends one frame per binding to the fight track. Called with the lock held.
func (st *commandersState) noteTracks(list []model.CommanderBinding, ts int64, now time.Time) {
	if st.tracks == nil {
		st.tracks = map[string]*commanderTrack{}
	}
	for _, b := range list {
		t := st.tracks[b.Commander]
		if t == nil {
			t = &commanderTrack{names: map[uint32]string{}}
			st.tracks[b.Commander] = t
		}
		t.lastSeen = now
		if n := len(t.frames); n > 0 && t.frames[n-1].T == ts {
			continue
		}
		f := TrackFrame{T: ts, Map: b.MobsMap, Fight: b.Combat != nil}
		if f.Map == 0 {
			for _, bot := range b.Bots {
				if bot.Lead {
					f.Map = bot.MapID
					break
				}
			}
		}
		if b.Run != nil {
			f.Step = b.Run.Step
		}
		ids := make(map[string]uint32, len(b.Bots))
		for _, bot := range b.Bots {
			if bot.GUID == 0 {
				continue
			}
			id := bot.ID
			if id == "" {
				id = strconv.FormatUint(uint64(bot.GUID), 10)
			}
			ids[id] = bot.GUID
			f.Bots = append(f.Bots, TrackBot{GUID: bot.GUID, X: float32(bot.X), Y: float32(bot.Y), HP: hpPercent(bot.HP, bot.MaxHP), Alive: bot.Alive, Map: bot.MapID})
		}
		for _, m := range b.Mobs {
			mob := TrackMob{ID: m.ID, Entry: m.Entry, X: float32(m.X), Y: float32(m.Y), HP: uint8(m.HP), Rank: m.Rank,
				O: m.O, CastID: m.CastID, CastEnd: m.CastEndMS, CastTotal: m.CastTotalMS, CastTarget: m.CastTarget,
				Channel: m.Channel, Shape: m.Shape}
			if m.Victim != "" {
				if g, ok := ids[m.Victim]; ok {
					mob.VictimBot = g
				} else {
					mob.VictimOther = true
				}
			}
			f.Mobs = append(f.Mobs, mob)
			if _, ok := t.names[m.Entry]; !ok && len(t.names) < trackMaxNames {
				t.names[m.Entry] = m.Name
			}
		}
		t.frames = append(t.frames, f)
		if len(t.frames) > TrackMaxFrames {
			t.frames = append(t.frames[:0], t.frames[len(t.frames)-TrackMaxFrames:]...)
		}
	}
	for c, t := range st.tracks {
		if now.Sub(t.lastSeen) > TrackKeep {
			delete(st.tracks, c)
		}
	}
	for len(st.tracks) > TrackMaxCommanders {
		oldest := ""
		var oldestAt time.Time
		for c, t := range st.tracks {
			if oldest == "" || t.lastSeen.Before(oldestAt) || (t.lastSeen.Equal(oldestAt) && c > oldest) {
				oldest, oldestAt = c, t.lastSeen
			}
		}
		delete(st.tracks, oldest)
	}
}

// hpPercent is the health percentage a bot's frame carries: round(hp * 100 / maxHP), zero when the maximum is unknown.
func hpPercent(hp, maxHP uint32) uint8 {
	if maxHP == 0 {
		return 0
	}
	return uint8(math.Floor(float64(hp)*100/float64(maxHP) + 0.5))
}

// Track returns copies of a commander's frames over [from, to], plus the frame just before from and the one just after to when
// each is within TrackEdgeSec of the window, so both edges can be interpolated. The names map holds the entries of the returned
// frames only. History outlives the binding: a finished fight stays replayable after the group has walked away.
func (s *Store) Track(commander string, from, to int64) ([]TrackFrame, map[uint32]string) {
	frames, names, _ := s.TrackWindow(commander, from, to)
	return frames, names
}

// TrackWindow is Track plus whether the daemon keeps a track for that commander at all: false means the commander was never
// sampled (it is outside the four the daemon tracks), not that the window held no report. Read under one lock so the answer
// belongs to the frames.
func (s *Store) TrackWindow(commander string, from, to int64) ([]TrackFrame, map[uint32]string, bool) {
	st := &s.commanders
	st.mu.Lock()
	defer st.mu.Unlock()
	frames, names := st.trackLocked(commander, from, to)
	return frames, names, st.tracks[commander] != nil
}

// trackLocked is Track's body with the lock held.
func (st *commandersState) trackLocked(commander string, from, to int64) ([]TrackFrame, map[uint32]string) {
	frames := []TrackFrame{}
	names := map[uint32]string{}
	t := st.tracks[commander]
	if t == nil {
		return frames, names
	}
	for _, f := range t.frames {
		if f.T < from {
			continue
		}
		if f.T <= to {
			frames = append(frames, copyFrame(f))
			continue
		}
		if f.T-to <= TrackEdgeSec {
			frames = append(frames, copyFrame(f))
		}
		break
	}
	for i := len(t.frames) - 1; i >= 0; i-- {
		if t.frames[i].T < from {
			if from-t.frames[i].T <= TrackEdgeSec {
				frames = append([]TrackFrame{copyFrame(t.frames[i])}, frames...)
			}
			break
		}
	}
	for _, f := range frames {
		for _, m := range f.Mobs {
			if n, ok := t.names[m.Entry]; ok {
				names[m.Entry] = n
			}
		}
	}
	return frames, names
}

func copyFrame(f TrackFrame) TrackFrame {
	out := f
	out.Bots = append([]TrackBot(nil), f.Bots...)
	out.Mobs = append([]TrackMob(nil), f.Mobs...)
	for i := range out.Mobs {
		if out.Mobs[i].O != nil {
			o := *out.Mobs[i].O
			out.Mobs[i].O = &o
		}
		if sh := out.Mobs[i].Shape; sh != nil {
			copied := *sh
			out.Mobs[i].Shape = &copied
		}
	}
	return out
}

// ParseTrackWindow checks GET /api/v1/commanders/track's query: a commander name and a window of at most TrackMaxWindowSec seconds.
func ParseTrackWindow(q url.Values) (commander string, from, to int64, err error) {
	bad := errors.New("bad window")
	commander = q.Get("commander")
	if commander == "" {
		return "", 0, 0, bad
	}
	from, err = strconv.ParseInt(q.Get("from"), 10, 64)
	if err != nil {
		return "", 0, 0, bad
	}
	to, err = strconv.ParseInt(q.Get("to"), 10, 64)
	if err != nil {
		return "", 0, 0, bad
	}
	if to < from || to-from > TrackMaxWindowSec {
		return "", 0, 0, bad
	}
	return commander, from, to, nil
}

// trackMobJSON is one enemy of a frame in the wire shape: v is the targeted bound bot's guid, vo flags a victim that is not a
// bound bot (the module sends 1, not true). The boss overlay: o facing in radians (2 decimals), cid cast id, ce cast end unix
// ms, ctot cast total ms, ctg cast target guid, ch channel, sh the spell's area.
type trackMobJSON struct {
	ID    string                `json:"id"`
	Entry uint32                `json:"e"`
	X     float64               `json:"x"`
	Y     float64               `json:"y"`
	HP    uint8                 `json:"hp"`
	V     uint32                `json:"v,omitempty"`
	VO    int                   `json:"vo,omitempty"`
	RK    string                `json:"rk,omitempty"`
	O     *float64              `json:"o,omitempty"`
	CID   uint32                `json:"cid,omitempty"`
	CE    int64                 `json:"ce,omitempty"`
	CT    uint32                `json:"ctot,omitempty"`
	CTG   string                `json:"ctg,omitempty"`
	CH    int                   `json:"ch,omitempty"`
	SH    *model.CommanderShape `json:"sh,omitempty"`
}

type trackFrameJSON struct {
	T     int64          `json:"t"`
	Map   uint32         `json:"map"`
	Step  uint32         `json:"step"`
	Fight bool           `json:"fight"`
	Bots  [][]any        `json:"bots"`
	Mobs  []trackMobJSON `json:"mobs"`
}

// TrackJSON is the wire shape of GET /api/v1/commanders/track: bots as [guid, x, y, hp %, alive, map], places rounded to 0.1 yd.
// tracked says whether the daemon keeps a track for that commander at all: a commander outside the four it tracks has no
// positions to replay, which the page says instead of "nothing kept for this fight".
func TrackJSON(commander string, frames []TrackFrame, names map[uint32]string, tracked bool) any {
	out := struct {
		Commander string            `json:"commander"`
		Tracked   bool              `json:"tracked"`
		Frames    []trackFrameJSON  `json:"frames"`
		Names     map[string]string `json:"names"`
	}{
		Commander: commander,
		Tracked:   tracked,
		Frames:    make([]trackFrameJSON, 0, len(frames)),
		Names:     make(map[string]string, len(names)),
	}
	for _, f := range frames {
		bots := make([][]any, 0, len(f.Bots))
		for _, b := range f.Bots {
			alive := 0
			if b.Alive {
				alive = 1
			}
			bots = append(bots, []any{b.GUID, roundTenth(b.X), roundTenth(b.Y), b.HP, alive, b.Map})
		}
		mobs := make([]trackMobJSON, 0, len(f.Mobs))
		for _, m := range f.Mobs {
			o := trackMobJSON{ID: m.ID, Entry: m.Entry, X: roundTenth(m.X), Y: roundTenth(m.Y), HP: m.HP, V: m.VictimBot, RK: m.Rank,
				CID: m.CastID, CE: m.CastEnd, CT: m.CastTotal, CTG: m.CastTarget, CH: m.Channel, SH: m.Shape}
			if m.VictimOther {
				o.VO = 1
			}
			if m.O != nil {
				facing := roundHundredth(*m.O)
				o.O = &facing
			}
			mobs = append(mobs, o)
		}
		out.Frames = append(out.Frames, trackFrameJSON{T: f.T, Map: f.Map, Step: f.Step, Fight: f.Fight, Bots: bots, Mobs: mobs})
	}
	for e, n := range names {
		out.Names[strconv.FormatUint(uint64(e), 10)] = n
	}
	return out
}

func roundTenth(v float32) float64 {
	return math.Round(float64(v)*10) / 10
}

func roundHundredth(v float64) float64 {
	return math.Round(v*100) / 100
}
