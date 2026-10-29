package state

import "tortoise-observability/internal/model"

// ApplyCommanderPos merges one COMMANDER_POS datagram into the standing
// bindings: the bots and enemies are matched by guid (a bot) and by id (a
// mob), and anything the last full report does not carry is ignored (new
// units arrive with the next report).
//
// The bindings handed out by Commanders() share their bots and mobs slices,
// so the merge copies the slices of the binding it changes and replaces the
// binding in the list: a view taken before keeps the positions it was given.
// Trails, tracks and the report's own age are left alone (the fight replay
// keeps its 2 s frames).
func (s *Store) ApplyCommanderPos(p *model.CommanderPosPayload) (model.CommanderPosFrame, bool) {
	frame := model.CommanderPosFrame{}
	if p == nil || p.FMT != 1 {
		return frame, false
	}
	st := &s.commanders
	st.mu.Lock()
	defer st.mu.Unlock()
	if p.Session < st.session {
		return frame, false
	}
	// Older than the full report the positions would be merged into, or a
	// datagram that overtook a newer one of the same commander.
	if p.Session == st.session && p.TSMs < st.tsMs {
		return frame, false
	}
	if p.TSMs <= st.posTs[p.Commander] {
		return frame, false
	}
	i := -1
	for j := range st.list {
		if st.list[j].Commander == p.Commander {
			i = j
			break
		}
	}
	if i < 0 {
		return frame, false
	}

	bots := p.Bots
	if len(bots) > maxCommanderBots {
		bots = bots[:maxCommanderBots]
	}
	mobs := p.Mobs
	if len(mobs) > maxCommanderMobs {
		mobs = mobs[:maxCommanderMobs]
	}

	b := st.list[i]
	byGUID := make(map[uint32]int, len(bots))
	for k := range bots {
		byGUID[bots[k].G] = k
	}
	nb := append([]model.CommanderBot(nil), b.Bots...)
	for k := range nb {
		idx, ok := byGUID[nb[k].GUID]
		if !ok {
			continue
		}
		pb := bots[idx]
		nb[k].X, nb[k].Y, nb[k].Z = pb.X, pb.Y, pb.Z
		nb[k].HP, nb[k].MaxHP = pb.HP, pb.MHP
		nb[k].Alive = pb.A == 1
		nb[k].MapID = pb.M
	}
	byID := make(map[string]int, len(mobs))
	for k := range mobs {
		byID[mobs[k].ID] = k
	}
	nm := append([]model.CommanderMob(nil), b.Mobs...)
	for k := range nm {
		idx, ok := byID[nm[k].ID]
		if !ok {
			continue
		}
		pm := mobs[idx]
		nm[k].X, nm[k].Y, nm[k].HP, nm[k].Victim = pm.X, pm.Y, pm.HP, pm.V
		if pm.O != nil {
			facing := *pm.O
			nm[k].O = &facing
		}
	}
	b.Bots, b.Mobs = nb, nm
	st.list[i] = b
	if st.posTs == nil {
		st.posTs = map[string]int64{}
	}
	st.posTs[p.Commander] = p.TSMs

	return model.CommanderPosFrame{Commander: p.Commander, TSMs: p.TSMs, Combat: p.Combat, Bots: bots, Mobs: mobs}, true
}
