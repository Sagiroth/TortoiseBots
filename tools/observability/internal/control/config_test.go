package control

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func noSession(*http.Request) (string, uint32, error) { return "gmtester", 3, nil }

func TestControlOffWithoutPasswordFile(t *testing.T) {
	dir := t.TempDir()

	missing := filepath.Join(dir, "no-such-file")
	cfg, err := Configure("http://127.0.0.1:787", "soapuser", missing, 3, filepath.Join(dir, "audit.log"), noSession)
	if err == nil || !strings.Contains(err.Error(), "password file") {
		t.Errorf("Configure with a missing file: err = %v, want a password-file reason", err)
	}
	if cfg.Enabled {
		t.Errorf("Configure with a missing file: enabled")
	}

	loose := filepath.Join(dir, "loose")
	if err := os.WriteFile(loose, []byte("s3cr3t\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	cfg, err = Configure("http://127.0.0.1:787", "soapuser", loose, 3, filepath.Join(dir, "audit.log"), noSession)
	if err == nil || !strings.Contains(err.Error(), "0600") {
		t.Errorf("Configure with mode 0644: err = %v, want a 0600 reason", err)
	}
	if cfg.Enabled {
		t.Errorf("Configure with mode 0644: enabled")
	}

	// The reason never carries the password.
	secret := filepath.Join(dir, "secret")
	if err := os.WriteFile(secret, []byte("s3cr3t-pw\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := Configure("", "soapuser", secret, 3, filepath.Join(dir, "audit.log"), noSession); err == nil || strings.Contains(err.Error(), "s3cr3t") {
		t.Errorf("Configure without URL: err = %v, want a reason without the password", err)
	}
	if _, err := Configure("http://127.0.0.1:787", "", secret, 3, filepath.Join(dir, "audit.log"), noSession); err == nil || strings.Contains(err.Error(), "s3cr3t") {
		t.Errorf("Configure without user: err = %v, want a reason without the password", err)
	}

	cfg, err = Configure("http://127.0.0.1:787", "soapuser", secret, 3, filepath.Join(dir, "audit.log"), noSession)
	if err != nil {
		t.Fatalf("Configure with a 0600 file: %v", err)
	}
	if !cfg.Enabled {
		t.Errorf("Configure with a 0600 file: not enabled")
	}
	if cfg.Relay == nil || cfg.Relay.Client == nil {
		t.Fatalf("Configure with a 0600 file: no relay")
	}
	if strings.Contains(cfg.Relay.AuditPath, "s3cr3t") {
		t.Errorf("config carries the password")
	}
	if got := cfg.Relay.Client.Password; got != "s3cr3t-pw" {
		t.Errorf("password = %q, want trimmed file content", got)
	}
	if cfg.AuditPath == "" {
		t.Errorf("no audit path")
	}
}
