"use strict";
// WipeRecap: the pure logic behind the Fight debugger's "first 3 deaths" box (no DOM, no fetch). It reads what the page already
// has: the fight object (a filed fight's, or FdLive.fight's running one), the death recaps FightDebug.deathRecaps drew from it, and
// the positions track's frames (FightReplay.prep, or framesFromSamples for a running fight) for the health a bot had before it died.
// Everything here may be fed history or telemetry the daemon did not vet: every read is type-checked and every loop is bounded.
(function (root) {
  const SCAN_MAX = 5000;       // entries of a death list or a frame list looked at (a fight holds 200 deaths, a track 900 frames)
  const FRAMES_MAX = 2000;     // frames framesFromSamples makes (the page keeps 900 samples)
  const LEAD_MS = 1000;        // the health is read this long before the death: what the bot had before the hits that killed it
  const REACH_MS = 10000;      // and no older than this before that point
  const PRE_PULL_MS = 4000;    // samples from this long before the pull still count (a bot's health at the pull)
  const FIRST_N = 3;
  const N_MAX = 10;
  const CAST_KEEP = 20;        // casts per shown death that the signature reads
  const FIELD_MAX = 80;        // characters of one cast field that the signature reads

  const finite = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const isObj = (v) => v !== null && typeof v === "object";

  // The fight's bots by name, role kept; the number of bots.
  function botsOf(fight) {
    return isObj(fight) && Array.isArray(fight.bots) ? fight.bots.slice(0, SCAN_MAX).filter(isObj) : [];
  }

  // A wipe: the fight's outcome says so, or at least half of its bots (distinct names) died. A running fight has no outcome yet
  // ("live"), and a filed fight's outcome can be wrong, so the count decides there too. With no bots known the count says nothing.
  function isWipe(fight) {
    if (!isObj(fight)) return false;
    if (fight.outcome === "wipe") return true;
    const bots = botsOf(fight);
    if (!bots.length) return false;
    const known = {};
    bots.forEach((b) => { if (typeof b.name === "string") known[b.name] = true; });
    const dead = {};
    let n = 0;
    (Array.isArray(fight.deaths) ? fight.deaths.slice(0, SCAN_MAX) : []).forEach((d) => {
      if (!isObj(d) || typeof d.name !== "string" || !known[d.name] || dead[d.name]) return;
      dead[d.name] = true;
      n++;
    });
    return n >= Math.ceil(bots.length / 2);
  }

  // The first n deaths by time (ties by name, then by the recap's own order), each as one row. `i` is the recap's index, so the
  // page's death handler (which selects fight.deaths[i]) works on it. `hits` is the number of hits the recap holds (a running
  // fight's deaths have none), `spell` the name of its killing spell ("" when none) and `likely` whether that is a guess.
  function firstDeaths(fight, recaps, n) {
    const want = finite(n) === null ? FIRST_N : Math.max(1, Math.min(N_MAX, Math.floor(n)));
    const role = {};
    botsOf(fight).forEach((b) => { if (typeof b.name === "string" && typeof b.role === "string") role[b.name] = b.role; });
    const rows = [];
    (Array.isArray(recaps) ? recaps.slice(0, SCAN_MAX) : []).forEach((r, at) => {
      if (!isObj(r) || typeof r.name !== "string" || finite(r.t_ms) === null) return;
      const sp = isObj(r.spell) && typeof r.spell.name === "string" ? r.spell : null;
      rows.push({
        i: finite(r.i) !== null ? r.i : at,
        name: r.name,
        role: Object.prototype.hasOwnProperty.call(role, r.name) ? role[r.name] : "",
        t_ms: r.t_ms,
        killer: typeof r.killer === "string" && r.killer ? r.killer : "unknown",
        spell: sp ? sp.name : "",
        likely: !!(sp && (r.likely || sp.likely)),
        total: finite(r.total) !== null ? r.total : 0,
        hits: Array.isArray(r.hits) ? r.hits.length : 0,
        guid: finite(r.guid) !== null ? r.guid : undefined,
      });
    });
    rows.sort((a, b) => (a.t_ms - b.t_ms) || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0) || (a.i - b.i));
    return rows.slice(0, want);
  }

  // The bot's health in percent at the last frame at or before tMs - 1 s (tMs is fight time; fightStartMs is the fight's start on
  // the frames' own clock, unix ms), no older than 10 s before that point, and only a frame where the bot was alive. Frames are the
  // replay's: { t: unix seconds, bots: { guid: { hp, alive } } }; the bot is found by `guid`, else by name through track.names.
  // null when no frame qualifies.
  function hpBefore(track, name, tMs, fightStartMs, guid) {
    const t = finite(tMs), s = finite(fightStartMs);
    if (t === null || s === null) return null;
    const frames = Array.isArray(track) ? track : isObj(track) && Array.isArray(track.frames) ? track.frames : null;
    if (!frames) return null;
    let g = finite(guid) !== null && guid !== 0 ? String(guid) : null;
    if (g === null && isObj(track) && isObj(track.names) && typeof name === "string") {
      const ks = Object.keys(track.names);
      for (let k = 0; k < ks.length && k < SCAN_MAX; k++) if (track.names[ks[k]] === name) { g = ks[k]; break; }
    }
    if (g === null) return null;
    const target = s + t - LEAD_MS;
    let best = null, bestAt = -Infinity;
    const lim = Math.min(frames.length, SCAN_MAX);
    for (let k = 0; k < lim; k++) {
      const f = frames[k];
      if (!isObj(f)) continue;
      const ft = finite(f.t);
      if (ft === null) continue;
      const at = ft * 1000;
      if (at > target || at < target - REACH_MS || at <= bestAt) continue;
      const row = isObj(f.bots) && Object.prototype.hasOwnProperty.call(f.bots, g) ? f.bots[g] : null;
      if (!isObj(row) || row.alive === false || finite(row.hp) === null) continue;
      best = Math.round(row.hp);
      bestAt = at;
    }
    return best;
  }

  // dash-0's live samples (t in unix ms; bots by guid with hp % and a = 1 alive / 0 dead) as the frames hpBefore reads. Samples older
  // than 4 s before the pull are left out; at most the newest FRAMES_MAX are made. A row without a number is dropped.
  function framesFromSamples(samples, startMs) {
    const s = finite(startMs);
    const list = Array.isArray(samples) ? samples.slice(-FRAMES_MAX) : [];
    const frames = [];
    list.forEach((sm) => {
      if (!isObj(sm)) return;
      const t = finite(sm.t);
      if (t === null || (s !== null && s > 0 && t < s - PRE_PULL_MS) || !isObj(sm.bots)) return;
      const bots = {};
      Object.keys(sm.bots).slice(0, SCAN_MAX).forEach((g) => {
        const b = sm.bots[g];
        if (g === "__proto__" || !isObj(b) || finite(b.hp) === null) return;
        bots[g] = { hp: b.hp, alive: b.a !== 0 };
      });
      frames.push({ t: t / 1000, bots });
    });
    return { frames, names: {} };
  }

  // What the box would draw, in a short string: the outcome, whether it is a wipe, and for a wipe the first three deaths (index,
  // name, time, killer, role) with what the recap rows are made of: each death's hits (their number and the last one's time and spell,
  // which the damage and the killing spell come from) and the identity of the casts in the 3 s before it (the likely spell is read from
  // them). The page compares it between ticks, so a death, a role, a hit or a cast that changed with the same counts still repaints.
  // Bounded like every read here: the newest SCAN_MAX log entries are scanned, and the newest CAST_KEEP casts by time are kept.
  function signature(fight) {
    if (!isObj(fight)) return "";
    const head = String(fight.outcome);
    if (!isWipe(fight)) return head + "|n";
    const role = {};
    botsOf(fight).forEach((b) => { if (typeof b.name === "string" && typeof b.role === "string") role[b.name] = b.role; });
    const list = [];
    (Array.isArray(fight.deaths) ? fight.deaths.slice(0, SCAN_MAX) : []).forEach((d, i) => {
      if (isObj(d) && typeof d.name === "string" && finite(d.t_ms) !== null) list.push({ i, name: d.name, t: d.t_ms, by: d.killer == null ? "" : String(d.killer) });
    });
    list.sort((a, b) => (a.t - b.t) || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0) || (a.i - b.i));
    const log = Array.isArray(fight.log) ? fight.log.slice(-SCAN_MAX) : []; // the newest entries: a long fight's old ones are not the ones that change
    const shown = list.slice(0, FIRST_N);
    const rawDeaths = Array.isArray(fight.deaths) ? fight.deaths : [];
    const parts = shown.map((d) => {
      // The casts deathRecaps may pick the likely spell from: every cast in the 3 s before the death, each by its time, caster, target
      // and spell (each field cut to FIELD_MAX), the newest CAST_KEEP of them BY TIME (the log's order is not time's), so a cast replaced
      // by another at the same count still changes it.
      const found = [];
      log.forEach((e, at) => {
        if (!isObj(e) || e.k !== "cast" || finite(e.t) === null || e.t > d.t || e.t < d.t - 3000) return;
        found.push({ t: e.t, at, id: [e.t, e.n, e.tg, e.tn, e.s, e.sid].map((v) => (v == null ? "" : String(v).slice(0, FIELD_MAX))).join("\u0003") });
      });
      found.sort((a, b) => (a.t - b.t) || (a.at - b.at));
      const casts = found.slice(-CAST_KEEP).map((c) => c.id);
      // The hits the recap's damage and killing spell come from: how many, and the last one's time and spell.
      const hits = isObj(rawDeaths[d.i]) && Array.isArray(rawDeaths[d.i].hits) ? rawDeaths[d.i].hits.slice(0, SCAN_MAX).filter((h) => isObj(h) && finite(h.t_ms) !== null) : [];
      let last = null;
      hits.forEach((h) => { if (!last || h.t_ms >= last.t_ms) last = h; });
      const hitId = hits.length + "," + (last ? [last.t_ms, last.s, last.sid, last.d].map((v) => (v == null ? "" : String(v).slice(0, FIELD_MAX))).join("\u0003") : "");
      return [d.i, d.name, d.t, d.by, Object.prototype.hasOwnProperty.call(role, d.name) ? role[d.name] : "", hitId, casts.join("\u0004")].join("\u0001");
    });
    return head + "|w|" + parts.join("\u0002");
  }

  // The fight's start in unix ms: its own start_ms when it has one (a running fight's header does), else its whole-second start.
  function startMs(fight) {
    if (!isObj(fight)) return null;
    if (finite(fight.start_ms) !== null && fight.start_ms > 0) return fight.start_ms;
    return finite(fight.start) !== null ? fight.start * 1000 : null;
  }

  // m:ss of a fight time in ms.
  function clock(ms) {
    const v = finite(ms);
    const s = v === null || v < 0 ? 0 : Math.floor(v / 1000);
    return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
  }

  const api = { FIRST_N, isWipe, firstDeaths, hpBefore, framesFromSamples, signature, startMs, clock };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.WipeRecap = api;
})(typeof self !== "undefined" ? self : this);
