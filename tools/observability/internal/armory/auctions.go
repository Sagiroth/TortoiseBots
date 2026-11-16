package armory

import (
	"database/sql"
	"errors"
	"fmt"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/go-sql-driver/mysql"
)

// AuctionPageSize is the number of rows one /api/v1/auctions page returns.
const AuctionPageSize = 100

// AuctionFilter narrows the read-only auction house view. Zero values mean
// "no constraint"; Sort is one of name, quality, level, seller, bid, buyout,
// expire (default).
type AuctionFilter struct {
	Query    string // item name fragment
	Quality  int    // minimum quality, -1 = any
	MinLevel uint32 // required level range
	MaxLevel uint32
	Seller   string // seller name fragment
	Kind     string // "", "ahbot", "bot", "player"
	House    string // "", "alliance", "horde", "neutral"
	Sort     string
	Desc     bool
	Page     uint32 // 0-based
}

type AuctionRow struct {
	ID            uint32 `json:"id"`
	House         string `json:"house"`
	ItemTemplate  uint32 `json:"item_template"`
	Name          string `json:"name"`
	Quality       uint8  `json:"quality"`
	ItemClass     uint8  `json:"item_class"`
	RequiredLevel uint32 `json:"required_level"`
	ItemLevel     uint32 `json:"item_level"`
	Icon          string `json:"icon"`
	Count         uint32 `json:"count"`
	SellerGUID    uint32 `json:"seller_guid"`
	SellerName    string `json:"seller_name"`
	SellerClass   uint8  `json:"seller_class"`
	SellerKind    string `json:"seller_kind"` // ahbot, bot, player, market (module's ownerless market listing)
	StartBid      uint32 `json:"start_bid"`
	Bid           uint32 `json:"bid"`
	Buyout        uint32 `json:"buyout"`
	ExpireAt      int64  `json:"expire_at"`

	buyGUID   uint32
	itemGUID  uint32
	displayID uint32
}

type AuctionSummary struct {
	Total       uint32            `json:"total"`
	WithBid     uint32            `json:"with_bid"`
	BuyoutValue uint64            `json:"buyout_value"`
	ByQuality   map[uint8]uint32  `json:"by_quality"`
	ByHouse     map[string]uint32 `json:"by_house"`
}

type AhBotHouseQualityCounts struct {
	House     string           `json:"house"`
	Total     uint32           `json:"total"`
	WithBid   uint32           `json:"with_bid"`
	Buyout    uint64           `json:"buyout_value"`
	ByQuality map[uint8]uint32 `json:"by_quality"`
}

type AhBotCategoryRow struct {
	House     string           `json:"house"`
	Category  string           `json:"category"`
	ClassID   uint8            `json:"class_id"`
	Total     uint32           `json:"total"`
	ByQuality map[uint8]uint32 `json:"by_quality"`
}

type AhBotSellerHealth struct {
	AccountID      uint32 `json:"account_id"`
	AccountName    string `json:"account_name,omitempty"`
	AccountChecked bool   `json:"account_checked"`
	AccountExists  bool   `json:"account_exists"`
	AccountBanned  bool   `json:"account_banned"`
	AccountLocked  bool   `json:"account_locked"`
	SellersTotal   uint32 `json:"sellers_total"`
	HealthyTotal   uint32 `json:"healthy_total"`
	MissingTotal   uint32 `json:"missing_total"`
	AllianceTotal  uint32 `json:"alliance_total"`
	AllianceOK     uint32 `json:"alliance_ok"`
	HordeTotal     uint32 `json:"horde_total"`
	HordeOK        uint32 `json:"horde_ok"`
	Status         string `json:"status"` // "off", "healthy", "degraded", "unhealthy"
	Note           string `json:"note,omitempty"`
}

type AhBotSellerRow struct {
	GUID      uint32 `json:"guid"`
	Name      string `json:"name,omitempty"`
	Faction   string `json:"faction"`
	AccountID uint32 `json:"account_id"`
	Healthy   bool   `json:"healthy"`
	State     string `json:"state"` // "ok", "deleted", "missing", "wrong_account"
	Listings  uint32 `json:"listings"`
	CreatedAt int64  `json:"created_at,omitempty"`
}

type AhBotRates struct {
	Derivable     bool   `json:"derivable"`
	SoldPerHour   uint32 `json:"sold_per_hour"`
	BoughtPerHour uint32 `json:"bought_per_hour"`
	ActiveBids    uint32 `json:"active_bids"`
	Note          string `json:"note,omitempty"`
}

type AhBotSummary struct {
	Enabled      bool                      `json:"enabled"`
	TablePresent bool                      `json:"table_present"`
	Total        uint32                    `json:"total"`
	WithBid      uint32                    `json:"with_bid"`
	BuyoutValue  uint64                    `json:"buyout_value"`
	ByHouse      []AhBotHouseQualityCounts `json:"by_house"`
	Breakdown    []AhBotCategoryRow        `json:"breakdown"`
	Health       AhBotSellerHealth         `json:"health"`
	Sellers      []AhBotSellerRow          `json:"sellers"`
	Rates        AhBotRates                `json:"rates"`
}

type AuctionPage struct {
	Rows     []AuctionRow   `json:"rows"`
	Matched  uint32         `json:"matched"`
	Page     uint32         `json:"page"`
	PageSize uint32         `json:"page_size"`
	Summary  AuctionSummary `json:"summary"`
	AhBot    AhBotSummary   `json:"ahbot"`
}

type ahBotSellerRecord struct {
	AccountID uint32
	GUID      uint32
	Faction   string
	CreatedAt int64
}

type ahBotCharRecord struct {
	Name       string
	AccountID  uint32
	DeleteDate sql.NullInt64
}

type ahBotAccountRecord struct {
	Username string
	Banned   bool
	Locked   bool
}

type ahBotData struct {
	TablePresent bool
	Sellers      []ahBotSellerRecord
	Chars        map[uint32]ahBotCharRecord
	Accounts     map[uint32]ahBotAccountRecord
	AccountCheck bool
	Sold1h       uint32
	Bought1h     uint32
	TotalMail    uint32
}

// itemClassName maps 1.12 ItemPrototype.h ItemClass IDs to category labels.
func itemClassName(class uint8) string {
	switch class {
	case 0:
		return "Consumable"
	case 1:
		return "Container"
	case 2:
		return "Weapon"
	case 3:
		return "Gem"
	case 4:
		return "Armor"
	case 5:
		return "Reagent"
	case 6:
		return "Projectile"
	case 7:
		return "TradeGood"
	case 8:
		return "Generic"
	case 9:
		return "Recipe"
	case 10:
		return "Money"
	case 11:
		return "Quiver"
	case 12:
		return "Quest"
	case 13:
		return "Key"
	case 14:
		return "Permanent"
	case 15:
		return "Misc"
	default:
		return fmt.Sprintf("Class %d", class)
	}
}

func houseSortOrder(house string) int {
	switch house {
	case "alliance":
		return 0
	case "horde":
		return 1
	default:
		return 2
	}
}

func isMissingTableErr(err error) bool {
	if err == nil {
		return false
	}
	var myErr *mysql.MySQLError
	if errors.As(err, &myErr) && myErr.Number == 1146 {
		return true
	}
	msg := strings.ToLower(err.Error())
	return strings.Contains(msg, "1146") || strings.Contains(msg, "doesn't exist")
}

func parseCreatedAt(raw sql.NullString) int64 {
	if !raw.Valid {
		return 0
	}
	s := strings.TrimSpace(raw.String)
	if s == "" {
		return 0
	}
	if ts, err := strconv.ParseInt(s, 10, 64); err == nil {
		return ts
	}
	for _, layout := range []string{
		"2006-01-02 15:04:05",
		time.RFC3339,
		"2006-01-02T15:04:05",
	} {
		if t, err := time.ParseInLocation(layout, s, time.UTC); err == nil {
			return t.Unix()
		}
	}
	return 0
}

// auctionHouse follows AuctionHouseMgr::GetAuctionHouseTeam: 1-3 Alliance,
// 4-6 Horde, anything else neutral.
func auctionHouse(id uint32) string {
	switch {
	case id >= 1 && id <= 3:
		return "alliance"
	case id >= 4 && id <= 6:
		return "horde"
	}
	return "neutral"
}

// ListAuctions returns one page of the auction house plus a whole-market
// summary and AH bot breakdown. Read-only, and kept to short indexed
// statements: auction, characters and item_display_info are MyISAM, so long
// reads or unindexed joins hold table locks that stall the core's character
// saves.
func (s *Service) ListAuctions(f AuctionFilter) (*AuctionPage, error) {
	if s == nil || s.db == nil {
		return nil, fmt.Errorf("armory database is not initialized")
	}
	ahbot, err := s.loadAhBotData()
	if err != nil {
		return nil, err
	}
	sellerSet := make(map[uint32]bool, len(ahbot.Sellers))
	for _, sr := range ahbot.Sellers {
		sellerSet[sr.GUID] = true
	}
	all, err := s.loadAuctions(sellerSet)
	if err != nil {
		return nil, err
	}

	page := buildAuctionPage(all, f, ahbot)
	if err := s.fillPageDetails(page.Rows); err != nil {
		return nil, err
	}
	return page, nil
}

func buildAuctionPage(all []AuctionRow, f AuctionFilter, ahbot ahBotData) *AuctionPage {
	page := &AuctionPage{
		Rows:     []AuctionRow{},
		Page:     f.Page,
		PageSize: AuctionPageSize,
		Summary:  AuctionSummary{ByQuality: map[uint8]uint32{}, ByHouse: map[string]uint32{}},
		AhBot:    buildAhBotSummary(all, ahbot),
	}
	query := strings.ToLower(f.Query)
	seller := strings.ToLower(f.Seller)
	var matched []AuctionRow
	for _, r := range all {
		sum := &page.Summary
		sum.Total++
		if r.Bid > 0 {
			sum.WithBid++
		}
		sum.BuyoutValue += uint64(r.Buyout)
		sum.ByQuality[r.Quality]++
		sum.ByHouse[r.House]++

		if query != "" && !strings.Contains(strings.ToLower(r.Name), query) ||
			f.Quality >= 0 && int(r.Quality) < f.Quality ||
			f.MinLevel > 0 && r.RequiredLevel < f.MinLevel ||
			f.MaxLevel > 0 && r.RequiredLevel > f.MaxLevel ||
			seller != "" && !strings.Contains(strings.ToLower(r.SellerName), seller) ||
			f.Kind != "" && r.SellerKind != f.Kind ||
			f.House != "" && r.House != f.House {
			continue
		}
		matched = append(matched, r)
	}
	page.Matched = uint32(len(matched))

	key := func(r *AuctionRow) interface{} {
		switch f.Sort {
		case "name":
			return strings.ToLower(r.Name)
		case "seller":
			return strings.ToLower(r.SellerName)
		case "quality":
			return int64(r.Quality)
		case "level":
			return int64(r.RequiredLevel)
		case "bid":
			return int64(max(r.Bid, r.StartBid))
		case "buyout":
			return int64(r.Buyout)
		}
		return r.ExpireAt
	}
	sort.SliceStable(matched, func(i, j int) bool {
		a, b := key(&matched[i]), key(&matched[j])
		if a == b {
			return matched[i].ID < matched[j].ID
		}
		var less bool
		if as, ok := a.(string); ok {
			less = as < b.(string)
		} else {
			less = a.(int64) < b.(int64)
		}
		return less != f.Desc
	})

	start := int(f.Page) * AuctionPageSize
	if start < len(matched) {
		page.Rows = matched[start:min(start+AuctionPageSize, len(matched))]
	}
	return page
}

func buildAhBotSummary(all []AuctionRow, data ahBotData) AhBotSummary {
	out := AhBotSummary{
		TablePresent: data.TablePresent,
		ByHouse:      []AhBotHouseQualityCounts{},
		Breakdown:    []AhBotCategoryRow{},
		Sellers:      []AhBotSellerRow{},
	}
	if !data.TablePresent {
		out.Health = AhBotSellerHealth{
			Status: "off",
			Note:   "AH bot off (tortoise_bots_ahbot_seller table not present)",
		}
		return out
	}
	if len(data.Sellers) == 0 {
		out.Health = AhBotSellerHealth{
			Status: "off",
			Note:   "AH bot off (no seller characters registered)",
		}
		return out
	}

	out.Enabled = true
	sellerSet := make(map[uint32]bool, len(data.Sellers))
	for _, sr := range data.Sellers {
		sellerSet[sr.GUID] = true
	}

	houseOrder := []string{"alliance", "horde", "neutral"}
	houseMap := make(map[string]*AhBotHouseQualityCounts, 3)
	for _, h := range houseOrder {
		houseMap[h] = &AhBotHouseQualityCounts{
			House:     h,
			ByQuality: map[uint8]uint32{},
		}
	}
	type catKey struct {
		house   string
		classID uint8
	}
	catMap := map[catKey]*AhBotCategoryRow{}
	listingsBySeller := map[uint32]uint32{}

	for _, r := range all {
		if r.buyGUID != 0 && sellerSet[r.buyGUID] {
			out.Rates.ActiveBids++
		}
		if r.SellerKind != "ahbot" {
			continue
		}
		out.Total++
		if r.Bid > 0 {
			out.WithBid++
		}
		out.BuyoutValue += uint64(r.Buyout)
		listingsBySeller[r.SellerGUID]++

		hm, ok := houseMap[r.House]
		if !ok {
			hm = &AhBotHouseQualityCounts{House: r.House, ByQuality: map[uint8]uint32{}}
			houseMap[r.House] = hm
		}
		hm.Total++
		if r.Bid > 0 {
			hm.WithBid++
		}
		hm.Buyout += uint64(r.Buyout)
		hm.ByQuality[r.Quality]++

		ck := catKey{house: r.House, classID: r.ItemClass}
		cr, ok := catMap[ck]
		if !ok {
			cr = &AhBotCategoryRow{
				House:     r.House,
				Category:  itemClassName(r.ItemClass),
				ClassID:   r.ItemClass,
				ByQuality: map[uint8]uint32{},
			}
			catMap[ck] = cr
		}
		cr.Total++
		cr.ByQuality[r.Quality]++
	}

	for _, h := range houseOrder {
		out.ByHouse = append(out.ByHouse, *houseMap[h])
	}
	for _, cr := range catMap {
		out.Breakdown = append(out.Breakdown, *cr)
	}
	sort.Slice(out.Breakdown, func(i, j int) bool {
		hi, hj := houseSortOrder(out.Breakdown[i].House), houseSortOrder(out.Breakdown[j].House)
		if hi != hj {
			return hi < hj
		}
		return out.Breakdown[i].ClassID < out.Breakdown[j].ClassID
	})

	// Pick the primary account ID (most frequent across seller rows).
	accCounts := map[uint32]int{}
	var primaryAcc uint32
	bestCount := 0
	for _, sr := range data.Sellers {
		accCounts[sr.AccountID]++
		if accCounts[sr.AccountID] > bestCount || (accCounts[sr.AccountID] == bestCount && sr.AccountID > primaryAcc) {
			bestCount = accCounts[sr.AccountID]
			primaryAcc = sr.AccountID
		}
	}

	health := AhBotSellerHealth{
		AccountID:      primaryAcc,
		AccountChecked: data.AccountCheck,
		SellersTotal:   uint32(len(data.Sellers)),
	}
	if acc, ok := data.Accounts[primaryAcc]; ok {
		health.AccountExists = true
		health.AccountName = acc.Username
		health.AccountBanned = acc.Banned
		health.AccountLocked = acc.Locked
	}

	for _, sr := range data.Sellers {
		faction := strings.ToLower(strings.TrimSpace(sr.Faction))
		row := AhBotSellerRow{
			GUID:      sr.GUID,
			Faction:   faction,
			AccountID: sr.AccountID,
			Listings:  listingsBySeller[sr.GUID],
			CreatedAt: sr.CreatedAt,
		}
		if ch, ok := data.Chars[sr.GUID]; !ok {
			row.State = "missing"
		} else {
			row.Name = ch.Name
			switch {
			case ch.DeleteDate.Valid:
				row.State = "deleted"
			case ch.AccountID != sr.AccountID:
				row.State = "wrong_account"
			default:
				row.State = "ok"
				row.Healthy = true
			}
		}

		if row.Healthy {
			health.HealthyTotal++
		} else {
			health.MissingTotal++
		}
		switch faction {
		case "alliance":
			health.AllianceTotal++
			if row.Healthy {
				health.AllianceOK++
			}
		case "horde":
			health.HordeTotal++
			if row.Healthy {
				health.HordeOK++
			}
		}
		out.Sellers = append(out.Sellers, row)
	}

	switch {
	case (health.AccountChecked && (!health.AccountExists || health.AccountBanned)) || health.HealthyTotal == 0:
		health.Status = "unhealthy"
	case health.MissingTotal > 0 || health.AllianceOK == 0 || health.HordeOK == 0 || health.AccountLocked:
		health.Status = "degraded"
	default:
		health.Status = "healthy"
	}

	var notes []string
	if health.AccountChecked {
		if !health.AccountExists {
			notes = append(notes, fmt.Sprintf("Account #%d missing", health.AccountID))
		} else {
			accLabel := fmt.Sprintf("Account #%d", health.AccountID)
			if health.AccountName != "" {
				accLabel = fmt.Sprintf("Account %s (#%d)", health.AccountName, health.AccountID)
			}
			if health.AccountBanned {
				accLabel += " BANNED"
			} else if health.AccountLocked {
				accLabel += " locked"
			}
			notes = append(notes, accLabel)
		}
	} else if health.AccountID > 0 {
		notes = append(notes, fmt.Sprintf("Account #%d", health.AccountID))
	}
	notes = append(notes,
		fmt.Sprintf("%d/%d Alliance", health.AllianceOK, health.AllianceTotal),
		fmt.Sprintf("%d/%d Horde", health.HordeOK, health.HordeTotal),
	)
	if health.MissingTotal > 0 {
		notes = append(notes, fmt.Sprintf("%d unhealthy", health.MissingTotal))
	}
	health.Note = strings.Join(notes, " · ")
	out.Health = health

	if data.TotalMail > 0 {
		out.Rates.Derivable = true
		out.Rates.SoldPerHour = data.Sold1h
		out.Rates.BoughtPerHour = data.Bought1h
		out.Rates.Note = "From unpurged seller mail in the last 1 h"
	} else {
		out.Rates.Note = "Seller mail is empty or purged; sold/bought per hour cannot be derived from DB"
	}

	return out
}

func (s *Service) loadAhBotData() (ahBotData, error) {
	out := ahBotData{
		Chars:    map[uint32]ahBotCharRecord{},
		Accounts: map[uint32]ahBotAccountRecord{},
	}

	rows, err := s.db.Query(`
		SELECT account_id, guid, faction, created_at
		FROM tortoise_bots_ahbot_seller
		ORDER BY faction, guid
	`)
	if err != nil {
		if isMissingTableErr(err) {
			return out, nil
		}
		return out, err
	}
	out.TablePresent = true
	for rows.Next() {
		var sr ahBotSellerRecord
		var rawCreated sql.NullString
		if err := rows.Scan(&sr.AccountID, &sr.GUID, &sr.Faction, &rawCreated); err != nil {
			rows.Close()
			return out, err
		}
		sr.CreatedAt = parseCreatedAt(rawCreated)
		out.Sellers = append(out.Sellers, sr)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return out, err
	}
	rows.Close()

	if len(out.Sellers) == 0 {
		return out, nil
	}

	guidArgs := make([]interface{}, 0, len(out.Sellers))
	accSeen := map[uint32]bool{}
	accArgs := make([]interface{}, 0, 2)
	for _, sr := range out.Sellers {
		guidArgs = append(guidArgs, sr.GUID)
		if sr.AccountID != 0 && !accSeen[sr.AccountID] {
			accSeen[sr.AccountID] = true
			accArgs = append(accArgs, sr.AccountID)
		}
	}

	// Primary-key lookup on characters (MyISAM) for the small seller list.
	guidPlaceholders := "?" + strings.Repeat(",?", len(guidArgs)-1)
	crows, err := s.db.Query(
		"SELECT guid, name, account, deleteDate FROM characters WHERE guid IN ("+guidPlaceholders+")",
		guidArgs...,
	)
	if err != nil {
		return out, err
	}
	for crows.Next() {
		var guid uint32
		var ch ahBotCharRecord
		if err := crows.Scan(&guid, &ch.Name, &ch.AccountID, &ch.DeleteDate); err != nil {
			crows.Close()
			return out, err
		}
		out.Chars[guid] = ch
	}
	if err := crows.Err(); err != nil {
		crows.Close()
		return out, err
	}
	crows.Close()

	// Primary-key lookup on logon account (InnoDB) when LogonDB is configured.
	if s.cfg.LogonDB != "" && len(accArgs) > 0 {
		out.AccountCheck = true
		accPlaceholders := "?" + strings.Repeat(",?", len(accArgs)-1)
		arows, err := s.db.Query(
			fmt.Sprintf("SELECT id, username, banned, locked FROM %s.account WHERE id IN (%s)", s.cfg.LogonDB, accPlaceholders),
			accArgs...,
		)
		if err != nil {
			return out, err
		}
		for arows.Next() {
			var id uint32
			var acc ahBotAccountRecord
			var banned, locked int
			if err := arows.Scan(&id, &acc.Username, &banned, &locked); err != nil {
				arows.Close()
				return out, err
			}
			acc.Banned = banned != 0
			acc.Locked = locked != 0
			out.Accounts[id] = acc
		}
		if err := arows.Err(); err != nil {
			arows.Close()
			return out, err
		}
		arows.Close()
	}

	// Indexed lookup via idx_receiver on mail (InnoDB); if seller mail is purged,
	// totalMail stays 0 and Rates.Derivable stays false.
	cutoff := time.Now().Unix() - 3600
	mailArgs := make([]interface{}, 0, 2+len(guidArgs))
	mailArgs = append(mailArgs, cutoff, cutoff)
	mailArgs = append(mailArgs, guidArgs...)
	if err := s.db.QueryRow(
		`SELECT
			COALESCE(SUM(CASE WHEN deliver_time >= ? AND subject LIKE '%:0:2' THEN 1 ELSE 0 END), 0),
			COALESCE(SUM(CASE WHEN deliver_time >= ? AND subject LIKE '%:0:1' THEN 1 ELSE 0 END), 0),
			COUNT(*)
		FROM mail
		WHERE receiver IN (`+guidPlaceholders+`)
			AND messageType = 2
			AND (isDeleted IS NULL OR isDeleted = 0)`,
		mailArgs...,
	).Scan(&out.Sold1h, &out.Bought1h, &out.TotalMail); err != nil {
		return out, err
	}

	return out, nil
}

// loadAuctions reads every auction with its item and seller in two short
// statements (~5 ms and ~20 ms on a 7k-auction, 1k-character realm).
func (s *Service) loadAuctions(ahbotSellers map[uint32]bool) ([]AuctionRow, error) {
	rows, err := s.db.Query(fmt.Sprintf(`
		SELECT a.id, a.houseid, a.itemguid, a.item_template, a.itemowner, a.buyguid, a.startbid, a.lastbid, a.buyoutprice, a.time,
		       it.name, it.quality, it.class, it.required_level, it.item_level, it.display_id
		FROM auction a
		JOIN %s.item_template it ON it.entry = a.item_template
	`, s.cfg.WorldDB))
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	all := []AuctionRow{}
	for rows.Next() {
		var r AuctionRow
		var houseID uint32
		if err := rows.Scan(&r.ID, &houseID, &r.itemGUID, &r.ItemTemplate, &r.SellerGUID, &r.buyGUID, &r.StartBid, &r.Bid, &r.Buyout, &r.ExpireAt,
			&r.Name, &r.Quality, &r.ItemClass, &r.RequiredLevel, &r.ItemLevel, &r.displayID); err != nil {
			return nil, err
		}
		r.House = auctionHouse(houseID)
		r.Count = 1
		all = append(all, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	// A seller is a pool bot when its account is in tortoise_bots_pool_account,
	// an AH bot when its guid is registered in tortoise_bots_ahbot_seller, and
	// owner 0 is the module's legacy ownerless market listing.
	type sellerInfo struct {
		name  string
		class uint8
		bot   bool
	}
	sellers := map[uint32]sellerInfo{}
	srows, err := s.db.Query(`
		SELECT c.guid, c.name, c.class, p.account_id IS NOT NULL
		FROM characters c
		LEFT JOIN tortoise_bots_pool_account p ON p.account_id = c.account
		WHERE c.guid IN (SELECT DISTINCT itemowner FROM auction)
	`)
	if err != nil {
		return nil, err
	}
	defer srows.Close()
	for srows.Next() {
		var guid uint32
		var si sellerInfo
		if err := srows.Scan(&guid, &si.name, &si.class, &si.bot); err != nil {
			return nil, err
		}
		sellers[guid] = si
	}
	if err := srows.Err(); err != nil {
		return nil, err
	}

	for i := range all {
		r := &all[i]
		si, ok := sellers[r.SellerGUID]
		switch {
		case r.SellerGUID == 0:
			r.SellerKind = "market"
		case ahbotSellers[r.SellerGUID]:
			r.SellerKind = "ahbot"
		case ok && si.bot:
			r.SellerKind = "bot"
		default:
			r.SellerKind = "player"
		}
		r.SellerName, r.SellerClass = si.name, si.class
	}
	return all, nil
}

// fillPageDetails adds stack counts and icons for one page only.
// item_display_info has no index on ID, so it is never joined to the whole
// auction table.
func (s *Service) fillPageDetails(page []AuctionRow) error {
	if len(page) == 0 {
		return nil
	}
	placeholders := "?" + strings.Repeat(",?", len(page)-1)
	itemArgs := make([]interface{}, len(page))
	displayArgs := make([]interface{}, len(page))
	for i, r := range page {
		itemArgs[i] = r.itemGUID
		displayArgs[i] = r.displayID
	}

	counts := map[uint32]uint32{}
	rows, err := s.db.Query("SELECT guid, `count` FROM item_instance WHERE guid IN ("+placeholders+")", itemArgs...)
	if err != nil {
		return err
	}
	for rows.Next() {
		var guid, count uint32
		if err := rows.Scan(&guid, &count); err != nil {
			rows.Close()
			return err
		}
		counts[guid] = count
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return err
	}

	icons := map[uint32]string{}
	rows, err = s.db.Query(fmt.Sprintf("SELECT ID, icon FROM %s.item_display_info WHERE ID IN (%s)", s.cfg.WorldDB, placeholders), displayArgs...)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		var id uint32
		var icon string
		if err := rows.Scan(&id, &icon); err != nil {
			return err
		}
		icons[id] = icon
	}
	if err := rows.Err(); err != nil {
		return err
	}

	for i := range page {
		if c := counts[page[i].itemGUID]; c > 0 {
			page[i].Count = c
		}
		page[i].Icon = icons[page[i].displayID]
	}
	return nil
}
