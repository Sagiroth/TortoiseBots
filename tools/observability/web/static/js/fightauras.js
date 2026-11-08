"use strict";
// FightAuras: the pure logic behind the Fight debugger's Auras panel. It turns a fight's aura rows (the module's watched-aura
// changes of a finished fight, or the rows FdLive rebuilds from the 2 s boss-debuff samples of a running one) into uptime
// intervals, per-bot and raid uptime, the boss-cast markers on the strip, who had the aura at each cast, per-cycle uptime and
// the application lengths. No DOM, no fetch, no markup: unit-tested by internal/commandersjs/fightauras_check.js under node.
(function (root) {
  const CAP_ROWS = 1000; // the module keeps the newest 1000 aura rows of a fight
  const MAX_BOT_ROWS = 40;

  // Names of the auras the module watches (and of the debuffs the presets read); another id is "spell <id>" unless the caller
  // knows it (the live view learns names from the bots' debuff lists).
  const AURA_NAMES = {
    24210: "Mark of Arlokk", 24306: "Delusions of Jin'do", 24466: "Banish", 24321: "Poisonous Blood",
    24327: "Cause Insanity", 24328: "Corrupted Blood", 17172: "Hex", 24261: "Brainwash",
  };
  // Boss presets: the aura to open on and the boss cast to mark (core: boss_hakkar.cpp Blood Siphon 24324 / Poisonous Blood
  // 24321; boss_jindo.cpp Hex 17172, Delusions of Jin'do 24306).
  const PRESETS = {
    14834: { auras: [24321], marker: { sid: 24324, name: "Blood Siphon" } },
    11380: { auras: [17172, 24306], marker: { sid: 17172, name: "Hex" } },
  };

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

  function auraName(sid, names) {
    if (Object.prototype.hasOwnProperty.call(AURA_NAMES, sid)) return AURA_NAMES[sid];
    if (names && typeof names === "object" && Object.prototype.hasOwnProperty.call(names, sid) && typeof names[sid] === "string" && names[sid]) return names[sid];
    return "spell " + sid;
  }

  const durOf = (fight) => Math.max(0, num(fight && fight.dur_ms) || 0);
  const bots = (fight) => (fight && Array.isArray(fight.bots) ? fight.bots.filter((b) => b && b.guid != null) : []);
  const overlap = (a0, a1, b0, b1) => Math.max(0, Math.min(a1, b1) - Math.max(a0, b0));

  // Intervals of every (spell, bot) from [t_ms, guid, spell id, 1 apply | -1 remove] rows. An apply while one is open is ignored; a
  // remove with none open means the aura was on at the pull ([0, t]); what is still open at the end closes at dur_ms.
  function intervals(fight) {
    const dur = durOf(fight);
    const rows = fight && Array.isArray(fight.auras) ? fight.auras : [];
    const ok = [];
    rows.forEach((r, i) => {
      if (!Array.isArray(r) || r.length < 4 || !r.slice(0, 4).every((x) => typeof x === "number" && isFinite(x))) return;
      ok.push({ r, i });
    });
    ok.sort((a, b) => a.r[0] - b.r[0] || a.i - b.i); // stable
    const clamp = (t) => Math.max(0, Math.min(dur, t));
    const open = {}; // "sid:guid" -> start
    const bySid = {};
    const put = (sid, guid, from, to) => {
      const s = bySid[sid] || (bySid[sid] = {});
      (s[guid] || (s[guid] = [])).push([from, to]);
    };
    ok.forEach(({ r }) => {
      const t = clamp(r[0]), guid = String(r[1]), sid = r[2], k = sid + ":" + guid;
      if (r[3] > 0) {
        if (!(k in open)) open[k] = t;
      } else if (k in open) {
        put(sid, guid, open[k], Math.max(open[k], t));
        delete open[k];
      } else {
        put(sid, guid, 0, t);
      }
    });
    Object.keys(open).forEach((k) => {
      const at = k.indexOf(":");
      put(Number(k.slice(0, at)), k.slice(at + 1), open[k], dur);
    });
    Object.keys(bySid).forEach((sid) => Object.keys(bySid[sid]).forEach((g) => bySid[sid][g].sort((a, b) => a[0] - b[0])));
    const sids = Object.keys(bySid).map(Number).sort((a, b) => a - b);
    // A sampled live fight is not the module's capped list: its row count says nothing about dropped rows.
    return { bySid, capped: rows.length >= CAP_ROWS && !(fight && fight.aura_src === "sampled"), sids };
  }

  // Where each bot is alive: from the pull to its first death in fight.deaths (else the end). The module records no
  // resurrection, so a dead bot stays dead for the rest of the fight.
  function aliveSpans(fight) {
    const dur = durOf(fight), out = {};
    const died = {};
    (fight && Array.isArray(fight.deaths) ? fight.deaths : []).forEach((d) => {
      if (d && d.name != null && !(d.name in died) && num(d.t_ms) !== null) died[d.name] = d.t_ms;
    });
    bots(fight).forEach((b) => {
      out[String(b.guid)] = [0, b.name in died ? Math.max(0, Math.min(dur, died[b.name])) : dur];
    });
    return out;
  }

  function botUptime(ivs, span, range, dur) {
    const r0 = range ? Math.max(0, Math.min(dur, range.from)) : 0, r1 = range ? Math.max(0, Math.min(dur, range.to)) : dur;
    const aliveMs = overlap(span[0], span[1], r0, r1);
    let upMs = 0, apps = 0;
    (ivs || []).forEach(([f, t]) => {
      upMs += overlap(Math.max(f, span[0]), Math.min(t, span[1]), r0, r1);
      if (f >= r0 && f < r1) apps++;
    });
    return { upMs, aliveMs, pct: aliveMs ? (upMs / aliveMs) * 100 : 0, apps };
  }

  // Uptime of one aura over a range ({from,to}, or null for the whole fight): the raid sums every bot of the fight (a bot that
  // never had it counts with 0), the list holds the bots that had it, best first.
  function uptime(iv, sid, fight, range) {
    const dur = durOf(fight), spans = aliveSpans(fight), by = (iv && iv.bySid && iv.bySid[sid]) || {};
    let up = 0, alive = 0;
    const list = [];
    bots(fight).forEach((b) => {
      const g = String(b.guid), u = botUptime(by[g], spans[g], range, dur);
      up += u.upMs;
      alive += u.aliveMs;
      if (u.upMs > 0 || u.apps > 0) list.push({ guid: g, name: b.name, role: b.role, upMs: u.upMs, aliveMs: u.aliveMs, pct: u.pct, apps: u.apps });
    });
    list.sort((a, b) => b.pct - a.pct || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    return { raid: { upMs: up, aliveMs: alive, pct: alive ? (up / alive) * 100 : 0 }, bots: list.slice(0, MAX_BOT_ROWS) };
  }

  // The markers drawn on the strip: a boss cast ({sid, name}: the log's casts by spell id or by name), or "deaths".
  function markers(fight, pick) {
    const out = [];
    if (pick === "deaths") {
      const byName = {};
      bots(fight).forEach((b) => { if (!(b.name in byName)) byName[b.name] = String(b.guid); });
      (fight && Array.isArray(fight.deaths) ? fight.deaths : []).forEach((d) => {
        if (!d || num(d.t_ms) === null) return;
        const m = { t: d.t_ms, label: d.name + " (" + (d.killer || "?") + ")", death: true };
        if (d.name in byName) m.guid = byName[d.name];
        out.push(m);
      });
    } else if (pick && typeof pick === "object") {
      const name = typeof pick.name === "string" && pick.name ? pick.name.toLowerCase() : null;
      (fight && Array.isArray(fight.log) ? fight.log : []).forEach((e) => {
        if (!e || e.k !== "cast" || num(e.t) === null) return;
        const bySid = pick.sid != null && e.sid != null && Number(e.sid) === Number(pick.sid);
        const byName = name !== null && typeof e.s === "string" && e.s.toLowerCase() === name;
        if (bySid || byName) out.push({ t: e.t, label: e.s || pick.name || "spell " + pick.sid });
      });
    }
    return out.sort((a, b) => a.t - b.t);
  }

  const holds = (ivs, t, closed) => (ivs || []).some(([f, to]) => f <= t && (closed ? t <= to : t < to));

  // Who had the aura at each marker. A cast marker: the bots alive then whose interval holds the time; a death marker: whether
  // the dead bot itself had it.
  function atMarkers(iv, sid, fight, marks) {
    const spans = aliveSpans(fight), by = (iv && iv.bySid && iv.bySid[sid]) || {};
    return (marks || []).map((m) => {
      if (m.death) {
        const on = m.guid != null && holds(by[m.guid], m.t, true) ? 1 : 0;
        return { t: m.t, label: m.label, on, alive: 1, names: on ? [bots(fight).find((b) => String(b.guid) === m.guid).name] : [] };
      }
      let on = 0, alive = 0;
      const names = [];
      bots(fight).forEach((b) => {
        const g = String(b.guid);
        if (!(m.t < spans[g][1])) return;
        alive++;
        if (holds(by[g], m.t, false)) { on++; names.push(b.name); }
      });
      return { t: m.t, label: m.label, on, alive, names };
    });
  }

  // One row per boss cast: the stretch since the cast before (or the pull) with the raid's uptime and the deaths in it, who had
  // it at the cast, then the stretch after the last cast.
  function cycles(iv, sid, fight, marks) {
    const casts = (marks || []).filter((m) => !m.death);
    if (!casts.length) return [];
    const at = atMarkers(iv, sid, fight, casts), dur = durOf(fight);
    const deathsIn = (from, to) => (fight && Array.isArray(fight.deaths) ? fight.deaths : []).filter((d) => d && num(d.t_ms) !== null && d.t_ms >= from && d.t_ms < to);
    const rows = casts.map((m, k) => {
      const from = k ? casts[k - 1].t : 0;
      return { i: k + 1, from, to: m.t, raidPct: uptime(iv, sid, fight, { from, to: m.t }).raid.pct, on: at[k].on, alive: at[k].alive, deaths: deathsIn(from, m.t) };
    });
    const last = casts[casts.length - 1].t;
    rows.push({ i: "after", from: last, to: dur, raidPct: uptime(iv, sid, fight, { from: last, to: dur }).raid.pct, deaths: deathsIn(last, Infinity) });
    return rows;
  }

  // How long each bot kept the aura per application (median and longest), longest first.
  function applications(iv, sid, fight) {
    const by = (iv && iv.bySid && iv.bySid[sid]) || {}, out = [];
    bots(fight).forEach((b) => {
      const list = by[String(b.guid)];
      if (!list || !list.length) return;
      const d = list.map(([f, t]) => t - f).sort((a, c) => a - c), h = d.length >> 1;
      out.push({ guid: String(b.guid), name: b.name, n: d.length, medianMs: d.length % 2 ? d[h] : (d[h - 1] + d[h]) / 2, longestMs: d[d.length - 1] });
    });
    return out.sort((a, b) => b.longestMs - a.longestMs || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  }

  // The aura and boss cast the panel opens on: the boss's preset when the fight has its aura, else the first aura seen.
  function defaults(fight, iv) {
    const sids = (iv && iv.sids) || [];
    const p = fight && (PRESETS[fight.boss] || PRESETS[fight.boss_phase_entry]);
    if (p && sids.indexOf(p.auras[0]) >= 0) return { sid: p.auras[0], marker: { sid: p.marker.sid, name: p.marker.name } };
    return { sid: sids.length ? sids[0] : null, marker: null };
  }

  // The boss casts to offer as markers: distinct by spell id (else by name), by name.
  function castChoices(fight) {
    const seen = {}, out = [];
    (fight && Array.isArray(fight.log) ? fight.log : []).forEach((e) => {
      if (!e || e.k !== "cast") return;
      const hasSid = e.sid != null && num(Number(e.sid)) !== null, name = typeof e.s === "string" && e.s ? e.s : "";
      if (!hasSid && !name) return;
      const k = hasSid ? "s" + Number(e.sid) : "n" + name.toLowerCase();
      if (seen[k]) return;
      seen[k] = true;
      out.push({ sid: hasSid ? Number(e.sid) : null, name: name || "spell " + e.sid });
    });
    return out.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  }

  const api = { AURA_NAMES, PRESETS, CAP_ROWS, auraName, intervals, aliveSpans, uptime, markers, atMarkers, cycles, applications, defaults, castChoices };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightAuras = api;
})(typeof self !== "undefined" ? self : this);
