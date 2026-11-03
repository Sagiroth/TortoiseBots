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
      run: g.run || null,
      layout: g.layout || null,
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
        if (m.st != null) bot.st = m.st;
        if (m.act != null) bot.act = m.act;
        if (m.res != null) bot.res = m.res;
        if (m.cast != null) bot.cast = m.cast;
        if (m.ctgt != null) bot.ctgt = m.ctgt;
        if (m.cc != null) bot.cc = m.cc;
        if (m.rest != null) bot.rest = m.rest;
        if (m.mv != null) bot.mv = m.mv;
        if (m.tname != null) bot.tname = m.tname;
        if (m.rdy != null) bot.rdy = m.rdy;
        if (m.db != null) bot.db = m.db;
        if (m.mc != null) bot.mc = m.mc;
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

  // The target's name for a party row: the mob's name when the bind's mob
  // list names the target guid, else the activity's own target name (G1's
  // extras), else the raw guid.
  function targetName(bind, bot) {
    const b = bot || {};
    const mobs = (bind && Array.isArray(bind.mobs)) ? bind.mobs : [];
    if (b.target) {
      const mob = mobs.find((m) => m && m.id === b.target);
      if (mob && mob.name) return mob.name;
    }
    if (b.tname) return b.tname;
    return b.target || '';
  }

  // The fight the inline sections show: newest finished, else the live one
  // behind cmdFightsCache, else null.
  function newestFight(bind, fightsCache) {
    const list = fightsCache && fightsCache[bind && bind.commander] && fightsCache[bind.commander].list;
    if (!Array.isArray(list) || !list.length) return null;
    return list[0] || null;
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

  const RaidView = { bindOf, listRows, stateWord, windowTotals, targetName, newestFight };

  if (typeof module !== "undefined" && module.exports) module.exports = RaidView;
  else root.RaidView = RaidView;
})(typeof self !== "undefined" ? self : this);
