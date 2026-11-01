package combat

import (
	"errors"
	"net/url"
	"strconv"
	"strings"
)

// ListQuery is the optional paging and filtering a fights list takes (GET /api/v1/combat/fights and
// /api/v1/combat/history). Every field's zero value means "not asked", so a request without the params answers exactly
// as before.
//
//	limit=N          at most N rows (1..MaxListLimit), newest first
//	before=S[,KEY]   only rows older than the cursor: start < S, or start == S and key > KEY (the list order is start
//	                 descending, then key ascending, so the last row of a page is the next page's cursor)
//	since=S          only rows with start >= S
//	boss=1           only boss fights (a boss name)
//	deaths=1         only fights with at least one death
//	outcome=a,b      only these outcomes (kill, wipe, ...)
type ListQuery struct {
	Limit     int
	Before    int64
	BeforeKey string
	HasBefore bool
	Since     int64
	Boss      bool
	Deaths    bool
	Outcomes  map[string]bool
}

// MaxListLimit caps one page.
const MaxListLimit = 500

// Active reports whether any param was given.
func (q ListQuery) Active() bool {
	return q.Limit > 0 || q.HasBefore || q.Since > 0 || q.Boss || q.Deaths || len(q.Outcomes) > 0
}

// Spans reports whether the query names its own time range (before or since), so a day-bound list may drop the day.
func (q ListQuery) Spans() bool { return q.HasBefore || q.Since > 0 }

// ParseListQuery reads the params; a malformed one is an error (the routes answer 400).
func ParseListQuery(v url.Values) (ListQuery, error) {
	var q ListQuery
	if s := v.Get("limit"); s != "" {
		n, err := strconv.Atoi(s)
		if err != nil || n < 1 {
			return q, errors.New("bad limit")
		}
		if n > MaxListLimit {
			n = MaxListLimit
		}
		q.Limit = n
	}
	if s := v.Get("before"); s != "" {
		start, key, _ := strings.Cut(s, ",")
		n, err := strconv.ParseInt(start, 10, 64)
		if err != nil || n <= 0 || len(key) > 200 {
			return q, errors.New("bad before")
		}
		q.Before, q.BeforeKey, q.HasBefore = n, key, true
	}
	if s := v.Get("since"); s != "" {
		n, err := strconv.ParseInt(s, 10, 64)
		if err != nil || n <= 0 {
			return q, errors.New("bad since")
		}
		q.Since = n
	}
	flag := func(name string) (bool, error) {
		switch v.Get(name) {
		case "":
			return false, nil
		case "1", "true":
			return true, nil
		case "0", "false":
			return false, nil
		}
		return false, errors.New("bad " + name)
	}
	var err error
	if q.Boss, err = flag("boss"); err != nil {
		return q, err
	}
	if q.Deaths, err = flag("deaths"); err != nil {
		return q, err
	}
	if s := v.Get("outcome"); s != "" {
		q.Outcomes = map[string]bool{}
		for _, o := range strings.Split(s, ",") {
			o = strings.TrimSpace(o)
			if o == "" || len(o) > 32 {
				return q, errors.New("bad outcome")
			}
			q.Outcomes[o] = true
		}
	}
	return q, nil
}

// Match reports whether one row passes the filters and the time range (not the limit).
func (q ListQuery) Match(r FightSummary) bool {
	if q.HasBefore && !(r.Start < q.Before || (r.Start == q.Before && q.BeforeKey != "" && r.Key > q.BeforeKey)) {
		return false
	}
	if q.Since > 0 && r.Start < q.Since {
		return false
	}
	if q.Boss && r.BossName == "" {
		return false
	}
	if q.Deaths && r.Deaths <= 0 {
		return false
	}
	if len(q.Outcomes) > 0 && !q.Outcomes[r.Outcome] {
		return false
	}
	return true
}

// Apply filters rows (already newest first) and cuts them to the limit; more is true when the limit dropped rows. A live
// row (a fight still running) passes the time range and outcome filters only when it matches like any other row.
func (q ListQuery) Apply(rows []FightSummary) (out []FightSummary, more bool) {
	if !q.Active() {
		return rows, false
	}
	out = make([]FightSummary, 0, len(rows))
	for _, r := range rows {
		if !q.Match(r) {
			continue
		}
		if q.Limit > 0 && len(out) == q.Limit {
			return out, true
		}
		out = append(out, r)
	}
	return out, false
}
