package model

import "time"

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

	// Calculated 2D projection percentages on the active zone map
	PctX float64 `json:"pct_x,omitempty"`
	PctY float64 `json:"pct_y,omitempty"`

	// Breadcrumb trail (last 10 positions)
	Trail []Coordinate `json:"trail,omitempty"`
}

type Coordinate struct {
	X    float64 `json:"x"`
	Y    float64 `json:"y"`
	PctX float64 `json:"pct_x"`
	PctY float64 `json:"pct_y"`
}

// StateRatios holds the percentage time spent across bot macro states.
type StateRatios struct {
	Combat  float64 `json:"combat"`
	Moving  float64 `json:"moving"`
	Resting float64 `json:"resting"`
	Dead    float64 `json:"dead"`
	Idle    float64 `json:"idle"`
}

// HeartbeatPayload is received periodically over UDP from the C++ module.
type HeartbeatPayload struct {
	TS          int64         `json:"ts"`
	Type        string        `json:"type"`
	Uptime      uint32        `json:"uptime"`
	TickDiffMs  float64       `json:"diff"`
	HumansCount uint32        `json:"humans"`
	BotsCount   uint32        `json:"bots"`
	States      StateRatios   `json:"states"`
	BotList     []BotSnapshot `json:"bot_list"`
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
