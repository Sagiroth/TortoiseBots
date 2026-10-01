package model

import "time"

// ProtocolVersion is bumped whenever the C++ -> Go datagram layout changes in
// a way the daemon must understand. It is carried in every datagram.
const ProtocolVersion = 7

// Anomaly types accepted from the game server. Anything else is rejected so
// that Prometheus label cardinality stays bounded. STUCK is counter-only
// (tortoisebots_anomalies_total): the 8 s emitter rule is row noise, the 60 s
// STUCK issue episode is the surfaced signal, so STUCK never enters the
// Incidents ring buffer.
var AcceptedAnomalyTypes = map[string]bool{
	"STUCK":              true,
	"ACTION_LOOP":        true,
	"UNREACHABLE_TARGET": true,
	"BOT_DEATH":          true,
}

// BotSnapshot represents an active bot's live state in the world.
type BotSnapshot struct {
	Name     string  `json:"name"`
	GUID     uint32  `json:"guid"`
	Class    string  `json:"class"`
	Role     string  `json:"role"`
	Level    uint32  `json:"level"`
	XP       uint32  `json:"xp,omitempty"`      // PLAYER_XP: progress into the current level
	NextXP   uint32  `json:"next_xp,omitempty"` // PLAYER_NEXT_LEVEL_XP: XP needed to finish the level
	HP       uint32  `json:"hp"`
	MaxHP    uint32  `json:"max_hp"`
	Power     uint32 `json:"power"`
	MaxPower  uint32 `json:"max_power"`
	PowerType string `json:"power_type,omitempty"` // mana, rage, energy, focus, happiness
	MapID     uint32 `json:"map"`
	ZoneID   uint32  `json:"zone"`
	X        float64 `json:"x"`
	Y        float64 `json:"y"`
	Z        float64 `json:"z"`
	O        float64 `json:"o"`
	Target      string `json:"target"`
	TargetLevel uint32 `json:"target_level,omitempty"` // selected-unit target level (0 = none/self)
	Strategy    string `json:"strategy"`
	State       string `json:"state"` // "combat", "moving", "busy", "stalled", "resting", "dead", "idle"
	LastAction  string `json:"last_action,omitempty"`
	LastTrigger string `json:"last_trigger,omitempty"`
	// TravelPurpose/TravelTo describe the active travel destination ("grind",
	// "vendor", ... + title); empty when the bot is not travelling.
	TravelPurpose string `json:"travel_purpose,omitempty"`
	TravelTo      string `json:"travel_to,omitempty"`

	// XpPerHour is daemon-derived from successive XP samples (level-up
	// aware), not on the wire. XpGainAgeSec is seconds since the last
	// positive XP delta (-1 = no gain observed yet).
	XpPerHour    float64 `json:"xp_per_hour"`
	XpGainAgeSec float64 `json:"xp_gain_age_sec"`

	// Calculated 2D projection percentages on the active zone map. Projected
	// distinguishes a real (0,0) edge coordinate from "no mapping available".
	Projected bool    `json:"projected"`
	PctX      float64 `json:"pct_x"`
	PctY      float64 `json:"pct_y"`

	// Projected breadcrumb trail (server-maintained, newest last)
	Trail []Coordinate `json:"trail,omitempty"`
}

type Coordinate struct {
	X    float64 `json:"x"`
	Y    float64 `json:"y"`
	PctX float64 `json:"pct_x"`
	PctY float64 `json:"pct_y"`
}

// StateRatios holds the share of time spent across bot macro states. Values
// are a rolling-window ratio (0.0 - 1.0), not a lifetime average. Busy means
// standing still but doing real work (looting, casting, eating, or movement
// in the window); stalled means standing still for >= 45 s with nothing but
// churn (an active travel target or action-name changes); idle means no
// observable activity at all for >= 45 s.
type StateRatios struct {
	Combat  float64 `json:"combat"`
	Moving  float64 `json:"moving"`
	Busy    float64 `json:"busy"`
	Stalled float64 `json:"stalled"`
	Resting float64 `json:"resting"`
	Dead    float64 `json:"dead"`
	Idle    float64 `json:"idle"`
}

type ClassRoleCount struct {
	Class string `json:"class"`
	Role  string `json:"role"`
	Count uint32 `json:"count"`
}

// HeartbeatPayload is received periodically over UDP from the C++ module.
// It opens a snapshot cycle identified by Seq; BOT_BATCH datagrams carrying
// the same Seq complete that cycle.
type HeartbeatPayload struct {
	V           int              `json:"v"`
	Session     uint64           `json:"session"`
	Seq         uint64           `json:"seq"`
	TS          int64            `json:"ts"`
	Type        string           `json:"type"`
	Uptime      uint32           `json:"uptime"`
	TickDiffMs  float64          `json:"diff"`
	WindowSecs  uint32           `json:"window_secs,omitempty"`
	HumansCount uint32           `json:"humans"`
	BotsCount   uint32           `json:"bots"`
	States      StateRatios      `json:"states"`
	Counts      []ClassRoleCount `json:"counts,omitempty"`
}

// BotBatchPayload delivers one chunk of a snapshot cycle. A cycle is only
// published once every index in [0, TotalBatches) has been received.
type BotBatchPayload struct {
	V            int           `json:"v"`
	Session      uint64        `json:"session"`
	Seq          uint64        `json:"seq"`
	TS           int64         `json:"ts"`
	Type         string        `json:"type"`
	BatchIndex   int           `json:"batch_index"`
	TotalBatches int           `json:"total_batches"`
	Bots         []BotSnapshot `json:"bots"`
}
// ServerInfoPayload is the effective running configuration the emitter sends
// at startup and every few minutes. Every value is a live getter result
// (core sWorld rates, AiPlayerbot fields), never a config-file read, and
// carries no secrets: numbers and on/off only.
type ServerInfoPayload struct {
	V             int                `json:"v"`
	Session       uint64             `json:"session"`
	Seq           uint64             `json:"seq"`
	TS            int64              `json:"ts"`
	Type          string             `json:"type"`
	ModuleVersion string             `json:"module_version"`
	CoreRevision  string             `json:"core_revision"`
	CoreDate      string             `json:"core_date"`
	Uptime        uint32             `json:"uptime"`
	MaxLevel      uint32             `json:"max_level"`
	Rates         map[string]float64 `json:"rates"`
	Bots          map[string]string  `json:"bots"`
	Diagnostics   map[string]string  `json:"diagnostics"`
}

// GrindingSummary is the daemon's pool-wide "are they grinding" rollup,
// computed from live XP deltas, combat state, and travel purpose. Deaths are
// BOT_DEATH anomalies (a bot dying, not a bot killing): they measure pool
// casualties, not grinding productivity.
type GrindingSummary struct {
	BotsTracked   int            `json:"bots_tracked"`
	BotsGainingXP int            `json:"bots_gaining_xp"`
	PctGainingXP  float64        `json:"pct_gaining_xp"`
	MedianXpHour  float64        `json:"median_xp_hour"`
	TotalXpHour   float64        `json:"total_xp_hour"`
	DeathsPerMin  float64        `json:"deaths_per_min"`
	PctDied5Min   float64        `json:"pct_died_5min"`
	// StateCounts is the single authoritative per-state census (roster
	// states, not the 3-min rolling ratios): combat/moving/busy/stalled/
	// resting/dead/idle counts. The dashboard Activity block renders counts
	// + % from here; Fleet Health and Grinding no longer duplicate them.
	StateCounts map[string]int `json:"state_counts"`
	// LevelBands is adaptive: per level while the pool is narrow (e.g.
	// L1..L7 during launch), widening to 1-9/10-19/.../60 as it spreads.
	LevelBands []LevelBand `json:"level_bands"`
}

// LevelBand is one adaptive level bucket: [Lo, Hi] with Count bots.
type LevelBand struct {
	Lo    uint32 `json:"lo"`
	Hi    uint32 `json:"hi"`
	Count int    `json:"count"`
}

// ServerStatus is the daemon's single authoritative view of the game server
// and the freshness of the last complete roster snapshot.
type ServerStatus struct {
	Online              bool        `json:"online"`
	Stale               bool        `json:"stale"`
	Seq                 uint64      `json:"seq"`
	Uptime              uint32      `json:"uptime"`
	TickDiffMs          float64     `json:"diff"`
	WindowSecs          uint32      `json:"window_secs,omitempty"`
	Humans              uint32      `json:"humans"`
	Bots                uint32      `json:"bots"`
	States              StateRatios `json:"states"`
	LastHeartbeatAgeSec float64     `json:"last_heartbeat_age_sec"`
	LastSnapshotAgeSec  float64     `json:"last_snapshot_age_sec"`
	SnapshotsPublished  uint64      `json:"snapshots_published"`
}

// SnapshotPayload is the coherent roster handed to REST and WebSocket clients.
type SnapshotPayload struct {
	Seq      uint64          `json:"seq"`
	Server   ServerStatus    `json:"server"`
	Bots     []BotSnapshot   `json:"bots"`
	Issues   IssueSnapshot   `json:"issues"`
	Grinding GrindingSummary `json:"grinding"`
	Info     *ServerInfoPayload `json:"info,omitempty"`
}

// Issue is one persistent bot problem tracked as an episode (open while the
// condition lasts, closed and archived when it clears).
type Issue struct {
	GUID        uint32  `json:"guid"`
	Bot         string  `json:"bot"`
	Class       string  `json:"class"`
	Level       uint32  `json:"level"`
	Type        string  `json:"type"`     // STUCK, DEAD_LONG, UNREACHABLE_TARGET
	Severity    string  `json:"severity"` // watch, persistent
	DurationSec float64 `json:"duration_sec"`
	Action      string  `json:"action,omitempty"`
	Trigger     string  `json:"trigger,omitempty"`
	Target      string  `json:"target,omitempty"`
	Details     string  `json:"details,omitempty"`
	MapID       uint32  `json:"map"`
	ZoneID      uint32  `json:"zone"`
}

// IssueSnapshot is the issue view attached to each roster snapshot.
type IssueSnapshot struct {
	Active       []Issue        `json:"active"`
	Resolved     []Issue        `json:"resolved"`
	CountsByType map[string]int `json:"counts_by_type"`
}

// Position represents 3D coordinates.
type Position struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	Z float64 `json:"z"`
}

// AnomalyPayload represents an anomaly event emitted from C++ to Go.
type AnomalyPayload struct {
	ID         int64     `json:"id,omitempty"`
	TS         int64     `json:"ts"`
	TimeStr    string    `json:"time_str,omitempty"`
	Type       string    `json:"type"`     // "STUCK", "ACTION_LOOP", "UNREACHABLE_TARGET", "BOT_DEATH"
	Severity   string    `json:"severity"` // "WARN", "ERROR", "INFO"
	Bot        string    `json:"bot"`
	GUID       uint32    `json:"guid"`
	Class      string    `json:"class"`
	Level      uint32    `json:"level"`
	MapID      uint32    `json:"map"`
	ZoneID     uint32    `json:"zone"`
	Pos        Position  `json:"pos"`
	Target     string    `json:"target"`
	Strategy   string    `json:"strategy"`
	LastAction string    `json:"last_action"`
	Details    string    `json:"details"`
	ReceivedAt time.Time `json:"-"`
}

// ZoneBoundingBox matches WorldMapArea.dbc records.
type ZoneBoundingBox struct {
	WmaID     uint32  `json:"wma_id"`
	MapID     uint32  `json:"map_id"`
	AreaID    uint32  `json:"area_id"`
	Name      string  `json:"name"`
	LocLeft   float64 `json:"loc_left"`   // y1
	LocRight  float64 `json:"loc_right"`  // y2
	LocTop    float64 `json:"loc_top"`    // x1
	LocBottom float64 `json:"loc_bottom"` // x2
}
