package main

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/ringbuf"
	"tortoise-observability/internal/state"
	"tortoise-observability/internal/udp"
)

type quietHub struct{}

func (quietHub) Broadcast(string, interface{}) {}

// TestShutdownFlushesQueuedFight runs the daemon's shutdown in process: HTTP and UDP ingestion are up, a fight is still inside
// the writer's six-second wait, and cancelling the context (what SIGTERM does) must leave the fight on disk.
func TestShutdownFlushesQueuedFight(t *testing.T) {
	dir := t.TempDir()
	zp, err := zoneproject.New([]byte(`{}`))
	if err != nil {
		t.Fatal(err)
	}
	listener := udp.NewListener("127.0.0.1", 0, state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16)),
		metrics.New(), zp, quietHub{})
	fights, err := history.Open(history.Config{Dir: dir}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	listener.SetHistory(fights)
	if err := listener.Start(); err != nil {
		t.Fatal(err)
	}

	server := &http.Server{Addr: "127.0.0.1:0", Handler: http.NewServeMux()}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- serve(ctx, server, listener, fights) }()

	start := time.Now().Unix() - 60
	fight := combat.Fight{ID: "f1", Commander: "Glacienne", Group: 1, Map: 249, Kind: "commander", Start: start, DurMS: 60000, Outcome: "kill"}
	if !fights.Enqueue(history.Item{Session: 3, Fight: fight, At: time.Now()}) {
		t.Fatal("Enqueue refused the fight")
	}
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("serve: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("shutdown did not finish")
	}

	day := time.Unix(start, 0).UTC().Format("2006-01-02")
	data, err := os.ReadFile(filepath.Join(dir, history.DirName("Glacienne"), day+".jsonl"))
	if err != nil {
		t.Fatalf("the queued fight was not flushed: %v", err)
	}
	var stored struct {
		Key   string `json:"key"`
		Fight struct {
			ID string `json:"id"`
		} `json:"fight"`
	}
	if err := json.Unmarshal(data, &stored); err != nil {
		t.Fatalf("stored line: %v", err)
	}
	if stored.Key != combat.Key("Glacienne", 3, "f1") || stored.Fight.ID != "f1" {
		t.Fatalf("stored line = %+v", stored)
	}
	if fights.Enqueue(history.Item{Session: 3, Fight: fight, At: time.Now()}) {
		t.Error("Enqueue after Close accepted a fight")
	}
}
