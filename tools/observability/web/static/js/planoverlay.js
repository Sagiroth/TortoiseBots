"use strict";
// Commanders map: the run leader's plan as map layers — the route (done and ahead), its steps and stands, the pack
// mobs of each pull/boss step, the aggro ring around each mob and the scripted wake ring a boss has (the plan's
// wake_yd, from the daemon's wake table). Strays are move points that pass inside a later pack's ring or a wake ring.
// Works for any raid plan: many legs, many bosses, many packs.
//
// One plan is drawn on the map of its own card: view() returns the plain data app.js renders, all numbers raw (the SVG
// builder formats them). The steps, rings and mobs keep their z so the page can draw only the floor the map shows.
// No DOM, no fetch, no markup: unit-tested by internal/commandersjs/planoverlay_check.js under node.
(function (root) {
  const AGGRO_MAX_LEVEL_DIFF = 25;  // the level difference the core's GetAttackDistance clamps at -25
  const AGGRO_MIN_REACH_YD = 5;     // ... and the floor it keeps, capped by the detection range
  const PARTY_LEVEL_DEFAULT = 60;   // used when the binding reports no level (an unknown party is a level 60 one)
  const MOVE_KINDS = { move: 1, move_direct: 1, move_hop_exact: 1 };
  const NO_POINT_KINDS = { use_item: 1, rest: 1 };
  const PACK_KINDS = { pull: 1, boss: 1 };

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const list = (v) => (Array.isArray(v) ? v : []);
  const wakeOf = (mob) => {
    const w = mob && num(mob.wake_yd);
    return w !== null && w > 0 ? w : 0;
  };
  const dist3 = (a, b) => {
    const dx = a.x - b.x, dy = a.y - b.y, dz = (a.z || 0) - (b.z || 0);
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
  };

  // The aggro radius the run leader uses (ai::AggroRadius, a copy of the core's Creature::GetAttackDistance): the
  // detection range minus the party-level difference, that difference clamped at -25, the result floored at
  // min(detection, 5); 0 for a NO_AGGRO unit or a detection range under 1. The lowest party level gives the widest ring.
  function aggroYd(mob, partyLevel) {
    const m = mob || {};
    const det = num(m.det);
    if (m.noaggro || det === null || det < 1) return 0;
    const party = num(partyLevel) !== null && partyLevel > 0 ? partyLevel : PARTY_LEVEL_DEFAULT;
    const lvl = num(m.lvl) || 0;
    const reach = det - Math.max(party - lvl, -AGGRO_MAX_LEVEL_DIFF);
    return Math.max(reach, Math.min(det, AGGRO_MIN_REACH_YD));
  }

  // The ring a mob gets: its scripted wake when the plan reports one, else its aggro radius.
  function ringYd(mob, partyLevel) {
    const wake = wakeOf(mob);
    return wake > 0 ? wake : aggroYd(mob, partyLevel);
  }

  function wakeText(yd) {
    return "wakes at ~" + Math.round(num(yd) || 0) + " yd (scripted)";
  }

  // The party's level: the lowest a bot reports (the widest ring), else the default.
  function partyLevel(bind) {
    let low = 0;
    list(bind && bind.bots).forEach((b) => {
      const lvl = b && num(b.level);
      if (lvl !== null && lvl > 0 && (!low || lvl < low)) low = lvl;
    });
    return low || PARTY_LEVEL_DEFAULT;
  }

  // The run numbers the plan the server loaded only: a run whose step count differs from the plan's kept steps cannot
  // line its step up with these rows.
  function numberingOk(plan, run) {
    if (!run || !run.steps) return true;
    return !!plan && run.steps === plan.steps_total;
  }

  // Every step's state for one current step: before it done, it cur, after it todo; cur 0 (or none) leaves all todo.
  function states(plan, cur) {
    const out = {}, c = num(cur) || 0;
    list(plan && plan.steps).forEach((s) => {
      out[s.i] = c > 0 && s.i < c ? "done" : c > 0 && s.i === c ? "cur" : "todo";
    });
    return out;
  }

  function entriesOf(step) {
    const out = [];
    if (step.k === "boss" && num(step.e) > 0) out.push(step.e);
    list(step.entries).forEach((e) => {
      if (num(e) > 0 && out.indexOf(e) < 0) out.push(e);
    });
    return out;
  }

  // The pack a fight was about: the first pull/boss step at or after the fight's step that holds one of its engaged
  // entries, or whose boss entry is the fight's boss. A fight known only by its step (no entries, no boss) marks the
  // next pack: that is the one the run was walking to.
  function fightPack(plan, fight) {
    if (!plan || !fight) return 0;
    const from = num(fight.step) || 0;
    const boss = num(fight.boss) || 0;
    const engaged = list(fight.engaged).map((x) => (x ? num(x.e) : null)).filter((e) => e !== null && e > 0);
    const known = boss > 0 || engaged.length > 0;
    const steps = list(plan.steps);
    for (let i = 0; i < steps.length; i++) {
      const s = steps[i];
      if (!PACK_KINDS[s.k] || s.i < from) continue;
      if (!known) return s.i;
      if (boss > 0 && s.k === "boss" && s.e === boss) return s.i;
      if (engaged.length) {
        const ents = entriesOf(s);
        if (engaged.some((e) => ents.indexOf(e) >= 0)) return s.i;
      }
    }
    return 0;
  }

  function nextPack(steps, fromI) {
    for (let i = 0; i < steps.length; i++) {
      if (steps[i].i > fromI && PACK_KINDS[steps[i].k]) return steps[i];
    }
    return null;
  }

  // Each stand's margin to its pack: the pack mob the stand is closest to in 3D after subtracting that mob's ring
  // (a negative margin means the stand is already inside the ring). {i: {dist, ring, margin, name, wake}}.
  function standMargins(plan, partyLevel) {
    const out = {};
    const steps = list(plan && plan.steps);
    const mobs = list(plan && plan.mobs);
    steps.forEach((s) => {
      if (!s.stand) return;
      const pack = nextPack(steps, s.i);
      if (!pack) return;
      let best = null;
      mobs.forEach((m) => {
        if (m.step !== pack.i) return;
        const ring = ringYd(m, partyLevel);
        if (!(ring > 0)) return;
        const dist = dist3(s, m), margin = dist - ring;
        if (!best || margin < best.margin || (margin === best.margin && m.guid < best.guid)) {
          best = { dist, ring, margin, name: m.n || "", wake: wakeOf(m) > 0 };
        }
      });
      if (best) out[s.i] = best;
    });
    return out;
  }

  // Pathing check: a move point that lies inside the ring of a mob of a later pack — not the move's own next pack,
  // whose ring the pull is made from — or inside any scripted wake ring (its own pack included). 3D distance, by
  // step then pack. [{i, pack, guid, name, dist, ring, wake}].
  function strays(plan, partyLevel) {
    const out = [];
    const steps = list(plan && plan.steps);
    const mobs = list(plan && plan.mobs);
    steps.forEach((s) => {
      if (!MOVE_KINDS[s.k]) return;
      const own = nextPack(steps, s.i);
      steps.forEach((p) => {
        if (!PACK_KINDS[p.k] || p.i <= s.i) return;
        mobs.forEach((m) => {
          if (m.step !== p.i) return;
          const ring = ringYd(m, partyLevel);
          if (!(ring > 0)) return;
          const dist = dist3(s, m);
          if (dist >= ring) return;
          const wake = wakeOf(m) > 0;
          if (own && p.i === own.i && !wake) return;
          out.push({ i: s.i, pack: p.i, guid: m.guid, name: m.n || "", dist, ring, wake });
        });
      });
    });
    out.sort((a, b) => a.i - b.i || a.pack - b.pack || a.guid - b.guid);
    return out;
  }

  // Everything the page draws for one card's map: null when there is no plan or it belongs to another map. cur is the
  // current step (0: nothing highlighted), fightStep the selected fight's step (0: no fight mark). opts.partyLevel
  // comes from partyLevel(bind); opts.run is the binding's run (its step count must match the plan's).
  function view(plan, opts) {
    const o = opts || {};
    if (!plan || plan.map !== o.mapId) return null;
    const steps = list(plan.steps);
    const numbering = numberingOk(plan, o.run);
    const cur = (num(o.cur) || 0) > 0 ? o.cur : 0;
    const byState = numbering ? states(plan, cur) : states(plan, 0);
    const party = num(o.partyLevel) !== null && o.partyLevel > 0 ? o.partyLevel : PARTY_LEVEL_DEFAULT;
    const fightStep = num(o.fightStep) || 0;
    const pack = fightStep > 0 ? fightPack(plan, { step: fightStep }) : 0;
    const margins = standMargins(plan, party);
    const strayList = strays(plan, party);
    const strayI = {};
    strayList.forEach((x) => { strayI[x.i] = true; });
    const stepByI = {};
    steps.forEach((s) => { stepByI[s.i] = s; });
    const stateOf = (i) => byState[i] || "todo";

    const outSteps = steps.map((s) => ({
      i: s.i, k: s.k, x: s.x, y: s.y, z: s.z, r: num(s.r) || 0, n: s.n || "", c: s.c || "",
      state: stateOf(s.i), stand: !!s.stand, pack: !!PACK_KINDS[s.k], fight: s.i === pack,
      stray: !!strayI[s.i], margin: margins[s.i] || null,
    }));

    // The route over the steps that have a point: the done part ends on the current step, the ahead part starts there.
    const routeSteps = steps.filter((s) => !NO_POINT_KINDS[s.k]);
    const pt = (s) => ({ x: s.x, y: s.y });
    const done = routeSteps.filter((s) => byState[s.i] === "done" || (cur > 0 && byState[s.i] === "cur")).map(pt);
    const ahead = routeSteps.filter((s) => byState[s.i] === "cur" || byState[s.i] === "todo").map(pt);
    const route = [];
    if (done.length >= 2) route.push({ done: true, points: done });
    if (ahead.length >= 2) route.push({ done: false, points: ahead });

    const rings = list(plan.mobs).filter((m) => ringYd(m, party) > 0).map((m) => ({
      x: m.x, y: m.y, z: m.z, r: ringYd(m, party), wake: wakeOf(m) > 0, step: m.step, guid: m.guid,
      name: m.n || "", state: stateOf(m.step), patrol: !!m.patrol,
    }));

    const mobs = list(plan.mobs).map((m) => ({
      x: m.x, y: m.y, z: m.z, step: m.step, guid: m.guid, name: m.n || "", state: stateOf(m.step),
      patrol: !!m.patrol, boss: !!(stepByI[m.step] && stepByI[m.step].k === "boss"),
    }));

    return { numbering, route, steps: outSteps, rings, mobs, strays: strayList };
  }

  const api = {
    AGGRO_MAX_LEVEL_DIFF, AGGRO_MIN_REACH_YD, PARTY_LEVEL_DEFAULT, MOVE_KINDS, NO_POINT_KINDS,
    aggroYd, ringYd, wakeText, partyLevel, numberingOk, states, fightPack, standMargins, strays, view,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.PlanOverlay = api;
})(typeof self !== "undefined" ? self : this);
