package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/gorilla/websocket"
	"github.com/prometheus/client_golang/prometheus/promhttp"

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
	issueMinAgeSec := flag.Int("issue-min-age-sec", getEnvInt("ISSUE_MIN_AGE_SEC", 300), "Only surface bot issues that persist at least this many seconds")
	devNoAuth := flag.Bool("dev-no-auth", false, "Disable Game Master authentication check for local dev testing")
	flag.Parse()

	log.Println("=====================================================")
	log.Println(" Tortoise WoW — Bot & Server Observability Platform")
	log.Println("=====================================================")

	// 1. Authoritative state store (roster snapshots, server status, anomalies)
	anomalies := ringbuf.New(1000)
	store := state.New(state.Config{
		IssueMinAge: time.Duration(*issueMinAgeSec) * time.Second,
	}, anomalies)

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

	// 4. realmd authentication
	authService, err := auth.NewService(auth.Config{
		DBHost:     *dbHost,
		DBPort:     *dbPort,
		DBUser:     *dbUser,
		DBPassword: *dbPass,
		DBName:     *dbName,
		SecretKey:  getEnv("SESSION_SECRET", "tortoise-observability-salt-secret"),
	})
	if err != nil {
		log.Fatalf("Failed to initialize auth service: %v", err)
	}
	log.Printf("[Auth] Realmd MySQL authentication ready against %s:%d/%s", *dbHost, *dbPort, *dbName)

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

	mux.HandleFunc("/api/v1/issues", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, store.Issues())
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
		hub.Serve(w, r, upgrader,
			ws.Event{Name: "snapshot", Data: store.Snapshot()},
			ws.Event{Name: "status", Data: store.Status()},
		)
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
