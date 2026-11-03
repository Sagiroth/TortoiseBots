package armory

// SpecsFor names the talent specs of the given characters: guid -> non-empty
// spec. A bot without talent ranks, a missing DBC directory or a failed query
// gives no entry, so the caller can leave the spec out of its row. Read-only.
func (s *Service) SpecsFor(guids []uint32) map[uint32]string {
	out := map[uint32]string{}
	if s == nil || len(guids) == 0 {
		return out
	}
	bots := make([]BotSummary, len(guids))
	for i, guid := range guids {
		bots[i] = BotSummary{GUID: guid}
	}
	s.resolveBotSpecs(bots)
	for _, b := range bots {
		if b.Spec != "" {
			out[b.GUID] = b.Spec
		}
	}
	return out
}
