package control

import (
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

var errNoSession = errors.New("no session")

func testRelay(t *testing.T, session func(*http.Request) (string, uint32, error), answer string, block chan struct{}) (*Relay, *httptest.Server) {
	t.Helper()
	soap := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if block != nil {
			<-block
		}
		if answer == "" {
			answer = "paused"
		}
		_, _ = w.Write([]byte(`<?xml version="1.0"?><SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"><SOAP-ENV:Body><ns1:executeCommandResponse xmlns:ns1="urn:MaNGOS"><result>` + answer + `</result></ns1:executeCommandResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>`))
	}))
	t.Cleanup(soap.Close)
	r := &Relay{
		Client:    &SOAPClient{URL: soap.URL, User: "u", Password: "pw", Timeout: 10 * time.Second},
		MinGM:     3,
		AuditPath: filepath.Join(t.TempDir(), "control-audit.log"),
		Session:   session,
		Timeout:   10 * time.Second,
	}
	return r, soap
}

func okSession(rank uint32) func(*http.Request) (string, uint32, error) {
	return func(*http.Request) (string, uint32, error) { return "gmtester", rank, nil }
}

func postControl(t *testing.T, srv *httptest.Server, cmd, origin string, header bool) *http.Response {
	t.Helper()
	var body string
	if cmd != "" {
		enc, _ := json.Marshal(map[string]string{"cmd": cmd})
		body = string(enc)
	} else {
		body = `{}`
	}
	req, err := http.NewRequest(http.MethodPost, srv.URL, strings.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	if origin != "" {
		req.Header.Set("Origin", origin)
	}
	if header {
		req.Header.Set("X-TBM-Control", "1")
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	return resp
}

func readBody(t *testing.T, resp *http.Response) string {
	t.Helper()
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)
	return string(raw)
}

func TestControlRefusals(t *testing.T) {
	r, _ := testRelay(t, okSession(3), "paused", nil)
	srv := httptest.NewServer(r)
	defer srv.Close()
	origin := "http://" + strings.TrimPrefix(srv.URL, "http://")

	// GET is not a control call.
	getReq, _ := http.NewRequest(http.MethodGet, srv.URL, nil)
	getReq.Header.Set("Origin", origin)
	getReq.Header.Set("X-TBM-Control", "1")
	if resp, err := http.DefaultClient.Do(getReq); err != nil {
		t.Fatal(err)
	} else {
		if resp.StatusCode != http.StatusMethodNotAllowed {
			t.Errorf("GET status = %d, want 405")
			resp.Body.Close()
		} else if b := readBody(t, resp); !isJSONRefusal(b, "method not allowed") {
			t.Errorf("GET body = %q, want JSON {ok:false,error}", b)
		}
	}

	// Missing Origin.
	if resp := postControl(t, srv, ".bot run for Gb pause", "", true); resp.StatusCode != http.StatusForbidden {
		t.Errorf("missing origin status = %d, want 403", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "origin") {
			t.Errorf("missing origin body = %q, want JSON {ok:false,error} with origin", b)
		}
	}

	// Foreign Origin.
	if resp := postControl(t, srv, ".bot run for Gb pause", "http://evil.example", true); resp.StatusCode != http.StatusForbidden {
		t.Errorf("foreign origin status = %d, want 403", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "origin") {
			t.Errorf("foreign origin body = %q, want JSON {ok:false,error} with origin", b)
		}
	}

	// Missing header.
	if resp := postControl(t, srv, ".bot run for Gb pause", origin, false); resp.StatusCode != http.StatusForbidden {
		t.Errorf("missing header status = %d, want 403", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "header") {
			t.Errorf("missing header body = %q, want JSON {ok:false,error} with header", b)
		}
		resp.Body.Close()
	}

	// No session.
	rNo, _ := testRelay(t, func(*http.Request) (string, uint32, error) {
		return "", 0, errNoSession
	}, "paused", nil)
	srvNo := httptest.NewServer(rNo)
	defer srvNo.Close()
	originNo := "http://" + strings.TrimPrefix(srvNo.URL, "http://")
	if resp := postControl(t, srvNo, ".bot run for Gb pause", originNo, true); resp.StatusCode != http.StatusUnauthorized {
		t.Errorf("no session status = %d, want 401", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "unauthorized") {
			t.Errorf("no session body = %q, want JSON {ok:false,error}", b)
		}
	}
	if _, err := os.Stat(rNo.AuditPath); !os.IsNotExist(err) {
		t.Errorf("refusal before the session check wrote an audit line")
	}

	// Rank too low.
	rLow, _ := testRelay(t, okSession(2), "paused", nil)
	srvLow := httptest.NewServer(rLow)
	defer srvLow.Close()
	originLow := "http://" + strings.TrimPrefix(srvLow.URL, "http://")
	if resp := postControl(t, srvLow, ".bot run for Gb pause", originLow, true); resp.StatusCode != http.StatusForbidden {
		t.Errorf("low rank status = %d, want 403", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "rank") {
			t.Errorf("low rank body = %q, want JSON {ok:false,error} with rank", b)
		}
	}

	// Disallowed command: 400 and an audit line with ok:false.
	if resp := postControl(t, srv, ".server shutdown", origin, true); resp.StatusCode != http.StatusBadRequest {
		t.Errorf("disallowed status = %d, want 400", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		if !isJSONRefusal(b, "not allowed") {
			t.Errorf("disallowed body = %q, want JSON {ok:false,error} with not allowed", b)
		}
		resp.Body.Close()
	}
	audit, err := os.ReadFile(r.AuditPath)
	if err != nil {
		t.Fatalf("audit read: %v", err)
	}
	if !strings.Contains(string(audit), `"ok":false`) || !strings.Contains(string(audit), ".server shutdown") {
		t.Errorf("audit after refusal = %q, want an ok:false line with the command", audit)
	}

	// A good call: 200 with the fake answer, and an audit line.
	if resp := postControl(t, srv, ".bot run for Gb pause", origin, true); resp.StatusCode != http.StatusOK {
		t.Errorf("good call status = %d, want 200", resp.StatusCode)
		resp.Body.Close()
	} else {
		b := readBody(t, resp)
		var got map[string]any
		if err := json.Unmarshal([]byte(b), &got); err != nil {
			t.Fatalf("good call body is not JSON: %q", b)
		}
		if got["ok"] != true || got["answer"] != "paused" {
			t.Errorf("good call body = %q, want ok with the answer", b)
		}
		resp.Body.Close()
	}
	audit, _ = os.ReadFile(r.AuditPath)
	lines := strings.Count(strings.TrimSpace(string(audit)), "\n") + 1
	if lines < 2 {
		t.Errorf("audit has %d lines, want the refusal and the good call", lines)
	}
	if strings.Contains(string(audit), "s3cr3t") || strings.Contains(string(audit), `"Password"`) {
		t.Errorf("audit leaks the credential: %q", audit)
	}

	// A second call while the fake SOAP blocks: 429.
	block := make(chan struct{})
	rBusy, _ := testRelay(t, okSession(3), "paused", block)
	srvBusy := httptest.NewServer(rBusy)
	defer srvBusy.Close()
	originBusy := "http://" + strings.TrimPrefix(srvBusy.URL, "http://")
	done := make(chan *http.Response, 1)
	go func() {
		resp := postControl(t, srvBusy, ".bot run for Gb pause", originBusy, true)
		done <- resp
	}()
	time.Sleep(300 * time.Millisecond)
	resp := postControl(t, srvBusy, ".bot run for Gb status", originBusy, true)
	if resp.StatusCode != http.StatusTooManyRequests {
		t.Errorf("concurrent call status = %d, want 429", resp.StatusCode)
	}
	b := readBody(t, resp)
	if !strings.Contains(b, "busy") {
		t.Errorf("concurrent call body = %q, want busy", b)
	}
	close(block)
	first := <-done
	first.Body.Close()
}

// isJSONRefusal reports whether the body is {"ok":false,"error":...} with
// the fragment in the error text.
func isJSONRefusal(body, fragment string) bool {
	var got map[string]any
	if err := json.Unmarshal([]byte(body), &got); err != nil {
		return false
	}
	ok, _ := got["ok"].(bool)
	msg, _ := got["error"].(string)
	return !ok && strings.Contains(msg, fragment)
}

func TestControlSOAPTimeout(t *testing.T) {
	slow := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(11 * time.Second)
	}))
	defer slow.Close()
	r := &Relay{
		Client:    &SOAPClient{URL: slow.URL, User: "u", Password: "pw", Timeout: 10 * time.Second},
		MinGM:     3,
		AuditPath: filepath.Join(t.TempDir(), "control-audit.log"),
		Session:   okSession(3),
		Timeout:   time.Second,
	}
	srv := httptest.NewServer(r)
	defer srv.Close()
	origin := "http://" + strings.TrimPrefix(srv.URL, "http://")
	resp := postControl(t, srv, ".bot run for Gb pause", origin, true)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusGatewayTimeout {
		t.Errorf("slow SOAP status = %d, want 504", resp.StatusCode)
	}
	if b := readBody(t, resp); !strings.Contains(b, "timeout") {
		t.Errorf("slow SOAP body = %q, want timeout", b)
	}
}

func TestControlAnswerClipped(t *testing.T) {
	long := strings.Repeat("x", 5000)
	r, _ := testRelay(t, okSession(3), long, nil)
	srv := httptest.NewServer(r)
	defer srv.Close()
	origin := "http://" + strings.TrimPrefix(srv.URL, "http://")
	resp := postControl(t, srv, ".bot run for Gb status", origin, true)
	b := readBody(t, resp)
	var got map[string]any
	if err := json.Unmarshal([]byte(b), &got); err != nil {
		t.Fatalf("body is not JSON: %.100q", b)
	}
	if ans, _ := got["answer"].(string); len(ans) > 4000 {
		t.Errorf("answer is %d bytes, want at most 4000", len(ans))
	}
	audit, _ := os.ReadFile(r.AuditPath)
	var line map[string]any
	for _, l := range strings.Split(strings.TrimSpace(string(audit)), "\n") {
		if err := json.Unmarshal([]byte(l), &line); err != nil {
			t.Fatalf("audit line is not JSON: %q", l)
		}
	}
	if ans, _ := line["answer"].(string); len(ans) > 200 {
		t.Errorf("audit answer is %d bytes, want at most 200", len(ans))
	}
}

func TestControlBodyTooLarge(t *testing.T) {
	var calls atomic.Int32
	soap := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
	}))
	defer soap.Close()
	r := &Relay{
		Client:    &SOAPClient{URL: soap.URL, User: "u", Password: "pw", Timeout: time.Second},
		MinGM:     3,
		AuditPath: filepath.Join(t.TempDir(), "control-audit.log"),
		Session:   okSession(3),
		Timeout:   time.Second,
	}
	srv := httptest.NewServer(r)
	defer srv.Close()
	origin := "http://" + strings.TrimPrefix(srv.URL, "http://")
	huge := ".bot run for Gb pause " + strings.Repeat("x", 600)
	resp := postControl(t, srv, huge, origin, true)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusBadRequest {
		t.Errorf("oversize body status = %d, want 400", resp.StatusCode)
	}
	if calls.Load() != 0 {
		t.Errorf("oversize body reached SOAP")
	}
}
