"use strict";
// Fight debugger: the pure logic behind the Commanders card's Fight debugger mode. It turns one finished fight
// (internal/combat/types.go: Fight) and the live bind into plain data — checks, verdicts, death recaps, the
// timeline, raid frames, role tables, the fight rail and the live window — that app.js renders with esc.
// No DOM, no fetch, no markup: unit-tested by internal/commandersjs/fightdebug_check.js under node.
(function (root) {
  const DEBUFF_CHECKS = [
    { key: "faerie_fire", name: "Faerie Fire" },
    { key: "coe", name: "Curse of the Elements" },
    { key: "expose_armor", name: "Expose Armor" },
  ];
  // The taunts of the 1.12 game: a warrior, druid, paladin or bear off-tank has at least one of them.
  const TAUNTS = ["Taunt", "Growl", "Hand of Reckoning", "Mocking Blow", "Challenging Shout", "Challenging Roar"];
  // First guesses from walk 2; tune after raid nights (see the spec's backlog).
  const LIMITS = { sunderOk: 1.5, sunderWarn: 0.8, takenShare: 0.15, overheal: 0.45, debuffOk: 0.6, minFightMs: 10000 };

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const round1 = (v) => Math.round(v * 10) / 10;
  const clampPct = (t, dur) => (dur > 0 ? Math.max(0, Math.min(100, (t / dur) * 100)) : 0);

  // Integer with thousands separators, by regex: node's toLocaleString depends on the ICU build.
  function fmtInt(n) {
    const v = Math.round(num(n) || 0);
    return String(v).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  }

  function roleOf(b) {
    const r = String((b && b.role) || "");
    if (/tank/i.test(r)) return "tank";
    if (/heal/i.test(r)) return "heal";
    return "dps";
  }

  function spellCount(b, name) {
    const spells = (b && b.spells) || [];
    return spells.reduce((s, x) => s + (x.n === name ? num(x.c) || 0 : 0), 0);
  }

  function mainTank(fight, layout) {
    const bots = (fight && fight.bots) || [];
    const named = layout && layout.mt ? bots.find((b) => b.name === layout.mt) : null;
    if (named && roleOf(named) === "tank") return named;
    const first = bots.find((b) => roleOf(b) === "tank");
    return first || null;
  }

  function offTanks(fight, layout) {
    const bots = (fight && fight.bots) || [];
    if (layout && Array.isArray(layout.ot)) {
      return layout.ot.map((name) => bots.find((b) => b.name === name)).filter(Boolean);
    }
    const mt = mainTank(fight, layout);
    return bots.filter((b) => roleOf(b) === "tank" && b !== mt);
  }

  const LEVEL = { fail: 0, warn: 1, ok: 2 };

  function checks(fight, layout, warnings) {
    fight = fight || {};
    const bots = Array.isArray(fight.bots) ? fight.bots : [];
    const deaths = Array.isArray(fight.deaths) ? fight.deaths : [];
    const durMs = num(fight.dur_ms) || 0;
    const secs = Math.max(1, durMs / 1000);
    const mt = mainTank(fight, layout);
    const tankNames = {};
    bots.forEach((b) => { if (roleOf(b) === "tank") tankNames[b.name] = true; });
    const out = [];

    // The main tank's Sunder Armor stack: under 0.8 per 10 s it never reaches 5 in a real fight.
    if (mt && mt.class === "warrior" && durMs >= LIMITS.minFightMs) {
      const n = spellCount(mt, "Sunder Armor");
      const shieldSlam = spellCount(mt, "Shield Slam");
      const rate = (n / secs) * 10;
      out.push({
        id: "mt_sunder",
        lvl: rate >= LIMITS.sunderOk ? "ok" : rate >= LIMITS.sunderWarn ? "warn" : "fail",
        title: "Main tank Sunder rate",
        num: rate.toFixed(1) + "/10 s",
        ev: mt.name + " cast Sunder Armor " + n + "× in " + Math.round(secs) + " s (Shield Slam " + shieldSlam
          + "×). Under 0.8 per 10 s the stack never reaches 5.",
      });
    }

    const tankDeaths = [];
    const otherDeaths = [];
    deaths.forEach((d, i) => { (tankNames[d.name] ? tankDeaths : otherDeaths).push({ d, i }); });
    const deathEv = (list) => list.map((x) => x.d.name + " at " + (x.d.t_ms / 1000).toFixed(1) + " s to " + x.d.killer).join(" · ");

    if (tankDeaths.length) {
      const count = {};
      const names = [];
      tankDeaths.forEach((x) => {
        if (!(x.d.name in count)) { count[x.d.name] = 0; names.push(x.d.name); }
        count[x.d.name]++;
      });
      out.push({
        id: "tank_death",
        lvl: "fail",
        title: "Tank died: " + names.map((n) => n + (count[n] > 1 ? " (" + count[n] + "×)" : "")).join(", "),
        num: tankDeaths.length,
        ev: deathEv(tankDeaths),
        bot: tankDeaths[0].d.name,
        death: tankDeaths[0].i,
      });
    }

    if (otherDeaths.length) {
      out.push({
        id: "other_death",
        lvl: "fail",
        title: otherDeaths.length + " death(s) outside the tanks",
        num: otherDeaths.length,
        ev: deathEv(otherDeaths),
        death: otherDeaths[0].i,
      });
    }

    // Damage taken outside the tanks: a non-tank eating a sixth of it means threat was lost.
    const nonTanks = bots.filter((b) => roleOf(b) !== "tank");
    if (nonTanks.length) {
      const tankTotal = bots.filter((b) => roleOf(b) === "tank").reduce((s, b) => s + (num(b.taken) || 0), 0);
      const allTotal = bots.reduce((s, b) => s + (num(b.taken) || 0), 0);
      const top = nonTanks.reduce((best, b) => ((num(b.taken) || 0) > (num(best.taken) || 0) ? b : best), nonTanks[0]);
      const taken = num(top.taken) || 0;
      const share = allTotal > 0 ? taken / allTotal : 0;
      out.push({
        id: "taken_outside",
        lvl: share > LIMITS.takenShare ? "warn" : "ok",
        title: "Most damage taken outside the tanks: " + top.name + " (" + (roleOf(top) === "heal" ? "healer" : "damage") + ")",
        num: Math.round(share * 100) + "%",
        ev: "Tanks took " + fmtInt(tankTotal) + "; " + top.name + " took " + fmtInt(taken) + " of " + fmtInt(allTotal)
          + ". Over 15% on one non-tank usually means threat was lost.",
        bot: top.name,
      });
    }

    const healers = bots.filter((b) => roleOf(b) === "heal");
    const effective = healers.reduce((s, b) => s + (num(b.heal) || 0), 0);
    const overheal = healers.reduce((s, b) => s + (num(b.over) || 0), 0);
    if (effective + overheal > 0) {
      const pct = overheal / (effective + overheal);
      out.push({
        id: "overheal",
        lvl: pct > LIMITS.overheal ? "warn" : "ok",
        title: "Healer overheal",
        num: Math.round(pct * 100) + "%",
        ev: fmtInt(effective) + " effective, " + fmtInt(overheal) + " overheal across " + healers.length + " healers.",
      });
    }

    const idle = bots.filter((b) => !(num(b.casts) || 0) && !(num(b.dmg) || 0) && !(num(b.heal) || 0));
    out.push({
      id: "idle",
      lvl: idle.length ? "warn" : "ok",
      title: idle.length ? idle.length + " bot(s) did nothing" : "Every bot acted",
      num: idle.length ? idle.length : "",
      ev: idle.length ? idle.map((b) => b.name).join(", ") : "All " + bots.length + " bots cast, hit or healed.",
      bot: idle.length ? idle[0].name : undefined,
    });

    const debuffs = fight.debuffs || {};
    const up = debuffs.up_s || {};
    const sampled = num(debuffs.sampled_s) || 0;
    DEBUFF_CHECKS.forEach((d) => {
      if (!(d.key in up) || sampled <= 0) return;
      const pct = (num(up[d.key]) || 0) / sampled;
      out.push({
        id: "debuff:" + d.key,
        lvl: pct >= LIMITS.debuffOk ? "ok" : "warn",
        title: d.name + " up",
        num: Math.round(pct * 100) + "%",
        ev: (num(up[d.key]) || 0) + " of " + sampled + " sampled seconds.",
      });
    });

    offTanks(fight, layout).forEach((b) => {
      const spells = TAUNTS.map((n) => ({ n, c: spellCount(b, n) })).filter((x) => x.c > 0);
      const taunts = spells.reduce((s, x) => s + x.c, 0);
      out.push({
        id: "ot_taunt:" + b.name,
        lvl: taunts > 0 ? "ok" : "warn",
        title: "Off-tank " + b.name + " taunted",
        num: taunts + "×",
        ev: (b.class || "") + ": " + (spells.length ? spells.map((x) => x.n + " " + x.c).join(", ") : "no taunt")
          + "; took " + fmtInt(b.taken) + ".",
        bot: b.name,
      });
    });

    (Array.isArray(warnings) ? warnings : []).forEach((w) => {
      out.push({
        id: "rule:" + w.rule + ":" + (w.bot || ""),
        lvl: w.level === "red" ? "fail" : w.level === "yellow" ? "warn" : "ok",
        title: String(w.text == null ? "" : w.text),
        num: "",
        ev: "Server rule " + w.rule,
        bot: w.bot,
      });
    });

    // Stable sort: level first, the build order inside a level.
    return out.map((c, i) => ({ c, i }))
      .sort((a, b) => (LEVEL[a.c.lvl] - LEVEL[b.c.lvl]) || (a.i - b.i))
      .map((x) => x.c);
  }

  function verdict(fight, layout, warnings) {
    const list = checks(fight, layout, warnings);
    if (list.some((c) => c.lvl === "fail")) return "fail";
    if (list.some((c) => c.lvl === "warn")) return "warn";
    return "ok";
  }

  // The worst finished fight: most fails, then most warns, then most deaths; ties keep the earliest (the newest fight).
  function worstIndex(details, layout) {
    let best = -1;
    let bestScore = null;
    (Array.isArray(details) ? details : []).forEach((det, i) => {
      if (!det || !det.fight) return;
      const list = checks(det.fight, layout, det.warnings || []);
      const score = [
        list.filter((c) => c.lvl === "fail").length,
        list.filter((c) => c.lvl === "warn").length,
        (det.fight.deaths || []).length,
      ];
      const worse = !bestScore
        || score[0] > bestScore[0]
        || (score[0] === bestScore[0] && (score[1] > bestScore[1]
          || (score[1] === bestScore[1] && score[2] > bestScore[2])));
      if (worse) { best = i; bestScore = score; }
    });
    return best;
  }

  // Death recaps with only the hits of that life: the module repeats hits from before the first death in the
  // second death's list, so keep what is after the same bot's previous death.
  function deathRecaps(fight) {
    const deaths = (fight && fight.deaths) || [];
    const bots = (fight && fight.bots) || [];
    const previous = {};
    return deaths.map((d, i) => {
      const after = previous[d.name];
      const hits = (d.hits || [])
        .filter((h) => h.t_ms <= d.t_ms && (after === undefined || h.t_ms > after))
        .map((h) => ({ t_ms: h.t_ms, a: h.a, d: h.d }));
      previous[d.name] = d.t_ms;
      const total = hits.reduce((s, h) => s + (num(h.d) || 0), 0);
      const b = bots.find((x) => x.name === d.name);
      return {
        i,
        name: d.name,
        guid: b && b.guid != null ? b.guid : undefined,
        t_ms: d.t_ms,
        killer: d.killer,
        x: d.x,
        y: d.y,
        hits,
        total,
        spanMs: hits.length ? d.t_ms - hits[0].t_ms : 0,
      };
    });
  }

  function timeline(fight) {
    fight = fight || {};
    const durMs = num(fight.dur_ms) || 0;
    const step = durMs <= 90000 ? 5000 : durMs <= 180000 ? 10000 : 30000;
    const ticks = [];
    for (let t = 0; t <= durMs; t += step) ticks.push({ pct: clampPct(t, durMs), label: Math.round(t / 1000) + " s" });
    const recaps = deathRecaps(fight);
    const deaths = recaps.map((r) => ({ i: r.i, pct: clampPct(r.t_ms, durMs), name: r.name, t_ms: r.t_ms }));
    const hits = [];
    recaps.forEach((r) => r.hits.forEach((h) => hits.push({
      i: r.i, pct: clampPct(h.t_ms, durMs), d: h.d, a: h.a, h: Math.min(44, 6 + h.d / 70),
    })));
    return { durMs, ticks, deaths, hits };
  }

  function frameRole(b) {
    if (b.alive === false) return "dead";
    const r = String(b.role || "");
    if (/tank/i.test(r)) return "tank";
    if (/heal/i.test(r)) return "healer";
    if (/dps/i.test(r)) return "dps";
    return "other";
  }

  function framePower(b) {
    const kind = String(b.power_type || "").toLowerCase();
    return kind === "rage" || kind === "energy" || kind === "mana" ? kind : "mana";
  }

  function raidFrames(bind) {
    const bots = (bind && bind.bots) || [];
    if (!bots.length) return [];
    const tagged = bots.some((b) => typeof b.sub === "number" && isFinite(b.sub));
    const layout = bind.layout;
    const groups = {};
    const order = [];
    bots.forEach((b) => {
      let g = null;
      if (tagged) {
        const s = b.sub;
        g = typeof s === "number" && isFinite(s) && s >= 0 && s <= 7 ? Math.floor(s) : 7;
      }
      const key = g === null ? "party" : String(g);
      if (!(key in groups)) { groups[key] = { label: g === null ? "Party" : "Group " + (g + 1), tiles: [] }; order.push(key); }
      const maxHp = num(b.max_hp) || 0;
      const maxPower = num(b.max_power) || 0;
      groups[key].tiles.push({
        name: b.name,
        guid: b.guid,
        role: frameRole(b),
        cls: b.class || "",
        hpPct: maxHp > 0 ? Math.round(((num(b.hp) || 0) / maxHp) * 100) : 0,
        powerPct: maxPower > 0 ? Math.round(((num(b.power) || 0) / maxPower) * 100) : 0,
        hasPower: maxPower > 0,
        powerKind: framePower(b),
        alive: b.alive !== false,
        tag: layout && b.name === layout.mt ? "MT"
          : layout && Array.isArray(layout.ot) && layout.ot.indexOf(b.name) >= 0 ? "OT" : "",
      });
    });
    return order.filter((k) => k !== "party").map((k) => groups[k])
      .concat(groups.party ? [groups.party] : []);
  }

  const byName = (a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  const byDesc = (key) => (a, b) => (b[key] - a[key]) || byName(a, b);

  function roleTables(fight, layout) {
    fight = fight || {};
    const bots = Array.isArray(fight.bots) ? fight.bots : [];
    const deaths = Array.isArray(fight.deaths) ? fight.deaths : [];
    const secs = Math.max(1, (num(fight.dur_ms) || 0) / 1000);
    const mt = mainTank(fight, layout);
    const tanks = bots.filter((b) => roleOf(b) === "tank");
    const tankRows = tanks.filter((b) => b === mt).concat(tanks.filter((b) => b !== mt)).map((b) => ({
      name: b.name,
      guid: b.guid,
      cls: b.class || "",
      main: b === mt,
      sunder: spellCount(b, "Sunder Armor"),
      taunts: TAUNTS.reduce((s, n) => s + spellCount(b, n), 0),
      taken: num(b.taken) || 0,
      died: deaths.filter((d) => d.name === b.name).length,
    }));
    const healerRows = bots.filter((b) => roleOf(b) === "heal").map((b) => {
      const heal = num(b.heal) || 0;
      const over = num(b.over) || 0;
      return {
        name: b.name,
        guid: b.guid,
        cls: b.class || "",
        hps: heal / secs,
        overPct: heal + over > 0 ? Math.round((over / (heal + over)) * 100) : 0,
        casts: num(b.casts) || 0,
      };
    }).sort(byDesc("hps"));
    const dpsRows = bots.filter((b) => roleOf(b) === "dps").map((b) => ({
      name: b.name,
      guid: b.guid,
      cls: b.class || "",
      dps: (num(b.dmg) || 0) / secs,
      taken: num(b.taken) || 0,
      casts: num(b.casts) || 0,
    })).sort(byDesc("dps"));
    return {
      tanks: tankRows,
      healers: healerRows,
      dps: dpsRows,
      maxHps: healerRows.length ? healerRows[0].hps : 0,
      maxDps: dpsRows.length ? dpsRows[0].dps : 0,
    };
  }

  function botPanel(fight, name) {
    const bots = (fight && fight.bots) || [];
    const b = bots.find((x) => x.name === name);
    if (!b) return null;
    return {
      name: b.name,
      cls: b.class || "",
      sub: typeof b.sub === "number" ? b.sub : null,
      dmg: num(b.dmg) || 0,
      heal: num(b.heal) || 0,
      taken: num(b.taken) || 0,
      casts: num(b.casts) || 0,
      spells: (b.spells || []).map((s) => ({ n: s.n, c: num(s.c) || 0 }))
        .sort((x, y) => (y.c - x.c) || (x.n < y.n ? -1 : x.n > y.n ? 1 : 0)),
    };
  }

  function railCards(list, detailsByKey, layout) {
    const rows = Array.isArray(list) ? list : [];
    const maxDur = rows.reduce((m, r) => Math.max(m, num(r.dur) || 0), 0);
    return rows.map((row, i) => {
      const det = detailsByKey && detailsByKey[row.key];
      const fight = det && det.fight;
      const dur = num(row.dur) || 0;
      const engaged = fight && fight.engaged && fight.engaged[0];
      const dmg = fight && Array.isArray(fight.bots) ? fight.bots.reduce((s, b) => s + (num(b && b.dmg) || 0), 0) : null;
      return {
        key: row.key,
        // The daemon lists fights newest first; the oldest is the first pack of the fight.
        label: "Pack " + (rows.length - i),
        mob: row.boss_name || (fight && fight.boss_name) || (engaged && engaged.n) || "trash",
        step: num(row.step) || 0,
        durMs: dur,
        dmg: dmg,
        deaths: num(row.deaths) || 0,
        lenPct: maxDur > 0 ? (dur / maxDur) * 100 : 0,
        verdict: fight ? verdict(fight, layout, det.warnings || []) : null,
      };
    });
  }

  function liveSummary(bind) {
    if (!bind || !bind.combat) return null;
    const combat = bind.combat;
    const window = Array.isArray(combat.bots) ? combat.bots : [];
    return {
      fightMs: num(combat.fight_ms) || 0,
      winS: num(combat.win_s) || 0,
      dps: window.reduce((s, b) => s + (num(b.dps) || 0), 0),
      hps: window.reduce((s, b) => s + (num(b.hps) || 0), 0),
      active: window.length,
      alive: (bind.bots || []).filter((b) => b.alive !== false).length,
    };
  }

  function liveChecks(bind) {
    if (!bind || !bind.combat) return [];
    const bots = bind.bots || [];
    const combat = bind.combat;
    const window = Array.isArray(combat.bots) ? combat.bots : [];
    const out = [];
    const layout = bind.layout;
    const tanks = bots.filter((b) => (layout && (b.name === layout.mt || (layout.ot || []).indexOf(b.name) >= 0))
      || roleOf(b) === "tank");
    const dead = tanks.filter((b) => b.alive === false);
    if (dead.length) {
      out.push({
        id: "live_tank_dead",
        lvl: "fail",
        title: "Tank down: " + dead.map((b) => b.name).join(", "),
        num: dead.length,
        ev: dead.map((b) => b.name + " is dead").join(" · "),
        bot: dead[0].name,
      });
    }
    if ((num(combat.fight_ms) || 0) >= (num(combat.win_s) || 0) * 1000) {
      const seen = {};
      window.forEach((w) => { seen[String(w.g)] = true; });
      const idle = bots.filter((b) => b.alive !== false && !seen[String(b.guid)]);
      out.push({
        id: "live_idle",
        lvl: idle.length ? "warn" : "ok",
        title: idle.length ? idle.length + " bot(s) did nothing" : "Every bot acted",
        num: idle.length ? idle.length : "",
        ev: idle.length ? idle.map((b) => b.name).join(", ") : "Every bot acted in the last " + combat.win_s + " s",
        bot: idle.length ? idle[0].name : undefined,
      });
    }
    return out;
  }

  function liveTables(bind) {
    const bots = (bind && bind.bots) || [];
    const window = (bind && bind.combat && bind.combat.bots) || [];
    const byGuid = {};
    window.forEach((w) => { byGuid[String(w.g)] = w; });
    return bots.map((b) => {
      const w = byGuid[String(b.guid)] || {};
      return {
        name: b.name,
        role: roleOf(b),
        dps: num(w.dps) || 0,
        hps: num(w.hps) || 0,
        casts: num(w.c) || 0,
        last: w.ls == null ? "" : String(w.ls),
      };
    }).sort((a, b) => (b.dps - a.dps) || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  }

  const api = {
    LIMITS, TAUNTS, DEBUFF_CHECKS,
    roleOf, spellCount, fmtInt, mainTank, offTanks, checks, verdict, worstIndex,
    deathRecaps, timeline, raidFrames, roleTables, botPanel, railCards, liveSummary, liveChecks, liveTables,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightDebug = api;
})(typeof self !== "undefined" ? self : this);
