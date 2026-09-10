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
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"tortoise-observability/internal/auth"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/ringbuf"
	"tortoise-observability/internal/udp"
	"tortoise-observability/web"
)

// WebSocket client hub
type ClientHub struct {
	mu      sync.RWMutex
	clients map[*websocket.Conn]bool
}

func (h *ClientHub) Broadcast(msgType string, payload interface{}) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	if len(h.clients) == 0 {
		return
	}

	msg := map[string]interface{}{
		"event": msgType,
		"data":  payload,
	}
	bytes, err := json.Marshal(msg)
	if err != nil {
		return
	}

	for conn := range h.clients {
		_ = conn.WriteMessage(websocket.TextMessage, bytes)
	}
}

func (h *ClientHub) Add(conn *websocket.Conn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.clients[conn] = true
}

func (h *ClientHub) Remove(conn *websocket.Conn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.clients, conn)
	_ = conn.Close()
}

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
	udpPort := flag.Int("udp-port", getEnvInt("UDP_PORT", 9195), "UDP telemetry listener port")
	dbHost := flag.String("db-host", getEnv("DB_HOST", "127.0.0.1"), "MariaDB / MySQL host")
	dbPort := flag.Int("db-port", getEnvInt("DB_PORT", 3306), "MariaDB / MySQL port")
	dbUser := flag.String("db-user", getEnv("DB_USER", "mangos"), "MariaDB / MySQL user")
	dbPass := flag.String("db-pass", getEnv("DB_PASSWORD", "mangos"), "MariaDB / MySQL password")
	dbName := flag.String("db-name", getEnv("DB_LOGIN", "tw_logon"), "MariaDB / MySQL realmd database name")
	devNoAuth := flag.Bool("dev-no-auth", false, "Disable Game Master authentication check for local dev testing")
	flag.Parse()

	log.Println("=====================================================")
	log.Println(" Tortoise WoW — Bot & Server Observability Platform")
	log.Println("=====================================================")

	// 1. Initialize Ring Buffer
	ringBuffer := ringbuf.New(1000)

	// 2. Initialize Prometheus Metrics
	metricsRegistry := metrics.New()

	// 3. Initialize Zone Map Projection Engine
	zoneData, err := web.FS.ReadFile("data/zones.json")
	if err != nil {
		log.Fatalf("Failed to read embedded zone coordinate data: %v", err)
	}
	projectionEngine, err := zoneproject.New(zoneData)
	if err != nil {
		log.Fatalf("Failed to initialize zone projection engine: %v", err)
	}
	log.Printf("[Map] Initialized coordinate projection engine with %d zones", len(projectionEngine.GetAllZones()))

	// 4. Initialize Auth Service
	authService, err := auth.NewService(auth.Config{
		DBHost:     *dbHost,
		DBPort:     *dbPort,
		DBUser:     *dbUser,
		DBPassword: *dbPass,
		DBName:     *dbName,
		SecretKey:  getEnv("SESSION_SECRET", "tortoise-observability-salt-secret"),
	})
	if err != nil {
		log.Printf("[Auth] Warning: failed to connect to database (%v); auth will reject logins unless DB becomes reachable", err)
	} else {
		log.Printf("[Auth] Realmd MySQL authentication ready against %s:%d/%s", *dbHost, *dbPort, *dbName)
	}

	// 5. WebSocket Hub & UDP Telemetry Listener
	hub := &ClientHub{clients: make(map[*websocket.Conn]bool)}
	udpListener := udp.NewListener(*udpPort, ringBuffer, metricsRegistry, projectionEngine, hub)
	if err := udpListener.Start(); err != nil {
		log.Fatalf("Failed to start UDP listener: %v", err)
	}
	defer udpListener.Stop()

	// 6. Router Setup
	mux := http.NewServeMux()

	// Static files: /static/*, /maps/*, and /data/*
	mux.Handle("/static/", http.FileServer(http.FS(web.FS)))
	mux.Handle("/maps/", http.FileServer(http.FS(web.FS)))
	mux.Handle("/data/", http.FileServer(http.FS(web.FS)))

	// Prometheus Scrape Endpoint
	mux.Handle("/metrics", promhttp.Handler())

	// Auth Handlers
	mux.HandleFunc("/login", func(w http.ResponseWriter, r *http.Request) {
		loginHtml, _ := web.FS.ReadFile("login.html")
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(loginHtml)
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
			_ = json.NewEncoder(w).Encode(map[string]interface{}{
				"success": false,
				"error":   err.Error(),
			})
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

	// Auth Middleware
	requireAuth := func(next http.HandlerFunc) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if *devNoAuth {
				next(w, r)
				return
			}
			_, err := authService.GetSessionFromRequest(r)
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
			next(w, r)
		}
	}

	// Dashboard SPA Route
	indexHtml, _ := web.FS.ReadFile("index.html")
	dashboardHandler := requireAuth(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(indexHtml)
	})

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" || r.URL.Path == "/dashboard" {
			dashboardHandler(w, r)
			return
		}
		http.NotFound(w, r)
	})

	// REST API: Anomalies
	mux.HandleFunc("/api/v1/anomalies", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		limit := 1000
		if lStr := r.URL.Query().Get("limit"); lStr != "" {
			if parsed, err := strconv.Atoi(lStr); err == nil && parsed > 0 {
				limit = parsed
			}
		}
		tFilter := r.URL.Query().Get("type")
		sFilter := r.URL.Query().Get("severity")

		list := ringBuffer.GetRecent(limit, tFilter, sFilter)
		if list == nil {
			list = []model.AnomalyPayload{}
		}

		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(list)
	}))

	// REST API: Bots
	mux.HandleFunc("/api/v1/bots", requireAuth(func(w http.ResponseWriter, r *http.Request) {
		bots := udpListener.GetLastBots()
		if bots == nil {
			bots = []model.BotSnapshot{}
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(bots)
	}))

	// WebSocket Stream Endpoint
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

		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Printf("[WS] Upgrade error: %v", err)
			return
		}
		hub.Add(conn)

		// Send initial state immediately
		if lastHb := udpListener.GetLastHeartbeat(); lastHb != nil {
			_ = conn.WriteJSON(map[string]interface{}{
				"event": "heartbeat",
				"data":  lastHb,
			})
		}

		// Keep connection open and read discard
		go func() {
			defer hub.Remove(conn)
			for {
				if _, _, err := conn.ReadMessage(); err != nil {
					break
				}
			}
		}()
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
