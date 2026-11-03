package control

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestSOAPEnvelope(t *testing.T) {
	var gotMethod, gotUser, gotBody, gotCT string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotMethod = r.Method
		u, p, _ := r.BasicAuth()
		gotUser = u + ":" + p
		gotCT = r.Header.Get("Content-Type")
		raw, _ := io.ReadAll(r.Body)
		gotBody = string(raw)
		_, _ = w.Write([]byte(`<?xml version="1.0"?><SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"><SOAP-ENV:Body><ns1:executeCommandResponse xmlns:ns1="urn:MaNGOS"><result>ok</result></ns1:executeCommandResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>`))
	}))
	defer srv.Close()

	secret := "s3cr3t-pw"
	c := &SOAPClient{URL: srv.URL, User: "soapuser", Password: secret, Timeout: 5 * time.Second}
	ans, err := c.Run(context.Background(), ".bot run for Gb pause")
	if err != nil {
		t.Fatalf("Run returned error: %v", err)
	}
	if ans != "ok" {
		t.Fatalf("answer = %q, want %q", ans, "ok")
	}
	if gotMethod != http.MethodPost {
		t.Errorf("method = %q, want POST", gotMethod)
	}
	if gotUser != "soapuser:"+secret {
		t.Errorf("basic auth user = %q, want soapuser with the password", gotUser)
	}
	if !strings.Contains(gotCT, "text/xml") {
		t.Errorf("content-type = %q, want text/xml", gotCT)
	}
	if !strings.Contains(gotBody, `<command>.bot run for Gb pause</command>`) {
		t.Errorf("body missing the command element: %q", gotBody)
	}
	if !strings.Contains(gotBody, "urn:MaNGOS") || !strings.Contains(gotBody, "executeCommand") {
		t.Errorf("body is not the executeCommand envelope: %q", gotBody)
	}

	// The command is XML-escaped in the envelope.
	c2 := &SOAPClient{URL: srv.URL, User: "soapuser", Password: secret, Timeout: 5 * time.Second}
	if _, err := c2.Run(context.Background(), ".bot run for Gb&evil <x>"); err == nil {
		_ = gotBody
	}
	if strings.Contains(gotBody, "<x>") {
		t.Errorf("command not escaped in envelope: %q", gotBody)
	}
}

func TestSOAPFaultIsErrorWithoutPassword(t *testing.T) {
	secret := "s3cr3t-pw"
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`<SOAP-ENV:Envelope><SOAP-ENV:Body><SOAP-ENV:Fault><faultstring>no such command</faultstring></SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>`))
	}))
	defer srv.Close()

	c := &SOAPClient{URL: srv.URL, User: "soapuser", Password: secret, Timeout: 5 * time.Second}
	_, err := c.Run(context.Background(), ".bot run for Gb pause")
	if err == nil {
		t.Fatal("fault answer returned no error")
	}
	if strings.Contains(err.Error(), secret) {
		t.Errorf("error leaks the password: %v", err)
	}
	if !strings.Contains(err.Error(), "no such command") {
		t.Errorf("error = %v, want the faultstring", err)
	}
}

func TestSOAPHTTPErrorWithoutPassword(t *testing.T) {
	secret := "s3cr3t-pw"
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
	}))
	defer srv.Close()

	c := &SOAPClient{URL: srv.URL, User: "soapuser", Password: secret, Timeout: 5 * time.Second}
	_, err := c.Run(context.Background(), ".bot run for Gb pause")
	if err == nil {
		t.Fatal("HTTP 401 returned no error")
	}
	if strings.Contains(err.Error(), secret) {
		t.Errorf("error leaks the password: %v", err)
	}
}
