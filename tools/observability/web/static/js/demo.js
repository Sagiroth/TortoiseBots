"use strict";
// Commanders tab demo: a simulated group, so every drawing layer of the map can be tried without a live run. Pure logic (no page, no
// markup): frame(sec) answers one COMMANDERS binding, the same shape the game server sends, and app.js feeds it through the very same
// functions as a real report. Nothing here talks to the server. The story repeats every LOOP seconds: a party of five walks a looping
// route on Uldaman (map 70), pulls a pack (a sheep, a fleeing mob, a death), rests, opens a door, kills a boss with adds, loots and
// opens things, then wipes to a wandering elite and starts again.
(function (root) {
  const MAP = 70, ZONE = 1337, LOOP = 140, COMMANDER = "DEMO (simulated)";
  const WAYPOINTS = [[-60, 100], [-20, 140], [30, 150], [60, 195], [35, 245], [-10, 270], [-60, 245], [-85, 195], [-85, 140]];

  // ---- the route and where the party is on it
  const legs = [];
  let ROUTE = 0;
  WAYPOINTS.forEach((p, i) => {
    const q = WAYPOINTS[(i + 1) % WAYPOINTS.length], d = Math.hypot(q[0] - p[0], q[1] - p[1]);
    legs.push({ from: p, to: q, start: ROUTE, len: d });
    ROUTE += d;
  });
  function routeAt(s) {
    s = ((s % ROUTE) + ROUTE) % ROUTE;
    const l = legs.find((g) => s <= g.start + g.len) || legs[legs.length - 1], f = (s - l.start) / l.len;
    return { x: l.from[0] + (l.to[0] - l.from[0]) * f, y: l.from[1] + (l.to[1] - l.from[1]) * f, hx: (l.to[0] - l.from[0]) / l.len, hy: (l.to[1] - l.from[1]) / l.len };
  }

  // The story's times (seconds into the loop).
  const T = {
    fightA: [24, 48], mageDies: 40, mageUp: 58, rest: [62, 80], door: 82, lever: 60, refused: 88, bossFight: [100, 126], chest: 128, gong: 132,
    sentinel: [128, 140], wipe: 134,
  };
  const WALK = [[0, 24], [48, 62], [84, 100], [126, 134]];
  const WALK_TOTAL = WALK.reduce((n, w) => n + (w[1] - w[0]), 0);
  function walked(t) { return WALK.reduce((n, w) => n + Math.max(0, Math.min(t, w[1]) - w[0]), 0); }
  const moving = (t) => WALK.some((w) => t >= w[0] && t < w[1]);
  const partyAt = (t) => routeAt((walked(t) / WALK_TOTAL) * ROUTE);
  const ahead = (c, fwd, side) => ({ x: c.x + c.hx * fwd - c.hy * side, y: c.y + c.hy * fwd + c.hx * side });
  const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
  const within = (t, w) => t >= w[0] && t < w[1];
  const round1 = (n) => Math.round(n * 10) / 10;

  // The things that stand in the world at fixed places (anchored to where the party will be).
  const A = partyAt(T.fightA[0]), packA = (fwd, side) => ahead(A, fwd, side);
  const B = partyAt(T.bossFight[0]), packBoss = (fwd, side) => ahead(B, fwd, side);
  const R = partyAt(70), restSpot = ahead(R, 0, 0);
  const DOOR = ahead(partyAt(T.door), 12, 0), LEVER = ahead(partyAt(T.lever), 0, 14), CHEST = ahead(B, 30, 6), GONG = ahead(B, 22, -16);
  const PACK_IDLE = ahead(partyAt(56), 8, 40); // a pack off to the side the run skips

  // ---- the party
  const BOTS = [
    { name: "Simtank", guid: 9001, class: "warrior", role: "tank", power_type: "rage", max_hp: 4200, max_power: 100, fwd: 5, side: 0 },
    { name: "Simhealer", guid: 9002, class: "priest", role: "healer", power_type: "mana", max_hp: 2600, max_power: 3000, fwd: -4, side: 3 },
    { name: "Simmage", guid: 9003, class: "mage", role: "dps", power_type: "mana", max_hp: 2300, max_power: 3400, fwd: -6, side: -3 },
    { name: "Simhunter", guid: 9004, class: "hunter", role: "dps", power_type: "mana", max_hp: 2900, max_power: 2400, fwd: -2, side: 6 },
    { name: "Simlock", guid: 9005, class: "warlock", role: "dps", power_type: "mana", max_hp: 2400, max_power: 3200, fwd: -8, side: 0 },
  ];
  const bid = (i) => String(9001 + i);
  const TANK = bid(0), HEALER = bid(1);

  // ---- hostile packs. life: [engaged at, dies at] (null: never in the loop's fights)
  function packMobs() {
    return [
      { id: "50001", entry: 4850, name: "Shadowforge Cultist", lvl: 42, at: packA(16, -4), eng: 24, die: 38, victim: TANK },
      { id: "50002", entry: 4850, name: "Shadowforge Cultist", lvl: 42, at: packA(18, 4), eng: 24, die: 44, victim: TANK, cast: [[30, 33, "Shadow Bolt"], [38, 41, "Shadow Bolt"]] },
      { id: "50003", entry: 4851, name: "Shadowforge Darkweaver", lvl: 43, at: packA(22, 0), eng: 26, die: 42, victim: HEALER, cast: [[28, 31, "Fear"]] },
      { id: "50004", entry: 4852, name: "Shadowforge Ambusher", lvl: 41, at: packA(14, 9), eng: 24, die: 47, sheep: [28, 44] },
      { id: "50005", entry: 4853, name: "Shadowforge Scout", lvl: 41, at: packA(20, -10), eng: 24, die: 47, flee: [36, 46], victim: TANK },
      { id: "50010", entry: 4860, name: "Stone Sentry", lvl: 41, at: { x: PACK_IDLE.x, y: PACK_IDLE.y }, eng: null },
      { id: "50011", entry: 4860, name: "Stone Sentry", lvl: 41, at: { x: PACK_IDLE.x + 7, y: PACK_IDLE.y + 5 }, eng: null },
      { id: "50012", entry: 4861, name: "Stonevault Brute", lvl: 43, rank: "e", at: { x: PACK_IDLE.x - 6, y: PACK_IDLE.y + 8 }, eng: null },
      { id: "50020", entry: 6906, name: "Revelosh", lvl: 44, rank: "b", at: packBoss(24, 0), eng: 100, die: 124, victim: TANK, cast: [[108, 111, "Chain Lightning"], [116, 119, "Chain Lightning"]] },
      { id: "50021", entry: 6910, name: "Stonevault Warden", lvl: 43, rank: "e", at: packBoss(22, -8), eng: 100, die: 114, victim: TANK },
      { id: "50022", entry: 6910, name: "Stonevault Warden", lvl: 43, rank: "e", at: packBoss(22, 8), eng: 100, die: 119, victim: HEALER },
    ];
  }
  const PACKS = packMobs();

  // One hostile unit at time t (null: not there).
  function mobAt(m, t, c) {
    let hp = 100, victim = "", cast = "", cc = "";
    if (m.eng != null) {
      if (t >= m.die && m.rank !== "b") return null;
      if (t >= m.eng) hp = clamp(100 - ((t - m.eng) / (m.die - m.eng)) * 92, 0, 100);
      if (m.rank === "b" && t >= m.die) hp = 0;
      if (t >= m.eng && t < m.die) victim = m.victim || "";
      if (m.rank === "b" && t > m.die + 4) return null;
    }
    let { x, y } = m.at;
    if (m.sheep && within(t, m.sheep)) { cc = "c"; victim = ""; }
    if (m.flee && within(t, m.flee)) { cc = "f"; hp = Math.min(hp, 14); victim = ""; const k = (t - m.flee[0]) * 4; x += c.hx * k; y += c.hy * k; }
    (m.cast || []).forEach((w) => { if (t >= w[0] && t < w[1] && t % 2 < 1.6) cast = w[2]; });
    if (m.rank === "b" && hp <= 0) { victim = ""; cast = ""; }
    return mob(m, x, y, hp, victim, cast, cc);
  }
  function mob(m, x, y, hp, victim, cast, cc) {
    const o = { id: m.id, entry: m.entry, name: m.name, hp: Math.round(hp), x: round1(x), y: round1(y), reaction: m.reaction || "h", lvl: m.lvl };
    if (m.rank) o.rank = m.rank;
    if (m.kind) o.kind = m.kind;
    if (victim) o.victim = victim;
    if (cast) o.cast = cast;
    if (cc) o.cc = cc;
    return o;
  }

  function mobsAt(t, c) {
    const out = [];
    PACKS.forEach((m) => { const o = mobAt(m, t, c); if (o) out.push(o); });
    // the wanderer that wipes the party at the end of the loop
    if (within(t, T.sentinel)) {
      const p = ahead(c, 12, 0);
      out.push(mob({ id: "50030", entry: 7000, name: "Obsidian Sentinel", lvl: 45, rank: "e" }, p.x, p.y, 100, t >= 130 ? TANK : "", t >= 131 && t % 2 < 1.5 ? "Shatter" : "", ""));
    }
    // a neutral, a critter that wanders and a friendly unit
    out.push(mob({ id: "50040", entry: 2400, name: "Wandering Digger", lvl: 40, reaction: "n" }, restSpot.x + 18, restSpot.y - 22, 100, "", "", ""));
    out.push(mob({ id: "50041", entry: 2401, name: "Cave Rat", lvl: 1, reaction: "n", kind: "k" }, A.x - 20 + Math.sin(t / 5) * 4, A.y - 6 + Math.cos(t / 5) * 4, 100, "", "", ""));
    out.push(mob({ id: "50042", entry: 2402, name: "Archaeologist Ratchet", lvl: 45, reaction: "f", kind: "v" }, restSpot.x - 25, restSpot.y + 12, 100, "", "", ""));
    return out;
  }

  // ---- bots
  function botAt(spec, i, t, mobs) {
    const rest = within(t, T.rest);
    const deadFrom = i === 2 && t >= T.mageDies && t < T.mageUp ? T.mageDies : t >= T.wipe ? T.wipe : null;
    const alive = deadFrom == null;
    const c = partyAt(deadFrom != null ? deadFrom : t), p = ahead(c, spec.fwd, spec.side);
    const fight = within(t, T.fightA) || within(t, T.bossFight) || t >= 130;
    let hp = spec.max_hp * 0.97, power = spec.max_power * 0.9;
    if (fight) {
      const wave = (Math.sin(t / 2.5 + i) + 1) / 2;
      hp = spec.max_hp * (spec.role === "tank" ? 0.5 + 0.35 * wave : spec.role === "healer" ? 0.4 + 0.45 * wave : 0.7 + 0.25 * wave);
      power = spec.power_type === "rage" ? spec.max_power * (0.2 + 0.6 * wave) : spec.max_power * (0.8 - 0.4 * clamp((t - T.fightA[0]) / 24, 0, 1));
    }
    if (within(t, [T.fightA[1], T.rest[0]])) power = spec.power_type === "rage" ? 0 : spec.max_power * 0.3;
    if (rest) { const f = (t - T.rest[0]) / (T.rest[1] - T.rest[0]); hp = spec.max_hp * (0.7 + 0.3 * f); power = spec.power_type === "rage" ? 0 : spec.max_power * (0.3 + 0.65 * f); }
    const b = {
      name: spec.name, guid: spec.guid, class: spec.class, role: spec.role, level: 42, hp: alive ? Math.round(hp) : 0, max_hp: spec.max_hp,
      power: alive ? Math.round(power) : 0, max_power: spec.max_power, power_type: spec.power_type, alive, lead: i === 0, map: MAP, zone: ZONE,
      x: round1(p.x), y: round1(p.y), z: 0, id: bid(i), st: fight && alive ? "c" : alive ? "n" : "d",
    };
    if (!alive) return b;
    const engaged = mobs.filter((m) => m.victim && m.hp > 0 && m.cc !== "c" && m.cc !== "f");
    const first = engaged[0] || mobs.find((m) => m.hp > 0 && m.reaction === "h" && !m.victim && m.rank === "b" && t >= 94 && t < T.bossFight[0]);
    const sheep = mobs.find((m) => m.cc === "c");
    if (i === 0 && first) b.target = first.id;
    if (i === 3 && first) b.target = first.id;                         // the hunter picks the boss before the tank pulls
    if (i === 4 && first) b.target = (engaged[1] || first).id;
    if (i === 2) { const tg = sheep || engaged[0]; if (tg) b.target = tg.id; }
    if (i === 1 && engaged.length) b.target = engaged[0].id;
    if (b.target) { const tm = mobs.find((m) => m.id === b.target); if (tm) b.tname = tm.name; }
    if (fight) {
      if (i === 1 && t % 3 < 2) { b.cast = "Flash Heal"; b.ctgt = t % 9 < 6 ? TANK : bid(3); }
      if (i === 2 && sheep) { b.cast = "Polymorph"; b.ctgt = sheep.id; b.act = "polymorph"; }
      else if (i === 2 && t % 4 < 2.5) { b.cast = "Frostbolt"; b.ctgt = b.target; }
      if (i === 4 && t % 3 < 2) { b.cast = "Shadow Bolt"; b.ctgt = b.target; }
    }
    if (i === 3 && within(t, [T.bossFight[0] - 6, T.bossFight[0] + 1])) b.act = "pull target";
    if (rest) b.rest = i === 1 || i === 2 || i === 4 ? "d" : "e";
    if (!b.act) b.act = rest ? (b.rest === "d" ? "drink" : "eat") : fight ? "attack" : "follow";
    b.res = "O";
    if (moving(t)) b.mv = true;
    if (i === 4 && t >= 120 && t < 124) { b.cc = "r"; }   // a root on the warlock
    if (i === 3 && t >= 108 && t < 112) { b.cc = "s"; }   // a stun on the hunter
    return b;
  }

  // ---- doors, chests, a lever and a gong
  function objectsAt(t) {
    const o = (id, entry, name, type, state, p) => ({ id, entry, name, type, state, x: round1(p.x), y: round1(p.y) });
    return [
      o("60001", 2087, "Ancient Door", "d", t >= T.door ? "a" : "r", DOOR),
      o("60002", 2088, "Lever of the Keepers", "b", t >= T.lever ? "a" : "r", LEVER),
      o("60003", 2089, "Ancient Treasure", "c", t >= T.chest ? "u" : "r", CHEST),
      o("60004", 2090, "Gong of Archaedas", "g", t >= T.gong ? "a" : "r", GONG),
    ];
  }

  // ---- the run
  const STEPS = [
    { i: 1, k: "move", at: () => partyAt(0) },
    { i: 2, k: "move", at: () => partyAt(18) },
    { i: 3, k: "pull", at: () => packA(18, 0), r: 18, e: 4850, n: "Shadowforge Cultist" },
    { i: 4, k: "pull", at: () => PACK_IDLE, r: 14, e: 4860, n: "Stone Sentry", sk: true },
    { i: 5, k: "rest", at: () => restSpot, r: 20 },
    { i: 6, k: "open", at: () => DOOR, e: 2087, n: "Ancient Door" },
    { i: 7, k: "move_direct", at: () => partyAt(92) },
    { i: 8, k: "pull", at: () => packBoss(22, 0), r: 22, e: 6910, n: "Stonevault Warden" },
    { i: 9, k: "boss", at: () => packBoss(24, 0), r: 30, e: 6906, n: "Revelosh" },
    { i: 10, k: "loot", at: () => CHEST, e: 2089, n: "Ancient Treasure" },
    { i: 11, k: "use_item", at: () => GONG, e: 2090, n: "Gong of Archaedas" },
    { i: 12, k: "move", at: () => partyAt(137) },
  ];
  const STEP_FROM = [[0, 1], [10, 2], [24, 3], [46, 5], [80, 6], [84, 7], [96, 8], [100, 9], [126, 10], [130, 11], [134, 12]];
  const PHASE = ["", "walking", "walking", "fighting", "", "resting", "opening", "walking", "pulling", "fighting", "looting", "using", "walking"];

  function runAt(t, start) {
    const step = STEP_FROM.reduce((s, r) => (t >= r[0] ? r[1] : s), 1), st = STEPS[step - 1];
    const paused = within(t, T.rest) || t >= T.wipe;
    const changedLt = paused ? (t >= T.wipe ? T.wipe : T.rest[0]) : STEP_FROM.filter((r) => r[0] <= t).pop()[0];
    return {
      plan: "uldaman-demo", state: paused ? "paused" : "running", step, steps: STEPS.length, leg: "stonekeepers hall", kind: st.k,
      phase: paused ? "waiting" : PHASE[step],
      note: t >= T.wipe ? "party wiped, waiting for the release" : within(t, T.rest) ? "waiting for mana: healer 40%" : "",
      changed: Math.floor(start + changedLt),
      plan_steps: STEPS.map((s) => { const p = s.at(), o = { i: s.i, k: s.k, x: round1(p.x), y: round1(p.y) }; if (s.r) o.r = s.r; if (s.e) o.e = s.e; if (s.n) o.n = s.n; if (s.sk) o.sk = true; return o; }),
    };
  }

  // ---- the orders the commander gave (oldest first, only those already given in this loop)
  function ordersAt(t, start) {
    const bad = ahead(partyAt(T.refused), 40, 30);
    const all = [
      [2, "run", "start uldaman-demo", true, "ok"],
      [24, "pull", "entry:4850", true, "ok"],
      [28, "cc", "Shadowforge Ambusher", true, "ok: polymorph"],
      [62, "rest", "", true, "ok"],
      [T.door, "open", "entry:2087", true, "ok"],
      [T.refused, "move", round1(bad.x) + " " + round1(bad.y), false, "no-path: No path to the target"],
      [T.refused + 4, "attack", "Stone Sentry", false, "invalid-target: Target is not in line of sight"],
      [T.chest, "loot", "entry:2089", true, "ok"],
      [T.gong, "use", "entry:2090", true, "ok"],
    ];
    return all.filter((a) => a[0] <= t).map((a) => ({ at: Math.floor(start + a[0]), verb: a[1], detail: a[2], ok: a[3], result: a[4] }));
  }

  // One binding. `sec` is the clock (unix seconds) and `start` the second the demo started; the story is a function of their difference,
  // so the same instant always gives the same picture.
  function frame(sec, start) {
    const t = (((sec - start) % LOOP) + LOOP) % LOOP, base = sec - t;
    const c = partyAt(t), mobs = mobsAt(t, c);
    const bots = BOTS.map((s, i) => botAt(s, i, t, mobs));
    const cp = ahead(partyAt(0), -40, -60);
    return {
      commander: COMMANDER, commander_guid: 9000, online: true, view: true, group: 4242, lead: "Simtank", demo: true, bots,
      orders: ordersAt(t, base), commander_pos: { map: MAP, x: round1(cp.x), y: round1(cp.y) }, mobs_map: MAP, mobs, bound_at: Math.floor(start),
      run: runAt(t, base), objects: objectsAt(t),
    };
  }

  const api = { frame, LOOP, MAP, COMMANDER, ROUTE: () => ROUTE, T };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.CmdDemo = api;
})(typeof self !== "undefined" ? self : this);
