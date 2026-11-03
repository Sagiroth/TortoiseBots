package control

import (
	"fmt"
	"net/http"
	"os"
	"strings"
	"time"
)

// Config wires the relay into the daemon: it reads the SOAP password once
// from a file that must exist with mode 0600 or stricter, and refuses to
// enable the route otherwise. The returned reason names the missing setting,
// never the credential.
type Config struct {
	Enabled   bool
	Relay     *Relay
	AuditPath string
	Reason    string
}

// Configure builds the relay. session translates a dashboard request into
// the logged-in user and rank.
func Configure(soapURL, soapUser, passwordFile string, minGM uint32, auditPath string, session func(*http.Request) (string, uint32, error)) (Config, error) {
	if strings.TrimSpace(soapURL) == "" {
		return Config{Reason: "controls off: no SOAP URL (TBDASH_SOAP_URL)"}, fmt.Errorf("controls off: no SOAP URL (TBDASH_SOAP_URL)")
	}
	if strings.TrimSpace(soapUser) == "" {
		return Config{Reason: "controls off: no SOAP user (TBDASH_SOAP_USER)"}, fmt.Errorf("controls off: no SOAP user (TBDASH_SOAP_USER)")
	}
	if strings.TrimSpace(passwordFile) == "" {
		return Config{Reason: "controls off: no SOAP password file (TBDASH_SOAP_PASSWORD_FILE)"}, fmt.Errorf("controls off: no SOAP password file (TBDASH_SOAP_PASSWORD_FILE)")
	}
	fi, err := os.Stat(passwordFile)
	if err != nil {
		return Config{Reason: "controls off: no readable SOAP password file (TBDASH_SOAP_PASSWORD_FILE)"}, fmt.Errorf("controls off: no readable SOAP password file (TBDASH_SOAP_PASSWORD_FILE)")
	}
	if perm := fi.Mode().Perm(); perm&0o077 != 0 {
		return Config{Reason: "controls off: the SOAP password file is not mode 0600 or stricter (TBDASH_SOAP_PASSWORD_FILE)"}, fmt.Errorf("controls off: the SOAP password file is not mode 0600 or stricter (TBDASH_SOAP_PASSWORD_FILE)")
	}
	raw, err := os.ReadFile(passwordFile)
	if err != nil {
		return Config{Reason: "controls off: no readable SOAP password file (TBDASH_SOAP_PASSWORD_FILE)"}, fmt.Errorf("controls off: no readable SOAP password file (TBDASH_SOAP_PASSWORD_FILE)")
	}
	password := strings.TrimSpace(string(raw))
	if password == "" {
		return Config{Reason: "controls off: the SOAP password file is empty (TBDASH_SOAP_PASSWORD_FILE)"}, fmt.Errorf("controls off: the SOAP password file is empty (TBDASH_SOAP_PASSWORD_FILE)")
	}
	return Config{
		Enabled: true,
		Relay: &Relay{
			Client:    &SOAPClient{URL: soapURL, User: soapUser, Password: password, Timeout: 10 * time.Second},
			MinGM:     minGM,
			AuditPath: auditPath,
			Session:   session,
			Timeout:   10 * time.Second,
		},
		AuditPath: auditPath,
	}, nil
}
