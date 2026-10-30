package auth

import (
	"crypto/hmac"
	"crypto/sha1"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

type Config struct {
	DBHost     string
	DBPort     int
	DBUser     string
	DBPassword string
	DBName     string
	SecretKey  string
	MinGMLevel int
}

type Service struct {
	cfg       Config
	db        *sql.DB
	secretKey []byte
}

type SessionData struct {
	AccountID uint32
	Username  string
	Rank      uint32
	ExpiresAt int64
}

// NewService initializes the database connection and session key.
// SecretKey must be provided: the server generates a random key at startup
// when SESSION_SECRET is unset, so no shipped default ever signs a session.
func NewService(cfg Config) (*Service, error) {
	if cfg.SecretKey == "" {
		return nil, fmt.Errorf("SecretKey is required: set SESSION_SECRET or start without it to use a random key")
	}

	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?timeout=5s",
		cfg.DBUser, cfg.DBPassword, cfg.DBHost, cfg.DBPort, cfg.DBName)

	db, err := sql.Open("mysql", dsn)
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	db.SetMaxOpenConns(5)
	db.SetMaxIdleConns(2)
	db.SetConnMaxLifetime(5 * time.Minute)

	return &Service{
		cfg:       cfg,
		db:        db,
		secretKey: []byte(cfg.SecretKey),
	}, nil
}

// CalculateShaPassHash returns SHA1(UPPER(username) + ":" + UPPER(password)) in hex uppercase,
// matching the core hexEncodeByteArray storage so comparisons stay case-insensitive.
func CalculateShaPassHash(username, password string) string {
	combined := strings.ToUpper(username) + ":" + strings.ToUpper(password)
	hasher := sha1.New()
	hasher.Write([]byte(combined))
	return strings.ToUpper(hex.EncodeToString(hasher.Sum(nil)))
}

// Authenticate verifies the user against the database and checks for gmlevel/rank >= MinGMLevel.
func (s *Service) Authenticate(username, password string) (*SessionData, error) {
	username = strings.TrimSpace(username)
	if username == "" || password == "" {
		return nil, fmt.Errorf("username and password are required")
	}

	hash := CalculateShaPassHash(username, password)

	// Primary query: Penqle tw_logon uses account.rank. Hash comparison is
	// case-insensitive so lowercase-computed hashes match uppercase storage.
	var accountID uint32
	var rank uint32

	query := `SELECT id, rank FROM account WHERE UPPER(username) = UPPER(?) AND UPPER(sha_pass_hash) = UPPER(?) LIMIT 1`
	err := s.db.QueryRow(query, username, hash).Scan(&accountID, &rank)
	if err != nil {
		// Fallback query: check if account_access table exists (legacy MaNGOS/Trinity style)
		fallbackQuery := `
			SELECT a.id, aa.gmlevel
			FROM account a
			JOIN account_access aa ON a.id = aa.id
			WHERE UPPER(a.username) = UPPER(?) AND UPPER(a.sha_pass_hash) = UPPER(?) LIMIT 1`
		fbErr := s.db.QueryRow(fallbackQuery, username, hash).Scan(&accountID, &rank)
		if fbErr != nil {
			return nil, fmt.Errorf("invalid username or password")
		}
	} else {
		// A zero-rank account row must not mask a real GM level stored in
		// account_access: take the highest of both sources.
		var accessRank uint32
		accessQuery := `SELECT gmlevel FROM account_access WHERE id = ? ORDER BY gmlevel DESC LIMIT 1`
		if accessErr := s.db.QueryRow(accessQuery, accountID).Scan(&accessRank); accessErr == nil && accessRank > rank {
			rank = accessRank
		}
	}

	minLevel := s.minLevel()
	if rank < minLevel {
		// Same error text as a wrong credential: a wrong password and a
		// right password with too low a rank are indistinguishable.
		return nil, fmt.Errorf("invalid username or password")
	}

	return &SessionData{
		AccountID: accountID,
		Username:  strings.ToUpper(username),
		Rank:      rank,
		ExpiresAt: time.Now().Add(24 * time.Hour).Unix(),
	}, nil
}

// minLevel is the configured minimum GM rank (2, game master, when unset).
func (s *Service) minLevel() uint32 {
	if s.cfg.MinGMLevel <= 0 {
		return 2
	}
	return uint32(s.cfg.MinGMLevel)
}

// RecheckAccount re-reads an account's current rank and reports whether it
// still meets the minimum. A long-lived stream calls it periodically: a
// revoked account (its row gone) or a downgraded one must stop receiving data
// without waiting for the signed token to expire. A database error also fails
// the check, so the stream is closed rather than fed without revalidation;
// the client reconnects and the login path decides again.
func (s *Service) RecheckAccount(accountID uint32) error {
	var rank uint32
	if err := s.db.QueryRow(`SELECT rank FROM account WHERE id = ? LIMIT 1`, accountID).Scan(&rank); err != nil && !errors.Is(err, sql.ErrNoRows) {
		return fmt.Errorf("rank check: %w", err)
	}
	var accessRank uint32
	if err := s.db.QueryRow(`SELECT gmlevel FROM account_access WHERE id = ? ORDER BY gmlevel DESC LIMIT 1`, accountID).Scan(&accessRank); err == nil && accessRank > rank {
		rank = accessRank
	}
	if rank < s.minLevel() {
		return fmt.Errorf("account %d no longer meets the minimum GM rank", accountID)
	}
	return nil
}

// CreateSessionToken generates an HMAC-signed token.
func (s *Service) CreateSessionToken(data *SessionData) string {
	payload := fmt.Sprintf("%d:%s:%d:%d", data.AccountID, data.Username, data.Rank, data.ExpiresAt)
	mac := hmac.New(sha256.New, s.secretKey)
	mac.Write([]byte(payload))
	signature := hex.EncodeToString(mac.Sum(nil))
	return fmt.Sprintf("%s.%s", payload, signature)
}

// VerifySessionToken validates the HMAC signature and token expiration.
func (s *Service) VerifySessionToken(token string) (*SessionData, error) {
	parts := strings.Split(token, ".")
	if len(parts) != 2 {
		return nil, fmt.Errorf("invalid token format")
	}
	payload, signature := parts[0], parts[1]

	mac := hmac.New(sha256.New, s.secretKey)
	mac.Write([]byte(payload))
	expectedSig := hex.EncodeToString(mac.Sum(nil))

	if !hmac.Equal([]byte(signature), []byte(expectedSig)) {
		return nil, fmt.Errorf("invalid signature")
	}

	fields := strings.Split(payload, ":")
	if len(fields) != 4 {
		return nil, fmt.Errorf("invalid payload format")
	}

	accID, err := strconv.ParseUint(fields[0], 10, 32)
	if err != nil {
		return nil, fmt.Errorf("invalid account ID")
	}

	rank, err := strconv.ParseUint(fields[2], 10, 32)
	if err != nil {
		return nil, fmt.Errorf("invalid rank")
	}

	exp, err := strconv.ParseInt(fields[3], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("invalid expiry")
	}

	if time.Now().Unix() > exp {
		return nil, fmt.Errorf("session expired")
	}

	return &SessionData{
		AccountID: uint32(accID),
		Username:  fields[1],
		Rank:      uint32(rank),
		ExpiresAt: exp,
	}, nil
}

// GetSessionFromRequest reads and validates the session cookie from http.Request.
func (s *Service) GetSessionFromRequest(r *http.Request) (*SessionData, error) {
	cookie, err := r.Cookie("tbm_session")
	if err != nil {
		return nil, fmt.Errorf("no session cookie found")
	}
	return s.VerifySessionToken(cookie.Value)
}

// SetSessionCookie writes the session cookie to the response.
func (s *Service) SetSessionCookie(w http.ResponseWriter, token string) {
	http.SetCookie(w, &http.Cookie{
		Name:     "tbm_session",
		Value:    token,
		Path:     "/",
		Expires:  time.Now().Add(24 * time.Hour),
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
	})
}

// ClearSessionCookie removes the session cookie.
func (s *Service) ClearSessionCookie(w http.ResponseWriter) {
	http.SetCookie(w, &http.Cookie{
		Name:     "tbm_session",
		Value:    "",
		Path:     "/",
		Expires:  time.Unix(0, 0),
		HttpOnly: true,
	})
}
