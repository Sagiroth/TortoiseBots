"use strict";
// Raids tab: the pure logic of the live raid runs, free of the page so a test can run it under node
// (internal/commandersjs). A run is one raid instance copy; a group is one raid group inside it (group id,
// leader, offline members, live combat window) built from the roster by the daemon (internal/state/raids.go)
// and enriched by the RAIDS datagram. Depends on commanders.js (CmdView) and dungeons.js (DgnView) being
// loaded first.
(function (root) {
  // One raid group as the card/map machinery expects it: a pseudo-binding whose bots are the group's members
  // (roster rows plus the offline ones the RAIDS row names), exactly as DgnView.bindOf maps them, plus sub
  // for raidBlocks, alive from dead/offline, and the group's combat/run/mobs. `observed` marks the cards the
  // Commanders Live list must skip, so bound raids show in both tabs. The lead is the leader when a living
  // member carries that name, else the first living tank.
  function bindOf(run, group) {
    const r = run || {}, g = group || {};
    const members = g.members || [];
    const leader = members.find((m) => m && !m.offline && !m.dead && m.state !== "dead" && m.name && m.name === g.leader)
      || members.find((m) => m && m.role === "tank" && !m.dead && !m.offline && m.state !== "dead");
    const bind = {
      commander: g.key || ("raid#" + g.group),
      cardKey: "raid:" + (r.key || "") + ":" + (g.key || ("raid#" + g.group)),
      observed: true,
      raidKey: r.key || "",
      group: g.group || 0,
      lead: leader ? leader.name : "",
      online: true,
      bound: !!g.bound,
      raidCommander: g.commander || "",
      run: g.run || null,
      raidRun: r,
      bots: members.map((m) => {
        const bot = {
          name: m.name, guid: m.guid, player: m.player, class: m.class, role: m.role, level: m.level,
          hp: m.hp, max_hp: m.max_hp, power: m.power, max_power: m.max_power, power_type: m.power_type,
          map: m.map == null ? r.map : m.map, zone: m.zone, x: m.x, y: m.y, z: m.z, o: m.o,
          alive: !m.dead && !m.offline, state: m.offline ? "offline" : m.state,
          sub: typeof m.subgroup === "number" ? m.subgroup : (typeof m.sub === "number" ? m.sub : undefined),
          offline: !!m.offline,
        };
        if (m.id) bot.id = m.id;
        if (m.target) bot.target = m.target;
        return bot;
      }),
    };
    if (g.combat) bind.combat = g.combat;
    if (Array.isArray(r.mobs)) {
      bind.mobs = r.mobs;
      bind.mobs_map = r.mobs_map;
      bind.mobs_total = r.mobs_total;
    }
    return bind;
  }

  // The list rows in display order: fighting first (a combat window or a member in combat), then by key.
  function listRows(run) {
    const groups = ((run && run.groups) || []).slice();
    const fighting = (g) => !!(g && (g.combat || (g.members || []).some((m) => m && m.state === "combat")));
    groups.sort((a, b) => {
      const fa = fighting(a) ? 0 : 1, fb = fighting(b) ? 0 : 1;
      if (fa !== fb) return fa - fb;
      const ka = String(a && a.key || ""), kb = String(b && b.key || "");
      return ka < kb ? -1 : ka > kb ? 1 : 0;
    });
    return groups;
  }

  // The short state of a group: fighting when the combat window or a member says so, wiped when all dead, else idle.
  function stateWord(group) {
    const members = (group && group.members) || [];
    if (!members.length && !(group && group.combat)) return "";
    if (group && group.combat) return "fighting";
    if (members.some((m) => m && m.state === "combat" && !m.dead && !m.offline)) return "fighting";
    if (members.length && members.every((m) => !m || m.dead || m.offline || m.state === "dead")) return "wiped";
    return "idle";
  }

  // The raid dps/hps summed over the combat window's bots.
  function windowTotals(combat) {
    let dps = 0, hps = 0;
    ((combat && combat.bots) || []).forEach((b) => {
      dps += (b && typeof b.dps === "number" && isFinite(b.dps)) ? b.dps : 0;
      hps += (b && typeof b.hps === "number" && isFinite(b.hps)) ? b.hps : 0;
    });
    return { dps, hps };
  }

  const RaidView = { bindOf, listRows, stateWord, windowTotals };

  if (typeof module !== "undefined" && module.exports) module.exports = RaidView;
  else root.RaidView = RaidView;
})(typeof self !== "undefined" ? self : this);
