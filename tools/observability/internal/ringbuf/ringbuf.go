package ringbuf

import (
	"sync"
	"time"

	"tortoise-observability/internal/model"
)

// RingBuffer stores the last N anomaly events in RAM.
type RingBuffer struct {
	mu       sync.RWMutex
	capacity int
	events   []model.AnomalyPayload
	cursor   int
	full     bool
	nextID   int64
}

// New creates a RingBuffer with the given capacity.
func New(capacity int) *RingBuffer {
	if capacity <= 0 {
		capacity = 1000
	}
	return &RingBuffer{
		capacity: capacity,
		events:   make([]model.AnomalyPayload, capacity),
	}
}

// Add pushes an event into the ring buffer, overwriting the oldest when full.
func (r *RingBuffer) Add(e model.AnomalyPayload) model.AnomalyPayload {
	r.mu.Lock()
	defer r.mu.Unlock()

	r.nextID++
	e.ID = r.nextID
	if e.TS == 0 {
		e.TS = time.Now().Unix()
	}
	e.TimeStr = time.Unix(e.TS, 0).Format("2006-01-02 15:04:05")
	e.ReceivedAt = time.Now()

	r.events[r.cursor] = e
	r.cursor = (r.cursor + 1) % r.capacity
	if r.cursor == 0 {
		r.full = true
	}
	return e
}

// GetAll returns all stored events in chronological order (oldest to newest).
func (r *RingBuffer) GetAll() []model.AnomalyPayload {
	r.mu.RLock()
	defer r.mu.RUnlock()

	var result []model.AnomalyPayload
	if !r.full {
		result = make([]model.AnomalyPayload, r.cursor)
		copy(result, r.events[:r.cursor])
		return result
	}

	result = make([]model.AnomalyPayload, r.capacity)
	n := copy(result, r.events[r.cursor:])
	copy(result[n:], r.events[:r.cursor])
	return result
}

// GetRecent returns the last N events (newest first).
func (r *RingBuffer) GetRecent(limit int, typeFilter, severityFilter string) []model.AnomalyPayload {
	all := r.GetAll()
	var filtered []model.AnomalyPayload

	// Iterate in reverse (newest first)
	for i := len(all) - 1; i >= 0; i-- {
		e := all[i]
		if typeFilter != "" && e.Type != typeFilter {
			continue
		}
		if severityFilter != "" && e.Severity != severityFilter {
			continue
		}
		filtered = append(filtered, e)
		if limit > 0 && len(filtered) >= limit {
			break
		}
	}
	return filtered
}

// Count returns the number of events currently in the buffer.
func (r *RingBuffer) Count() int {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if r.full {
		return r.capacity
	}
	return r.cursor
}

// Clear resets the buffer.
func (r *RingBuffer) Clear() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.cursor = 0
	r.full = false
	r.events = make([]model.AnomalyPayload, r.capacity)
}
