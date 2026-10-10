package model

import "time"

// ProtocolVersion is bumped whenever the C++ -> Go datagram layout changes in
// a way the daemon must understand. It is carried in every datagram.
const ProtocolVersion = 8

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
	Killer      string `json:"killer,omitempty"`
	KillerLevel uint32 `json:"killer_level,omitempty"`
	LastAction  string `json:"last_action,omitempty"`
	LastTrigger string `json:"last_trigger,omitempty"`
	// TravelPurpose/TravelTo describe the active travel destination ("grind",
	// "vendor", ... + title); empty when the bot is not travelling.
	TravelPurpose string `json:"travel_purpose,omitempty"`
	TravelTo      string `json:"travel_to,omitempty"`
	// TravelStatus is the travel target state ("travel", "work",
	// "cooldown", ...); TravelDist the yards left (-1 when idle).
	TravelStatus string `json:"travel_status,omitempty"`
	TravelDist   int32  `json:"travel_dist,omitempty"`
	// AiVisits counts AI updates since login; AiAgeMs is ms since the last.
	AiVisits uint32 `json:"ai_visits,omitempty"`
	AiAgeMs  uint32 `json:"ai_age_ms,omitempty"`
	// Pvp is "bg" inside a battleground, "queue" when queued, else empty.
	Pvp string `json:"pvp,omitempty"`
	// PvpBg names the battleground (or the queued ones, comma separated).
	PvpBg string `json:"pvp_bg,omitempty"`

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

	// Activity is the daemon's rolling per-bot activity rollup (counters only;
	// the level timeline lives in the /api/v1/activity detail). Nil until the
	// bot has produced its first tracked event.
	Activity *BotActivity `json:"activity,omitempty"`

	// Gear is the bot's equipped-gear summary, refreshed by the daemon from
	// the character DB every few minutes (never per tick). Nil until the first
	// gear sweep completes.
	Gear *BotGear `json:"gear,omitempty"`
}

// BotGear is one bot's equipped-gear summary: the average item level of the
// equipment slots that carry an item (0-18 except shirt and tabard, weapons
// and ranged included) and how many of those pieces fall in each quality tier.
// Derived from items equipped in the character DB; the average ignores empty
// slots, so a half-dressed bot is not dragged down by slots it never filled.
type BotGear struct {
	ItemLevel float64 `json:"item_level"`
	Pieces    int     `json:"pieces"`
	Grey      int     `json:"grey"`
	White     int     `json:"white"`
	Green     int     `json:"green"`
	Blue      int     `json:"blue"`
	Epic      int     `json:"epic"`
}

// BotEvent is one activity event forwarded by the game server's BOT_EVENTS
// datagram. The module forwards only a whitelist of bot_events.csv rows
// (quests, loot, vendor/trainer/repair/give-up/auction/AH, deaths) so the
// dashboard can roll them up per bot without a per-tick DB read. Item events
// carry the item prototype's quality and prices (looked up server-side, no
// DB); money is the bot's copper at event time, from which the daemon derives
// earned/spent deltas.
type BotEvent struct {
	Event  string `json:"event"`
	Info1  string `json:"info1,omitempty"`
	Info2  string `json:"info2,omitempty"`
	Bot    string `json:"bot,omitempty"`
	GUID   uint32 `json:"guid,omitempty"`
	Class  string `json:"class,omitempty"`
	Level  uint32 `json:"level,omitempty"`
	MapID  uint32 `json:"map,omitempty"`
	ZoneID uint32 `json:"zone,omitempty"`
	// Item enrichment, present only for events whose info2 is an item id.
	ItemID  uint32 `json:"item_id,omitempty"`
	Quality uint32 `json:"quality,omitempty"`
	Sell    uint32 `json:"sell,omitempty"`
	Buy     uint32 `json:"buy,omitempty"`
	// Money is the bot's copper at event time (0 = unknown).
	Money uint64 `json:"money,omitempty"`
}

// BotEventsPayload delivers the activity events emitted during one snapshot
// cycle. Events are not tied to a specific batch: the daemon applies them as
// they arrive and the next published roster carries the refreshed counters.
type BotEventsPayload struct {
	V       int        `json:"v"`
	Session uint64     `json:"session"`
	Seq     uint64     `json:"seq"`
	TS      int64      `json:"ts"`
	Type    string     `json:"type"`
	Events  []BotEvent `json:"events"`
}

// BotActivity is the daemon's rolling per-bot activity rollup. Counters are
// session-scoped (reset with the roster on a game-server restart or roster
// wipe). Money fields are copper.
type BotActivity struct {
	QuestsRewarded  int      `json:"quests_rewarded"`
	QuestsAccepted  int      `json:"quests_accepted"`
	QuestsCompleted int      `json:"quests_completed"`
	QuestHandIns    int      `json:"quest_handins"`
	OpenQuests      int      `json:"open_quests"`
	LootItems       int      `json:"loot_items"`
	LootValue       uint64   `json:"loot_value"`
	NotableLoot     int      `json:"notable_loot"`
	ItemsSold       int      `json:"items_sold"`
	SoldValue       uint64   `json:"sold_value"`
	ItemsBought     int      `json:"items_bought"`
	BoughtValue     uint64   `json:"bought_value"`
	MoneyEarned     uint64   `json:"money_earned"`
	MoneySpent      uint64   `json:"money_spent"`
	Kills           int      `json:"kills"`
	Deaths          int      `json:"deaths"`
	GhostSeconds    float64  `json:"ghost_seconds"`
	TrainerVisits   int      `json:"trainer_visits"`
	SpellsLearned   int      `json:"spells_learned"`
	VendorVisits    int      `json:"vendor_visits"`
	Repairs         int      `json:"repairs"`
	RepairCost      uint64   `json:"repair_cost"`
	GiveUps         int      `json:"giveups"`
	Skinning        int      `json:"skinning"`
	Gathering       int      `json:"gathering"`
	SkillUps        int      `json:"skillups"`
	AHListings      int      `json:"ah_listings"`
	AHBids          int      `json:"ah_bids"`
	Events          int64    `json:"events"`
	LevelsGained    int      `json:"levels_gained"`
	FirstSeen       int64    `json:"first_seen"`
	LastEvent       int64    `json:"last_event"`
	Levels          []LevelEvent `json:"levels,omitempty"`
}

// LevelEvent is one observed level for a bot: the first time the roster
// reported that level. The gap between consecutive entries is the time spent
// on the previous level.
type LevelEvent struct {
	Level uint32 `json:"level"`
	At    int64  `json:"at"`
}

// LootFeedItem is one row of the pool-wide loot feed: a looted/stored item, a
// gathering or skinning drop, or a money pickup. Source is "loot", "gather",
// "skin" or "money".
type LootFeedItem struct {
	At      int64  `json:"at"`
	// TimeStr is At rendered in the daemon's own time zone. The dashboard
	// renders it instead of formatting At in the viewer's zone, so feed rows
	// and incident rows cannot disagree about the hour.
	TimeStr string `json:"time_str,omitempty"`
	Bot     string `json:"bot"`
	GUID    uint32 `json:"guid"`
	Class   string `json:"class"`
	Level   uint32 `json:"level"`
	MapID   uint32 `json:"map"`
	ZoneID  uint32 `json:"zone"`
	Source  string `json:"source"`
	Item    string `json:"item,omitempty"`
	ItemID  uint32 `json:"item_id,omitempty"`
	Quality uint32 `json:"quality"`
	Value   uint64 `json:"value,omitempty"`
	Money   uint64 `json:"money,omitempty"`
}

// QuestFeedItem is one row of the pool-wide quest feed.
type QuestFeedItem struct {
	At      int64  `json:"at"`
	TimeStr string `json:"time_str,omitempty"`
	Bot     string `json:"bot"`
	GUID    uint32 `json:"guid"`
	Class   string `json:"class"`
	Level   uint32 `json:"level"`
	Event   string `json:"event"`
	Quest   string `json:"quest"`
	QuestID uint32 `json:"quest_id"`
}

// ActivityLevelItem is one level-up observed across the pool, used for the
// pool-wide level timeline.
type ActivityLevelItem struct {
	At      int64  `json:"at"`
	TimeStr string `json:"time_str,omitempty"`
	Bot     string `json:"bot"`
	GUID  uint32 `json:"guid"`
	Class string `json:"class"`
	Level uint32 `json:"level"`
}

// ActivitySummary is the pool-wide activity rollup.
type ActivitySummary struct {
	BotsTracked int `json:"bots_tracked"`
	// Since/SinceStr are the start of the window every counter covers: the
	// current game-server session. Since is 0 and SinceStr empty before the
	// first heartbeat. The dashboard must label counters with this window
	// ("since 10:24"), never present them as lifetime totals.
	Since      int64       `json:"since"`
	SinceStr   string      `json:"since_str,omitempty"`
	ElapsedSec float64     `json:"elapsed_sec"`
	Counters   BotActivity `json:"counters"`
}

// ActivityBot is one bot's activity in the /api/v1/activity response.
type ActivityBot struct {
	GUID     uint32      `json:"guid"`
	Name     string      `json:"name"`
	Class    string      `json:"class"`
	Level    uint32      `json:"level"`
	Activity BotActivity `json:"activity"`
}

// ActivityResponse is the payload of GET /api/v1/activity.
type ActivityResponse struct {
	Summary   ActivitySummary    `json:"summary"`
	Bots      []ActivityBot      `json:"bots"`
	LevelFeed []ActivityLevelItem `json:"level_feed"`
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
	TickAvgMs   float64          `json:"diff_avg,omitempty"`
	TickWorstMs float64          `json:"diff_worst,omitempty"`
	LagP50Ms    float64          `json:"lag_p50,omitempty"`
	LagP95Ms    float64          `json:"lag_p95,omitempty"`
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
	Rates map[string]float64 `json:"rates"`
	// Bots mixes numbers (pool sizes, intervals, budgets) with "0"/"1" flag
	// strings: the emitter writes each field in its natural JSON type, so this
	// must not be map[string]string or the whole SERVER_INFO datagram is
	// dropped by the unmarshaller. The UI renders numbers and flags alike.
	Bots        map[string]any    `json:"bots"`
	Diagnostics map[string]string `json:"diagnostics"`
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

// LevelBand is one adaptive level bucket: [Lo, Hi] with Count bots. AvgItemLevel
// is the mean equipped item level of the band's bots that have gear data
// (0 = no gear sweep has covered them yet).
type LevelBand struct {
	Lo           uint32  `json:"lo"`
	Hi           uint32  `json:"hi"`
	Count        int     `json:"count"`
	AvgItemLevel float64 `json:"avg_item_level,omitempty"`
	GearBots     int     `json:"gear_bots,omitempty"`
}

// ServerStatus is the daemon's single authoritative view of the game server
// and the freshness of the last complete roster snapshot.
type ServerStatus struct {
	Online              bool        `json:"online"`
	Stale               bool        `json:"stale"`
	Seq                 uint64      `json:"seq"`
	Uptime              uint32      `json:"uptime"`
	TickDiffMs          float64     `json:"diff"`
	TickAvgMs           float64     `json:"diff_avg,omitempty"`
	TickWorstMs         float64     `json:"diff_worst,omitempty"`
	LagP50Ms            float64     `json:"lag_p50,omitempty"`
	LagP95Ms            float64     `json:"lag_p95,omitempty"`
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

// HistorySample is one daemon-side chart observation: the values the
// dashboard's 10-minute timelines draw, sampled once per heartbeat
// (~2 s) so a page refresh can seed its charts instead of starting
// empty. At is a unix-millis timestamp (the browser's own Date.now
// scale); Issues reuse the IssueSnapshot per-type active counts.
type HistorySample struct {
	At       int64          `json:"at"`
	Bots     uint32         `json:"bots"`
	Humans   uint32         `json:"humans"`
	LagP50Ms float64        `json:"lag_p50"`
	LagP95Ms float64        `json:"lag_p95"`
	Issues   map[string]int `json:"issues"`
}

// HistoryResponse is the payload of GET /api/v1/history: the rolling
// in-memory chart history, oldest first. Daemon restarts clear it;
// game-server restarts clip it to the current session (see Since).
type HistoryResponse struct {
	Samples []HistorySample `json:"samples"`
	Since   int64           `json:"since"`
}

// AnomalyTotals is the cumulative per-session anomaly count behind
// GET /api/v1/anomalies/totals. The incident feed (/api/v1/anomalies) is a
// rolling 1000-row window, so whole-run counts (e.g. ACTION_LOOP rows for
// the KPI report) live here instead. Totals counts every accepted type
// (including counter-only STUCK); ByAction breaks down only the types whose
// last_action names the failing behaviour (ACTION_LOOP,
// UNREACHABLE_TARGET). Since/SinceStr mark when counting started: the first
// counted anomaly, or the session change / roster wipe that reset them.
type AnomalyTotals struct {
	Since    int64                        `json:"since"`
	SinceStr string                       `json:"since_str,omitempty"`
	Totals   map[string]uint64            `json:"totals"`
	ByAction map[string]map[string]uint64 `json:"by_action,omitempty"`
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
