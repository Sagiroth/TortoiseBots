"use strict";
// Fight replay: the pure logic behind the Fight debugger's replay of a finished fight, fed by the daemon's kept
// positions (GET /api/v1/commanders/track, one frame per COMMANDERS report). Positions are interpolated between the
// samples (straight lines, every 2 s), a unit that jumped or changed map is held until its next report, and a unit
// that vanished is held for a moment so the drawn group does not flicker.
// No DOM, no fetch, no markup: unit-tested by internal/commandersjs/fightreplay_check.js under node.
(function (root) {
  const TICK_MS = 250;                 // one playback step; a setTimeout chain, never an interval
  const SPEEDS = [0.5, 1, 2, 4];
  const HOLD_S = 3;                    // how long a unit that left the report stays drawn
  // Same trail-break numbers as the daemon's JumpLimitYd (internal/state/commanders.go): what a unit can walk in a gap.
  const TRAVEL_SLACK_YD = 12;
  const TRAVEL_MAX_YD_PER_SEC = 20;
  const TRAVEL_MAX_GAP_SEC = 30;

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

  function jumpLimitYd(gapS) {
    let gap = num(gapS);
    if (gap === null) gap = 1;
    if (gap < 1) gap = 1;
    if (gap > TRAVEL_MAX_GAP_SEC) gap = TRAVEL_MAX_GAP_SEC;
    return TRAVEL_SLACK_YD + TRAVEL_MAX_YD_PER_SEC * gap;
  }

  // The wire shape of 3.2 into frames of maps: bots by guid, mobs by id. Bad rows are dropped, not fatal.
  function prep(json) {
    const src = json && Array.isArray(json.frames) ? json.frames : [];
    const frames = [];
    src.forEach((raw) => {
      if (!raw || typeof raw !== "object") return;
      const t = num(raw.t);
      if (t === null) return;
      const map = num(raw.map) === null ? 0 : raw.map;
      const bots = {};
      (Array.isArray(raw.bots) ? raw.bots : []).forEach((row) => {
        if (!Array.isArray(row) || row.length < 3) return;
        const guid = num(row[0]);
        const x = num(row[1]);
        const y = num(row[2]);
        if (!guid || x === null || y === null) return;
        const hp = num(row[3]);
        bots[guid] = {
          x: x,
          y: y,
          hp: hp === null ? 0 : hp,
          alive: row[4] === 1 || row[4] === true,
          map: num(row[5]) === null ? map : row[5],
        };
      });
      const mobs = {};
      (Array.isArray(raw.mobs) ? raw.mobs : []).forEach((m) => {
        if (!m || typeof m !== "object" || m.id === undefined || m.id === null || m.id === "") return;
        const e = num(m.e);
        const x = num(m.x);
        const y = num(m.y);
        if (e === null || x === null || y === null) return;
        const hp = num(m.hp);
        const v = num(m.v);
        mobs[String(m.id)] = {
          e: e,
          x: x,
          y: y,
          hp: hp === null ? 0 : hp,
          v: v === null ? 0 : v,
          vo: m.vo ? 1 : 0,
          rk: typeof m.rk === "string" ? m.rk : "",
        };
      });
      frames.push({
        t: t,
        map: map,
        step: num(raw.step) === null ? 0 : raw.step,
        fight: raw.fight === true,
        bots: bots,
        mobs: mobs,
      });
    });
    frames.sort((a, b) => a.t - b.t);
    const names = {};
    if (json && json.names && typeof json.names === "object") {
      Object.keys(json.names).forEach((k) => { names[k] = String(json.names[k]); });
    }
    return {
      frames: frames,
      names: names,
      t0: frames.length ? frames[0].t : 0,
      t1: frames.length ? frames[frames.length - 1].t : 0,
    };
  }

  // The fight's window over the kept frames. gapMaxS is the largest hole between neighbouring reports around it, so the page
  // can say out loud when the daemon was not reporting. ok = at least two samples: below that there is nothing to interpolate.
  function window(track, fight) {
    const startS = (num(fight && fight.start) || 0);
    const endS = startS + (num(fight && fight.dur_ms) || 0) / 1000;
    const frames = (track && Array.isArray(track.frames)) ? track.frames : [];
    let inside = 0;
    let prev = null;
    let gapMaxS = 0;
    frames.forEach((f) => {
      if (f.t >= startS && f.t <= endS) inside += 1;
      if (f.t < startS - 4 || f.t > endS + 4) return;
      if (prev !== null) gapMaxS = Math.max(gapMaxS, f.t - prev);
      prev = f.t;
    });
    return { startS: startS, endS: endS, frames: inside, gapMaxS: gapMaxS, ok: inside >= 2 };
  }

  function coverageText(win) {
    if (!win || !win.ok) return "No positions kept for this fight";
    let s = "positions every 2 s · " + win.frames + " reports";
    if (win.gapMaxS > 3) s += " · gap " + Math.round(win.gapMaxS) + " s";
    return s;
  }

  function botOut(u) {
    return { x: u.x, y: u.y, hp: Math.round(u.hp), alive: u.alive, map: u.map };
  }

  function mobOut(u, names) {
    return { e: u.e, n: names[u.e] || "", x: u.x, y: u.y, hp: Math.round(u.hp), v: u.v, vo: u.vo, rk: u.rk };
  }

  // The units of one kind at tS: interpolation between the two bracketing reports, a hold across a jump, a map change or a
  // unit's last report, and absence for a unit that only appears in the later report.
  function at(track, tS, wantMobs) {
    const out = {};
    if (!track || !Array.isArray(track.frames) || !track.frames.length) return out;
    const frames = track.frames;
    let t = num(tS);
    if (t === null) return out;
    if (t < track.t0) t = track.t0;
    if (t > track.t1) t = track.t1;
    let i = 0;
    while (i + 1 < frames.length && frames[i + 1].t <= t) i += 1;
    const a = frames[i];
    const b = frames[i + 1];
    const units = wantMobs ? a.mobs : a.bots;
    Object.keys(units).forEach((key) => {
      const u = units[key];
      const mapA = wantMobs ? a.map : u.map;
      const later = b ? (wantMobs ? b.mobs : b.bots)[key] : undefined;
      if (!later) {
        if (t - a.t <= HOLD_S) out[key] = wantMobs ? mobOut(u, track.names) : botOut(u);
        return;
      }
      const mapB = wantMobs ? b.map : later.map;
      const gap = b.t - a.t;
      if (mapA !== mapB || Math.hypot(later.x - u.x, later.y - u.y) > jumpLimitYd(gap)) {
        out[key] = wantMobs ? mobOut(u, track.names) : botOut(u);
        return;
      }
      const f = gap > 0 ? (t - a.t) / gap : 0;
      const ux = u.x + (later.x - u.x) * f;
      const uy = u.y + (later.y - u.y) * f;
      const hp = Math.round(u.hp + (later.hp - u.hp) * f);
      if (wantMobs) {
        out[key] = { e: u.e, n: track.names[u.e] || "", x: ux, y: uy, hp: hp, v: u.v, vo: u.vo, rk: u.rk };
      } else {
        out[key] = { x: ux, y: uy, hp: hp, alive: u.alive, map: u.map };
      }
    });
    return out;
  }

  function botsAt(track, tS) { return at(track, tS, false); }
  function mobsAt(track, tS) { return at(track, tS, true); }

  function nextSpeed(s) {
    const i = SPEEDS.indexOf(s);
    return i < 0 ? 1 : SPEEDS[(i + 1) % SPEEDS.length];
  }

  function clock(ms, durMs) {
    const t = num(ms) === null ? 0 : Math.max(0, ms);
    const d = num(durMs) === null ? 0 : Math.max(0, durMs);
    return (t / 1000).toFixed(1) + " / " + (d / 1000).toFixed(1) + " s";
  }

  // The run timeline's clock: whole minutes and seconds, "m:ss of m:ss", as the run strip reads it.
  function spanClock(ms, durMs) {
    const fmt = (v) => {
      const s = Math.max(0, Math.round((num(v) === null ? 0 : v) / 1000));
      return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
    };
    return fmt(ms) + " of " + fmt(durMs);
  }

  const api = {
    TICK_MS, SPEEDS, HOLD_S,
    jumpLimitYd, prep, window, botsAt, mobsAt, nextSpeed, clock, spanClock, coverageText,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightReplay = api;
})(typeof self !== "undefined" ? self : this);
