"use strict";
// FightMap: the pure logic behind what the Fight debugger draws on the card map for one fight: trails over the range, the deaths with
// their killer, the clouds a dying Son of Hakkar leaves, the puller's path, the boss's anchor circle and stand point from the data's
// tactic rows, damage and death heat, the unit focus filter, the box the map fits, and one pin per pull for the Dungeons tab.
// No DOM, no fetch, no timers: unit-tested by internal/commandersjs/fightmap_check.js under node. Every input comes from the daemon or
// the browser's own state and is read defensively: a bad row is skipped, a count is capped, nothing here throws.
//
// Frames are one shape for both sources: { t (ms from the fight's start), bots: { guid: { x, y, hp, alive, m } },
// mobs: { id: { e (entry), x, y, hp, v (victim guid), n } } }. fromTrack() makes them from FightReplay's parsed track (a finished
// fight, one frame per 2 s report) and fromLive() from FdLive's 0.5 s position frames (a running fight). A range is { from, to } in
// the same milliseconds (FdView's), null for all of it. Positions are game coordinates; the page projects them.
(function (root) {
  // The units a Son-of-Hakkar-style mechanic spawns and the page names on the map, by creature entry (BossTacticsPolicy.h,
  // SeekAuraPolicy.h): Son of Hakkar 11357, Shade of Jin'do 14986.
  const SUMMONS = { 11357: "Son of Hakkar", 14986: "Shade of Jin'do" };
  // The units that leave a cloud when they die, with the spell of the aura it carries and a radius to use when the data has none.
  const CLOUD_SOURCES = { 11357: { sid: 24321, defaultR: 4 } };
  const MAX_POINTS = 300;       // points of one unit's trail
  const JUMP_YD = 40;           // a step longer than this between two frames is a teleport, not a walk: the trail breaks there
  const MAX_MOBS = 20;          // mob trails drawn
  const CLOUD_MS = 20000;       // how long a cloud stays on the map after its source died
  const MAX_CLOUDS = 60;
  const HEAT_CELLS = 400;       // cells kept (the strongest)
  const HEAT_CELL_YD = 4;
  const DEATH_HEAT = 100;       // what one death adds to its cell, in hp percent
  const MAX_FRAMES = 1500;      // frames read from one source
  const MAX_DEATHS = 200;
  const MAX_MECHANICS = 8;
  const MAX_PINS = 60;
  const PIN_SLACK_S = 10;       // the lead's frame must be within this many seconds after the fight's start
  const TRACK_EDGE_S = 4;       // the seconds before the start and after the end of a fight that its frames reach (FightReplay's own)
  const MAX_NAME = 80;          // FdFilter's longest text value
  const MAX_BOTS = 80;          // bots read per frame
  const MAX_COORD = 50000;      // a coordinate beyond this is no place in the game world
  const MAX_RADIUS_YD = 100;    // the largest circle drawn for a mechanic or a cloud
  const OUTLIER_YD = 300;       // a point farther than this from the median of the fight's points does not count for the fit
  const WIPE_TAIL_MS = 4000;    // a unit that vanishes in the last seconds of a wipe is the reset, not a death

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const isObj = (v) => !!v && typeof v === "object" && !Array.isArray(v);
  const place = (v) => num(v) !== null && Math.abs(v) <= MAX_COORD;

  function inRange(t, range) {
    return !range || (num(range.from) !== null && num(range.to) !== null ? t >= range.from && t <= range.to : true);
  }

  function framesIn(frames, range) {
    const out = [];
    (Array.isArray(frames) ? frames : []).forEach((f) => {
      if (isObj(f) && num(f.t) !== null && inRange(f.t, range)) out.push(f);
    });
    return out;
  }

  // ---- the two sources, as frames of fight milliseconds

  // FightReplay.prep(json) of a finished fight: the frames inside the fight and the few seconds the track keeps around it.
  function fromTrack(track, fight) {
    const out = [];
    const frames = track && Array.isArray(track.frames) ? track.frames : [];
    const startS = num(fight && fight.start);
    if (startS === null) return out;
    const endS = startS + (num(fight && fight.dur_ms) || 0) / 1000;
    frames.forEach((f) => {
      if (out.length >= MAX_FRAMES || !isObj(f) || num(f.t) === null) return;
      if (f.t < startS - TRACK_EDGE_S || f.t > endS + TRACK_EDGE_S) return;
      const bots = {};
      Object.keys(isObj(f.bots) ? f.bots : {}).slice(0, MAX_BOTS).forEach((g) => {
        const b = f.bots[g];
        if (!isObj(b) || !place(b.x) || !place(b.y)) return;
        // FightReplay.prep marks a health the report did not carry (hk false): it is unknown, not 0
        bots[g] = { x: b.x, y: b.y, hp: b.hk === false || num(b.hp) === null ? null : b.hp, alive: b.alive !== false, m: num(b.map) === null ? 0 : b.map };
      });
      const mobs = {};
      Object.keys(isObj(f.mobs) ? f.mobs : {}).forEach((id) => {
        const m = f.mobs[id];
        if (!isObj(m) || !place(m.x) || !place(m.y) || num(m.e) === null) return;
        mobs[id] = { e: m.e, x: m.x, y: m.y, hp: m.hk === false || num(m.hp) === null ? null : m.hp, v: num(m.v) || 0 };
      });
      out.push({ t: Math.round((f.t - startS) * 1000), bots, mobs });
    });
    return out;
  }

  // FdLive's state: its 0.5 s position frames (bots [guid, x, y, hp %, alive], mobs [id, x, y, hp %]), the mobs' entry and victim from the 2 s
  // samples (the position frame has neither). A unit at 0, 0 has no position.
  function fromLive(st) {
    const out = [];
    if (!st || !Array.isArray(st.pos) || !num(st.startMs)) return out;
    const samples = Array.isArray(st.samples) ? st.samples : [];
    const entry = {};
    samples.forEach((s) => (Array.isArray(s && s.mobs) ? s.mobs : []).forEach((m) => { if (m && m.id != null && num(m.e)) entry[String(m.id)] = m.e; }));
    let j = -1;
    st.pos.slice(-MAX_FRAMES).forEach((p) => {
      if (!isObj(p) || num(p.t) === null) return;
      while (j + 1 < samples.length && samples[j + 1] && samples[j + 1].t <= p.t) j++;
      const victim = {};
      if (j >= 0) (Array.isArray(samples[j].mobs) ? samples[j].mobs : []).forEach((m) => { if (m && m.id != null) victim[String(m.id)] = m.v; });
      const bots = {};
      (Array.isArray(p.bots) ? p.bots : []).slice(0, MAX_BOTS).forEach((r) => {
        if (!Array.isArray(r) || r.length < 3 || r[0] == null || !place(r[1]) || !place(r[2]) || (r[1] === 0 && r[2] === 0)) return;
        bots[String(r[0])] = { x: r[1], y: r[2], hp: num(r[3]) === null ? null : r[3], alive: r[4] !== 0, m: 0 };
      });
      const mobs = {};
      (Array.isArray(p.mobs) ? p.mobs : []).forEach((r) => {
        if (!Array.isArray(r) || r.length < 3 || r[0] == null || !place(r[1]) || !place(r[2]) || (r[1] === 0 && r[2] === 0)) return;
        const id = String(r[0]);
        mobs[id] = { e: entry[id] || 0, x: r[1], y: r[2], hp: num(r[3]) === null ? null : r[3], v: num(Number(victim[id])) || 0 };
      });
      out.push({ t: p.t - st.startMs, bots, mobs });
    });
    return out;
  }

  // ---- trails

  // One unit's points [x, y, t] of the range, in frame order, cut where the unit jumped (more than JUMP_YD between two frames) or
  // changed map, and where it was dead; a `null` stands for each cut. At most MAX_POINTS points are kept: every n-th, the first and the
  // last always.
  function cutTrail(list) {
    let n = 1;
    if (list.length > MAX_POINTS) n = Math.ceil(list.length / (MAX_POINTS - 1));
    const out = [];
    let lastSeg = -1;
    list.forEach((p, i) => {
      if (n > 1 && i % n !== 0 && i !== list.length - 1) return;
      if (out.length && p.seg !== lastSeg) out.push(null);
      lastSeg = p.seg;
      out.push([p.x, p.y, p.t]);
    });
    return out;
  }

  // Trails of the bots by guid, and with opts.mobs of the summons and the boss (opts.boss, a creature entry) by "m:<id>:<entry>" (at most
  // MAX_MOBS of them).
  function trails(frames, range, opts) {
    const o = opts || {};
    const acc = {};      // key -> { pts, seg, last }
    const mobIds = [];
    framesIn(frames, range).forEach((f) => {
      const step = (key, u, mapId) => {
        let a = acc[key];
        if (!a) a = acc[key] = { pts: [], seg: 0, last: null, map: 0 };
        if (a.last && (mapId !== a.map || Math.hypot(u.x - a.last.x, u.y - a.last.y) > JUMP_YD)) a.seg++;
        a.pts.push({ x: u.x, y: u.y, t: f.t, seg: a.seg });
        a.last = u;
        a.map = mapId;
      };
      Object.keys(isObj(f.bots) ? f.bots : {}).forEach((g) => {
        const b = f.bots[g];
        if (!isObj(b) || num(b.x) === null || num(b.y) === null) return;
        // a position on another map than the fight's is not on this map
        if (num(o.mapId) && num(b.m) && b.m !== o.mapId) return;
        if (b.alive === false) { if (acc[g]) { acc[g].last = null; acc[g].seg++; } return; }
        step(g, b, num(b.m) || 0);
      });
      if (!o.mobs) return;
      Object.keys(isObj(f.mobs) ? f.mobs : {}).forEach((id) => {
        const m = f.mobs[id];
        if (!isObj(m) || num(m.x) === null || num(m.y) === null) return;
        if (!(SUMMONS[m.e] || (num(o.boss) && m.e === o.boss))) return;
        const key = "m:" + id + ":" + m.e;
        if (!acc[key]) {
          if (mobIds.length >= MAX_MOBS) return;
          mobIds.push(key);
        }
        if (num(m.hp) !== null && m.hp <= 0) { if (acc[key]) { acc[key].last = null; acc[key].seg++; } return; }
        step(key, m, 0);
      });
    });
    const out = {};
    Object.keys(acc).forEach((k) => { out[k] = cutTrail(acc[k].pts); });
    return out;
  }

  // ---- death marks

  function clock(ms) {
    const s = Math.max(0, Math.floor((num(ms) || 0) / 1000));
    return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
  }

  // The deaths of the fight inside the range that have a place (a death with no x, y or the 0, 0 of "unknown" has none), with the role of
  // the bot from the fight's bot list. `i` is the death's index in fight.deaths.
  function deathMarks(fight, range) {
    const out = [];
    const deaths = fight && Array.isArray(fight.deaths) ? fight.deaths : [];
    const role = {};
    (fight && Array.isArray(fight.bots) ? fight.bots : []).forEach((b) => { if (isObj(b) && typeof b.name === "string") role[b.name] = typeof b.role === "string" ? b.role : ""; });
    deaths.slice(0, MAX_DEATHS).forEach((d, i) => {
      if (!isObj(d) || num(d.x) === null || num(d.y) === null || (d.x === 0 && d.y === 0)) return;
      const t = num(d.t_ms);
      if (t === null || !inRange(t, range)) return;
      const name = typeof d.name === "string" ? d.name : "";
      out.push({ i, x: d.x, y: d.y, t, t_ms: t, name, killer: typeof d.killer === "string" && d.killer ? d.killer : "unknown", role: Object.prototype.hasOwnProperty.call(role, name) ? role[name] : "" });
    });
    return out;
  }

  // The hover text of a death mark: "Name · killed by X · m:ss" (a plain string: the page escapes it where it writes markup).
  function deathTitle(mark) {
    return String(mark.name) + " · killed by " + String(mark.killer) + " · " + clock(mark.t_ms);
  }

  // ---- tactic rows (GET /api/v1/boss/tactics: map, boss, kind, target, spell, priority, param, spotX/Y/Z)

  const tacticRows = (tactics) => (Array.isArray(tactics) ? tactics.filter(isObj).slice(0, 5000) : []);
  const hasSpot = (r) => num(r.spotX) !== null && num(r.spotY) !== null && (r.spotX !== 0 || r.spotY !== 0);

  // What the data says about the boss's ground: the anchor circle (radius `param` for everyone, `priority` for the tanks, centre the
  // row's spot) and the stand point of a seek_aura row. Rows with no spot give nothing. The platform's edge is not in the data and is not
  // drawn.
  function mechanics(tactics, mapId, bossEntry) {
    const out = [];
    tacticRows(tactics).forEach((r) => {
      if (out.length >= MAX_MECHANICS || r.map !== mapId || r.boss !== bossEntry || !hasSpot(r)) return;
      if (r.kind === "anchor") {
        const rad = num(r.param) !== null && r.param > 0 ? Math.min(r.param, MAX_RADIUS_YD) : 0;
        if (!rad) return;
        const tank = num(r.priority) !== null && r.priority > 0 && r.priority < rad ? r.priority : 0;
        out.push({ kind: "anchor", x: r.spotX, y: r.spotY, r: rad, rTank: tank, label: "Anchor " + rad + " yd" + (tank ? " · tanks " + tank + " yd" : "") });
      } else if (r.kind === "seek_aura") {
        out.push({ kind: "stand", x: r.spotX, y: r.spotY, label: "Stand B" });
      }
    });
    return out;
  }

  // The radius of the cloud a unit of entry `e` leaves: the seek_aura row that names it (its `param`), else the source's default.
  function cloudRadius(tactics, e) {
    const src = CLOUD_SOURCES[e];
    if (!src) return 0;
    const row = tacticRows(tactics).find((r) => r.kind === "seek_aura" && r.target === e && num(r.param) !== null && r.param > 0);
    return row ? Math.min(row.param, MAX_RADIUS_YD) : src.defaultR;
  }

  // The clouds of the range: a unit of CLOUD_SOURCES seen in one frame and not in the next (or at 1 % health or less) leaves a cloud at its
  // last place, at that frame's time. A unit still there in the last frame at some health has not died yet.
  // opts.endMs and opts.wipe: a unit that merely vanishes in the last seconds of a wipe or reset is the reset, not a death, and leaves no cloud.
  function clouds(frames, range, tactics, opts) {
    const o = opts || {};
    const out = [];
    const list = (Array.isArray(frames) ? frames : []).filter((f) => isObj(f) && num(f.t) !== null);
    const done = {};
    list.forEach((f, i) => {
      Object.keys(isObj(f.mobs) ? f.mobs : {}).forEach((id) => {
        const m = f.mobs[id];
        if (!isObj(m) || !CLOUD_SOURCES[m.e] || done[id] || num(m.x) === null || num(m.y) === null) return;
        const next = list[i + 1];
        const gone = !!next && !(isObj(next.mobs) && next.mobs[id]);
        const dying = num(m.hp) !== null && m.hp <= 1;
        if (!(gone || dying)) return;
        done[id] = true;
        if (!dying && o.wipe && num(o.endMs) !== null && f.t >= o.endMs - WIPE_TAIL_MS) return;
        if (!inRange(f.t, range) || out.length >= MAX_CLOUDS) return;
        out.push({ x: m.x, y: m.y, t: f.t, r: cloudRadius(tactics, m.e), label: "Son cloud" });
      });
    });
    return out;
  }

  // The clouds still on the map at a moment (fight ms): those that appeared at or before it and not more than CLOUD_MS ago.
  function cloudsAt(list, shownMs) {
    const t = num(shownMs);
    return (Array.isArray(list) ? list : []).filter((c) => t === null || (c.t <= t && t <= c.t + CLOUD_MS));
  }

  // The bot a Son of Hakkar targeted first inside the range, and its trail over the frames in which some Son targets it ([x, y, t]).
  function pullerPath(frames, range) {
    const list = framesIn(frames, range);
    // The bots (guids present in the frame) the Sons of the frame target, in the frame's order.
    const targets = (f) => {
      const out = [];
      Object.keys(isObj(f.mobs) ? f.mobs : {}).forEach((id) => {
        const m = f.mobs[id];
        if (isObj(m) && m.e === 11357 && num(m.v) && m.v > 0 && isObj(f.bots) && f.bots[String(m.v)] && out.indexOf(String(m.v)) < 0) out.push(String(m.v));
      });
      return out;
    };
    let guid = null;
    for (let i = 0; i < list.length && guid === null; i++) {
      const t = targets(list[i]);
      if (t.length) guid = t[0];
    }
    if (guid === null) return null;
    const pts = [];
    list.forEach((f) => {
      const b = isObj(f.bots) ? f.bots[guid] : null;
      if (b && num(b.x) !== null && num(b.y) !== null && targets(f).indexOf(guid) >= 0) pts.push([b.x, b.y, f.t]);
    });
    return pts.length ? { g: guid, pts } : null;
  }

  // ---- heat

  // Where hp was lost: per bot, the drop in hp percent between two consecutive frames (both alive) lands in the cell of the earlier
  // frame's place; each death in the range adds DEATH_HEAT at its place. At most HEAT_CELLS cells (the strongest), `w` scaled to 0-1.
  function heat(frames, fight, range, cell) {
    const size = num(cell) !== null && cell > 0 ? cell : HEAT_CELL_YD;
    const acc = {};
    const add = (x, y, w) => {
      if (!(w > 0)) return;
      const cx = Math.floor(x / size), cy = Math.floor(y / size), key = cx + "," + cy;
      if (!acc[key]) acc[key] = { cx, cy, w: 0 };
      acc[key].w += w;
    };
    const list = (Array.isArray(frames) ? frames : []).filter((f) => isObj(f) && num(f.t) !== null);
    for (let i = 0; i + 1 < list.length; i++) {
      const a = list[i], b = list[i + 1];
      if (!inRange(a.t, range) || !isObj(a.bots) || !isObj(b.bots)) continue;
      Object.keys(a.bots).forEach((g) => {
        const u = a.bots[g], v = b.bots[g];
        if (!isObj(u) || !isObj(v) || u.alive === false || v.alive === false || num(u.hp) === null || num(v.hp) === null) return;
        if (num(u.x) === null || num(u.y) === null) return;
        add(u.x, u.y, u.hp - v.hp);
      });
    }
    deathMarks(fight, range).forEach((d) => add(d.x, d.y, DEATH_HEAT));
    let cells = Object.keys(acc).map((k) => acc[k]);
    cells.sort((p, q) => q.w - p.w || p.cx - q.cx || p.cy - q.cy);
    cells = cells.slice(0, HEAT_CELLS);
    const max = cells.length ? cells[0].w : 0;
    return { cell: size, cells: cells.map((c) => [c.cx, c.cy, max > 0 ? c.w / max : 0]) };
  }

  // ---- the unit under the focus

  // The unit with this guid (a bot) or id (a mob) in one frame: { kind, ...unit } or null.
  function unitAt(frame, id) {
    if (!isObj(frame) || id == null) return null;
    const key = String(id);
    if (isObj(frame.bots) && isObj(frame.bots[key])) return Object.assign({ kind: "bot" }, frame.bots[key]);
    if (isObj(frame.mobs) && isObj(frame.mobs[key])) return Object.assign({ kind: "mob" }, frame.mobs[key]);
    return null;
  }

  // The filter a tap on a unit sets: every event the unit is the source or the target of. null for a name FdFilter would refuse.
  function focusGroup(name) {
    const v = typeof name === "string" ? name.trim() : "";
    if (!v || v.length > MAX_NAME) return null;
    const cond = (f) => ({ f, op: "is", v, not: false });
    return { op: "or", not: false, items: [cond("source"), cond("target")] };
  }

  // ---- fit

  const median = (list) => {
    const v = list.slice().sort((p, q) => p - q);
    return v.length ? v[v.length >> 1] : 0;
  };

  // The box the map fits when a fight opens: the unit positions of the frames (on the card's map: a position on another map is left out),
  // the deaths and the mechanics (an anchor circle with its radius, at most MAX_RADIUS_YD). A point farther than OUTLIER_YD from the median
  // of all of them is an outlier (a corrupt report) and does not count. null when nothing is left.
  function bounds(frames, marks, mapId) {
    const pts = [];       // [x, y, pad]
    (Array.isArray(frames) ? frames : []).forEach((f) => {
      if (!isObj(f) || !isObj(f.bots)) return;
      Object.keys(f.bots).slice(0, MAX_BOTS).forEach((g) => {
        const b = f.bots[g];
        if (!isObj(b) || !place(b.x) || !place(b.y)) return;
        if (num(mapId) && num(b.m) && b.m !== mapId) return;
        pts.push([b.x, b.y, 0]);
      });
    });
    const m = marks || {};
    (Array.isArray(m.deaths) ? m.deaths : []).forEach((d) => { if (isObj(d) && place(d.x) && place(d.y)) pts.push([d.x, d.y, 0]); });
    (Array.isArray(m.mechanics) ? m.mechanics : []).forEach((d) => {
      if (isObj(d) && place(d.x) && place(d.y)) pts.push([d.x, d.y, d.kind === "anchor" && num(d.r) !== null ? Math.max(0, Math.min(d.r, MAX_RADIUS_YD)) : 0]);
    });
    if (!pts.length) return null;
    const stride = Math.max(1, Math.ceil(pts.length / 4000));
    const sample = pts.filter((_, i) => i % stride === 0);
    const mx = median(sample.map((p) => p[0])), my = median(sample.map((p) => p[1]));
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    pts.forEach((p) => {
      if (Math.hypot(p[0] - mx, p[1] - my) > OUTLIER_YD) return;
      x0 = Math.min(x0, p[0] - p[2]); x1 = Math.max(x1, p[0] + p[2]);
      y0 = Math.min(y0, p[1] - p[2]); y1 = Math.max(y1, p[1] + p[2]);
    });
    return x1 >= x0 && y1 >= y0 ? { x0, y0, x1, y1 } : null;
  }

  // ---- the model of one view of one fight: everything the layers draw, computed only for the layers that are on.
  // in: { frames, fight, range, tactics, mapId, boss }; on: { trails, marks, heat, summons }
  function model(input, on) {
    const i = input || {}, flags = on || {};
    const frames = Array.isArray(i.frames) ? i.frames : [];
    const mech = flags.marks || i.fit ? mechanics(i.tactics, i.mapId, i.boss) : [];
    const out = { trails: {}, clouds: [], puller: null, mechanics: flags.marks ? mech : [], heat: null, bounds: null };
    if (flags.trails || flags.summons) {
      const all = trails(frames, i.range, { mobs: !!flags.summons, boss: i.boss, mapId: i.mapId });
      Object.keys(all).forEach((k) => {
        const isMob = k.indexOf("m:") === 0;
        if ((isMob && flags.summons) || (!isMob && flags.trails)) out.trails[k] = all[k];
      });
    }
    if (flags.marks) {
      const f = i.fight || {};
      out.clouds = clouds(frames, i.range, i.tactics, { endMs: num(f.dur_ms), wipe: f.outcome === "wipe" || f.outcome === "reset" });
      out.puller = pullerPath(frames, i.range);
    }
    if (flags.heat) out.heat = heat(frames, i.fight, i.range, HEAT_CELL_YD);
    if (i.fit) out.bounds = bounds(frames, { deaths: deathMarks(i.fight, null), mechanics: mech }, i.mapId);
    return out;
  }

  // ---- the Dungeons tab's pull pins

  // One pin per fight (the list rows of GET /api/v1/combat/fights: key, start in unix seconds, boss_name, outcome, live) at the lead's
  // first frame at or after the fight's start (FightReplay's frames, t in unix seconds). A fight the kept frames do not cover, or whose
  // lead is not in a frame within PIN_SLACK_S after the start, gets none.
  function pullPins(fights, trackFrames, leadGuid) {
    const out = [];
    const frames = (Array.isArray(trackFrames) ? trackFrames : []).filter((f) => isObj(f) && num(f.t) !== null).sort((a, b) => a.t - b.t);
    if (!frames.length || leadGuid == null) return out;
    const g = String(leadGuid);
    (Array.isArray(fights) ? fights : []).slice(0, 500).forEach((r) => {
      if (out.length >= MAX_PINS || !isObj(r) || typeof r.key !== "string" || num(r.start) === null) return;
      const f = frames.find((fr) => fr.t >= r.start && fr.t <= r.start + PIN_SLACK_S && isObj(fr.bots) && isObj(fr.bots[g]) && num(fr.bots[g].x) !== null && num(fr.bots[g].y) !== null);
      if (!f) return;
      const b = f.bots[g];
      out.push({ key: r.key, x: b.x, y: b.y, label: typeof r.boss_name === "string" && r.boss_name ? r.boss_name : "trash", wipe: r.outcome === "wipe", live: r.live === true });
    });
    return out;
  }

  const api = {
    SUMMONS, CLOUD_SOURCES, MAX_POINTS, JUMP_YD, MAX_MOBS, CLOUD_MS, HEAT_CELLS, HEAT_CELL_YD, MAX_PINS,
    fromTrack, fromLive, trails, deathMarks, deathTitle, clock, mechanics, cloudRadius, clouds, cloudsAt, pullerPath, heat, unitAt, focusGroup,
    bounds, model, pullPins,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightMap = api;
})(typeof self !== "undefined" ? self : this);
