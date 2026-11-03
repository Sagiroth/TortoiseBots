package armory

import "testing"

// TestSpecsForWithoutDB pins the empty answers: an armory service with no
// configured DBC directory must answer no spec rather than query a nil DB.
func TestSpecsForWithoutDB(t *testing.T) {
	if got := (&Service{}).SpecsFor([]uint32{1}); len(got) != 0 {
		t.Fatalf("SpecsFor = %v, want an empty map", got)
	}
	var nilService *Service
	if got := nilService.SpecsFor([]uint32{1}); len(got) != 0 {
		t.Fatalf("nil service SpecsFor = %v, want an empty map", got)
	}
	if got := (&Service{}).SpecsFor(nil); len(got) != 0 {
		t.Fatalf("SpecsFor(nil) = %v, want an empty map", got)
	}
}
