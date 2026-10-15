"use strict";
// Commanders tab map drawing: the logic behind the path, object, stall and combat-line layers, free of the page so a test can
// run it under node (internal/commandersjs). Pure except the two small stores it is handed (the bot trails and the stall spots), which
// only keep what they are fed. app.js turns these results into SVG; nothing here builds markup.
//
// Positions are game coordinates {x north, y west}; CmdView.project() puts them on the drawing plane.
(function (root) {
  const TRAIL_MAX = 240;          // positions kept per bot (the trail-length slider shows up to this many)
  const TRAIL_STEP_YD = 2;        // a new point after this much movement
  const TRAIL_LEN_DEFAULT = 120;  // positions drawn until the viewer moves the slider
  const TRAIL_LEN_MIN = 10;
  const STALL_KEEP_SEC = 15 * 60; // a stall spot stays on the map this long
  const STALL_MAX = 12;           // per commander
  const OBJECTS_MAX = 24;
  const AGGRO_BASE_YD = 20, AGGRO_MIN_YD = 5, AGGRO_MAX_YD = 45;

  const num = (n) => typeof n === "number" && isFinite(n);
  const isId = (s) => typeof s === "string" && /^[0-9]{1,20}$/.test(s);

  // ---- trails of every bound bot ------------------------------------------------------------------------------------
  // The lead's own trail (with its times, heading and stamps) lives in CmdView's tracker; this store keeps the others'. Keyed by
  // commander, then bot guid. Feed it each fresh report once; seed it once from the daemon's short history (GET /api/v1/commanders/trails).

  function newTrails() { return { by: {}, seeded: {} }; }

  function trailOf(tr, commander, guid) {
    const bots = tr.by[commander];
    return bots && bots[guid] ? bots[guid] : null;
  }

  // Add each bot's place when it has moved TRAIL_STEP_YD since its last point; a bot on a new map starts a new trail.
  function noteTrails(tr, bind, nowSec) {
    const bots = tr.by[bind.commander] || (tr.by[bind.commander] = {});
    (bind.bots || []).forEach((b) => {
      if (!b || !b.guid || !num(b.x) || !num(b.y)) return;
      let t = bots[b.guid];
      if (!t || t.map !== b.map) t = bots[b.guid] = { map: b.map, pts: [] };
      const last = t.pts[t.pts.length - 1];
      if (last && Math.hypot(last[0] - b.x, last[1] - b.y) < TRAIL_STEP_YD) return;
      t.pts.push([b.x, b.y, nowSec]);
      if (t.pts.length > TRAIL_MAX) t.pts.splice(0, t.pts.length - TRAIL_MAX);
    });
  }

  // The daemon's history for one bot as clean points [x, y, t] (oldest first), or [] for anything malformed. Never more than TRAIL_MAX.
  function cleanPoints(pts) {
    if (!Array.isArray(pts)) return [];
    const out = [];
    pts.forEach((p) => { if (Array.isArray(p) && num(p[0]) && num(p[1]) && num(p[2])) out.push([p[0], p[1], p[2]]); });
    return out.slice(-TRAIL_MAX);
  }

  // Put the daemon's older points in front of what this page has seen itself: only the points older than the page's first one count,
  // and only on the same map. data = {commander: {guid: {map, pts}}}. Returns how many points were taken.
  function seedTrails(tr, data) {
    let taken = 0;
    if (!data || typeof data !== "object") return 0;
    Object.keys(data).forEach((c) => {
      const bots = data[c];
      if (!bots || typeof bots !== "object") return;
      const mine = tr.by[c]; // only a binding and bots this page has already seen report
      if (!mine) return;
      Object.keys(bots).forEach((g) => {
        const guid = Number(g), h = bots[g], t = mine[guid];
        if (!t || !h || !num(h.map) || t.map !== h.map) return;
        const first = t.pts.length ? t.pts[0][2] : Infinity;
        const older = cleanPoints(h.pts).filter((p) => p[2] < first);
        if (!older.length) return;
        t.pts = older.concat(t.pts).slice(-TRAIL_MAX);
        taken += older.length;
      });
    });
    return taken;
  }

  // Forget the commanders that are gone, and the bots that left a binding.
  function pruneTrails(tr, commanders) {
    const live = {};
    commanders.forEach((c) => { live[c.commander] = c; });
    Object.keys(tr.by).forEach((k) => { if (!live[k]) delete tr.by[k]; });
    Object.keys(tr.seeded).forEach((k) => { if (!live[k]) delete tr.seeded[k]; });
    Object.keys(tr.by).forEach((k) => {
      const guids = {};
      (live[k].bots || []).forEach((b) => { guids[b.guid] = true; });
      Object.keys(tr.by[k]).forEach((g) => { if (!guids[g]) delete tr.by[k][g]; });
    });
  }

  // The last n points of a trail on mapId (n from the length slider), or [].
  function trailPoints(t, mapId, n) {
    if (!t || t.map !== mapId) return [];
    const k = Math.max(0, Math.min(TRAIL_MAX, Math.round(num(n) ? n : TRAIL_LEN_DEFAULT)));
    return k >= t.pts.length ? t.pts : t.pts.slice(t.pts.length - k);
  }

  // Which bots get a member trail: every living-or-not bot on the map but the lead (it has its own layer), with at least two points.
  // Returns [{bot, pts}] with pts already cut to n.
  function memberTrails(tr, bind, mapId, n) {
    const out = [];
    (bind.bots || []).forEach((b) => {
      if (!b || b.lead || b.map !== mapId) return;
      const pts = trailPoints(trailOf(tr, bind.commander, b.guid), mapId, n);
      if (pts.length >= 2) out.push({ bot: b, pts });
    });
    return out;
  }

  // ---- stall spots and refused moves ---------------------------------------------------------------------------------
  // A stall spot is where something went wrong: an order was refused. The place is fixed when the
  // report is first seen, since the lead walks on. Kept per commander for STALL_KEEP_SEC, at most STALL_MAX.

  function newStalls() { return { by: {} }; }

  const REFUSED_VERBS = { move: 1, attack: 1, pull: 1, follow: 1, open: 1, use: 1, loot: 1, enter: 1, cc: 1, focus: 1, rest: 1 };

  // The point a "move" order named ("1517 833 9", "to 1517, 833"): [x, y] or null.
  function orderPoint(detail) {
    const m = /^\s*(-?\d+(?:\.\d+)?)[ ,]+(-?\d+(?:\.\d+)?)/.exec(String(detail || ""));
    return m ? [parseFloat(m[1]), parseFloat(m[2])] : null;
  }

  // Where the lead was at server second t, from its trail (positions [x, y, t]): the newest point not after t, else the oldest, else null.
  function placeAt(pts, t) {
    if (!pts || !pts.length) return null;
    let best = pts[0];
    for (let i = 0; i < pts.length; i++) { if (pts[i][2] <= t) best = pts[i]; else break; }
    return { x: best[0], y: best[1] };
  }

  // Note the new stalls of a fresh report. leadPts = the lead's trail on its map; leadNow = {x, y} or null; mapId = the
  // map the party is on (a spot is only drawn on that map).
  // ids make a stall noted once: a refusal by the order's time and verb.
  function noteStalls(st, bind, nowSec, leadPts, leadNow, mapId) {
    const list = st.by[bind.commander] || (st.by[bind.commander] = []);
    const have = {};
    list.forEach((s) => { have[s.id] = s; });
    (bind.orders || []).forEach((o) => {
      const verb = String(o.verb || "").toLowerCase();
      if (o.ok || !REFUSED_VERBS[verb]) return;
      const id = "o:" + o.at + ":" + verb;
      if (have[id]) return;
      const pt = verb === "move" ? orderPoint(o.detail) : null;
      const at = pt ? { x: pt[0], y: pt[1] } : placeAt(leadPts, o.at) || leadNow;
      if (!at) return;
      list.push({ id, kind: "refused", map: mapId, x: at.x, y: at.y, reason: refusedReason(o), verb, at: o.at || nowSec, live: false });
    });
    // drop the old ones and keep the newest STALL_MAX
    const keep = list.filter((s) => nowSec - s.at <= STALL_KEEP_SEC).sort((a, b) => a.at - b.at).slice(-STALL_MAX);
    st.by[bind.commander] = keep;
  }

  // The refusal in words: the module's message after the code ("invalid-target: No unit" -> "No unit").
  function refusedReason(o) {
    const r = String(o.result || "").trim();
    const m = /^([\w-]+):\s*(.*)$/.exec(r);
    let msg = (m ? m[2] : r).replace(/[.\s]+$/, "");
    if (!msg) msg = m ? m[1].replace(/[_-]+/g, " ") : "no reason given";
    return msg.length > 80 ? msg.slice(0, 79) + "…" : msg;
  }

  function stallsOf(st, commander, mapId) { return (st.by[commander] || []).filter((s) => s.map === mapId); }

  function pruneStalls(st, commanders) {
    const live = {};
    commanders.forEach((c) => { live[c] = true; });
    Object.keys(st.by).forEach((k) => { if (!live[k]) delete st.by[k]; });
  }

  // A stall's tooltip: "Move refused: <why>".
  function stallText(s) {
    const verb = s.verb ? s.verb[0].toUpperCase() + s.verb.slice(1) : "Order";
    return verb + " refused: " + s.reason;
  }

  // ---- objects --------------------------------------------------------------------------------------------------------

  const OBJ_TYPE = { d: "Door", c: "Chest", b: "Lever", g: "Gong or altar", r: "Ritual", o: "Object" };
  const OBJ_STATE = { r: "closed", a: "open", u: "used" };

  // The game objects of a binding that can be drawn on mapId: [] when the module reports none or they are on another map.
  function objectsOf(bind, mapId) {
    if (!bind || !Array.isArray(bind.objects) || bind.mobs_map !== mapId) return [];
    const out = [];
    bind.objects.slice(0, OBJECTS_MAX).forEach((o) => {
      if (!o || !isId(o.id) || !num(o.x) || !num(o.y)) return;
      out.push({
        id: o.id, entry: num(o.entry) ? o.entry : 0, name: typeof o.name === "string" ? o.name.slice(0, 40) : "", x: o.x, y: o.y,
        type: OBJ_TYPE[o.type] ? o.type : "o", state: OBJ_STATE[o.state] ? o.state : "r",
      });
    });
    return out;
  }
  function objectText(o) { return (o.name || OBJ_TYPE[o.type]) + " - " + OBJ_TYPE[o.type].toLowerCase() + ", " + OBJ_STATE[o.state]; }

  // ---- aggro rings ----------------------------------------------------------------------------------------------------

  // The distance at which a hostile unit notices a player (vanilla's rule of thumb): 20 yd at equal level, one yard more per level the
  // unit is above the player and one less per level below, clamped to 5-45. An unknown level on either side counts as equal.
  function aggroRadius(mobLevel, partyLevel) {
    if (!(mobLevel > 0) || !(partyLevel > 0)) return AGGRO_BASE_YD;
    return Math.min(AGGRO_MAX_YD, Math.max(AGGRO_MIN_YD, AGGRO_BASE_YD + (mobLevel - partyLevel)));
  }

  // ---- combat lines ---------------------------------------------------------------------------------------------------

  // Healers' lines: [{from: bot, to: bot}] for each living bot casting a spell at another living bot of the party (the spell's own target,
  // from a module that reports it). A healer-role bot with no cast target but a bot selected falls back to that selection.
  function healLines(bots) {
    const byId = {};
    (bots || []).forEach((b) => { if (b.id) byId[b.id] = b; });
    const out = [];
    (bots || []).forEach((b) => {
      if (b.alive === false) return;
      let to = b.cast && b.ctgt ? byId[b.ctgt] : null;
      if (!to && b.role === "healer" && !b.ctgt && b.target) to = byId[b.target];
      if (to && to !== b && to.alive !== false) out.push({ from: b, to });
    });
    return out;
  }

  // Crowd-control lines: [{from: bot, to: mob}] for each living bot casting at a mob that is crowd controlled (stunned, confused,
  // fleeing, rooted - the mob's `cc`).
  function ccLines(bots, mobs) {
    const byId = {};
    (mobs || []).forEach((m) => { byId[m.id] = m; });
    const out = [];
    (bots || []).forEach((b) => {
      if (b.alive === false || !b.cast || !b.ctgt) return;
      const m = byId[b.ctgt];
      if (m && m.cc) out.push({ from: b, to: m });
    });
    return out;
  }

  // Whether the report carries cast targets at all (a module that sends them), for the layers' availability.
  function hasCastTargets(bots) { return (bots || []).some((b) => typeof b.ctgt === "string"); }

  const api = {
    TRAIL_MAX, TRAIL_STEP_YD, TRAIL_LEN_DEFAULT, TRAIL_LEN_MIN, STALL_KEEP_SEC, STALL_MAX, AGGRO_BASE_YD,
    newTrails, trailOf, noteTrails, seedTrails, pruneTrails, trailPoints, memberTrails, cleanPoints,
    newStalls, noteStalls, stallsOf, pruneStalls, stallText, orderPoint, placeAt, refusedReason,
    objectsOf, objectText, aggroRadius, healLines, ccLines, hasCastTargets,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.CmdPaths = api;
})(typeof self !== "undefined" ? self : this);
