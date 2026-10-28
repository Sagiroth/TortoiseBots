"use strict";
// Aggro lanes: the pure logic behind the Fight debugger's "who each enemy targeted" strip. It reads the daemon's
// track answer (GET /api/v1/commanders/track) and one finished fight, and turns the sampled mob victims into one lane
// per engaged enemy: tank, healer, damage dealer, other, dead or none, in milliseconds from the fight's start. A lane
// segment lasts until the next report of that enemy, capped at HOLD_S so a stale report cannot paint the rest of the
// fight. No DOM, no fetch: unit-tested by internal/commandersjs/aggrolanes_check.js under node.
(function (root) {
  const MAX_LANES = 12;
  const HOLD_S = 3;

  const isNum = (v) => typeof v === "number" && isFinite(v);

  function roleKind(role) {
    const r = String(role || "");
    if (r === "tank") return "tank";
    if (r === "healer" || r === "heal") return "healer";
    if (r === "dps") return "dps";
    return "other";
  }

  function botMaps(fight, bindBots) {
    const fromFight = new Map();
    for (const b of (fight && Array.isArray(fight.bots) ? fight.bots : [])) {
      if (b && b.guid != null) fromFight.set(String(b.guid), b);
    }
    const fromBind = new Map();
    for (const b of (Array.isArray(bindBots) ? bindBots : [])) {
      if (b && b.guid != null) fromBind.set(String(b.guid), b);
    }
    return { fromFight, fromBind };
  }

  // The kind and the victim of one mob report: a kill is dead, then a bound bot's role, then anything else it faces.
  function kindOf(mob, maps) {
    if (isNum(mob.hp) && mob.hp === 0) return { kind: "dead", victim: "" };
    if (mob.v != null && mob.v !== 0 && mob.v !== "") {
      const key = String(mob.v);
      const b = maps.fromFight.get(key) || maps.fromBind.get(key);
      if (b) return { kind: roleKind(b.role), victim: b.name ? String(b.name) : "" };
      return { kind: "other", victim: "" };
    }
    if (mob.vo) return { kind: "other", victim: "" };
    return { kind: "none", victim: "" };
  }

  function lanes(json, fight, bindBots) {
    fight = fight || {};
    const all = (json && Array.isArray(json.frames) ? json.frames : [])
      .filter((f) => f && isNum(f.t))
      .sort((a, b) => a.t - b.t);
    const startS = isNum(fight.start) ? fight.start : 0;
    const durMs = isNum(fight.dur_ms) && fight.dur_ms > 0 ? fight.dur_ms : 0;
    const endS = startS + durMs / 1000;

    // One frame before the start (the last one) closes the first segment; nothing after the end is used.
    let edge = -1;
    for (let i = 0; i < all.length; i++) {
      if (all[i].t < startS) edge = i;
    }
    const frames = [];
    for (let i = 0; i < all.length; i++) {
      const f = all[i];
      if (f.t <= endS && (f.t >= startS || i === edge)) frames.push(f);
    }

    const engaged = new Set();
    for (const f of frames) {
      if (f.t < startS || f.t > endS) continue;
      for (const mob of (Array.isArray(f.mobs) ? f.mobs : [])) {
        if (!mob || mob.id == null || mob.id === "") continue;
        if (mob.v || mob.vo || (isNum(mob.hp) && mob.hp < 100)) engaged.add(String(mob.id));
      }
    }

    const maps = botMaps(fight, bindBots);
    const names = (json && json.names) || {};
    const byId = new Map();
    for (let i = 0; i < frames.length; i++) {
      const f = frames[i];
      const nextT = i + 1 < frames.length ? frames[i + 1].t : null;
      for (const mob of (Array.isArray(f.mobs) ? f.mobs : [])) {
        if (!mob || mob.id == null) continue;
        const id = String(mob.id);
        if (!engaged.has(id)) continue;
        let lane = byId.get(id);
        if (!lane) {
          lane = { id, e: mob.e, boss: false, segs: [] };
          byId.set(id, lane);
        }
        if (mob.rk === "b") lane.boss = true;
        const fromMs = Math.max(0, (f.t - startS) * 1000);
        const endT = nextT == null ? f.t + 2 : Math.min(nextT, f.t + HOLD_S);
        const toMs = Math.min(durMs, (endT - startS) * 1000);
        if (!(toMs > fromMs)) continue;
        const k = kindOf(mob, maps);
        const seg = { fromMs, toMs, kind: k.kind, bad: k.kind === "healer" || k.kind === "dps", victim: k.victim };
        const prev = lane.segs[lane.segs.length - 1];
        if (prev && prev.kind === seg.kind && prev.victim === seg.victim && prev.toMs === seg.fromMs) {
          prev.toMs = seg.toMs;
        } else {
          lane.segs.push(seg);
        }
      }
    }

    const sorted = [...byId.values()].sort((a, b) =>
      (b.boss - a.boss) || (a.segs[0].fromMs - b.segs[0].fromMs) || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));

    const out = sorted.map((lane) => {
      let badMs = 0;
      const byKind = { tank: 0, healer: 0, dps: 0, other: 0 };
      const badNames = [];
      for (const s of lane.segs) {
        const ms = s.toMs - s.fromMs;
        if (s.kind in byKind) byKind[s.kind] += ms;
        if (s.bad) {
          badMs += ms;
          if (s.victim && badNames.indexOf(s.victim) < 0) badNames.push(s.victim);
        }
      }
      const total = byKind.tank + byKind.healer + byKind.dps + byKind.other;
      return {
        id: lane.id,
        e: lane.e,
        name: names[lane.e] || "enemy",
        label: "",
        boss: lane.boss,
        segs: lane.segs,
        badMs,
        onTankPct: total > 0 ? Math.round((byKind.tank / total) * 100) : 0,
        badNames,
      };
    });

    const kept = out.slice(0, MAX_LANES);
    const perEntry = new Map();
    for (const lane of kept) perEntry.set(lane.e, (perEntry.get(lane.e) || 0) + 1);
    kept.forEach((lane, i) => {
      lane.label = perEntry.get(lane.e) > 1 ? lane.name + " " + (i + 1) : lane.name;
    });
    return { lanes: kept, more: out.length - kept.length, frames: frames.length };
  }

  const api = { MAX_LANES, HOLD_S, lanes };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.AggroLanes = api;
})(typeof self !== "undefined" ? self : this);
