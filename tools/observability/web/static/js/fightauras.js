"use strict";
// FightAuras: the pure logic behind the Fight debugger's Auras panel. It turns a fight's aura rows (the module's watched-aura
// changes of a finished fight, or the rows FdLive rebuilds from the 2 s boss-debuff samples of a running one) into uptime
// intervals, per-bot and raid uptime, the boss-cast markers on the strip, who had the aura at each cast, per-cycle uptime and
// the application lengths. No DOM, no fetch, no markup: unit-tested by internal/commandersjs/fightauras_check.js under node.
(function (root) {
  const CAP_ROWS = 1000; // the module keeps the newest 1000 aura rows of a fight
  const MAX_BOT_ROWS = 80; // every bot of the fight is listed (a raid is at most 40); the cap only bounds a malformed fight
  const SAMPLE_SLACK_MS = 2000; // a sampled aura edge is known to the report interval
  const DEATH_SLACK_MS = 50;    // an exact aura row for a victim may be logged this much before its death row

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
    // Hex lands when its cast finishes: "had it at the cast" looks 2 s past the cast start.
    11380: { auras: [17172, 24306], marker: { sid: 17172, name: "Hex", after: 2000 } },
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
    const sampled = !!(fight && fight.aura_src === "sampled");
    // A list at the module's cap has lost its oldest rows: nothing is known before the earliest kept row (capFrom), for any bot.
    const capped = rows.length >= CAP_ROWS && !sampled;
    const capFrom = capped && ok.length ? clamp(ok[0].r[0]) : 0;
    const rowGuids = {}; // sid -> guid -> true: the bots that have a kept row for the aura
    const open = {}; // "sid:guid" -> start
    const bySid = {};
    const put = (sid, guid, from, to) => {
      if (!(to > from)) return; // an instant is not uptime
      const s = bySid[sid] || (bySid[sid] = {});
      (s[guid] || (s[guid] = [])).push([from, to]);
    };
    ok.forEach(({ r }) => {
      const t = clamp(r[0]), guid = String(r[1]), sid = r[2], k = sid + ":" + guid;
      (rowGuids[sid] || (rowGuids[sid] = {}))[guid] = true;
      if (r[3] > 0) {
        if (!(k in open)) open[k] = t;
      } else if (k in open) {
        put(sid, guid, open[k], Math.max(open[k], t));
        delete open[k];
      } else if (capped) {
        put(sid, guid, capFrom, t); // a removal with its apply lost: up from the earliest kept row (the time before is unknown)
      } else {
        put(sid, guid, 0, t);
      }
    });
    Object.keys(open).forEach((k) => {
      const at = k.indexOf(":");
      put(Number(k.slice(0, at)), k.slice(at + 1), open[k], dur);
    });
    Object.keys(bySid).forEach((sid) => Object.keys(bySid[sid]).forEach((g) => bySid[sid][g].sort((a, b) => a[0] - b[0])));
    const seen = {};
    Object.keys(bySid).concat(Object.keys(rowGuids)).forEach((k) => { seen[k] = true; });
    const sids = Object.keys(seen).map(Number).sort((a, b) => a - b);
    // A sampled live fight is not the module's capped list: its row count says nothing about dropped rows.
    return { bySid, rowGuids, capped, capFrom, sids };
  }

  // The stretches of the fight whose state for one bot and aura the list does not tell: for a capped list, everything before its
  // earliest kept row, and for a bot with no kept row of that aura the whole fight (it may have held it throughout). None otherwise.
  function unknownSpans(iv, sid, guid, fight) {
    if (!iv || !iv.capped || !(iv.capFrom > 0)) return [];
    const has = iv.rowGuids && iv.rowGuids[sid] && iv.rowGuids[sid][String(guid)];
    return [[0, has ? iv.capFrom : durOf(fight)]];
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

  function botUptime(ivs, span, range, dur, unk) {
    const r0 = range ? Math.max(0, Math.min(dur, range.from)) : 0, r1 = range ? Math.max(0, Math.min(dur, range.to)) : dur;
    const aliveMs = overlap(span[0], span[1], r0, r1);
    let upMs = 0, apps = 0;
    (ivs || []).forEach(([f, t]) => {
      upMs += overlap(Math.max(f, span[0]), Math.min(t, span[1]), r0, r1);
      if (f >= r0 && f < r1) apps++;
    });
    // Unknown stretches (capped history): the bot's percentage is then a lower bound.
    let unknownMs = 0;
    (unk || []).forEach(([f, t]) => { unknownMs += overlap(Math.max(f, span[0]), Math.min(t, span[1]), r0, r1); });
    return { upMs, aliveMs, pct: aliveMs ? (upMs / aliveMs) * 100 : 0, apps, unknownMs };
  }

  // Uptime of one aura over a range ({from,to}, or null for the whole fight): the raid sums every bot of the fight (a bot that
  // never had it counts with 0), the list holds the bots that had it, best first.
  function uptime(iv, sid, fight, range) {
    const dur = durOf(fight), spans = aliveSpans(fight), by = (iv && iv.bySid && iv.bySid[sid]) || {};
    let up = 0, alive = 0, unknownMs = 0;
    const list = [];
    bots(fight).forEach((b) => {
      const g = String(b.guid), u = botUptime(by[g], spans[g], range, dur, unknownSpans(iv, sid, g, fight));
      up += u.upMs;
      alive += u.aliveMs;
      unknownMs += u.unknownMs;
      list.push({ guid: g, name: b.name, role: b.role, upMs: u.upMs, aliveMs: u.aliveMs, pct: u.pct, apps: u.apps, lower: u.unknownMs > 0 });
    });
    list.sort((a, b) => b.pct - a.pct || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    return { raid: { upMs: up, aliveMs: alive, pct: alive ? (up / alive) * 100 : 0, lower: unknownMs > 0 }, bots: list.slice(0, MAX_BOT_ROWS) };
  }

  // How long after a boss cast starts the aura it causes may land (Hex: its cast time), from the presets.
  function afterOf(pick) {
    const name = typeof pick.name === "string" ? pick.name.toLowerCase() : null;
    const hit = Object.keys(PRESETS).map((k) => PRESETS[k].marker).find((m) => (pick.sid != null && m.sid === Number(pick.sid)) || (pick.sid == null && name !== null && m.name.toLowerCase() === name));
    return hit && hit.after ? hit.after : 0;
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
      const after = afterOf(pick);
      (fight && Array.isArray(fight.log) ? fight.log : []).forEach((e) => {
        if (!e || e.k !== "cast" || num(e.t) === null) return;
        const bySid = pick.sid != null && e.sid != null && Number(e.sid) === Number(pick.sid);
        // With a spell id the id decides; the name only stands in for an event that carries none.
        const byName = name !== null && (pick.sid == null || e.sid == null) && typeof e.s === "string" && e.s.toLowerCase() === name;
        if (bySid || byName) out.push({ t: e.t, label: e.s || pick.name || "spell " + pick.sid, after });
      });
    }
    return out.sort((a, b) => a.t - b.t);
  }

  // Whether one bot's intervals hold the aura at t. A window (a preset's after-the-cast time) asks for any overlap with [t, t + win].
  const holds = (ivs, t, win) => (ivs || []).some(([f, to]) => (win ? f <= t + win && to > t : f <= t && t < to));
  // A victim's aura row can be logged just before its death row (an exact row), or fall in the same report interval (a sampled one).
  const heldAtDeath = (ivs, t, slack) => (ivs || []).some(([f, to]) => f <= t && to >= t - slack);

  // Whether the window [from, to] touches a stretch the capped list does not tell.
  const inUnknown = (spans, from, to) => spans.some(([f, t]) => f <= to && t > from);

  // Who had the aura at each marker. A cast marker: the bots alive then whose interval holds the time; a death marker: whether
  // the dead bot itself had it. `unknown` counts the bots (0 or 1 for a death) that did not provably have it and whose state at the
  // marker lies in an unknown stretch of a capped list: they are neither "had it" nor "did not".
  function atMarkers(iv, sid, fight, marks) {
    const spans = aliveSpans(fight), by = (iv && iv.bySid && iv.bySid[sid]) || {};
    const sampled = !!(fight && fight.aura_src === "sampled");
    return (marks || []).map((m) => {
      if (m.death) {
        const slack = sampled ? SAMPLE_SLACK_MS : DEATH_SLACK_MS;
        const on = m.guid != null && heldAtDeath(by[m.guid], m.t, slack) ? 1 : 0;
        const unknown = !on && m.guid != null && inUnknown(unknownSpans(iv, sid, m.guid, fight), m.t - slack, m.t) ? 1 : 0;
        return { t: m.t, label: m.label, on, alive: 1, unknown, names: on ? [bots(fight).find((b) => String(b.guid) === m.guid).name] : [] };
      }
      let on = 0, alive = 0, unknown = 0;
      const names = [];
      const win = m.after ? m.after + (sampled ? SAMPLE_SLACK_MS : 0) : 0;
      bots(fight).forEach((b) => {
        const g = String(b.guid);
        if (!(m.t < spans[g][1])) return;
        alive++;
        if (holds(by[g], m.t, win)) { on++; names.push(b.name); } else if (inUnknown(unknownSpans(iv, sid, g, fight), m.t, m.t + win)) unknown++;
      });
      return { t: m.t, label: m.label, on, alive, unknown, names };
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
      const up = uptime(iv, sid, fight, { from, to: m.t }).raid;
      return { i: k + 1, from, to: m.t, raidPct: up.pct, upMs: up.upMs, lower: up.lower, on: at[k].on, alive: at[k].alive, unknown: at[k].unknown, deaths: deathsIn(from, m.t) };
    });
    const last = casts[casts.length - 1].t;
    const rest = uptime(iv, sid, fight, { from: last, to: dur }).raid;
    rows.push({ i: "after", from: last, to: dur, raidPct: rest.pct, upMs: rest.upMs, lower: rest.lower, deaths: deathsIn(last, Infinity) });
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

  const api = { AURA_NAMES, PRESETS, CAP_ROWS, auraName, intervals, unknownSpans, aliveSpans, uptime, markers, atMarkers, cycles, applications, defaults, castChoices };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightAuras = api;
})(typeof self !== "undefined" ? self : this);
