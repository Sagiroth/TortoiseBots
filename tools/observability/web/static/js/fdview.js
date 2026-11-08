"use strict";
// FdView: the pure state and arithmetic of the Fight debugger's time range. A range is {from, to} in fight milliseconds (null: the
// whole fight); one range filters every panel that has time data. The page draws it as a drag-select on the timeline, the lanes and
// the aura strip. No DOM, no fetch, no timers: unit-tested by internal/commandersjs/fdview_check.js under node.
(function (root) {
  const MIN_RANGE_MS = 1000; // a range shorter than this is a click, not a selection
  const STEP_MS = 100;       // a drag lands on tenths of a second

  function num(v) {
    return typeof v === "number" && isFinite(v) ? v : null;
  }

  // The pixel x inside a bar of `width` px that stands for a fight of durMs, in fight milliseconds on the 100 ms grid.
  function pxToMs(x, width, durMs) {
    if (!(width > 0) || !(durMs > 0) || num(x) === null) return 0;
    const f = Math.max(0, Math.min(1, x / width));
    return Math.round((f * durMs) / STEP_MS) * STEP_MS;
  }

  // Two ends in any order, held inside the fight; null when they are not numbers, the fight has no length or the range is shorter
  // than MIN_RANGE_MS.
  function clampRange(a, b, durMs) {
    if (num(a) === null || num(b) === null || num(durMs) === null || durMs <= 0) return null;
    let from = Math.min(a, b), to = Math.max(a, b);
    from = Math.max(0, Math.min(durMs, from));
    to = Math.max(0, Math.min(durMs, to));
    if (to - from < MIN_RANGE_MS) return null;
    return { from, to };
  }

  function inRange(t, r) {
    return !r || (r.from <= t && t <= r.to);
  }

  // The length of [a0, a1] that lies inside r (all of it when r is null).
  function overlapMs(a0, a1, r) {
    if (!r) return Math.max(0, a1 - a0);
    return Math.max(0, Math.min(a1, r.to) - Math.max(a0, r.from));
  }

  function clock(ms) {
    const s = Math.max(0, Math.floor(ms / 1000));
    return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
  }

  // "m:ss–m:ss (N s)", the length in tenths under 10 s.
  function label(r) {
    if (!r) return "";
    const len = (r.to - r.from) / 1000;
    const n = len < 10 ? String(Math.round(len * 10) / 10) : String(Math.round(len));
    return clock(r.from) + "–" + clock(r.to) + " (" + n + " s)";
  }

  // The inline style of the band that shows r over a bar standing for durMs.
  function bandStyle(r, durMs) {
    if (!r || !(durMs > 0)) return "left:0.00%;width:0.00%";
    const l = Math.max(0, Math.min(100, (r.from / durMs) * 100));
    const w = Math.max(0, Math.min(100 - l, ((r.to - r.from) / durMs) * 100));
    return "left:" + l.toFixed(2) + "%;width:" + w.toFixed(2) + "%";
  }

  // ---- the view of one fight: { key, range, follow, reclamp }. `follow` only matters for a running fight (the range then is
  // the follow window); `reclamp` is set by the hand-over: a fixed range is fitted to the filed fight on its first render.
  function create(key) {
    return { key: key == null ? null : key, range: null, follow: true, reclamp: false };
  }

  // The range a panel draws: a post-fight view shows its own range; a running fight shows the follow window while following, else
  // the fixed range.
  function effective(view, live, followRange) {
    const v = view || create(null);
    if (!live) return v.range || null;
    return v.follow !== false ? followRange || null : v.range || null;
  }

  // A drag or a Select button: the range, held inside the fight. A live view stops following. Too short a range changes nothing.
  function setRange(view, a, b, durMs, live) {
    const r = clampRange(a, b, durMs);
    const v = view || create(null);
    if (!r) return v;
    return { key: v.key, range: r, follow: live ? false : v.follow !== false, reclamp: false };
  }

  // Clear (and, in a live view, Follow live and the window picker): no fixed range, the view follows the fight again.
  function clear(view) {
    const v = view || create(null);
    return { key: v.key, range: null, follow: true, reclamp: false };
  }

  // The live fight was filed: the view moves to the filed key. A following view becomes the whole fight; a fixed range is kept and
  // fitted to the filed fight when it is first drawn.
  function handover(view, filedKey) {
    const v = view || create(null);
    const fixed = v.follow === false && !!v.range;
    return { key: filedKey, range: fixed ? { from: v.range.from, to: v.range.to } : null, follow: v.follow !== false, reclamp: fixed };
  }

  // The first render of a handed-over view that has its fight: the fixed range is fitted to the fight's length.
  function reclamp(view, durMs) {
    const v = view || create(null);
    if (!v.reclamp) return v;
    if (!(durMs > 0)) return v;
    const r = v.range ? clampRange(v.range.from, v.range.to, durMs) : null;
    return { key: v.key, range: r, follow: v.follow !== false, reclamp: false };
  }

  const api = { MIN_RANGE_MS, STEP_MS, pxToMs, clampRange, inRange, overlapMs, label, bandStyle, create, effective, setRange, clear, handover, reclamp };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdView = api;
})(typeof self !== "undefined" ? self : this);
