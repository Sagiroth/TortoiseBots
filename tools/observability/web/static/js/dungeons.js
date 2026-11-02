"use strict";
// Dungeons tab: the pure logic of the live dungeon runs (issue #895), free of the page so a test can run it under node
// (internal/commandersjs). A run is one dungeon copy (map + instance id) with at least one bot in it (raid copies live on the Raids tab); the daemon
// does the grouping (internal/state/dungeons.go) and serves it at /api/v1/dungeons. app.js draws the list and the cards; this file
// holds the wording and the party layout. Depends on commanders.js (CmdView) being loaded first.
(function (root) {
  // A run with more members than a party (a raid of 10+) is drawn as a compact subgroup list, not 40 full rows.
  const RAID_MIN_MEMBERS = 11;
  // Rows per subgroup block in a raid.
  const RAID_GROUP_SIZE = 5;
  const pct = (a, b) => (b > 0 ? Math.max(0, Math.min(100, (a / b) * 100)) : 0);
  const cap = (s) => (s ? s[0].toUpperCase() + s.slice(1) : "");
  const pretty = (s) => cap(String(s || "").replace(/[_\-]+/g, " ").trim());

  // tank | healer | dps | other, and dead for a unit that is down (the colour then is grey).
  function roleKey(m) {
    if (!m || m.dead || m.state === "dead") return "dead";
    return m.role === "tank" || m.role === "healer" || m.role === "dps" ? m.role : "other";
  }
  function roleLabel(m) {
    if (m && m.player) return "Player";
    const r = m && m.role;
    return r === "dps" ? "DPS" : r ? cap(r) : "Bot";
  }

  // The copy labels of a page of runs: two groups in one dungeon read "Ragefire Chasm · copy 1 / copy 2"
  // (ordered by instance id, the order the daemon reports them in), a lone group reads by name alone.
  // Returns run key -> copy number (1-based), or null when there is nothing to mark apart.
  function copyLabels(runs) {
    const byMap = new Map();
    (runs || []).forEach((r) => {
      const list = byMap.get(r.map) || [];
      list.push(r);
      byMap.set(r.map, list);
    });
    const out = {};
    byMap.forEach((list) => {
      if (list.length < 2) return;
      list.slice()
        .sort((a, b) => (a.instance || 0) - (b.instance || 0))
        .forEach((r, i) => { out[r.key] = i + 1; });
    });
    return out;
  }

  // The short state of a run, worst first: someone dead > fighting > moving > busy > resting > idle.
  function runState(run) {
    const members = (run && run.members) || [];
    const has = (s) => members.some((m) => m.state === s);
    if (members.some((m) => m.dead || m.state === "dead")) return "someone dead";
    if (has("combat")) return "fighting";
    if (has("moving")) return "moving";
    if (has("busy") || has("stalled")) return "busy";
    if (has("resting")) return "resting";
    return members.length ? "idle" : "";
  }

  // How long a run has been seen ("2 min", "1 h"): the same words as every other age on the page.
  function seenText(sec) {
    if (typeof CmdView === "undefined" || !CmdView.ageText) return sec == null ? "" : Math.round(sec) + " s";
    return CmdView.ageText(sec);
  }

  // The power bar of one member: null when the row has none to draw — dead, a module that reports
  // no power (no max_power) or a class with no resource (max_power 0) — else the Bots tab's fields:
  // the percentage, the bar kind (mana blue, rage red, energy yellow; anything else reads mana) and
  // the "current / max" words. A resource that really is 0 still draws, as long as max_power > 0.
  function powerOf(m) {
    if (!m || m.dead || m.state === "dead" || !(m.max_power > 0)) return null;
    const kind = m.power_type === "rage" || m.power_type === "energy" ? m.power_type : "mana";
    return { power: Math.round(pct(m.power || 0, m.max_power)), kind, text: (m.power || 0) + " / " + m.max_power };
  }

  // The party as rows: the real player(s) first (marked player), then by raid subgroup, then by name —
  // the order the daemon reports and this tab repeats.
  function memberRows(run) {
    const rows = ((run && run.members) || []).map((m) => {
      const pw = powerOf(m);
      return {
        name: m.name, guid: m.guid || 0, player: !!m.player, dead: !!m.dead,
        role: roleKey(m), roleLabel: roleLabel(m), cls: pretty(m.class), level: m.level || 0,
        hp: Math.round(pct(m.hp, m.max_hp)), hpText: m.hp + " / " + m.max_hp,
        hasPower: !!pw, power: pw ? pw.power : 0,
        powerKind: pw ? pw.kind : "mana", powerText: pw ? pw.text : "",
        subgroup: m.subgroup || 0, state: m.state || "",
      };
    });
    rows.sort((a, b) => (a.player ? -1 : 0) - (b.player ? -1 : 0) || a.subgroup - b.subgroup || (a.name < b.name ? -1 : 1));
    return rows;
  }

  // A raid (10+ members) reads as compact subgroup blocks (5 rows each, grouped by subgroup when the
  // module reports it, else chunks of 5 in the order above); a party is one unlabelled group.
  function memberGroups(rows) {
    if (!rows || rows.length < RAID_MIN_MEMBERS) return [{ label: "", rows }];
    const bySub = rows.some((r) => r.subgroup > 0);
    const groups = [];
    if (bySub) {
      rows.forEach((r) => {
        let g = groups.find((x) => x.subgroup === r.subgroup);
        if (!g) { g = { label: "Group " + r.subgroup, subgroup: r.subgroup, rows: [] }; groups.push(g); }
        g.rows.push(r);
      });
      groups.sort((a, b) => a.subgroup - b.subgroup);
      return groups;
    }
    for (let i = 0; i < rows.length; i += RAID_GROUP_SIZE)
      groups.push({ label: "Group " + (groups.length + 1), subgroup: 0, rows: rows.slice(i, i + RAID_GROUP_SIZE) });
    return groups;
  }

  // A run as the card/map machinery expects it: a pseudo-binding whose bots are the run's members (bots and the real player,
  // alive from the dead flag). `dungeon` marks it so the Commanders render never picks it up. The members' id/target (raw
  // guids as decimal strings) ride along for the threat and bot-target layers; the run's mobs ride along only when the daemon
  // reported a list (an array; absent means an older module, where the layers stay greyed). The lead is the first living tank,
  // so the lead mark and the aggro radius have a bot and a level on a dungeon card.
  function bindOf(run) {
    const r = run || {};
    const members = r.members || [];
    const lead = members.find((m) => m && m.role === "tank" && !m.dead && m.state !== "dead");
    const bind = {
      commander: "dgn:" + r.key,
      dungeon: true,
      dgn: r,
      online: true, group: 0, lead: lead ? lead.name : "",
      bots: members.map((m) => {
        const bot = {
          name: m.name, guid: m.guid, player: m.player, class: m.class, role: m.role, level: m.level,
          hp: m.hp, max_hp: m.max_hp, power: m.power, max_power: m.max_power, power_type: m.power_type,
          map: m.map, zone: m.zone, x: m.x, y: m.y, z: m.z, o: m.o,
          alive: !m.dead, state: m.state,
        };
        if (m.id) bot.id = m.id;
        if (m.target) bot.target = m.target;
        return bot;
      }),
    };
    if (Array.isArray(r.mobs)) {
      bind.mobs = r.mobs;
      bind.mobs_map = r.mobs_map;
      bind.mobs_total = r.mobs_total;
    }
    return bind;
  }

  const DgnView = {
    RAID_MIN_MEMBERS, RAID_GROUP_SIZE,
    roleKey, roleLabel, copyLabels, runState, seenText, powerOf, memberRows, memberGroups, bindOf,
  };

  if (typeof module !== "undefined" && module.exports) module.exports = DgnView;
  else root.DgnView = DgnView;
})(this);
