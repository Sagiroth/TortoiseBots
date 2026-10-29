"use strict";
// Glide: the browser-side interpolation between the reported positions, so a dot walks from where it was shown to where the
// server says it is instead of jumping every report. Free of the page so a test can run it under node (internal/commandersjs).
// The map draw (app.js) reads it through Glide.apply; the report path feeds it through LiveView.noteReport.
(function (root) {
  const CAP_MS = 2500;   // a longer silence (a lost report, a paused page) does not stretch the glide past this
  const MIN_MS = 100;    // two reports closer than this still glide over at least this long
  const FRAME_MS = 33;   // the scheduler redraws one key's glide at most this often (about 30 fps)
  // The same jump rule as the trails (commanders.js, the daemon's JumpLimitYd): 12 yd of slack plus 20 yd for each second,
  // seconds counted from 1 to 30.
  const SLACK_YD = 12;
  const YD_PER_SEC = 20;
  const MAX_GAP_SEC = 30;

  function jumpLimitYd(gapMs) {
    const sec = Math.min(MAX_GAP_SEC, Math.max(1, (Number(gapMs) || 0) / 1000));
    return SLACK_YD + YD_PER_SEC * sec;
  }

  // One glide state for one commander's card: units, the time of the last report and the report gap.
  function create() {
    return { units: {}, lastAt: null, gap: 2000 };
  }

  const botKey = (guid) => "b:" + String(guid);
  const mobKey = (id) => "m:" + String(id);

  // Once per report: the gap the next note interpolates over is the time since the previous report.
  function sampled(st, nowMs) {
    if (!st) return;
    if (typeof st.lastAt === "number") st.gap = Math.min(CAP_MS, Math.max(MIN_MS, nowMs - st.lastAt));
    st.lastAt = nowMs;
  }

  // The position drawn for a unit now: from + (to - from) * min(1, elapsed / gap).
  function at(st, id, nowMs) {
    const u = st && st.units && st.units[String(id)];
    if (!u) return null;
    const t = u.gap > 0 ? Math.min(1, Math.max(0, (nowMs - u.t0) / u.gap)) : 1;
    return { x: u.fx + (u.tx - u.fx) * t, y: u.fy + (u.ty - u.fy) * t };
  }

  // A new target for one unit. The glide starts from the position on screen, not from the last report, so a report that
  // arrives late never jumps the dot back. It snaps (no interpolation) when the map changed, the unit died, or the distance
  // is more than walking or riding explains (a summon, a portal, a spirit release).
  function note(st, id, x, y, map, alive, nowMs) {
    if (!st || id == null) return;
    if (!st.units) st.units = {};
    const key = String(id);
    const prev = st.units[key];
    const live = alive !== false;
    if (!prev) {
      st.units[key] = { fx: x, fy: y, tx: x, ty: y, t0: nowMs, gap: st.gap, map: map, alive: live };
      return;
    }
    const shown = at(st, key, nowMs) || { x: prev.tx, y: prev.ty };
    const snap = map !== prev.map || !live || Math.hypot(shown.x - x, shown.y - y) > jumpLimitYd(st.gap);
    st.units[key] = {
      fx: snap ? x : shown.x, fy: snap ? y : shown.y,
      tx: x, ty: y, t0: nowMs, gap: st.gap, map: map, alive: live,
    };
  }

  // Whether any unit is still walking towards its target (the page keeps drawing while this is true).
  function active(st, nowMs) {
    const units = (st && st.units) || {};
    return Object.keys(units).some((k) => {
      const u = units[k];
      return u.alive !== false && u.t0 + u.gap > nowMs && (u.fx !== u.tx || u.fy !== u.ty);
    });
  }

  // A shallow copy of a binding whose bots' and mobs' x/y are the glided positions (the reported ones where nothing is
  // being glided). The binding itself is never changed: the state list is what the next report replaces.
  function apply(bind, st, nowMs) {
    if (!bind) return bind;
    const bots = Array.isArray(bind.bots)
      ? bind.bots.map((b) => {
        const p = b ? at(st, botKey(b.guid), nowMs) : null;
        return p ? Object.assign({}, b, { x: p.x, y: p.y }) : b;
      })
      : bind.bots;
    const mobs = Array.isArray(bind.mobs)
      ? bind.mobs.map((m) => {
        const p = m ? at(st, mobKey(m.id), nowMs) : null;
        return p ? Object.assign({}, m, { x: p.x, y: p.y }) : m;
      })
      : bind.mobs;
    return Object.assign({}, bind, { bots: bots, mobs: mobs });
  }

  const api = {
    CAP_MS, MIN_MS, FRAME_MS, SLACK_YD, YD_PER_SEC, MAX_GAP_SEC,
    jumpLimitYd, create, botKey, mobKey, sampled, at, note, active, apply,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.Glide = api;
})(typeof self !== "undefined" ? self : this);
