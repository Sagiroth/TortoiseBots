package zoneproject

import (
	"encoding/json"
	"fmt"
	"sync"

	"tortoise-observability/internal/model"
)

// Engine handles mapping 3D world coordinates (X, Y, Z) to 2D zone map percentages.
type Engine struct {
	mu        sync.RWMutex
	zones     map[string]model.ZoneBoundingBox // key: "{map_id}_{area_id}"
	byAreaID  map[uint32]model.ZoneBoundingBox
	byName    map[string]model.ZoneBoundingBox
}

// New creates an Engine initialized with zone bounding box data.
func New(jsonData []byte) (*Engine, error) {
	var raw map[string]model.ZoneBoundingBox
	if err := json.Unmarshal(jsonData, &raw); err != nil {
		return nil, fmt.Errorf("failed to parse zone data: %w", err)
	}

	eng := &Engine{
		zones:    make(map[string]model.ZoneBoundingBox),
		byAreaID: make(map[uint32]model.ZoneBoundingBox),
		byName:   make(map[string]model.ZoneBoundingBox),
	}

	for key, z := range raw {
		eng.zones[key] = z
		eng.byAreaID[z.AreaID] = z
		eng.byName[z.Name] = z
	}

	return eng, nil
}

// Project converts world coords (x, y) for a given mapID and zoneID into map percentages (0%..100%).
func (e *Engine) Project(mapID, zoneID uint32, x, y float64) (pctX, pctY float64, ok bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	key := fmt.Sprintf("%d_%d", mapID, zoneID)
	box, found := e.zones[key]
	if !found {
		// Fallback: lookup by zoneID only
		box, found = e.byAreaID[zoneID]
	}

	if !found || box.LocLeft == box.LocRight || box.LocTop == box.LocBottom {
		return 0, 0, false
	}

	// Formula from Spec 3.3.2:
	// pctX = ((locLeft - Y) / (locLeft - locRight)) * 100%
	// pctY = ((locTop - X) / (locTop - locBottom)) * 100%
	pctX = ((box.LocLeft - y) / (box.LocLeft - box.LocRight)) * 100.0
	pctY = ((box.LocTop - x) / (box.LocTop - box.LocBottom)) * 100.0

	return pctX, pctY, true
}

// GetZoneBox returns bounding box by map and zone ID.
func (e *Engine) GetZoneBox(mapID, zoneID uint32) (model.ZoneBoundingBox, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	key := fmt.Sprintf("%d_%d", mapID, zoneID)
	b, ok := e.zones[key]
	if !ok {
		b, ok = e.byAreaID[zoneID]
	}
	return b, ok
}

// GetAllZones returns a list of all loaded zones.
func (e *Engine) GetAllZones() []model.ZoneBoundingBox {
	e.mu.RLock()
	defer e.mu.RUnlock()

	list := make([]model.ZoneBoundingBox, 0, len(e.zones))
	for _, z := range e.zones {
		list = append(list, z)
	}
	return list
}
