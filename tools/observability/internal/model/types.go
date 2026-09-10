package model

import "time"

// ProtocolVersion is bumped whenever the C++ -> Go datagram layout changes in
// a way the daemon must understand. It is carried in every datagram.
const ProtocolVersion = 2

// Anomaly types accepted from the game server. Anything else is rejected so
// that Prometheus label cardinality stays bounded.
var AcceptedAnomalyTypes = map[string]bool{
	"BOT_STUCK":          true,
	"ACTION_LOOP":        true,
	"UNREACHABLE_TARGET": true,
}

// BotSnapshot represents an active bot's live state in the world.
type BotSnapshot struct {
	Name     string  `json:"name"`
	GUID     uint32  `json:"guid"`
	Class    string  `json:"class"`
	Role     string  `json:"role"`
	Level    uint32  `json:"level"`
	HP       uint32  `json:"hp"`
	MaxHP    uint32  `json:"max_hp"`
	Power    uint32  `json:"power"`
	MaxPower uint32  `json:"max_power"`
	MapID    uint32  `json:"map"`
	ZoneID   uint32  `json:"zone"`
	X        float64 `json:"x"`
	Y        float64 `json:"y"`
	Z        float64 `json:"z"`
	O        float64 `json:"o"`
	Target   string  `json:"target"`
	Strategy string  `json:"strategy"`
	State    string  `json:"state"` // "combat", "moving", "resting", "dead", "idle"

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
// are a rolling-window ratio (0.0 - 1.0), not a lifetime average.
type StateRatios struct {
	Combat  float64 `json:"combat"`
	Moving  float64 `json:"moving"`
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
	Seq    uint64         `json:"seq"`
	Server ServerStatus   `json:"server"`
	Bots   []BotSnapshot  `json:"bots"`
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
	Type       string    `json:"type"`     // "BOT_STUCK", "ACTION_LOOP", "UNREACHABLE_TARGET"
	Severity   string    `json:"severity"` // "WARN", "ERROR", "INFO"
	Bot        string    `json:"bot"`
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
