package main

import (
	"os"
	"strings"
	"testing"
)

// TestTrackRouteWired guards the route, its parsing and its store call: the
// handler is an inline closure in main(), so the source is the only thing a
// test can see without a live daemon.
func TestTrackRouteWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"GET /api/v1/commanders/track", "state.ParseTrackWindow(", "store.TrackWindow("} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
}
