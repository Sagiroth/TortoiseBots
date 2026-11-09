"use strict";
// FightMeters: the pure logic behind the Fight debugger's Meters panel (no DOM, no fetch). A filed fight's series comes from the
// daemon (GET /api/v1/combat/fight/<key>, `meters`: internal/meters/meters.go); a running fight's is built here from the 2 s live
// samples (fromSamples). Both are the same shape:
//   { v: 1, step_ms, n, src, bots: [{ g, d: [cumulative damage at the end of bucket i], h: [the same for healing], flat? }] }
// Bucket i ends at (i + 1) * step_ms of fight time. at() interpolates linearly inside a bucket.
(function (root) {
  const MAX_BUCKETS = 150;       // a series never has more buckets (the daemon cuts a 30 min fight into 150)
  const MIN_STEP_MS = 2000;      // the report interval
  const MAX_GAP_S = 4;           // the time one report stands for at most
  const FIRST_GAP_S = 2;         // what the first counted report stands for
  const DEFAULT_WIN_S = 10;      // the window a sample names none for
  const TOP = 10;                // rows of a ranking
  const LIVE_SRC = "live-window";

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : 0);

  // The bucket length for a fight of durMs: whole seconds, at least MIN_STEP_MS, never more than MAX_BUCKETS buckets
  // (internal/meters StepFor).
  function stepFor(durMs) {
    const d = Math.max(0, num(durMs));
    const ms = Math.ceil(d / (MAX_BUCKETS * 1000)) * 1000;
    return Math.max(MIN_STEP_MS, ms);
  }

  // The series of a running fight from its samples ({ t: ms (unix), bots: { guid: { dps, hps } }, w: window seconds }).
  // The rules are internal/meters/meters.go Build's, without its last step (a running fight has no exact totals to scale to):
  // a sample with window W stands for the W seconds before it, so its rate belongs to t - W/2 (W = 10 when it names none);
  // only samples whose shifted time lies in [0, durMs] count; each adds rate x dt, dt being the gap to the previous counted
  // sample (at most MAX_GAP_S, FIRST_GAP_S for the first); the amount lands in bucket floor(shifted / step), clamped to the
  // last; the buckets are summed cumulatively. Bots are the ones some counted sample listed, in order of first appearance.
  // Null without a duration.
  function fromSamples(samples, startMs, durMs) {
    const dur = Math.max(0, Math.floor(num(durMs)));
    if (!(dur > 0)) return null;
    const step = stepFor(dur);
    const n = Math.max(1, Math.ceil(dur / step));
    const order = [];
    const acc = {};
    let used = 0, prev = 0;
    (Array.isArray(samples) ? samples : []).forEach((s) => {
      if (!s) return;
      const win = num(s.w) || DEFAULT_WIN_S;
      const t = num(s.t) - num(startMs) - win * 500;
      if (t < 0 || t > dur) return;
      let dt = FIRST_GAP_S;
      if (used > 0) dt = Math.max(0, Math.min(MAX_GAP_S, (t - prev) / 1000));
      prev = t;
      used++;
      const b = Math.min(n - 1, Math.floor(t / step));
      const bots = s.bots && typeof s.bots === "object" ? s.bots : {};
      Object.keys(bots).forEach((g) => {
        const r = bots[g];
        if (!r) return;
        let a = acc[g];
        if (!a) { a = acc[g] = { d: new Array(n).fill(0), h: new Array(n).fill(0) }; order.push(g); }
        a.d[b] += num(r.dps) * dt;
        a.h[b] += num(r.hps) * dt;
      });
    });
    const cum = (arr) => {
      let run = 0;
      return arr.map((v) => { run += v; return Math.round(run); });
    };
    return {
      v: 1, step_ms: step, n, src: LIVE_SRC,
      bots: order.map((g) => ({ g: Number(g), d: cum(acc[g].d), h: cum(acc[g].h) })),
    };
  }

  // One cumulative curve at tMs: 0 before the start, linear inside a bucket, the last value from the end on. endMs (optional) is the
  // fight's own length: the last bucket usually runs past it, and the curve reaches its last value there.
  function curveAt(arr, step, tMs, endMs) {
    const n = Array.isArray(arr) ? arr.length : 0;
    const t = num(tMs);
    if (!n || !(step > 0) || t <= 0) return 0;
    const last = num(arr[n - 1]);
    const end = num(endMs) > 0 ? Math.min(num(endMs), n * step) : n * step;
    if (t >= end) return last;
    const k = Math.min(n - 1, Math.floor(t / step));
    const lo = k === 0 ? 0 : num(arr[k - 1]);
    const loT = k * step;
    const hiT = Math.min((k + 1) * step, end);
    if (!(hiT > loT)) return num(arr[k]);
    return lo + (num(arr[k]) - lo) * (t - loT) / (hiT - loT);
  }

  // Every bot's totals at tMs: [{ g, dmg, heal }] in the series' order (before 0 all zero, after the end the last value).
  function at(series, tMs, endMs) {
    if (!series || !Array.isArray(series.bots)) return [];
    const step = num(series.step_ms);
    return series.bots.map((b) => ({
      g: b.g, dmg: curveAt(b.d, step, tMs, endMs), heal: curveAt(b.h, step, tMs, endMs),
    }));
  }

  // What each bot did inside [from, to] (the difference of at()), with at least zero.
  function window(series, from, to, endMs) {
    const a = at(series, from, endMs), b = at(series, Math.max(num(from), num(to)), endMs);
    return b.map((r, i) => ({ g: r.g, dmg: Math.max(0, r.dmg - a[i].dmg), heal: Math.max(0, r.heal - a[i].heal) }));
  }

  // The top n of rows ({ g, dmg, heal }) by field ("dmg" or "heal"), biggest first: { g, name, role, v (rounded), pct (of the top, 0-100) }.
  // Names and roles come from the fight's bots (by guid); a bot the fight does not name is listed by its guid. Rows with nothing are left
  // out; ties go to the name.
  function rank(fight, rows, field, n) {
    const limit = n > 0 ? n : TOP;
    const byGuid = {};
    ((fight && fight.bots) || []).forEach((b) => { if (b && b.guid != null) byGuid[b.guid] = b; });
    const list = (Array.isArray(rows) ? rows : [])
      .map((r) => {
        const b = byGuid[r.g];
        return { g: r.g, name: b && typeof b.name === "string" && b.name ? b.name : "#" + r.g, role: b && typeof b.role === "string" ? b.role : "", v: Math.round(num(r[field])) };
      })
      .filter((r) => r.v > 0);
    list.sort((a, b) => b.v - a.v || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0) || a.g - b.g);
    const top = list.length ? list[0].v : 0;
    return list.slice(0, limit).map((r) => ({ g: r.g, name: r.name, role: r.role, v: r.v, pct: top ? Math.round(r.v * 100 / top) : 0 }));
  }

  // The fight's deaths up to tMs (t_ms <= tMs), newest first.
  function deathsBy(fight, tMs) {
    const t = num(tMs);
    return ((fight && fight.deaths) || [])
      .filter((d) => d && num(d.t_ms) <= t)
      .sort((a, b) => num(b.t_ms) - num(a.t_ms));
  }

  const api = { MAX_BUCKETS, MIN_STEP_MS, MAX_GAP_S, FIRST_GAP_S, DEFAULT_WIN_S, TOP, LIVE_SRC, stepFor, fromSamples, at, window, rank, deathsBy };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightMeters = api;
})(typeof self !== "undefined" ? self : this);
