package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"tortoise-observability/internal/agentmaps"
	"tortoise-observability/internal/armory"
	"tortoise-observability/internal/auth"
	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/control"
	"tortoise-observability/internal/dungeons"
	"tortoise-observability/internal/history"
	"tortoise-observability/internal/lanes"
	"tortoise-observability/internal/live"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/mapdir"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/plans"
	"tortoise-observability/internal/ringbuf"
	"tortoise-observability/internal/state"
	"tortoise-observability/internal/udp"
	"tortoise-observability/internal/ws"
	"tortoise-observability/web"
)

func getEnv(key, fallback string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return fallback
}

// flagSet reports whether the command line set a flag (an explicit empty value included).
func flagSet(name string) bool {
	set := false
	flag.Visit(func(f *flag.Flag) {
		if f.Name == name {
			set = true
		}
	})
	return set
}

// defaultHistoryDir is the history folder beside the activity state file (the writable mount); "" keeps the history off.
func defaultHistoryDir(stateFile string) string {
	if stateFile == "" {
		return ""
	}
	return filepath.Join(filepath.Dir(stateFile), "fight-history")
}

func getEnvInt(key string, fallback int) int {
	if val := os.Getenv(key); val != "" {
		if i, err := strconv.Atoi(val); err == nil {
			return i
		}
	}
	return fallback
}

func main() {
	httpPort := flag.Int("http-port", getEnvInt("HTTP_PORT", 8095), "HTTP server port")
	// 127.0.0.1 by default: the dashboard is a management surface, so it must
	// not silently listen on all interfaces. Containers and LAN setups set
	// 0.0.0.0 (HTTP_HOST) to make the port publishable.
	httpHost := flag.String("http-host", getEnv("HTTP_HOST", "127.0.0.1"), "HTTP server bind address (127.0.0.1 = local only, 0.0.0.0 = all interfaces, for containers/LAN)")
	udpHost := flag.String("udp-host", getEnv("UDP_HOST", "127.0.0.1"), "UDP telemetry listen address")
	udpPort := flag.Int("udp-port", getEnvInt("UDP_PORT", 9195), "UDP telemetry listener port")
	forwardUDP := flag.String("forward-udp", getEnv("FORWARD_UDP", ""), "Comma-separated host:port list; every telemetry datagram is also sent to each (empty = no forwarding)")
	dbHost := flag.String("db-host", getEnv("DB_HOST", "127.0.0.1"), "MariaDB / MySQL host")
	dbPort := flag.Int("db-port", getEnvInt("DB_PORT", 3306), "MariaDB / MySQL port")
	dbUser := flag.String("db-user", getEnv("DB_USER", "mangos"), "MariaDB / MySQL user")
	dbPass := flag.String("db-pass", getEnv("DB_PASSWORD", "mangos"), "MariaDB / MySQL password")
	dbName := flag.String("db-name", getEnv("DB_LOGIN", "tw_logon"), "MariaDB / MySQL realmd database name")
	dbChar := flag.String("db-char", getEnv("DB_CHAR", "tw_char"), "MariaDB / MySQL characters database name")
	dbWorld := flag.String("db-world", getEnv("DB_WORLD", "tw_world"), "MariaDB / MySQL world database name")
	dbcDir := flag.String("dbc-dir", getEnv("DBC_DIR", ""), "Optional operator DBC directory (same files mangosd reads); enables talent trees when world talent mirrors are empty")
	issueMinAgeSec := flag.Int("issue-min-age-sec", getEnvInt("ISSUE_MIN_AGE_SEC", 300), "Only surface bot issues that persist at least this many seconds")
	minGMLevel := flag.Int("min-gm-level", getEnvInt("MIN_GM_LEVEL", 2), "Minimum GM rank required for dashboard login (2 = gamemaster)")
	// An optional external commander dashboard (for example turtle-agent's
	// `turtle-agent dashboard`) that the Commanders tab embeds. Unset: the tab
	// shows what the module knows and nothing else.
	commanderURLRaw := flag.String("commander-url", getEnv("TBDASH_COMMANDER_URL", ""), "Optional URL of an external commander dashboard to embed in the Commanders tab (env TBDASH_COMMANDER_URL)")
	// Optional folder of real game-map pictures (turtle-agent `minimap-extract` output) for the Commanders tab maps. Never part of the
	// repository: the owner's own extracted assets. Unset: the maps keep their plain grid.
	mapDirRaw := flag.String("map-dir", getEnv("TBDASH_MAP_DIR", ""), "Optional folder of map pictures (<id>.png + <id>.json from `turtle-agent minimap-extract`) drawn under the Commanders tab maps (env TBDASH_MAP_DIR)")
	// The run summaries the module's run trace recorder writes (`.bot trace`, AiPlayerbot.RunTrace.Dir under LogsDir). Read-only.
	// Unset: look in the usual places (logs/traces, traces) next to where the daemon runs, and keep looking until one exists.
	traceDir := flag.String("trace-dir", getEnv("TBDASH_TRACE_DIR", ""), "Folder of run trace summaries (<run>.summary.json) for the Dungeon runs tab; the module's AiPlayerbot.RunTrace.Dir (env TBDASH_TRACE_DIR)")
	traceScanSec := flag.Int("trace-scan-sec", getEnvInt("TBDASH_TRACE_SCAN_SEC", 10), "How often the trace folder is checked for new or changed summaries, in seconds")
	// Optional folder of turtle-agent run maps (`turtle-agent map`: one SVG per run, plus the single-file HTML replay with --html).
	// Read-only and off by default: unset, the Agent maps tab is hidden and its routes 404.
	agentMapsDirRaw := flag.String("agent-maps-dir", getEnv("TBDASH_AGENT_MAPS_DIR", ""), "Optional folder of turtle-agent run maps (`turtle-agent map` .svg and .html replays) listed in the Agent maps tab (env TBDASH_AGENT_MAPS_DIR)")
	// Optional folder of patrol paths for the plan layer (turtle-agent `trash-check --dash-out`, or a `patrol-record` tb-patrols file).
	// Unset: the committed internal/plans/patrols.json only.
	patrolsDir := flag.String("patrols-dir", getEnv("TBDASH_PATROLS_DIR", ""), "Optional folder of patrol path files (turtle-agent trash-check --dash-out, or a tb-patrols patrol-record file) drawn on the plan layer (env TBDASH_PATROLS_DIR)")
	// The raid control relay (task A10): an authenticated SOAP forwarder for
	// allow-listed GM run, gear and revive lines. Off unless a SOAP URL, a
	// SOAP user and a mode-0600 password file are all configured.
	soapURL := flag.String("soap-url", getEnv("TBDASH_SOAP_URL", ""), "Game server SOAP URL for the raid control relay (env TBDASH_SOAP_URL; empty = controls off)")
	soapUser := flag.String("soap-user", getEnv("TBDASH_SOAP_USER", ""), "Game server SOAP account for the relay, rank 4 on test servers (env TBDASH_SOAP_USER; empty = controls off)")
	soapPasswordFile := flag.String("soap-password-file", getEnv("TBDASH_SOAP_PASSWORD_FILE", ""), "File holding the SOAP account password, mode 0600 or stricter (env TBDASH_SOAP_PASSWORD_FILE; empty = controls off)")
	controlMinGM := flag.Int("control-min-gm", getEnvInt("TBDASH_CONTROL_MIN_GM", 3), "Minimum dashboard session rank that may use the control relay (env TBDASH_CONTROL_MIN_GM)")
	controlAudit := flag.String("control-audit", getEnv("TBDASH_CONTROL_AUDIT", ""), "Where the control relay appends its audit lines (env TBDASH_CONTROL_AUDIT; empty = next to the activity state file)")
	devNoAuth := flag.Bool("dev-no-auth", false, "Disable Game Master authentication check for local dev testing")
	// The activity rollup is in-memory, so without this a dashboard restart
	// resets every counter. The file sits in the mounted icon-cache directory
	// by default (the only writable persistent path the deployment gives the
	// daemon) and is rewritten once a minute.
	activityStateFile := flag.String("activity-state-file",
		getEnv("ACTIVITY_STATE_FILE", filepath.Join(getEnv("ICON_CACHE_DIR", filepath.Join(os.TempDir(), "tortoise_icons")), "activity-state.json")),
		"Where to persist the per-bot activity counters across dashboard restarts (empty disables)")
	// The fight history sits next to the activity state file by default (the same writable mount); an empty path turns it off.
	// The default is filled after Parse: it depends on this run's -activity-state-file, which the flag package cannot see while
	// the defaults are built.
	fightHistoryDir := flag.String("fight-history-dir", getEnv("TBDASH_FIGHT_HISTORY_DIR", ""),
		"Where the fight history keeps its day files (<dir>/<commander>/<YYYY-MM-DD>.jsonl); default next to the activity state file, empty disables (env TBDASH_FIGHT_HISTORY_DIR)")
	// Client zone art is not shipped: operators generate it from their own
	// client with scripts/gen_zone_maps.py into maps/ next to the binary.
	defaultMapsDir := "maps"
	if exe, err := os.Executable(); err == nil {
		defaultMapsDir = filepath.Join(filepath.Dir(exe), "maps")
	}
	mapsDir := flag.String("maps-dir", getEnv("MAPS_DIR", defaultMapsDir),
		"Zone art generated by scripts/gen_zone_maps.py")
	flag.Parse()
	if !flagSet("fight-history-dir") && os.Getenv("TBDASH_FIGHT_HISTORY_DIR") == "" {
		*fightHistoryDir = defaultHistoryDir(*activityStateFile)
	}
	if art, _ := filepath.Glob(filepath.Join(*mapsDir, "*.webp")); len(art) > 0 {
		log.Printf("[Map] Serving %d generated zone maps from %s", len(art), *mapsDir)
	} else {
		log.Printf("[Map] No zone maps in %s; generate them with scripts/gen_zone_maps.py (see README)", *mapsDir)
	}

	log.Println("=====================================================")
	log.Println(" Tortoise WoW — Bot & Server Observability Platform")
	log.Println("=====================================================")

	// 1. Authoritative state store (roster snapshots, server status, anomalies)
	anomalies := ringbuf.New(1000)
	store := state.New(state.Config{
		IssueMinAge: time.Duration(*issueMinAgeSec) * time.Second,
	}, anomalies)

	// Activity counters survive a dashboard restart: restore the last
	// snapshot, then rewrite it every minute (one small JSON file, bounded by
	// the pool; never a per-tick DB read).
	if restored, err := store.RestoreActivity(*activityStateFile); err != nil {
		log.Printf("[Activity] state restore failed (%s): %v", *activityStateFile, err)
	} else if restored > 0 {
		log.Printf("[Activity] restored counters for %d bots from %s", restored, *activityStateFile)
	}
	go func() {
		for {
			time.Sleep(time.Minute)
			if err := store.SaveActivity(*activityStateFile); err != nil {
				log.Printf("[Activity] state save failed: %v", err)
			}
		}
	}()

	// 2. Prometheus metrics
	metricsRegistry := metrics.New()

	// 3. Zone map projection engine
	zoneData, err := web.FS.ReadFile("data/zones.json")
	if err != nil {
		log.Fatalf("Failed to read embedded zone coordinate data: %v", err)
	}
	projectionEngine, err := zoneproject.New(zoneData)
	if err != nil {
		log.Fatalf("Failed to initialize zone projection engine: %v", err)
	}
	log.Printf("[Map] Initialized coordinate projection engine with %d zones", len(projectionEngine.GetAllZones()))

	// The session cookie is signed with HMAC-SHA256 keyed by SESSION_SECRET.
	// There is no shipped default: unset, a random key is generated at
	// startup, which is safe but ends every session on a restart.
	sessionSecret := getEnv("SESSION_SECRET", "")
	if sessionSecret == "" {
		key := make([]byte, 32)
		if _, err := rand.Read(key); err != nil {
			log.Fatalf("Failed to generate a random session key: %v", err)
		}
		sessionSecret = hex.EncodeToString(key)
		log.Println("[Auth] SESSION_SECRET not set: generated a random session key; dashboard sessions end on restart (set SESSION_SECRET to keep them)")
	}

	authService, err := auth.NewService(auth.Config{
		DBHost:     *dbHost,
		DBPort:     *dbPort,
		DBUser:     *dbUser,
		DBPassword: *dbPass,
		DBName:     *dbName,
		SecretKey:  sessionSecret,
		MinGMLevel: *minGMLevel,
	})
	if err != nil {
		log.Fatalf("Failed to initialize auth service: %v", err)
	}
	log.Printf("[Auth] Realmd MySQL authentication ready against %s:%d/%s", *dbHost, *dbPort, *dbName)

	armoryService, err := armory.NewService(armory.Config{
		DBHost:     *dbHost,
		DBPort:     *dbPort,
		DBUser:     *dbUser,
		DBPassword: *dbPass,
		CharDB:     *dbChar,
		WorldDB:    *dbWorld,
		DBCDir:     *dbcDir,
	})
	if err != nil {
		log.Fatalf("Failed to initialize armory service: %v", err)
	}
	log.Printf("[Armory] Service initialized for %s and %s", *dbChar, *dbWorld)

	// Background DB sweeps: runs immediately then every 5 minutes.
	// 1. Equipped-gear rollup feeding roster item-level and armory.
	// 2. Lifetime completed quests and spells backfill from character DB.
	// 3. Counters of deleted characters are forgotten.
	go func() {
		const sweepInterval = 5 * time.Minute
		for {
			stats, err := armoryService.GearRollup()
			if err != nil {
				log.Printf("[Gear] sweep failed: %v", err)
			} else {
				store.SetGear(stats)
				log.Printf("[Gear] swept %d bots", len(stats))
			}

			if existing, err := armoryService.ExistingCharacters(); err != nil {
				log.Printf("[Activity] character sweep failed: %v", err)
			} else if n := store.ForgetDeletedBots(existing); n > 0 {
				log.Printf("[Activity] forgot %d deleted bots", n)
			}

			quests, qErr := armoryService.CompletedQuestsRollup()
			spells, sErr := armoryService.SpellsLearnedRollup()
			openQuests, oErr := armoryService.OpenQuestsRollup()
			if qErr == nil && sErr == nil && oErr == nil {
				store.BackfillActivityFromDB(quests, spells, openQuests)
				log.Printf("[Activity] backfilled lifetime quests (%d bots), spells (%d bots), open quests (%d bots) from DB", len(quests), len(spells), len(openQuests))
			} else {
				if qErr != nil {
					log.Printf("[Activity] quest backfill failed: %v", qErr)
				}
				if sErr != nil {
					log.Printf("[Activity] spell backfill failed: %v", sErr)
				}
				if oErr != nil {
					log.Printf("[Activity] open quests backfill failed: %v", oErr)
				}
			}

			time.Sleep(sweepInterval)
		}
	}()

	commanderURL, err := state.NormalizeCommanderURL(*commanderURLRaw)
	if err != nil {
		log.Printf("[Commanders] ignoring commander URL: %v", err)
		commanderURL = ""
	} else if commanderURL != "" {
		log.Printf("[Commanders] embedding the commander dashboard at %s", commanderURL)
	}

	mapPics, err := mapdir.New(*mapDirRaw)
	if err != nil {
		log.Printf("[Commanders] ignoring the map folder %q: %v", *mapDirRaw, err)
		mapPics = nil
	} else if mapPics != nil {
		log.Printf("[Commanders] real map pictures served from %s", mapPics.Root())
	}

	// Dungeon runs tab: one pass over the trace folder now, then a cheap directory check every few seconds. Requests read the in-memory index.
	dungeonIndex := dungeons.NewIndex(*traceDir)
	dungeonIndex.Scan()
	if m := dungeonIndex.Meta(); m.DirFound {
		log.Printf("[Dungeons] %d dungeon runs from %s", m.Runs, m.Dir)
	} else {
		log.Printf("[Dungeons] no trace folder yet (%s); runs appear once `.bot trace` has written one (set -trace-dir / TBDASH_TRACE_DIR)", m.Dir)
	}
	if *traceScanSec < 2 {
		*traceScanSec = 2
	}
	go dungeonIndex.Loop(time.Duration(*traceScanSec)*time.Second, make(chan struct{}))

	agentMaps, err := agentmaps.New(*agentMapsDirRaw)
	if err != nil {
		log.Printf("[AgentMaps] ignoring the run-map folder %q: %v", *agentMapsDirRaw, err)
		agentMaps = nil
	} else if agentMaps != nil {
		log.Printf("[AgentMaps] run maps served from %s (at most %d files, %d MiB each)", agentMaps.Root(), agentmaps.MaxFiles, agentmaps.MaxFileSize>>20)
	}

	// Dungeon and raid plans (the run leader's ai_dungeon_* rows, read-only, cached) for the map's plan layer: opened here so
	// the fight history's pack lookup can read them before the telemetry listener starts.
	plans.SetPatrolsDir(*patrolsDir)
	var planSvc *plans.Service
	if worldDB, err := plans.OpenWorld(*dbHost, *dbPort, *dbUser, *dbPass, *dbWorld); err != nil {
		log.Printf("[Plans] world database: %v (the plan layer is off)", err)
	} else {
		planSvc = plans.NewService(plans.NewSQLSource(worldDB))
	}

	// 5. WebSocket hub and UDP ingestion
	// Combat fights: the module's per-fight numbers (COMBAT_FIGHT datagrams), judged here by rules.json.
	combatRules, err := combat.LoadRules(combat.DefaultRules())
	if err != nil {
		log.Fatalf("Bad combat rules: %v", err)
	}
	combatStore := combat.NewStore(combatRules)
	// A live row is a fight only while the module's snapshot shows a hostile unit engaged with the group: buffs,
	// consumables and heals on a parked raid keep the module's book open but are not a fight.
	combatStore.SetEngaged(func(commander string) bool {
		for _, b := range store.Commanders().Commanders {
			if b.Commander == commander {
				return combat.Engaged(b)
			}
		}
		return false
	})
	hub := ws.NewHub()
	// The live stream's own fan-out: per-commander positions and fight events
	// over Server-Sent Events (GET /api/v1/live). The WebSocket above keeps
	// carrying the roster, the Commanders snapshots and the finished fights.
	liveHub := live.NewHub()

	// The fight history (C1): one JSONL file per commander per UTC day, indexed for the debugger's night picker and same-pack
	// table. The writer waits six seconds for a fight's last frames and runs off the UDP goroutine, which only enqueues.
	var historyStore *history.Store
	if *fightHistoryDir != "" {
		packOf := func(ctx context.Context, plan string, f combat.Fight) int {
			if planSvc == nil || plan == "" {
				return 0
			}
			p, err := planSvc.Get(ctx, plan)
			if err != nil || p == nil || p.Map != f.Map {
				return 0
			}
			engaged := make([]uint32, 0, len(f.Engaged))
			for _, e := range f.Engaged {
				engaged = append(engaged, e.E)
			}
			return plans.FightPack(p, f.Step, f.Boss, engaged)
		}
		lanesOf := func(commander string, f combat.Fight) (int64, int64, bool) {
			frames, _ := store.Track(commander, f.Start-state.TrackEdgeSec, f.Start+int64(f.DurMS)/1000+state.TrackEdgeSec)
			return lanes.NonTank(frames, f)
		}
		historyStore, err = history.Open(history.Config{Dir: *fightHistoryDir}, packOf, lanesOf)
		if err != nil {
			log.Printf("[History] fight history off: %v", err)
			historyStore = nil
		} else {
			historyStore.SetMetrics(metricsRegistry)
			log.Printf("[History] fights kept in %s (at most %d days, %d MiB)", *fightHistoryDir, history.HistoryDays, history.HistoryMaxBytes>>20)
		}
	}

	udpListener := udp.NewListener(*udpHost, *udpPort, store, metricsRegistry, projectionEngine, hub)
	udpListener.SetCombatStore(combatStore)
	udpListener.SetLiveSink(liveHub)
	if historyStore != nil {
		udpListener.SetHistory(historyStore)
	}
	var forwards []string
	for _, f := range strings.Split(*forwardUDP, ",") {
		if f = strings.TrimSpace(f); f != "" {
			forwards = append(forwards, f)
		}
	}
	if err := udpListener.SetForwards(forwards); err != nil {
		log.Fatalf("Refusing -forward-udp: %v", err)
	}
	if len(forwards) > 0 {
		log.Printf("[UDP] Forwarding every telemetry datagram to %s", strings.Join(forwards, ", "))
	}
	if err := udpListener.Start(); err != nil {
		log.Fatalf("Failed to start UDP listener: %v", err)
	}

	// The live stream's gauge and dropped counter for Prometheus; a once-a-
	// second sample is enough for a gauge and keeps the read loop free.
	go func() {
		ticker := time.NewTicker(time.Second)
		defer ticker.Stop()
		var dropped uint64
		for range ticker.C {
			metricsRegistry.SetLiveClients(liveHub.Clients())
			if d := liveHub.Dropped(); d != dropped {
				metricsRegistry.AddLiveDropped(d - dropped)
				dropped = d
			}
		}
	}()

	// 6. HTTP router
	mux := http.NewServeMux()

	// Embedded assets change with each deploy; disallow cache reuse so a
	// dashboard refresh always picks up the current UI.
	noCache := func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Cache-Control", "no-cache, must-revalidate")
			next.ServeHTTP(w, r)
		})
	}

	// On-demand icon cache: serves local cached icons, or downloads once from CDN into disk cache
	iconCacheDir := getEnv("ICON_CACHE_DIR", filepath.Join(os.TempDir(), "tortoise_icons"))
	_ = os.MkdirAll(iconCacheDir, 0755)

	mux.HandleFunc("/static/icons/", func(w http.ResponseWriter, r *http.Request) {
		name := strings.TrimPrefix(r.URL.Path, "/static/icons/")
		name = filepath.Base(name)
		if name == "" || name == "." || name == "/" {
			http.NotFound(w, r)
			return
		}
		rawName := strings.TrimSuffix(name, filepath.Ext(name))
		targetFile := filepath.Join(iconCacheDir, rawName+".jpg")
		if _, err := os.Stat(targetFile); os.IsNotExist(err) {
			client := &http.Client{Timeout: 8 * time.Second}
			cdnURL := fmt.Sprintf("https://wow.zamimg.com/images/wow/icons/medium/%s.jpg", strings.ToLower(rawName))
			resp, err := client.Get(cdnURL)
			if err == nil && resp.StatusCode == http.StatusOK {
				defer resp.Body.Close()
				data, err := io.ReadAll(resp.Body)
				if err == nil && len(data) > 0 {
					_ = os.WriteFile(targetFile, data, 0644)
				}
			}
		}
		if _, err := os.Stat(targetFile); err == nil {
			w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
			http.ServeFile(w, r, targetFile)
			return
		}
		noCache(http.FileServer(http.FS(web.FS))).ServeHTTP(w, r)
	})

	mux.Handle("/static/", noCache(http.FileServer(http.FS(web.FS))))
	mux.Handle("/maps/", noCache(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, filepath.Join(*mapsDir, filepath.Base(r.URL.Path)))
	})))
	mux.Handle("/data/", noCache(http.FileServer(http.FS(web.FS))))
	mux.Handle("/metrics", promhttp.Handler())

	mux.HandleFunc("/login", func(w http.ResponseWriter, r *http.Request) {
		loginHTML, _ := web.FS.ReadFile("login.html")
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write(loginHTML)
	})

	// Per-IP login backoff (in-memory): 5 failed logins in a row block that
	// address for 5 minutes. Every failure — wrong password, too low a rank
	// or a blocked address — answers with the same error text.
	loginBackoff := auth.NewLoginBackoff()

	mux.HandleFunc("/api/v1/auth/login", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}

		var req struct {
			Username string `json:"username"`
			Password string `json:"password"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "Invalid JSON body", http.StatusBadRequest)
			return
		}

		ip, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil {
			ip = r.RemoteAddr
		}
		const loginFailure = "invalid username or password"
		if !loginBackoff.Allow(ip) {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusTooManyRequests)
			_ = json.NewEncoder(w).Encode(map[string]interface{}{"success": false, "error": loginFailure})
			return
		}

		session, err := authService.Authenticate(req.Username, req.Password)
		if err != nil {
			loginBackoff.Fail(ip)
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusUnauthorized)
			_ = json.NewEncoder(w).Encode(map[string]interface{}{"success": false, "error": loginFailure})
			return
		}
		loginBackoff.Reset(ip)

		token := authService.CreateSessionToken(session)
		authService.SetSessionCookie(w, token)

		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]interface{}{
			"success": true,
			"user":    session.Username,
			"rank":    session.Rank,
		})
	})

	mux.HandleFunc("/api/v1/auth/logout", logoutHandler(authService))

	requireSession := func(next func(http.ResponseWriter, *http.Request, *auth.SessionData)) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if *devNoAuth {
				next(w, r, nil)
				return
			}
			session, err := authService.GetSessionFromRequest(r)
			if err != nil {
				if strings.HasPrefix(r.URL.Path, "/api/") {
					w.Header().Set("Content-Type", "application/json")
					w.WriteHeader(http.StatusUnauthorized)
					_ = json.NewEncoder(w).Encode(map[string]string{"error": "Unauthorized: GM session required"})
					return
				}
				http.Redirect(w, r, "/login", http.StatusFound)
				return
			}
			next(w, r, session)
		}
	}
	requireAuth := func(next http.HandlerFunc) http.HandlerFunc {
		return requireSession(func(w http.ResponseWriter, r *http.Request, _ *auth.SessionData) {
			next(w, r)
		})
	}

	indexHTML, _ := web.FS.ReadFile("index.html")
	dashboardHandler := requireAuth(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write(indexHTML)
	})

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" || r.URL.Path == "/dashboard" {
			dashboardHandler(w, r)
			return
		}
		http.NotFound(w, r)
	})

	mux.HandleFunc("/api/v1/status", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Status())
	}))

	mux.HandleFunc("/api/v1/bots", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Snapshot().Bots)
	}))

	mux.HandleFunc("/api/v1/grinding", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Snapshot().Grinding)
	}))

	// Chart history: the rolling in-memory samples (one per heartbeat)
	// behind the dashboard's 10-minute timelines. The page seeds its
	// charts from this on load instead of starting empty after a
	// refresh; live heartbeats then keep appending as before.
	mux.HandleFunc("GET /api/v1/history", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.History())
	}))

	// Issue episodes (active + recently resolved). The dashboard fetches this
	// on init and on Refresh to recover the issue tab/badge when a WebSocket
	// event was missed.
	mux.HandleFunc("/api/v1/issues", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Issues())
	}))

	// Activity: per-bot counters (quests, loot, money, kills, deaths, vendor
	// trips, ...) rolled up from the BOT_EVENTS datagrams, plus the pool-wide
	// loot/quest feeds. Sampled and bounded; no per-tick DB reads.
	mux.HandleFunc("/api/v1/activity", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Activity())
	}))

	mux.HandleFunc("/api/v1/activity/loot", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		f := state.LootFilter{
			MinQuality: qUint(q.Get("min_quality")),
			MinLevel:   qUint(q.Get("min_level")),
			MaxLevel:   qUint(q.Get("max_level")),
			Class:      q.Get("class"),
			Bot:        q.Get("bot"),
			Limit:      int(qUint(q.Get("limit"))),
		}
		if f.Limit <= 0 {
			f.Limit = 200
		}
		if f.Limit > 2000 {
			f.Limit = 2000
		}
		writeJSON(w, store.LootFeed(f))
	}))

	mux.HandleFunc("/api/v1/activity/quests", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		f := state.QuestFilter{Bot: q.Get("bot"), Limit: int(qUint(q.Get("limit")))}
		if f.Limit <= 0 {
			f.Limit = 200
		}
		if f.Limit > 2000 {
			f.Limit = 2000
		}
		writeJSON(w, store.QuestFeed(f))
	}))

	mux.HandleFunc("/api/v1/activity/pvp", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		f := state.PvpFilter{Bot: q.Get("bot"), Limit: int(qUint(q.Get("limit")))}
		if f.Limit <= 0 {
			f.Limit = 200
		}
		if f.Limit > 2000 {
			f.Limit = 2000
		}
		writeJSON(w, store.PvpFeed(f))
	}))

	// Commanders: the standing remote-commander bindings the module reports
	// (COMMANDERS datagrams), plus the embedded external dashboard's URL when
	// the operator set one. Memory only; nothing here reads a database.
	mux.HandleFunc("/api/v1/commanders", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, struct {
			model.CommandersView
			ExternalURL string `json:"external_url,omitempty"`
			RealMaps    bool   `json:"real_maps"`
		}{store.Commanders(), commanderURL, mapPics != nil})
	}))

	// The live dungeon runs (issue #895): one entry per instance copy (map +
	// instance id) of a dungeon or raid that has bots in it, members (bots and
	// real players) included. Read-only: the tab draws, it never orders.
	mux.HandleFunc("/api/v1/dungeons", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Dungeons())
	}))

	mux.HandleFunc("GET /api/v1/plans/{plan}", requireAuth(plans.Handler(planSvc)))

	// Combat fights ("a light Turtle Logs"): the module's per-fight numbers
	// (COMBAT_FIGHT datagrams), judged here by rules.json. The list is per
	// commander, newest first; each fight carries the v1 warnings with it.
	mux.HandleFunc("/api/v1/combat/fights", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		rows := combatStore.List(r.URL.Query().Get("commander"))
		if historyStore != nil {
			for i := range rows {
				if row, ok := historyStore.Lookup(rows[i].Key); ok {
					rows[i].PackKey, rows[i].NonTankMs = row.PackKey, row.NonTankMs
				}
			}
		}
		writeJSON(w, rows)
	}))
	mux.HandleFunc("/api/v1/combat/fight/", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		// The detail key is (commander, session, id): fight ids repeat
		// across commanders and sessions. The list rows carry the composed
		// key; callers may also pass the three parts as query params.
		q := r.URL.Query()
		key := strings.TrimPrefix(r.URL.Path, "/api/v1/combat/fight/")
		f, warnings, ok := combatStore.GetKey(key)
		if !ok {
			if commander, session, id, qok := combatFightKey(q); qok {
				f, warnings, ok = combatStore.Get(commander, session, id)
			}
		}
		if !ok {
			// Not in memory: a past night's fight, re-read from its history file and judged now with the same rules.
			if historyStore != nil {
				if hf, row, hok := historyStore.Fight(key); hok {
					writeJSON(w, struct {
						Fight    combat.Fight     `json:"fight"`
						Warnings []combat.Warning `json:"warnings"`
						Hist     bool             `json:"hist,omitempty"`
						PackKey  string           `json:"pack_key,omitempty"`
					}{hf, combat.Judge(hf, combatStore.Rules()), true, row.PackKey})
					return
				}
			}
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusNotFound)
			_, _ = w.Write([]byte(`{"error":"no such fight"}`))
			return
		}
		writeJSON(w, struct {
			Fight    combat.Fight     `json:"fight"`
			Warnings []combat.Warning `json:"warnings"`
		}{f, warnings})
	}))

	// The fight history across nights: the night picker's days, one day's fights, and the same pack across runs. Fights the
	// daemon still remembers and ones only on disk answer alike; the history disabled answers the empty shapes.
	mux.HandleFunc("GET /api/v1/combat/history/days", requireAuth(history.DaysHandler(historyStore)))
	mux.HandleFunc("GET /api/v1/combat/history/commanders", requireAuth(history.CommandersHandler(historyStore)))
	mux.HandleFunc("GET /api/v1/combat/history", requireAuth(history.ListHandler(historyStore)))
	mux.HandleFunc("GET /api/v1/combat/packs", requireAuth(history.PackHandler(historyStore)))

	// The bound bots' recent paths (a short bounded history kept in memory), so a page opened mid-run starts with the trails drawn:
	// {"<commander>": {"<bot guid>": {"map": 209, "pts": [[x, y, unix seconds], ...]}}}, oldest point first. A point where something
	// happened carries a fourth element: "d" the bot died here, "t" it teleported (a jump no walking explains), "r" the same while dead
	// (spirit release or resurrection), "m" it arrived on this map. A point marked t, r or m starts a new segment: draw no line from the
	// point before it.
	mux.HandleFunc("/api/v1/commanders/trails", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		type trailJSON struct {
			Map uint32  `json:"map"`
			Pts [][]any `json:"pts"`
		}
		out := map[string]map[string]trailJSON{}
		for c, bots := range store.Trails() {
			m := make(map[string]trailJSON, len(bots))
			for g, t := range bots {
				pts := make([][]any, len(t.Points))
				for i, p := range t.Points {
					pts[i] = []any{math.Round(float64(p.X)*10) / 10, math.Round(float64(p.Y)*10) / 10, p.T}
					if p.Kind != 0 {
						pts[i] = append(pts[i], string(rune(p.Kind)))
					}
				}
				m[strconv.FormatUint(uint64(g), 10)] = trailJSON{Map: t.Map, Pts: pts}
			}
			out[c] = m
		}
		writeJSON(w, out)
	}))

	// The bound bots' and nearby enemies' places over time (one frame per COMMANDERS report, the last 30 min), for the fight
	// debugger's replay and aggro lanes: ?commander=&from=&to= (unix seconds, at most 1800 s apart).
	mux.HandleFunc("GET /api/v1/commanders/track", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		c, from, to, err := state.ParseTrackWindow(r.URL.Query())
		if err != nil {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			_, _ = w.Write([]byte(`{"error":"bad window"}`))
			return
		}
		frames, names, tracked := store.TrackWindow(c, from, to)
		writeJSON(w, state.TrackJSON(c, frames, names, tracked))
	}))

	// The live stream (S1): Server-Sent Events for the commanders the open tab draws, so positions and fight events reach the
	// page as they happen instead of on the next report. The first frame is the state at connect (snapshot), then one frame per
	// accepted datagram. Same login and same-origin rule as the rest of the API; see tools/observability/README.md.
	initialFrames := func(commanders []string) []live.Frame {
		asked := make(map[string]bool, len(commanders))
		for _, c := range commanders {
			asked[c] = true
		}
		view := store.Commanders()
		binds := make([]model.CommanderBinding, 0, len(commanders))
		for _, b := range view.Commanders {
			if asked[b.Commander] {
				binds = append(binds, b)
			}
		}
		data, err := json.Marshal(struct {
			Commanders []model.CommanderBinding `json:"commanders"`
			Live       []combat.FightSummary    `json:"live"`
			UpdatedAt  int64                    `json:"updated_at"`
		}{binds, combatStore.LiveRows(commanders), view.UpdatedAt})
		if err != nil {
			return nil
		}
		return []live.Frame{{Event: "snapshot", Data: data}}
	}
	mux.HandleFunc("GET /api/v1/live", requireSession(func(w http.ResponseWriter, r *http.Request, session *auth.SessionData) {
		// The stream outlives this request's login check, so it carries the
		// signed expiry and a rank revalidation of its own.
		sess := live.Session{}
		if session != nil {
			sess = live.Session{
				ExpiresAt: time.Unix(session.ExpiresAt, 0),
				Check:     func() error { return authService.RecheckAccount(session.AccountID) },
			}
		}
		live.Handler(liveHub,
			live.Options{Heartbeat: 15 * time.Second, WriteTimeout: 10 * time.Second, Retry: 3 * time.Second, Recheck: 60 * time.Second},
			sess, initialFrames)(w, r)
	}))

	// Real map pictures for the Commanders maps: the layer list of a map id, and the .png/.json files of the TBDASH_MAP_DIR folder
	// (nothing else). Behind the same login as the rest of the API.
	if mapPics != nil {
		mux.HandleFunc(mapdir.MapsPrefix, requireAuth(mapPics.HandleLayers))
		mux.HandleFunc(mapdir.FilePrefix, requireAuth(mapPics.HandleFile))
	}

	// Dungeon runs (GET /api/v1/dungeon-runs; the live Dungeons tab is /api/v1/dungeons above): success rate, clear times, boss kill times and wipe spots from the run trace summaries. Files only; GET only.
	dungeonIndex.Register(mux, requireAuth)
	// Turtle-agent run maps (issue #1102): the list and the read-only files of --agent-maps-dir, behind the same login. The folder
	// is off by default: the list then answers 200 with enabled=false (the page hides the tab) and every file 404s.
	mux.HandleFunc(agentmaps.ListPrefix, requireAuth(agentMaps.HandleList))
	mux.HandleFunc(agentmaps.FilePrefix, requireAuth(agentMaps.HandleFile))

	mux.HandleFunc("/api/v1/server-info", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		if info := store.ServerInfo(); info != nil {
			writeJSON(w, info)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusServiceUnavailable)
		_, _ = w.Write([]byte(`{"error":"no server info received yet"}`))
	}))
	mux.HandleFunc("/api/v1/anomalies", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodDelete:
			store.Anomalies().Clear()
			w.WriteHeader(http.StatusNoContent)
		case http.MethodGet:
			limit := 1000
			if lStr := r.URL.Query().Get("limit"); lStr != "" {
				if parsed, err := strconv.Atoi(lStr); err == nil && parsed > 0 {
					limit = parsed
				}
			}
			list := store.Anomalies().GetRecent(limit,
				r.URL.Query().Get("type"), r.URL.Query().Get("severity"))
			if list == nil {
				list = []model.AnomalyPayload{}
			}
			writeJSON(w, list)
		default:
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		}
	}))

	// Cumulative per-session anomaly counts. The incident feed above is a
	// rolling 1000-row window (~24 min at 500-bot BOT_DEATH rates), so a
	// whole-run count of ACTION_LOOP etc. lives here instead. Totals reset
	// with the game-server session (or a roster wipe) and are not cleared
	// by DELETE /api/v1/anomalies, which only wipes the rolling window.
	mux.HandleFunc("GET /api/v1/anomalies/totals", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.AnomalyTotals())
	}))
	mux.HandleFunc("GET /api/v1/armory/bots", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query().Get("q")
		bots, err := armoryService.ListBots(q)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, bots)
	}))

	mux.HandleFunc("GET /api/v1/armory/bot/{guid}", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		guidStr := r.PathValue("guid")
		guid, err := strconv.ParseUint(guidStr, 10, 32)
		if err != nil {
			http.Error(w, "Invalid guid", http.StatusBadRequest)
			return
		}

		profile, err := armoryService.GetBotProfile(uint32(guid))
		if err != nil {
			if strings.Contains(err.Error(), "not found") {
				log.Printf("[Armory] profile guid=%d not found (deleted or non-bot account)", guid)
				http.Error(w, err.Error(), http.StatusNotFound)
			} else {
				log.Printf("[Armory] profile guid=%d failed: %v", guid, err)
				http.Error(w, err.Error(), http.StatusInternalServerError)
			}
			return
		}
		writeJSON(w, profile)
	}))

	// Read-only auction house view: one page per request, filtered and sorted
	// by the database; never touches the game server.
	mux.HandleFunc("GET /api/v1/auctions", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		quality := -1
		if v, err := strconv.Atoi(q.Get("quality")); err == nil {
			quality = v
		}
		page, err := armoryService.ListAuctions(armory.AuctionFilter{
			Query:    q.Get("q"),
			Quality:  quality,
			MinLevel: qUint(q.Get("min_level")),
			MaxLevel: qUint(q.Get("max_level")),
			Seller:   q.Get("seller"),
			Kind:     q.Get("kind"),
			House:    q.Get("house"),
			Sort:     q.Get("sort"),
			Desc:     q.Get("dir") != "asc",
			Page:     qUint(q.Get("page")),
		})
		if err != nil {
			log.Printf("[Auctions] list failed: %v", err)
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, page)
	}))
	// The raid control relay: allow-listed GM lines forwarded to the game
	// server over SOAP. Only when a SOAP URL, user and a mode-0600 password
	// file are all configured; otherwise the route answers 404 and the log
	// names the missing setting, never the credential.
	auditPath := *controlAudit
	if auditPath == "" && *activityStateFile != "" {
		auditPath = filepath.Join(filepath.Dir(*activityStateFile), "control-audit.log")
	}
	if auditPath == "" {
		auditPath = "control-audit.log"
	}
	controlCfg, _ := control.Configure(*soapURL, *soapUser, *soapPasswordFile, uint32(*controlMinGM), auditPath, func(r *http.Request) (string, uint32, error) {
		sd, err := authService.GetSessionFromRequest(r)
		if err != nil {
			return "", 0, err
		}
		return sd.Username, sd.Rank, nil
	})
	if controlCfg.Enabled {
		mux.Handle("/api/v1/control", requireAuth(controlCfg.Relay.ServeHTTP))
		mux.HandleFunc("GET /api/v1/control", requireAuth(func(w http.ResponseWriter, r *http.Request) {
			writeJSON(w, map[string]bool{"enabled": true})
		}))
		log.Printf("[Control] relay on (min GM rank %d, audit %s)", *controlMinGM, auditPath)
	} else {
		log.Printf("[Control] %s", controlCfg.Reason)
		mux.HandleFunc("/api/v1/control", requireAuth(func(w http.ResponseWriter, r *http.Request) {
			http.NotFound(w, r)
		}))
		mux.HandleFunc("GET /api/v1/control", requireAuth(func(w http.ResponseWriter, r *http.Request) {
			writeJSON(w, map[string]bool{"enabled": false})
		}))
	}

	upgrader := websocket.Upgrader{
		// Only the dashboard's own pages may open an authenticated stream
		// (ws.SameOrigin); non-browser clients send no Origin and pass.
		CheckOrigin: ws.SameOrigin,
	}

	mux.HandleFunc("/api/v1/stream", func(w http.ResponseWriter, r *http.Request) {
		if !*devNoAuth {
			if _, err := authService.GetSessionFromRequest(r); err != nil {
				http.Error(w, "Unauthorized", http.StatusUnauthorized)
				return
			}
		}

		// Bootstrap the client from the same store the live stream uses, so
		// REST polling is no longer required for correctness.
		initial := []ws.Event{
			{Name: "snapshot", Data: store.Snapshot()},
			{Name: "status", Data: store.Status()},
			{Name: "commanders", Data: store.Commanders()},
		}
		if info := store.ServerInfo(); info != nil {
			initial = append(initial, ws.Event{Name: "server_info", Data: info})
		}
		hub.Serve(w, r, upgrader, initial...)
	})

	serverAddr := net.JoinHostPort(*httpHost, strconv.Itoa(*httpPort))
	log.Printf("[HTTP] Dashboard & API running at http://localhost:%d/dashboard", *httpPort)
	log.Printf("[HTTP] Prometheus metrics available at http://localhost:%d/metrics", *httpPort)

	server := &http.Server{
		Addr:         serverAddr,
		Handler:      mux,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// A container restart sends SIGTERM (Ctrl+C, SIGINT): without a handler the process would die and lose the fights still
	// waiting in the history writer.
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if err := serve(ctx, server, udpListener, historyStore); err != nil {
		log.Fatalf("HTTP server failed: %v", err)
	}
}

// shutdownTimeout bounds the HTTP drain (an open SSE stream may never go idle; the rest of the shutdown still runs), and
// historyShutdownTimeout bounds the history writer, whose pack lookup reads the world database: a hung read must delay the
// process by seconds, not forever.
const (
	shutdownTimeout        = 5 * time.Second
	historyShutdownTimeout = 10 * time.Second
)

// shutdown stops the daemon in the order that keeps a finished fight: the HTTP server first (no new requests), then UDP
// ingestion (and waits for its read loop, so nothing enqueues afterwards), then the history writer, whose Close writes what is
// still queued or reports what a slow lookup kept from being written. Every step tolerates a second call.
func shutdown(server *http.Server, udpListener *udp.Listener, fights *history.Store) {
	ctx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
	defer cancel()
	if err := server.Shutdown(ctx); err != nil {
		log.Printf("[HTTP] Shutdown: %v", err)
		_ = server.Close()
	}
	udpListener.Stop()
	deadline, stopWrites := context.WithTimeout(context.Background(), historyShutdownTimeout)
	defer stopWrites()
	fights.Close(deadline)
}

// serve runs the HTTP server until it fails or ctx is cancelled, then shuts everything down and returns the server's error.
func serve(ctx context.Context, server *http.Server, udpListener *udp.Listener, fights *history.Store) error {
	serveErr := make(chan error, 1)
	go func() { serveErr <- server.ListenAndServe() }()
	var err error
	select {
	case err = <-serveErr:
		if errors.Is(err, http.ErrServerClosed) {
			err = nil
		}
	case <-ctx.Done():
		log.Printf("[HTTP] Stopping on %v", ctx.Err())
	}
	shutdown(server, udpListener, fights)
	return err
}

// logoutHandler ends the session. Logout is POST-only: a GET or HEAD must not
// clear the cookie, or a sandboxed page could log a GM out with a plain request.
// POST alone is not enough: a cross-site HTML form POST is a top-level
// navigation, so the browser would apply the clearing Set-Cookie. The request
// must also be same-origin (Origin matches the Host, as for the WebSocket
// stream); a missing, foreign or opaque (Origin: null) origin gets 403 and the
// cookie is left alone. The dashboard's button posts with fetch and navigates to
// /login itself.
func logoutHandler(authService *auth.Service) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			w.Header().Set("Allow", http.MethodPost)
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		// ws.SameOrigin lets a request with no Origin pass (the WebSocket stream
		// relies on that for non-browser clients). Logout is stricter: the
		// dashboard's button is a same-origin fetch POST, which every current
		// browser sends with Origin, so a POST without one is refused too.
		if r.Header.Get("Origin") == "" || !ws.SameOrigin(r) {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		authService.ClearSessionCookie(w)
		w.WriteHeader(http.StatusNoContent)
	}
}

func writeJSON(w http.ResponseWriter, payload interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(payload)
}

// combatFightKey reads the triple (commander, session, id) from the detail
// query params. Fight ids repeat across commanders and sessions, so the
// detail route requires all three.
func combatFightKey(q map[string][]string) (string, uint64, string, bool) {
	commanders, ok := q["commander"]
	if !ok || len(commanders) == 0 || commanders[0] == "" {
		return "", 0, "", false
	}
	sessions, ok := q["session"]
	if !ok || len(sessions) == 0 || sessions[0] == "" {
		return "", 0, "", false
	}
	session, err := strconv.ParseUint(sessions[0], 10, 64)
	if err != nil {
		return "", 0, "", false
	}
	ids, ok := q["id"]
	if !ok || len(ids) == 0 || ids[0] == "" {
		return "", 0, "", false
	}
	return commanders[0], session, ids[0], true
}

// qUint parses an optional unsigned query parameter; absent or malformed
// values are 0 (the filters treat 0 as "no constraint").
func qUint(s string) uint32 {
	if s == "" {
		return 0
	}
	v, err := strconv.ParseUint(s, 10, 32)
	if err != nil {
		return 0
	}
	return uint32(v)
}
