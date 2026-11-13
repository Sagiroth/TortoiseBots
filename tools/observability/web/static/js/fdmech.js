"use strict";
// FdMech: the per-fight mechanic row of the Fight debugger, one line of text for the fight in front of you (a running fight on the
// Raids and Dungeons live tiles and the debugger's live bar, a filed one above the night's Boss mechanics table). Pure: no DOM, no
// fetch. It reads the aura intervals FightAuras builds (exact rows of a filed fight, or the rows FdLive rebuilds from the 2 s boss-debuff
// samples of a running one) and the samples FdLive keeps. The night's table over many fights is computed by the daemon
// (internal/mechanics); DOSED_SHARE here and DosedShare there are one number, and a Go test fails when they differ.
// Unit-tested by internal/commandersjs/fdmech_check.js under node.
(function (root) {
  const DOSED_SHARE = 0.8; // a Blood Siphon cycle is dosed when at least this share of the members alive at the cast carry Poisonous Blood
  const HAKKAR = 14834, SON = 11357, JINDO = 11380;
  const SHADE_NAME = "Shade of Jin'do", SON_NAME = "Son of Hakkar";
  const SIPHON = { sid: 24324, name: "Blood Siphon" }, POISON = 24321, HEX = 17172;
  const SAMPLE_NOTE = "Live: the auras come from the 2 s boss-debuff samples, so \"dosed\" is within 2 s of the cast.";
  const FILED_NOTE = "From the fight's own aura rows.";

  const FightAuras = typeof require === "function" ? (function () { try { return require("./fightauras.js"); } catch (e) { return null; } })() : null;
  const FA = () => FightAuras || root.FightAuras;
  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const botsOf = (fight) => (fight && Array.isArray(fight.bots) ? fight.bots.filter((b) => b && b.guid != null) : []);
  const deathsOf = (fight) => (fight && Array.isArray(fight.deaths) ? fight.deaths.filter((d) => d && typeof d === "object") : []);

  // The fight's start in ms: a running fight carries it exactly, a filed one in whole seconds.
  function startMs(fight) {
    const ms = num(fight && fight.start_ms);
    if (ms !== null && ms > 0) return ms;
    return (num(fight && fight.start) || 0) * 1000;
  }

  // Hakkar: for every Blood Siphon cast, whether a Son of Hakkar was on a member in the stretch before it ([the cast before or the
  // pull, this cast)) by the samples. null for "unknown": no samples at all. A cycle whose stretch holds no sample at all is unknown
  // too, so the count is then a lower bound ({n, lower: true}), not "not pulled".
  function sonPulled(fight, marks, samples) {
    if (!Array.isArray(samples) || !samples.length) return null;
    const members = {};
    botsOf(fight).forEach((b) => { members[String(b.guid)] = true; });
    const t0 = startMs(fight);
    let n = 0, lower = false;
    marks.forEach((m, k) => {
      const from = k ? marks[k - 1].t : 0;
      let seen = false, hit = false;
      samples.forEach((s) => {
        if (hit || !s || num(s.t) === null) return;
        const rel = s.t - t0;
        if (rel < from || rel >= m.t) return;
        seen = true;
        if (Array.isArray(s.mobs) && s.mobs.some((x) => x && x.e === SON && x.v != null && members[String(x.v)] === true)) hit = true;
      });
      if (hit) n++; else if (!seen) lower = true;
    });
    return { n, lower };
  }

  function hakkarRow(fight, iv, samples) {
    const A = FA();
    const marks = A.markers(fight, SIPHON).filter((m) => !m.death);
    const parts = { cycles: marks.length, dosed: 0, sonPulled: null, sonLower: false, lower: false };
    if (!marks.length) return { boss: HAKKAR, text: "Hakkar: no Blood Siphon yet", parts };
    const at = A.atMarkers(iv, POISON, fight, marks);
    at.forEach((a) => {
      if (a.alive > 0 && a.on / a.alive >= DOSED_SHARE) parts.dosed++;
      // A capped list: a bot whose state was lost is neither "had it" nor "did not", so the count is a minimum.
      if (a.unknown > 0) parts.lower = true;
    });
    const son = sonPulled(fight, marks, samples);
    parts.sonPulled = son === null ? null : son.n;
    parts.sonLower = son !== null && son.lower;
    const m = parts.cycles;
    return {
      boss: HAKKAR,
      text: "Hakkar: dosed " + (parts.lower ? "≥" : "") + parts.dosed + "/" + m + " cycles · Son pulled " + (son === null ? "?" : (son.lower ? "≥" : "") + son.n) + "/" + m,
      parts,
    };
  }

  // The main tank. One rule with the night table (internal/mechanics MainTank): the tank-role bot with the largest damage taken, ties by name.
  // A running fight has no damage taken yet, and has no night table to disagree with: it reads the raid layout's tank first.
  function mainTank(fight, layout) {
    const bots = botsOf(fight);
    if (fight && fight.live === true && layout && typeof layout.mt === "string" && layout.mt) {
      const hit = bots.find((b) => b.name === layout.mt);
      if (hit) return hit;
    }
    const tanks = bots.filter((b) => b.role === "tank").sort((a, b) => (num(b.taken) || 0) - (num(a.taken) || 0) || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    return tanks.length ? tanks[0] : null;
  }

  function jindoRow(fight, iv, layout) {
    const shades = deathsOf(fight).filter((d) => d.killer === SHADE_NAME).length;
    const mt = mainTank(fight, layout);
    let hexS = null;
    if (mt && iv && Array.isArray(iv.sids) && iv.sids.length) {
      const list = (iv.bySid && iv.bySid[HEX] && iv.bySid[HEX][String(mt.guid)]) || [];
      hexS = list.reduce((n, [f, t]) => n + (t - f), 0) / 1000;
    }
    const parts = { mt: mt ? mt.name : null, hexS, shades };
    return { boss: JINDO, text: "Jin'do: Hex on MT " + (hexS === null ? "?" : Math.round(hexS) + " s") + " · Shade deaths " + shades, parts };
  }

  // The entry a fight is filed under: one rule with the daemon (internal/mechanics BossOf): its boss, else the boss phase's entry, else
  // the first preset entry among the engaged (a running fight carries no engaged list).
  function bossOf(fight) {
    const boss = num(fight && fight.boss) || 0;
    if (boss) return boss;
    const phase = num(fight && fight.boss_phase_entry) || 0;
    if (phase) return phase;
    const engaged = fight && Array.isArray(fight.engaged) ? fight.engaged.filter((g) => g && typeof g === "object") : [];
    for (const e of [HAKKAR, JINDO]) if (engaged.some((g) => g.e === e)) return e;
    return 0;
  }

  // The row of one fight: `{boss, text, parts, title}`. iv = FightAuras.intervals(fight); samples = FdLive's samples (running) or the
  // x=1 track's (filed, may be empty); layout = the binding's raid layout or null.
  function row(fight, iv, samples, layout) {
    if (!fight || typeof fight !== "object") return { boss: 0, text: "", parts: {}, title: "" };
    const boss = bossOf(fight);
    const kind = boss === HAKKAR ? HAKKAR : boss === JINDO ? JINDO : 0;
    const note = fight.aura_src === "sampled" ? SAMPLE_NOTE : FILED_NOTE;
    const ivs = iv || FA().intervals(fight);
    let out;
    if (kind === HAKKAR) out = hakkarRow(fight, ivs, samples);
    else if (kind === JINDO) out = jindoRow(fight, ivs, layout);
    else {
      const k = deathsOf(fight).length;
      const name = boss ? (typeof fight.boss_name === "string" && fight.boss_name ? fight.boss_name : "Boss") : "";
      out = { boss, text: (name ? name + ": " : "") + "deaths " + k, parts: { deaths: k } };
    }
    out.title = kind ? note : "";
    return out;
  }

  const api = { DOSED_SHARE, HAKKAR, SON, JINDO, SHADE_NAME, SON_NAME, row, mainTank, bossOf, startMs, SAMPLE_NOTE, FILED_NOTE };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdMech = api;
})(typeof self !== "undefined" ? self : this);
