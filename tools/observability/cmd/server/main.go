package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gorilla/websocket"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"tortoise-observability/internal/armory"
	"tortoise-observability/internal/auth"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
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
	udpHost := flag.String("udp-host", getEnv("UDP_HOST", "127.0.0.1"), "UDP telemetry listen address")
	udpPort := flag.Int("udp-port", getEnvInt("UDP_PORT", 9195), "UDP telemetry listener port")
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
	devNoAuth := flag.Bool("dev-no-auth", false, "Disable Game Master authentication check for local dev testing")
	// The activity rollup is in-memory, so without this a dashboard restart
	// resets every counter. The file sits in the mounted icon-cache directory
	// by default (the only writable persistent path the deployment gives the
	// daemon) and is rewritten once a minute.
	activityStateFile := flag.String("activity-state-file",
		getEnv("ACTIVITY_STATE_FILE", filepath.Join(getEnv("ICON_CACHE_DIR", filepath.Join(os.TempDir(), "tortoise_icons")), "activity-state.json")),
		"Where to persist the per-bot activity counters across dashboard restarts (empty disables)")
	flag.Parse()

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

	authService, err := auth.NewService(auth.Config{
		DBHost:     *dbHost,
		DBPort:     *dbPort,
		DBUser:     *dbUser,
		DBPassword: *dbPass,
		DBName:     *dbName,
		SecretKey:  getEnv("SESSION_SECRET", "tortoise-observability-salt-secret"),
		MinGMLevel: *minGMLevel,
	})
	if err != nil {
		log.Fatalf("Failed to initialize auth service: %v", err)
	}
	log.Printf("[Auth] Realmd MySQL authentication ready against %s:%d/%s", *dbHost, *dbPort, *dbName)

	armoryService, err := armory.NewService(armory.Config{
		DBHost:           *dbHost,
		DBPort:           *dbPort,
		DBUser:           *dbUser,
		DBPassword:       *dbPass,
		CharDB:           *dbChar,
		WorldDB:          *dbWorld,
		DBCDir:           *dbcDir,
	})
	if err != nil {
		log.Fatalf("Failed to initialize armory service: %v", err)
	}
	log.Printf("[Armory] Service initialized for %s and %s", *dbChar, *dbWorld)

	// Equipped-gear sweep: one aggregate character-DB query every few minutes
	// (never per tick) feeding the roster item-level column, the armory gear
	// panel and the per-level-band averages. Runs immediately, then on the
	// timer; a failed sweep keeps the previous numbers.
	go func() {
		const gearInterval = 5 * time.Minute
		for {
			stats, err := armoryService.GearRollup()
			if err != nil {
				log.Printf("[Gear] sweep failed: %v", err)
			} else {
				store.SetGear(stats)
				log.Printf("[Gear] swept %d bots", len(stats))
			}
			time.Sleep(gearInterval)
		}
	}()

	// 5. WebSocket hub and UDP ingestion
	hub := ws.NewHub()
	udpListener := udp.NewListener(*udpHost, *udpPort, store, metricsRegistry, projectionEngine, hub)
	if err := udpListener.Start(); err != nil {
		log.Fatalf("Failed to start UDP listener: %v", err)
	}
	defer udpListener.Stop()

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
	mux.Handle("/maps/", noCache(http.FileServer(http.FS(web.FS))))
	mux.Handle("/data/", noCache(http.FileServer(http.FS(web.FS))))
	mux.Handle("/metrics", promhttp.Handler())

	mux.HandleFunc("/login", func(w http.ResponseWriter, r *http.Request) {
		loginHTML, _ := web.FS.ReadFile("login.html")
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write(loginHTML)
	})

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

		session, err := authService.Authenticate(req.Username, req.Password)
		if err != nil {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusUnauthorized)
			_ = json.NewEncoder(w).Encode(map[string]interface{}{"success": false, "error": err.Error()})
			return
		}

		token := authService.CreateSessionToken(session)
		authService.SetSessionCookie(w, token)

		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]interface{}{
			"success": true,
			"user":    session.Username,
			"rank":    session.Rank,
		})
	})

	mux.HandleFunc("/api/v1/auth/logout", func(w http.ResponseWriter, r *http.Request) {
		authService.ClearSessionCookie(w)
		http.Redirect(w, r, "/login", http.StatusFound)
	})

	requireAuth := func(next http.HandlerFunc) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if *devNoAuth {
				next(w, r)
				return
			}
			if _, err := authService.GetSessionFromRequest(r); err != nil {
				if strings.HasPrefix(r.URL.Path, "/api/") {
					w.Header().Set("Content-Type", "application/json")
					w.WriteHeader(http.StatusUnauthorized)
					_ = json.NewEncoder(w).Encode(map[string]string{"error": "Unauthorized: GM session required"})
					return
				}
				http.Redirect(w, r, "/login", http.StatusFound)
				return
			}
			next(w, r)
		}
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

	upgrader := websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool { return true },
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
		}
		if info := store.ServerInfo(); info != nil {
			initial = append(initial, ws.Event{Name: "server_info", Data: info})
		}
		hub.Serve(w, r, upgrader, initial...)
	})

 serverAddr := fmt.Sprintf("0.0.0.0:%d", *httpPort)
	log.Printf("[HTTP] Dashboard & API running at http://localhost:%d/dashboard", *httpPort)
	log.Printf("[HTTP] Prometheus metrics available at http://localhost:%d/metrics", *httpPort)

	server := &http.Server{
		Addr:         serverAddr,
		Handler:      mux,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("HTTP server failed: %v", err)
	}
}

func writeJSON(w http.ResponseWriter, payload interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(payload)
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
