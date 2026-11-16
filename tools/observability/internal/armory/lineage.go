package armory

import "log"

// LineageInfo is the character's line of lineage alts: the level 1 alts level
// 60 pool bots rolled (tortoise_bots_lineage). Nil on the profile when the
// character neither rolled an alt nor is one.
type LineageInfo struct {
	ParentGUID  uint32          `json:"parent_guid,omitempty"`
	ParentName  string          `json:"parent_name,omitempty"`
	FounderGUID uint32          `json:"founder_guid"`
	Members     []LineageMember `json:"members"`
}

// LineageMember is one character of the line, founder first. Created and
// Reached60 are unix seconds (0 = unknown or not yet); a member whose
// character no longer exists keeps its name and class from creation.
type LineageMember struct {
	GUID       uint32 `json:"guid"`
	ParentGUID uint32 `json:"parent_guid,omitempty"`
	Name       string `json:"name"`
	Race       uint32 `json:"race"`
	Class      uint32 `json:"class"`
	Level      uint32 `json:"level"`
	Created    int64  `json:"created,omitempty"`
	Reached60  int64  `json:"reached_60,omitempty"`
	Online     bool   `json:"online"`
	LastOnline int64  `json:"last_online,omitempty"`
	Gone       bool   `json:"gone,omitempty"`
}

// lineage reads the small lineage table and the members' character rows by
// primary key. It never joins the large character tables, and an older
// server without the table simply has no lineage.
func (s *Service) lineage(guid uint32) *LineageInfo {
	founder := guid
	var parent uint32
	err := s.db.QueryRow("SELECT founder_guid, parent_guid FROM tortoise_bots_lineage WHERE guid = ?", guid).Scan(&founder, &parent)
	if err != nil {
		founder, parent = guid, 0
	}
	rows, err := s.db.Query(`SELECT guid, parent_guid, name, race, class, created_at, reached_60_at
		FROM tortoise_bots_lineage WHERE founder_guid = ? ORDER BY created_at, guid`, founder)
	if err != nil {
		return nil
	}
	members := []LineageMember{{GUID: founder}}
	for rows.Next() {
		var m LineageMember
		if err := rows.Scan(&m.GUID, &m.ParentGUID, &m.Name, &m.Race, &m.Class, &m.Created, &m.Reached60); err != nil {
			rows.Close()
			log.Printf("[Armory] lineage guid=%d: %v", guid, err)
			return nil
		}
		members = append(members, m)
	}
	rows.Close()
	if len(members) < 2 {
		return nil
	}

	ids := make([]uint32, len(members))
	for i, m := range members {
		ids[i] = m.GUID
	}
	ph, args := inList(ids)
	crows, err := s.db.Query("SELECT guid, name, race, class, level, online, logout_time FROM characters WHERE guid IN ("+ph+") AND deleteDate IS NULL", args...)
	if err != nil {
		log.Printf("[Armory] lineage members guid=%d: %v", guid, err)
		return nil
	}
	type row struct {
		name               string
		race, class, level uint32
		online             bool
		logout             int64
	}
	found := map[uint32]row{}
	for crows.Next() {
		var g uint32
		var r row
		var online uint8
		if err := crows.Scan(&g, &r.name, &r.race, &r.class, &r.level, &online, &r.logout); err != nil {
			crows.Close()
			log.Printf("[Armory] lineage members guid=%d: %v", guid, err)
			return nil
		}
		r.online = online != 0
		found[g] = r
	}
	crows.Close()

	info := &LineageInfo{FounderGUID: founder, ParentGUID: parent}
	for i := range members {
		m := &members[i]
		if r, ok := found[m.GUID]; ok {
			m.Name, m.Race, m.Class, m.Level, m.Online, m.LastOnline = r.name, r.race, r.class, r.level, r.online, r.logout
		} else {
			m.Gone = true
		}
		if m.GUID == parent {
			info.ParentName = m.Name
		}
	}
	info.Members = members
	return info
}
