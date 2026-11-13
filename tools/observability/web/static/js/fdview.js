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

  // The first render of a handed-over view that has its fight: the fixed range is fitted to the fight's length. The live clock can
  // lead the filed length by a few seconds, so a range that runs past the end is shifted left to keep its width (held at 0), not
  // clipped end by end; it is dropped only when the fight is shorter than MIN_RANGE_MS.
  function reclamp(view, durMs) {
    const v = view || create(null);
    if (!v.reclamp) return v;
    if (!(durMs > 0)) return v;
    let r = null;
    if (v.range) {
      let from = v.range.from, to = v.range.to;
      if (to > durMs) { const width = to - from; to = durMs; from = Math.max(0, to - width); }
      r = clampRange(from, to, durMs);
    }
    return { key: v.key, range: r, follow: v.follow !== false, reclamp: false };
  }

  // ---- the share link: the whole view of one fight as URL parameters (the keys below, in this order). toQuery writes only what differs
  // from the defaults; fromQuery reads a URL the page never trusted: every key is checked alone and a bad one is dropped (and
  // named in `bad`), so a hostile or stale link restores what it can and never throws.
  const KEYS = ["r", "f", "au", "mk", "n", "rp", "sp", "lw", "lay"];
  const MAX_MS = 86400000;     // a fight is shorter than a day
  const MAX_SID = 10000000;    // a spell id
  const MAX_NAME = 80;         // characters of a marker's spell name
  const DEFAULT_LW = 60;       // the live window's default, in seconds (it is not written)
  const RAW_MAX = 2100;        // a parameter longer than this is not read at all (the filter's own text is capped at 2000)

  // The helper modules are looked up when used: fdview.js loads before them in the page, and fdfilter.js needs this file in turn.
  function lib(name, file) {
    return root[name] || (typeof require === "function" ? require(file) : null);
  }
  const live = () => lib("FdLive", "./fdlive.js");
  const replay = () => lib("FightReplay", "./fightreplay.js");
  const filters = () => lib("FdFilter", "./fdfilter.js");
  const layouts = () => lib("FdLayout", "./fdlayout.js");

  const isInt = (n, lo, hi) => typeof n === "number" && isFinite(n) && Math.floor(n) === n && n >= lo && n <= hi;
  const DAY_RE = /^\d{4}-\d{2}-\d{2}$/;

  // The marker of the Auras panel in one URL value: deaths, s<spell id> or n<spell name>; "" for none or a malformed one.
  function markText(m) {
    if (m === "deaths") return "deaths";
    if (m && typeof m === "object") {
      if (isInt(m.sid, 1, MAX_SID)) return "s" + m.sid;
      if (typeof m.name === "string" && m.name.length >= 1 && m.name.length <= MAX_NAME) return "n" + m.name;
    }
    return "";
  }

  // v = { range, filter, au, marker, night, rp, sp, lw, lay, live, follow } -> the query text, no leading "?". A running fight (live) has no
  // replay and no night, and writes its range only while the reader has fixed it (follow === false).
  function toQuery(v) {
    v = v || {};
    const out = [];
    const add = (k, val) => { out.push(k + "=" + encodeURIComponent(String(val))); };
    const r = v.range;
    if (r && (!v.live || v.follow === false)) {
      const from = Math.round(Number(r.from)), to = Math.round(Number(r.to));
      if (isInt(from, 0, MAX_MS) && isInt(to, 0, MAX_MS) && from < to) add("r", from + "-" + to);
    }
    const F = filters();
    if (v.filter && F && !F.isEmpty(v.filter)) {
      const f = F.encode(v.filter);
      if (f) add("f", f);
    }
    if (isInt(v.au, 1, MAX_SID)) add("au", v.au);
    const mk = markText(v.marker);
    if (mk) add("mk", mk);
    if (!v.live) {
      if (typeof v.night === "string" && DAY_RE.test(v.night)) add("n", v.night);
      if (isInt(v.rp, 0, MAX_MS)) add("rp", v.rp);
      const R = replay();
      if (typeof v.sp === "number" && v.sp !== 1 && R && R.SPEEDS.indexOf(v.sp) >= 0) add("sp", v.sp);
    }
    const L = live();
    if (typeof v.lw === "number" && v.lw !== DEFAULT_LW && L && L.WINDOWS.indexOf(v.lw) >= 0) add("lw", v.lw);
    // The page's panel layout (one for the page, not per fight): written only while a chosen layout is on; the default grid has none.
    const Y = layouts();
    if (v.lay && Y) add("lay", Y.encode(v.lay));
    return out.join("&");
  }

  const rawOf = (params, k) => {
    const v = params ? (typeof params.get === "function" ? params.get(k) : params[k]) : null;
    if (v === null || v === undefined) return null;
    return typeof v === "string" || typeof v === "number" ? String(v) : "";
  };

  // The query of a link (URLSearchParams or a plain object) -> the same shape toQuery takes, plus `bad`: the keys that were present
  // but could not be read. opts.live says which kind of link it is: in a running fight's link n, rp and sp mean nothing, and in a
  // finished fight's lw does not, so those are dropped into `bad`; with no opts every key is read.
  function fromQuery(params, opts) {
    const isLive = !!(opts && opts.live === true), finished = !!(opts && opts.live === false);
    const out = { range: null, filter: null, au: null, marker: null, night: null, rp: null, sp: null, lw: null, lay: null, live: isLive, follow: true, bad: [] };
    KEYS.forEach(k => {
      const raw = rawOf(params, k);
      if (raw === null) return;
      let ok = false;
      if (raw.length > 0 && raw.length <= RAW_MAX) {
        let m;
        if (k === "r") {
          if ((m = /^(\d{1,9})-(\d{1,9})$/.exec(raw))) {
            const from = Number(m[1]), to = Number(m[2]);
            if (from < to && to <= MAX_MS) { out.range = { from, to }; if (isLive) out.follow = false; ok = true; }
          }
        } else if (k === "f") {
          const F = filters(), g = F ? F.decode(raw) : null;
          if (g) { out.filter = g; ok = true; }
        } else if (k === "au") {
          if (/^\d{1,8}$/.test(raw) && isInt(Number(raw), 1, MAX_SID)) { out.au = Number(raw); ok = true; }
        } else if (k === "mk") {
          if (raw === "deaths") { out.marker = "deaths"; ok = true; }
          else if ((m = /^s(\d{1,8})$/.exec(raw)) && isInt(Number(m[1]), 1, MAX_SID)) { out.marker = { sid: Number(m[1]) }; ok = true; }
          else if (raw.charAt(0) === "n" && raw.length - 1 <= MAX_NAME && raw.length > 1 && !/[\u0000-\u001f\u007f]/.test(raw)) { out.marker = { name: raw.slice(1) }; ok = true; }
        } else if (k === "n") {
          if (DAY_RE.test(raw) && !isLive) { out.night = raw; ok = true; }
        } else if (k === "rp") {
          if (/^\d{1,9}$/.test(raw) && isInt(Number(raw), 0, MAX_MS) && !isLive) { out.rp = Number(raw); ok = true; }
        } else if (k === "sp") {
          const R = replay();
          if (/^\d+(\.\d+)?$/.test(raw) && R && R.SPEEDS.indexOf(Number(raw)) >= 0 && !isLive) { out.sp = Number(raw); ok = true; }
        } else if (k === "lw") {
          const L = live();
          if (/^\d{1,4}$/.test(raw) && L && L.WINDOWS.indexOf(Number(raw)) >= 0 && !finished) { out.lw = Number(raw); ok = true; }
        } else if (k === "lay") {
          const Y = layouts(), g = Y ? Y.decode(raw) : null;
          if (g) { out.lay = g; ok = true; }
        }
      }
      if (!ok) out.bad.push(k);
    });
    return out;
  }

  // A copy of the query without the view's own keys: a new fight starts with a clean view, but ?bot= and ?p= stay.
  function stripFd(params) {
    if (params && typeof params.get === "function" && typeof URLSearchParams !== "undefined") {
      const q = new URLSearchParams();
      params.forEach((val, key) => { if (KEYS.indexOf(key) < 0) q.append(key, val); });
      return q;
    }
    const q = {};
    Object.keys(params || {}).forEach(key => { if (KEYS.indexOf(key) < 0) q[key] = params[key]; });
    return q;
  }

  const api = { KEYS, DEFAULT_LW, toQuery, fromQuery, stripFd, MIN_RANGE_MS, STEP_MS, pxToMs, clampRange, inRange, overlapMs, label, bandStyle, create, effective, setRange, clear, handover, reclamp };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdView = api;
})(typeof self !== "undefined" ? self : this);
