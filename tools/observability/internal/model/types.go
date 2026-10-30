package model

import "time"

// ProtocolVersion is bumped whenever the C++ -> Go datagram layout changes in
// a way the daemon must understand. It is carried in every datagram.
const ProtocolVersion = 12

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

// BotSnapshot represents one live row of the world roster: an active bot
// or (Player == true) a real online player. Players carry the same
// list/map/detail fields that make sense for them (name, level, class, zone,
// position, group) and leave bot-only detail empty; GroupID is the core
// group id (0 = ungrouped).
type BotSnapshot struct {
	Name   string `json:"name"`
	GUID   uint32 `json:"guid"`
	Player bool   `json:"player,omitempty"`
	// Alt marks an autonomous owned alt-bot (Issue #872). Additive since
	// protocol v10; an older module omits it and it reads false.
	Alt bool `json:"alt,omitempty"`
	// Kind is the dashboard kind label (Issue #872): pool, hired, claimed,
	// alt, companion or player. Additive since protocol v11; an older module
	// omits it and readers fall back to player/alt/pool from the flags.
	Kind      string `json:"kind,omitempty"`
	Class     string `json:"class"`
	GroupID   uint32 `json:"group,omitempty"`
	Role      string `json:"role"`
	Level     uint32 `json:"level"`
	XP        uint32 `json:"xp,omitempty"`      // PLAYER_XP: progress into the current level
	NextXP    uint32 `json:"next_xp,omitempty"` // PLAYER_NEXT_LEVEL_XP: XP needed to finish the level
	HP        uint32 `json:"hp"`
	MaxHP     uint32 `json:"max_hp"`
	Power     uint32 `json:"power"`
	MaxPower  uint32 `json:"max_power"`
	PowerType string `json:"power_type,omitempty"` // mana, rage, energy, focus, happiness
	// Guild is the guild name the unit carries (additive: an older module
	// omits it and it reads empty). Empty = guildless.
	Guild    string  `json:"guild,omitempty"`
	MapID     uint32 `json:"map"`
	ZoneID    uint32 `json:"zone"`
	// Instance is the instance copy the unit is in (v12, issue #895): 0 in
	// the open world, non-zero inside a dungeon, raid or battleground copy.
	// A pointer so a pre-v12 module (field absent, nil) and a new one
	// reporting the open world (present, 0) stay distinguishable: the old
	// module's dungeon units fall back to one run per map. SubGroup is the
	// unit's raid subgroup (0 outside a raid or group).
	Instance    *uint32 `json:"instance,omitempty"`
	SubGroup    uint32  `json:"subgroup,omitempty"`
	X           float64 `json:"x"`
	Y           float64 `json:"y"`
	Z           float64 `json:"z"`
	O           float64 `json:"o"`
	Target      string  `json:"target"`
	TargetLevel uint32  `json:"target_level,omitempty"` // selected-unit target level (0 = none/self)
	Strategy    string  `json:"strategy"`
	State       string  `json:"state"` // "combat", "moving", "busy", "stalled", "resting", "dead", "idle"
	Killer      string  `json:"killer,omitempty"`
	KillerLevel uint32  `json:"killer_level,omitempty"`
	LastAction  string  `json:"last_action,omitempty"`
	LastTrigger string  `json:"last_trigger,omitempty"`
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
	// WorldPvpSeeker marks a bot holding the open-world PvP seek role.
	WorldPvpSeeker bool `json:"world_pvp_seeker,omitempty"`
	// WorldPvpFighting names the enemy the bot is fighting right now in the
	// open world (empty when none): the target of its last engage event,
	// cleared on kill/death or after a minute without a new event.
	WorldPvpFighting string `json:"world_pvp_fighting,omitempty"`

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

// Kind constants for BotSnapshot.Kind (Issue #872).
const (
	KindPool      = "pool"
	KindHired     = "hired"
	KindClaimed   = "claimed"
	KindAlt       = "alt"
	KindCompanion = "companion"
	KindPlayer    = "player"
)

// KindFromFlags reads the kind off the pre-v11 player/alt flags: player,
// then alt, else pool (hired/claimed/companion need the v11 kind field).
func KindFromFlags(player, alt bool) string {
	if player {
		return KindPlayer
	}
	if alt {
		return KindAlt
	}
	return KindPool
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
	// TargetLevel is the other side's level for world PvP events
	// (0 = unknown, e.g. from an older module build).
	TargetLevel uint32 `json:"target_level,omitempty"`
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
	QuestsRewarded  int          `json:"quests_rewarded"`
	QuestsAccepted  int          `json:"quests_accepted"`
	QuestsCompleted int          `json:"quests_completed"`
	QuestHandIns    int          `json:"quest_handins"`
	OpenQuests      int          `json:"open_quests"`
	LootItems       int          `json:"loot_items"`
	LootValue       uint64       `json:"loot_value"`
	NotableLoot     int          `json:"notable_loot"`
	ItemsSold       int          `json:"items_sold"`
	SoldValue       uint64       `json:"sold_value"`
	ItemsBought     int          `json:"items_bought"`
	BoughtValue     uint64       `json:"bought_value"`
	MoneyEarned     uint64       `json:"money_earned"`
	MoneySpent      uint64       `json:"money_spent"`
	Kills           int          `json:"kills"`
	Deaths          int          `json:"deaths"`
	PvpEngagements  int          `json:"pvp_engagements"`
	PvpKills        int          `json:"pvp_kills"`
	PvpDeaths       int          `json:"pvp_deaths"`
	GhostSeconds    float64      `json:"ghost_seconds"`
	TrainerVisits   int          `json:"trainer_visits"`
	SpellsLearned   int          `json:"spells_learned"`
	VendorVisits    int          `json:"vendor_visits"`
	Repairs         int          `json:"repairs"`
	RepairCost      uint64       `json:"repair_cost"`
	GiveUps         int          `json:"giveups"`
	Skinning        int          `json:"skinning"`
	Gathering       int          `json:"gathering"`
	SkillUps        int          `json:"skillups"`
	AHListings      int          `json:"ah_listings"`
	AHBids          int          `json:"ah_bids"`
	Events          int64        `json:"events"`
	LevelsGained    int          `json:"levels_gained"`
	FirstSeen       int64        `json:"first_seen"`
	LastEvent       int64        `json:"last_event"`
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
	At int64 `json:"at"`
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

// PvpFeedItem is one row of the pool-wide world PvP feed: who fought whom,
// when and where. Event is engage, kill or death (from the bot's view);
// TargetKind is "bot" or "player" for engages (empty on rows from older
// module builds, which carry only the name and level).
type PvpFeedItem struct {
	At          int64  `json:"at"`
	TimeStr     string `json:"time_str,omitempty"`
	Bot         string `json:"bot"`
	GUID        uint32 `json:"guid"`
	Class       string `json:"class"`
	Level       uint32 `json:"level"`
	MapID       uint32 `json:"map"`
	ZoneID      uint32 `json:"zone"`
	Event       string `json:"event"`
	Target      string `json:"target"`
	TargetKind  string `json:"target_kind,omitempty"`
	TargetLevel uint32 `json:"target_level,omitempty"`
}

// ActivityLevelItem is one level-up observed across the pool, used for the
// pool-wide level timeline.
type ActivityLevelItem struct {
	At      int64  `json:"at"`
	TimeStr string `json:"time_str,omitempty"`
	Bot     string `json:"bot"`
	GUID    uint32 `json:"guid"`
	Class   string `json:"class"`
	Level   uint32 `json:"level"`
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
	Summary   ActivitySummary     `json:"summary"`
	Bots      []ActivityBot       `json:"bots"`
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
	Rates         map[string]float64 `json:"rates"`
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
	BotsTracked     int     `json:"bots_tracked"`
	BotsGainingXP   int     `json:"bots_gaining_xp"`
	PctGainingXP    float64 `json:"pct_gaining_xp"`
	MedianXpHour    float64 `json:"median_xp_hour"`
	TotalXpHour     float64 `json:"total_xp_hour"`
	DeathsPerMin    float64 `json:"deaths_per_min"`
	PctDied5Min     float64 `json:"pct_died_5min"`
	WorldPvpSeekers int     `json:"world_pvp_seekers"`
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
	Seq      uint64             `json:"seq"`
	Server   ServerStatus       `json:"server"`
	Bots     []BotSnapshot      `json:"bots"`
	Issues   IssueSnapshot      `json:"issues"`
	Grinding GrindingSummary    `json:"grinding"`
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

// CommandersPayload is the COMMANDERS datagram: the standing remote-commander
// bindings (a commander that leads a bot-only party from anywhere) with the
// bound bots and the commander's last orders. Sent every snapshot cycle while
// a binding stands, and once with an empty list when the last one ends. It
// describes what the module knows, for any client of the commander protocol.
type CommandersPayload struct {
	V       int    `json:"v"`
	Session uint64 `json:"session"`
	Seq     uint64 `json:"seq"`
	// Part/Parts split one snapshot cycle across datagrams when the bindings or bots pass the payload budget (a part
	// holds at most 8 bindings, 64 bots and 64 mobs, and a binding is never split). Both are absent from a module
	// whose cycle fits one datagram, which is byte-identical to the payload before parts existed.
	Part       int                `json:"part,omitempty"`
	Parts      int                `json:"parts,omitempty"`
	TS         int64              `json:"ts"`
	TSMs       int64              `json:"ts_ms,omitempty"`
	Type       string             `json:"type"`
	Commanders []CommanderBinding `json:"commanders"`
}

// CommanderBinding is one commander's standing binding.
type CommanderBinding struct {
	Commander     string           `json:"commander"`
	CommanderGUID uint32           `json:"commander_guid,omitempty"` // the commander character's guid (low part; absent from an older module): opens its detail view
	Online        bool             `json:"online"`                   // the commander has a network session
	View          bool             `json:"view"`                     // its TBM:VIEW perception feed is on
	Group         uint32           `json:"group"`
	Lead          string           `json:"lead"`
	Bots          []CommanderBot   `json:"bots"`
	Orders        []CommanderOrder `json:"orders"` // oldest first

	// Map data, from a module that reports it (all absent from an older one; the dashboard then greys the layers out).
	CommanderPos *CommanderPos    `json:"commander_pos,omitempty"` // where the commander stands, when online and in the world
	MobsMap      uint32           `json:"mobs_map,omitempty"`      // the map the mobs are on (the lead's)
	Mobs         []CommanderMob   `json:"mobs"`                    // hostile units near the lead; null = not reported, [] = reported, none near
	BoundAt      int64            `json:"bound_at,omitempty"`      // unix seconds the binding was first seen standing (the run timer starts here); absent from an older module
	Run          *CommanderRun    `json:"run,omitempty"`           // the run leader's run of this binding ("observe" mode); absent when it has none or from an older module
	Layout       *CommanderLayout `json:"layout,omitempty"`        // the raid layout the module applied to the group; absent when it applied none or from an older module

	// Doors, chests, altars and gongs the bots have seen near them (on the map of MobsMap), capped; null = not reported, [] = reported, none seen.
	Objects []CommanderObject `json:"objects"`

	// The live fight window (module CombatMonitor): present only while the bound group fights, absent outside a fight or from an older module.
	Combat *CommanderCombat `json:"combat,omitempty"`
}

// CommanderObject is a game object a bot of the binding has seen near it. IDs are decimal strings, as the mobs' are.
type CommanderObject struct {
	ID    string  `json:"id"`
	Entry uint32  `json:"entry"`
	Name  string  `json:"name"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
	Type  string  `json:"type"`  // d door, c chest, b button or lever, g goober (gong, altar, switch), r ritual, o other
	State string  `json:"state"` // r ready (closed, untouched), a active (open, pressed), u used (looted or spent)
}

// CommanderRunNote is one status line of the run leader's run (A6): the
// last 30 distinct TBM:RUN lines, oldest first, so the dashboard can say
// why the run waits and what it did before.
type CommanderRunNote struct {
	At    int64  `json:"at"`   // unix seconds the line went out
	Step  uint32 `json:"step"` // 1-based step shown at the time
	Leg   string `json:"leg"`
	Kind  string `json:"kind"`
	Phase string `json:"phase"`
	State string `json:"state"`
	Note  string `json:"note"`
}

// CommanderRunStep is one step of the run's plan in the current section: where it is and what it is for.
type CommanderRunStep struct {
	Index   uint32  `json:"i"` // 1-based position in the plan (the run's "step" counts the same way)
	Kind    string  `json:"k"` // move, move_direct, pull, boss, loot, open, use_item, wait_object, wait_clear, rest, ritual
	X       float64 `json:"x"`
	Y       float64 `json:"y"`
	Radius  float64 `json:"r,omitempty"`  // pull, boss, wait_clear: the area the step cares about
	Entry   uint32  `json:"e,omitempty"`  // the creature or object the step is about
	Name    string  `json:"n,omitempty"`  // its name when the module knows it
	Label   string  `json:"c,omitempty"`  // the row's own comment (a short description)
	Skipped bool    `json:"sk,omitempty"` // left out with `.bot run skip`
}

// CommanderRun is where the run leader (`.bot run`) stands in a dungeon plan for one binding.
type CommanderRun struct {
	Plan    string `json:"plan"`
	State   string `json:"state"`           // running, paused or done
	Step    uint32 `json:"step"`            // 1-based step shown (0 before the first)
	Steps   uint32 `json:"steps"`           // steps in the plan
	Leg     string `json:"leg"`             // the current leg's name: the section
	Kind    string `json:"kind"`            // move, pull, boss, loot, open ...
	Phase   string `json:"phase"`           // what the step is doing
	Label   string `json:"label,omitempty"` // the current step's own comment (the plan row's short description)
	Note    string `json:"note"`            // what it does now, or why it paused
	Changed int64  `json:"changed"`         // unix seconds the step, phase, state or note last changed

	// The plan's steps of the current section (the next ~20), from a module that reports them; absent otherwise.
	PlanSteps []CommanderRunStep `json:"plan_steps,omitempty"`

	// The run's last status lines, oldest first, from a module that reports them; absent otherwise.
	Notes []CommanderRunNote `json:"notes,omitempty"`
}

// CommanderPos is the commander character's own position.
type CommanderPos struct {
	MapID uint32  `json:"map"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
}

// CommanderAura is one aura the module saw on a unit: a boss's own buff or form, or a boss debuff on a bot. Stacks and
// LeftMS are left out when they add nothing (one stack, a permanent aura); Dispel is the dispel type a healer must bring
// (m magic, c curse, p poison, d disease) and is empty when the aura is not dispellable.
type CommanderAura struct {
	ID     uint32 `json:"id"`
	Name   string `json:"n,omitempty"`
	Stacks uint32 `json:"st,omitempty"`
	LeftMS uint32 `json:"l,omitempty"`
	Dispel string `json:"dt,omitempty"`
}

// CommanderMob is a unit near the lead of a binding. IDs are decimal strings: a creature guid is 64 bits, more than a JSON
// number holds in a browser.
type CommanderMob struct {
	ID       string  `json:"id"`
	Entry    uint32  `json:"entry"`
	Name     string  `json:"name"`
	HP       uint32  `json:"hp"` // percent
	X        float64 `json:"x"`
	Y        float64 `json:"y"`
	Victim   string  `json:"victim,omitempty"` // guid of the unit it targets; matches a bot's id
	Reaction string  `json:"reaction"`         // h hostile, n neutral, f friendly (to the bound bots)
	Kind     string  `json:"kind,omitempty"`   // k critter, v civilian, g guard, u unattackable; empty: an ordinary unit
	Level    uint32  `json:"lvl,omitempty"`    // absent: unknown
	Rank     string  `json:"rank,omitempty"`   // b boss, e elite, r rare; empty: an ordinary unit
	CC       string  `json:"cc,omitempty"`     // s stunned, c confused (polymorph, sleep), f fleeing, r rooted; empty: free
	Cast     string  `json:"cast,omitempty"`   // the spell it is casting right now

	// The boss overlay (fight debugger phase 2), from a module that reports it and only on boss and elite units: where the
	// unit faces, the cast it runs and the area that spell covers.
	O           *float64        `json:"o,omitempty"`       // facing, radians (0 = +x north, counter-clockwise toward +y west); a pointer so 0 is reported and absent stays absent
	CastID      uint32          `json:"cid,omitempty"`     // the spell being cast or channelled
	CastLeftMS  uint32          `json:"cleft,omitempty"`   // time left of that cast (or channel) when the module read it, ms
	CastTotalMS uint32          `json:"ctot,omitempty"`    // the cast's own time, or the channel's duration, ms
	CastTarget  string          `json:"ctg,omitempty"`     // guid (decimal string) of the unit the spell is aimed at
	Channel     int             `json:"ch,omitempty"`      // 1 when the spell is channelled
	Shape       *CommanderShape `json:"shape,omitempty"`   // the area the spell covers; absent when the module has no shape for it
	CastEndMS   int64           `json:"cend_ms,omitempty"` // when the cast ends, unix ms, filled by the daemon from the report's ts and the time left

	// The auras and flags of a boss or elite unit (the overlay rows only): its non-passive auras in holder-map order, and
	// the letters n not selectable, i not attackable, h hovering or flying, d dead (feigning), e evading, in that order.
	Auras []CommanderAura `json:"au,omitempty"`
	Flags string          `json:"uf,omitempty"` // absent: none of the flags above
}

// CommanderShape is the area a boss or elite spell covers, as the module's committed shape table knows it: k is cone (in
// front), back-cone (behind) or line (along the facing, or between two map points); a the full arc in degrees (cones), l
// the reach in yards along the facing (a line with a segment: the ends' distance), w the band across a line in yards. seg
// is a line's two ends as [x0,y0,x1,y1] in map coordinates when the module pins them (a damaging lane does not start at
// the boss's feet); a line without it runs along the facing. An unknown spell has no shape at all.
type CommanderShape struct {
	Kind    string    `json:"k"`
	Angle   uint16    `json:"a,omitempty"`
	Length  float64   `json:"l,omitempty"`
	Width   float64   `json:"w,omitempty"`
	Segment []float64 `json:"seg,omitempty"`
}

// CommanderBot is a bot of a bound party.
type CommanderBot struct {
	Name      string  `json:"name"`
	GUID      uint32  `json:"guid"`
	Class     string  `json:"class"`
	Role      string  `json:"role"`
	Level     uint32  `json:"level"`
	HP        uint32  `json:"hp"`
	MaxHP     uint32  `json:"max_hp"`
	Power     uint32  `json:"power"`
	MaxPower  uint32  `json:"max_power"`
	PowerType string  `json:"power_type"`
	Alive     bool    `json:"alive"`
	Lead      bool    `json:"lead"`
	MapID     uint32  `json:"map"`
	ZoneID    uint32  `json:"zone"`
	X         float64 `json:"x"`
	Y         float64 `json:"y"`
	Z         float64 `json:"z"`
	ID        string  `json:"id,omitempty"`     // the bot's guid as a decimal string (what a mob's victim and a bot's target name)
	Target    string  `json:"target,omitempty"` // guid of its target, absent when it has none

	// Activity, from a newer module (all absent from an older one).
	State      string `json:"st,omitempty"`   // the AI's state: c combat, n non-combat, d dead, r reaction
	Action     string `json:"act,omitempty"`  // the engine's newest action name
	Result     string `json:"res,omitempty"`  // that action's verdict: O ok, F failed, I impossible, U useless, P prereq, V vetoed, - none
	Cast       string `json:"cast,omitempty"` // the spell it is casting right now
	CastTarget string `json:"ctgt,omitempty"` // guid (decimal string) of the unit that spell is aimed at: a healer's heal target, a caster's crowd-controlled mob
	CC         string `json:"cc,omitempty"`   // s stunned, c confused, f fleeing, r rooted; empty: free
	Rest       string `json:"rest,omitempty"` // d drinking, e eating
	Moving     bool   `json:"mv,omitempty"`
	TargetName string `json:"tname,omitempty"` // name of its target
	// Raid, from a module that reports it (both absent from a party bot or an older module).
	Sub  *int `json:"sub,omitempty"`  // the raid subgroup 0-7
	Away bool `json:"away,omitempty"` // off the run's gather rule or the plan's map
	// The ready check's facts, from a module that reports them (absent from an older one).
	Ready *CommanderReady `json:"rdy,omitempty"`

	// The boss debuffs on the bot (up to four, dispellable first) and whether it is charmed or mind-controlled; both absent
	// from an older module.
	Debuffs []CommanderAura `json:"db,omitempty"`
	MC      int             `json:"mc,omitempty"` // 1: charmed / mind-controlled
}

// CommanderReady is what one bot brings to a ready check: the raid buffs it has (b), the consumables it has (c), and its
// key cooldowns (two minutes or longer): how many are ready (cr) out of how many it knows (cn), and the longest time left
// among the not-ready ones in seconds (cl, absent when 0).
type CommanderReady struct {
	B  uint32 `json:"b"`
	C  uint32 `json:"c"`
	CR uint8  `json:"cr"`
	CN uint8  `json:"cn"`
	CL uint32 `json:"cl,omitempty"`
}

// CommanderOrder is one order of the commander and the module's answer.
type CommanderOrder struct {
	At     int64  `json:"at"` // unix seconds
	Verb   string `json:"verb"`
	Detail string `json:"detail"`
	OK     bool   `json:"ok"`
	Result string `json:"result"`
}

// CommanderLayout is the raid layout the module applied to the binding's group (TBM:RAID_LAYOUT): the main tank, the
// off-tanks, each tank healer with the tank it was given, and the raid healers left healing the raid.
type CommanderLayout struct {
	MainTank    string                `json:"mt"`
	OffTanks    []string              `json:"ot"`
	TankHealers []CommanderTankHealer `json:"th"`
	RaidHealers []string              `json:"rh"`
}

// CommanderTankHealer is one tank healer of a layout: the healer and the tank it heals.
type CommanderTankHealer struct {
	Healer string `json:"h"`
	Tank   string `json:"t"`
}

// CommanderCombat is the live fight window the module's CombatMonitor reports while the bound group is in a fight: the length
// of the window, how long the fight has run, and the numbers of the bots that did something in it.
type CommanderCombat struct {
	WinS    uint32               `json:"win_s"`
	FightMS uint32               `json:"fight_ms"`
	Bots    []CommanderCombatBot `json:"bots"`
}

// CommanderCombatBot is one bot's numbers in the live window; a bot with nothing in the window is left out.
type CommanderCombatBot struct {
	GUID  uint32 `json:"g"`
	DPS   uint64 `json:"dps"`
	HPS   uint64 `json:"hps"`
	Casts uint32 `json:"c"`
	Last  string `json:"ls"`
}

// CommandersView is what GET /api/v1/commanders and the "commanders" stream
// event carry.
type CommandersView struct {
	Commanders []CommanderBinding `json:"commanders"`
	// UpdatedAt is the unix time of the last COMMANDERS datagram (0: none yet).
	UpdatedAt int64 `json:"updated_at"`
	// AgeSec is how old that datagram is; Fresh is false past the stale limit,
	// when Commanders is empty whatever the last datagram said.
	AgeSec float64 `json:"age_sec"`
	Fresh  bool    `json:"fresh"`
}

// DungeonMobsPayload is the DUNGEON_MOBS datagram (issue #1101): the mobs,
// member ids and member targets of every live dungeon run, sent once per
// snapshot cycle before the BOT_BATCH datagrams. It is additive: an older
// dashboard drops the unknown type, and a run without one keeps working.
type DungeonMobsPayload struct {
	V       int              `json:"v"`
	Session uint64           `json:"session"`
	Seq     uint64           `json:"seq"`
	TS      int64            `json:"ts"`
	TSMs    int64            `json:"ts_ms,omitempty"` // the millisecond report time when a sender provides it
	Type    string           `json:"type"`
	Part    int              `json:"part"`  // 0-based part of this cycle; informational
	Parts   int              `json:"parts"` // parts of this cycle; informational
	Runs    []DungeonMobsRun `json:"runs"`  // empty with the newest seq: every run ended
}

// DungeonMobsRun is one instance copy of DUNGEON_MOBS: the map and instance
// id that join it to a DungeonRun, its members' ids and targets, and the mobs
// their bots have seen.
type DungeonMobsRun struct {
	Map       uint32              `json:"map"`
	Instance  uint32              `json:"instance"`
	Raid      bool                `json:"raid"`
	Members   []DungeonMobsMember `json:"members"`
	MobsMap   uint32              `json:"mobs_map"`
	MobsTotal int                 `json:"mobs_total"` // before the module's cap: "24 of 57 shown"
	Mobs      *[]CommanderMob     `json:"mobs"`       // nil = not reported, &[] = reported, none near
}

// DungeonMobsMember is one unit of a run as DUNGEON_MOBS reports it: the low
// guid joins it to the roster row, id/target are raw guids as decimal strings
// (what a mob's victim and a bot's target name).
type DungeonMobsMember struct {
	GUID   uint32 `json:"guid"`
	ID     string `json:"id"`
	Target string `json:"target,omitempty"`
}

// DungeonMember is one unit (bot or real player) inside a dungeon run: the
// fields the Dungeons tab shows (party list, map dots, state line).
type DungeonMember struct {
	Name   string `json:"name"`
	GUID   uint32 `json:"guid"`
	Player bool   `json:"player"` // a real online player, drawn apart from the bots
	Kind   string `json:"kind"`
	Class  string `json:"class"`
	Role   string `json:"role"`
	Level  uint32 `json:"level"`
	HP     uint32 `json:"hp"`
	MaxHP  uint32 `json:"max_hp"`
	// Power is the unit's resource, the same fields the Bots tab's bar
	// draws (issue #1112): current, max and type (mana, rage, energy,
	// focus, happiness — the tab colours mana blue, rage red, energy
	// yellow). A pre-power module reports none, so the pointers stay nil
	// and the JSON omits them: the tab then draws no bar, and a resource
	// that really is 0 (an empty rage bar) is still present with max > 0.
	Power     *uint32 `json:"power,omitempty"`
	MaxPower  *uint32 `json:"max_power,omitempty"`
	PowerType string  `json:"power_type,omitempty"`
	// Dead is reported from the snapshot (state "dead" or a 0-HP corpse).
	Dead bool `json:"dead"`
	// SubGroup is the raid subgroup the unit sits in (0 outside a raid).
	SubGroup uint32 `json:"subgroup,omitempty"`
	State    string `json:"state"`
	// Position for the live map. All members of a run share MapID.
	MapID uint32  `json:"map"`
	Zone  uint32  `json:"zone"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
	Z     float64 `json:"z"`
	O     float64 `json:"o"`
	// ID and Target come from DUNGEON_MOBS (issue #1101): the member's guid
	// as a decimal string (what a mob's victim names) and its target's guid.
	// Both absent from an older module.
	ID     string `json:"id,omitempty"`
	Target string `json:"target,omitempty"`
}

// DungeonRun is one live instance copy (map + instance id) of a dungeon or
// raid map with at least one bot in it (issue #895).
type DungeonRun struct {
	// Key is the stable id of the copy: "<map id>:<instance id>" (a
	// pre-v12 module reports no instance, so the map id alone: "<map>:0").
	Key      string `json:"key"`
	MapID    uint32 `json:"map"`
	Instance uint32 `json:"instance"`
	// FirstSeen is the unix second the daemon first saw this copy with a
	// bot in it (this game-server session).
	FirstSeen int64 `json:"first_seen"`
	// SeenSec is how long the run has been seen (FirstSeen until now).
	SeenSec float64 `json:"seen_sec"`
	Bots    int     `json:"bots"`
	Players int     `json:"players"`
	Dead    int     `json:"dead"`
	// Members carries bots and players; players first, then by subgroup
	// (raids) and name.
	Members []DungeonMember `json:"members"`
	// Mobs, MobsMap and MobsTotal come from DUNGEON_MOBS (issue #1101).
	// Mobs nil = not reported (an older module or no datagram yet); a
	// non-nil empty list = reported and nothing near, so the map layers
	// stay selectable. MobsMap is the map the mobs are on (always the run's
	// map: every member of a copy shares it).
	Mobs      *[]CommanderMob `json:"mobs,omitempty"`
	MobsMap   *uint32         `json:"mobs_map,omitempty"`
	MobsTotal int             `json:"mobs_total,omitempty"`
}

// DungeonsView is what GET /api/v1/dungeons carries.
type DungeonsView struct {
	Runs []DungeonRun `json:"runs"`
	// GeneratedAt is the unix second the view was built at.
	GeneratedAt int64 `json:"generated_at"`
	// StaleModule is true when the game server runs a pre-v12 module that
	// reports no instance id: copies of one dungeon then fall back to one
	// run per map and cannot be told apart.
	StaleModule bool `json:"stale_module,omitempty"`
}

// CommanderPosPayload is the COMMANDER_POS datagram: where a binding's bots
// and the enemies near its lead are, sent faster than the 2 s COMMANDERS
// report while they fight. Additive to protocol v12; fmt 1 is the only
// layout this daemon reads, and an older daemon drops the unknown type.
type CommanderPosPayload struct {
	V             int      `json:"v"`
	FMT           int      `json:"fmt"`
	Session       uint64   `json:"session"`
	Seq           uint64   `json:"seq"`
	TS            int64    `json:"ts"`
	TSMs          int64    `json:"ts_ms"`
	Type          string   `json:"type"`
	Commander     string   `json:"commander"`
	CommanderGUID uint32   `json:"commander_guid"`
	Combat        bool     `json:"combat"`
	Bots          []PosBot `json:"bots"`
	Mobs          []PosMob `json:"mobs"`
}

// PosBot is one bot's place in a COMMANDER_POS datagram; M is the map, A is
// 1 alive and 0 dead (HP 0 then).
type PosBot struct {
	G   uint32  `json:"g"`
	M   uint32  `json:"m"`
	X   float64 `json:"x"`
	Y   float64 `json:"y"`
	Z   float64 `json:"z"`
	O   float64 `json:"o"`
	HP  uint32  `json:"hp"`
	MHP uint32  `json:"mhp"`
	A   int     `json:"a"`
}

// PosMob is one enemy near the lead: its guid as a decimal string, where it
// stands, its health percent and the raw guid of the unit it targets ("" none).
// O is where a boss or elite looks (radians, 0 = +x north, counter-clockwise
// toward +y west); the module adds it with the boss overlay, so an older one
// leaves it out and a nil keeps the full report's facing.
type PosMob struct {
	ID string   `json:"id"`
	X  float64  `json:"x"`
	Y  float64  `json:"y"`
	HP uint32   `json:"hp"`
	V  string   `json:"v"`
	O  *float64 `json:"o,omitempty"`
}

// CommanderPosFrame is one accepted position datagram as the live stream
// carries it.
type CommanderPosFrame struct {
	Commander string   `json:"commander"`
	TSMs      int64    `json:"ts_ms"`
	Combat    bool     `json:"combat"`
	Bots      []PosBot `json:"bots"`
	Mobs      []PosMob `json:"mobs"`
}
