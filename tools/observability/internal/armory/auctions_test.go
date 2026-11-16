package armory

import (
	"database/sql"
	"errors"
	"strings"
	"testing"

	"github.com/go-sql-driver/mysql"
)

func TestIsMissingTableErr(t *testing.T) {
	if isMissingTableErr(nil) {
		t.Fatal("nil error reported as missing table")
	}
	if !isMissingTableErr(&mysql.MySQLError{Number: 1146, Message: "Table 'tw_char.tortoise_bots_ahbot_seller' doesn't exist"}) {
		t.Fatal("MySQL 1146 should be treated as missing table")
	}
	if isMissingTableErr(&mysql.MySQLError{Number: 1054, Message: "Unknown column 'foo'"}) {
		t.Fatal("MySQL 1054 must not be treated as missing table")
	}
	if !isMissingTableErr(errors.New("Error 1146: Table doesn't exist")) {
		t.Fatal("string fallback should detect 1146 / doesn't exist")
	}
}

func TestParseCreatedAt(t *testing.T) {
	if got := parseCreatedAt(sql.NullString{}); got != 0 {
		t.Fatalf("null created_at = %d, want 0", got)
	}
	if got := parseCreatedAt(sql.NullString{String: "1794844402", Valid: true}); got != 1794844402 {
		t.Fatalf("unix created_at = %d, want 1794844402", got)
	}
	if got := parseCreatedAt(sql.NullString{String: "2026-11-16 15:54:00", Valid: true}); got == 0 {
		t.Fatal("SQL datetime created_at parsed to 0")
	}
	if got := parseCreatedAt(sql.NullString{String: "not-a-time", Valid: true}); got != 0 {
		t.Fatalf("invalid created_at = %d, want 0", got)
	}
}

func TestItemClassName(t *testing.T) {
	cases := map[uint8]string{
		0:  "Consumable",
		1:  "Container",
		2:  "Weapon",
		4:  "Armor",
		5:  "Reagent",
		6:  "Projectile",
		7:  "TradeGood",
		8:  "Generic",
		9:  "Recipe",
		11: "Quiver",
		12: "Quest",
		13: "Key",
		15: "Misc",
		99: "Class 99",
	}
	for id, want := range cases {
		if got := itemClassName(id); got != want {
			t.Errorf("itemClassName(%d) = %q, want %q", id, got, want)
		}
	}
}

func TestBuildAhBotSummary_OffStates(t *testing.T) {
	missing := buildAhBotSummary(nil, ahBotData{TablePresent: false})
	if missing.Enabled || missing.TablePresent {
		t.Fatalf("missing table should disable AH bot: %+v", missing)
	}
	if missing.Health.Status != "off" || !strings.Contains(missing.Health.Note, "AH bot off") {
		t.Fatalf("unexpected missing-table health: %+v", missing.Health)
	}

	empty := buildAhBotSummary(nil, ahBotData{TablePresent: true})
	if empty.Enabled || !empty.TablePresent {
		t.Fatalf("empty seller table should disable AH bot: %+v", empty)
	}
	if empty.Health.Status != "off" || !strings.Contains(empty.Health.Note, "AH bot off") {
		t.Fatalf("unexpected empty-table health: %+v", empty.Health)
	}
}

func TestBuildAhBotSummary_HealthyMatrixAndRates(t *testing.T) {
	rows := []AuctionRow{
		{ID: 1, House: "alliance", ItemClass: 7, Quality: 1, SellerGUID: 101, SellerKind: "ahbot", Buyout: 500, Bid: 100},
		{ID: 2, House: "alliance", ItemClass: 7, Quality: 2, SellerGUID: 101, SellerKind: "ahbot", Buyout: 1200},
		{ID: 3, House: "alliance", ItemClass: 4, Quality: 3, SellerGUID: 102, SellerKind: "ahbot", Buyout: 8000},
		{ID: 4, House: "horde", ItemClass: 0, Quality: 1, SellerGUID: 201, SellerKind: "ahbot", Buyout: 300},
		{ID: 5, House: "neutral", ItemClass: 2, Quality: 4, SellerGUID: 202, SellerKind: "ahbot", Buyout: 25000},
		// Non-AH-bot listings must not appear in AH bot breakdown, though buyGUID counts as active bid.
		{ID: 6, House: "alliance", ItemClass: 4, Quality: 3, SellerGUID: 301, SellerKind: "player", Buyout: 5000, Bid: 2000, buyGUID: 101},
		{ID: 7, House: "horde", ItemClass: 7, Quality: 1, SellerGUID: 302, SellerKind: "bot", Buyout: 900},
		{ID: 8, House: "neutral", ItemClass: 7, Quality: 1, SellerGUID: 0, SellerKind: "market", Buyout: 100},
	}

	data := ahBotData{
		TablePresent: true,
		AccountCheck: true,
		Sellers: []ahBotSellerRecord{
			{AccountID: 55, GUID: 101, Faction: "alliance", CreatedAt: 1700000000},
			{AccountID: 55, GUID: 102, Faction: "alliance", CreatedAt: 1700000000},
			{AccountID: 55, GUID: 201, Faction: "horde", CreatedAt: 1700000000},
			{AccountID: 55, GUID: 202, Faction: "horde", CreatedAt: 1700000000},
		},
		Chars: map[uint32]ahBotCharRecord{
			101: {Name: "Aldric", AccountID: 55},
			102: {Name: "Brina", AccountID: 55},
			201: {Name: "Gorak", AccountID: 55},
			202: {Name: "Zulra", AccountID: 55},
		},
		Accounts: map[uint32]ahBotAccountRecord{
			55: {Username: "TBAHBOT"},
		},
		Sold1h:    14,
		Bought1h:  3,
		TotalMail: 17,
	}

	page := buildAuctionPage(rows, AuctionFilter{Kind: "ahbot", Sort: "id"}, data)
	if page.Matched != 5 {
		t.Fatalf("matched ahbot rows = %d, want 5", page.Matched)
	}
	if page.Summary.Total != 8 {
		t.Fatalf("overall summary total = %d, want 8", page.Summary.Total)
	}

	ab := page.AhBot
	if !ab.Enabled || ab.Total != 5 || ab.WithBid != 1 || ab.BuyoutValue != 35000 {
		t.Fatalf("unexpected ahbot totals: %+v", ab)
	}
	if len(ab.ByHouse) != 3 {
		t.Fatalf("by_house len = %d, want 3", len(ab.ByHouse))
	}
	if ab.ByHouse[0].House != "alliance" || ab.ByHouse[0].Total != 3 || ab.ByHouse[0].ByQuality[1] != 1 || ab.ByHouse[0].ByQuality[2] != 1 || ab.ByHouse[0].ByQuality[3] != 1 {
		t.Fatalf("unexpected alliance house counts: %+v", ab.ByHouse[0])
	}
	if ab.ByHouse[1].House != "horde" || ab.ByHouse[1].Total != 1 {
		t.Fatalf("unexpected horde house counts: %+v", ab.ByHouse[1])
	}
	if ab.ByHouse[2].House != "neutral" || ab.ByHouse[2].Total != 1 || ab.ByHouse[2].ByQuality[4] != 1 {
		t.Fatalf("unexpected neutral house counts: %+v", ab.ByHouse[2])
	}

	// Breakdown sorted by house (alliance, horde, neutral), then class_id ascending:
	// alliance Armor (4) before alliance TradeGood (7).
	if len(ab.Breakdown) != 4 {
		t.Fatalf("breakdown len = %d, want 4", len(ab.Breakdown))
	}
	if ab.Breakdown[0].House != "alliance" || ab.Breakdown[0].Category != "Armor" || ab.Breakdown[0].ByQuality[3] != 1 {
		t.Fatalf("unexpected breakdown[0]: %+v", ab.Breakdown[0])
	}
	if ab.Breakdown[1].House != "alliance" || ab.Breakdown[1].Category != "TradeGood" || ab.Breakdown[1].Total != 2 || ab.Breakdown[1].ByQuality[1] != 1 || ab.Breakdown[1].ByQuality[2] != 1 {
		t.Fatalf("unexpected breakdown[1]: %+v", ab.Breakdown[1])
	}

	if ab.Health.Status != "healthy" || !ab.Health.AccountExists || ab.Health.AccountName != "TBAHBOT" {
		t.Fatalf("unexpected health: %+v", ab.Health)
	}
	if ab.Health.AllianceOK != 2 || ab.Health.HordeOK != 2 || ab.Health.MissingTotal != 0 {
		t.Fatalf("unexpected faction counts: %+v", ab.Health)
	}
	if !ab.Rates.Derivable || ab.Rates.SoldPerHour != 14 || ab.Rates.BoughtPerHour != 3 || ab.Rates.ActiveBids != 1 {
		t.Fatalf("unexpected rates: %+v", ab.Rates)
	}
}

func TestBuildAhBotSummary_HealthDegradedAndUnhealthy(t *testing.T) {
	baseData := func() ahBotData {
		return ahBotData{
			TablePresent: true,
			AccountCheck: true,
			Sellers: []ahBotSellerRecord{
				{AccountID: 55, GUID: 101, Faction: "alliance"},
				{AccountID: 55, GUID: 201, Faction: "horde"},
			},
			Chars: map[uint32]ahBotCharRecord{
				101: {Name: "Aldric", AccountID: 55},
				201: {Name: "Gorak", AccountID: 55, DeleteDate: sql.NullInt64{Int64: 1700000100, Valid: true}},
			},
			Accounts: map[uint32]ahBotAccountRecord{
				55: {Username: "TBAHBOT"},
			},
		}
	}

	// Deleted horde character -> degraded, rates not derivable when TotalMail == 0.
	deg := buildAhBotSummary(nil, baseData())
	if deg.Health.Status != "degraded" || deg.Health.AllianceOK != 1 || deg.Health.HordeOK != 0 || deg.Health.MissingTotal != 1 {
		t.Fatalf("expected degraded health, got: %+v", deg.Health)
	}
	if deg.Sellers[1].State != "deleted" || deg.Sellers[1].Healthy {
		t.Fatalf("expected deleted state for seller 201, got: %+v", deg.Sellers[1])
	}
	if deg.Rates.Derivable {
		t.Fatal("expected Rates.Derivable=false when TotalMail == 0")
	}

	// Missing account -> unhealthy.
	missingAcc := baseData()
	missingAcc.Accounts = map[uint32]ahBotAccountRecord{}
	unhealthy := buildAhBotSummary(nil, missingAcc)
	if unhealthy.Health.Status != "unhealthy" || unhealthy.Health.AccountExists {
		t.Fatalf("expected unhealthy when account missing, got: %+v", unhealthy.Health)
	}

	// Banned account -> unhealthy.
	bannedAcc := baseData()
	bannedAcc.Accounts[55] = ahBotAccountRecord{Username: "TBAHBOT", Banned: true}
	banned := buildAhBotSummary(nil, bannedAcc)
	if banned.Health.Status != "unhealthy" || !banned.Health.AccountBanned {
		t.Fatalf("expected unhealthy when account banned, got: %+v", banned.Health)
	}

	// Wrong account / missing char -> degraded when one healthy seller remains.
	wrongAcc := baseData()
	wrongAcc.Chars[201] = ahBotCharRecord{Name: "Gorak", AccountID: 99}
	w := buildAhBotSummary(nil, wrongAcc)
	if w.Health.Status != "degraded" || w.Sellers[1].State != "wrong_account" {
		t.Fatalf("expected wrong_account degraded, got health=%+v seller=%+v", w.Health, w.Sellers[1])
	}
}

func TestListAuctionsNilDB(t *testing.T) {
	if _, err := (&Service{}).ListAuctions(AuctionFilter{}); err == nil {
		t.Fatal("expected error when armory Service DB is nil")
	}
}
