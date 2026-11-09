package main

import (
	"strings"
	"testing"

	"tortoise-observability/web"
)

// TestLoginKeepsHash is a wiring pin for the shared dashboard links: the browser keeps a link's #/... route across the server's
// redirect to /login, and the login page must carry it on to the dashboard (the page itself does the same on a 401). Only a route
// of sane length is kept, so the redirect target is never built from anything else.
func TestLoginKeepsHash(t *testing.T) {
	b, err := web.FS.ReadFile("login.html")
	if err != nil {
		t.Fatal(err)
	}
	src := string(b)
	for _, want := range []string{"location.hash", "startsWith('#/')", "h.length <= 4096", "'/dashboard' + "} {
		if !strings.Contains(src, want) {
			t.Errorf("login.html does not contain %q", want)
		}
	}
	if strings.Contains(src, "window.location.href = '/dashboard';") {
		t.Error("login.html still redirects to a bare /dashboard and drops the link's route")
	}
	app, err := web.FS.ReadFile("static/js/app.js")
	if err != nil {
		t.Fatal(err)
	}
	if n := strings.Count(string(app), "'/login' + keepHash()"); n != 2 {
		t.Errorf("app.js sends the operator to /login with the link's route %d times, want 2 (logout and the 401)", n)
	}
}
