package control

import (
	"bytes"
	"context"
	"fmt"
	"html"
	"io"
	"net/http"
	"strings"
	"time"
)

// SOAPClient sends console lines to the game server's SOAP endpoint.
type SOAPClient struct {
	URL      string
	User     string
	Password string
	Timeout  time.Duration
	HTTP     *http.Client
}

// Run sends command through executeCommand and returns the result text.
func (c *SOAPClient) Run(ctx context.Context, command string) (string, error) {
	timeout := c.Timeout
	if timeout <= 0 {
		timeout = 10 * time.Second
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	body := `<?xml version="1.0" encoding="UTF-8"?>` +
		`<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:MaNGOS">` +
		`<SOAP-ENV:Body><ns1:executeCommand><command>` + html.EscapeString(command) + `</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>`
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.URL, strings.NewReader(body))
	if err != nil {
		return "", fmt.Errorf("soap request: %w", err)
	}
	req.SetBasicAuth(c.User, c.Password)
	req.Header.Set("Content-Type", "text/xml; charset=utf-8")
	req.Header.Set("SOAPAction", "urn:MaNGOS#executeCommand")

	client := c.HTTP
	if client == nil {
		client = http.DefaultClient
	}
	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("soap post: %w", err)
	}
	defer resp.Body.Close()
	raw, err := io.ReadAll(io.LimitReader(resp.Body, 65536))
	if err != nil {
		return "", fmt.Errorf("soap read: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("soap status %d", resp.StatusCode)
	}
	if i := bytes.Index(raw, []byte("<faultstring>")); i >= 0 {
		rest := raw[i+len("<faultstring>"):]
		if j := bytes.Index(rest, []byte("</faultstring>")); j >= 0 {
			return "", fmt.Errorf("soap fault: %s", rest[:j])
		}
		return "", fmt.Errorf("soap fault")
	}
	i := bytes.Index(raw, []byte("<result>"))
	if i < 0 {
		return "", fmt.Errorf("soap answer has no result")
	}
	rest := raw[i+len("<result>"):]
	j := bytes.Index(rest, []byte("</result>"))
	if j < 0 {
		return "", fmt.Errorf("soap answer has no result")
	}
	return strings.TrimSpace(html.UnescapeString(string(rest[:j]))), nil
}
