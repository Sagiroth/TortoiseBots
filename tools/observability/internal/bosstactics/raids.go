package bosstactics

import (
	_ "embed"
	"encoding/csv"
	"fmt"
	"sort"
	"strconv"
	"strings"
)

// raidsCSV is a copy of data/boss/raids.csv: the dashboard binary ships
// without the repository, so the raid maps the coverage view covers are
// embedded. A test keeps the copy equal to the committed file.
//
//go:embed raids.csv
var raidsCSV string

// RaidMaps returns the raid map ids in raids.csv, sorted and deduplicated.
func RaidMaps() ([]uint32, error) {
	records, err := csv.NewReader(strings.NewReader(raidsCSV)).ReadAll()
	if err != nil {
		return nil, fmt.Errorf("raids.csv: %w", err)
	}
	if len(records) == 0 {
		return nil, fmt.Errorf("raids.csv: no header")
	}
	column := -1
	for i, name := range records[0] {
		if strings.TrimSpace(name) == "map_id" {
			column = i
			break
		}
	}
	if column < 0 {
		return nil, fmt.Errorf("raids.csv: no map_id column")
	}
	seen := make(map[uint32]bool, len(records)-1)
	out := make([]uint32, 0, len(records)-1)
	for _, record := range records[1:] {
		if column >= len(record) {
			return nil, fmt.Errorf("raids.csv: row %q has no map_id", strings.Join(record, ","))
		}
		n, err := strconv.ParseUint(strings.TrimSpace(record[column]), 10, 32)
		if err != nil || n == 0 {
			return nil, fmt.Errorf("raids.csv: bad map_id %q", record[column])
		}
		id := uint32(n)
		if seen[id] {
			continue
		}
		seen[id] = true
		out = append(out, id)
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out, nil
}

// mapFilterMarker is where queryWithMaps puts the map filter: between the
// table name and the GROUP BY/ORDER BY of each query.
const mapFilterMarker = "/*maps*/"

// queryWithMaps builds one of the three queries filtered to maps. The ids are
// placeholders, never SQL text; no maps leaves the query unfiltered (the
// embedded list could not be read, and the LIMIT stays the guard).
func queryWithMaps(base string, maps []uint32) (string, []any) {
	if len(maps) == 0 {
		return strings.Replace(base, mapFilterMarker, "", 1), nil
	}
	holders := make([]string, len(maps))
	args := make([]any, len(maps))
	for i, id := range maps {
		holders[i] = "?"
		args[i] = id
	}
	clause := " WHERE `map` IN (" + strings.Join(holders, ", ") + ")"
	return strings.Replace(base, mapFilterMarker, clause, 1), args
}
