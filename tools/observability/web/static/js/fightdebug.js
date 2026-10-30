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
  const LIMITS = { sunderOk: 1.5, sunderWarn: 0.8, sunderFail: 0.4, sunderFailMinMs: 30000,
    idleCastsPerSec: 0.15, idleMinMs: 20000, takenShare: 0.15, overheal: 0.45, debuffOk: 0.6, minFightMs: 10000,
    shortPullMs: 30000 };

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
    // Without a layout the first tank in the list is often not the one holding the pull: the role tables' "taken"
    // says which tank the mobs actually hit. Ties keep the bot list's order.
    let best = null;
    bots.forEach((b) => {
      if (roleOf(b) !== "tank") return;
      if (!best || (num(b.taken) || 0) > (num(best.taken) || 0)) best = b;
    });
    return best;
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

  function checks(fight, layout, warnings, extra) {
    fight = fight || {};
    extra = extra || null;
    const bots = Array.isArray(fight.bots) ? fight.bots : [];
    const deaths = Array.isArray(fight.deaths) ? fight.deaths : [];
    const durMs = num(fight.dur_ms) || 0;
    const secs = Math.max(1, durMs / 1000);
    const mt = mainTank(fight, layout);
    const tankNames = {};
    bots.forEach((b) => { if (roleOf(b) === "tank") tankNames[b.name] = true; });
    const out = [];

    // The main tank's Sunder Armor stack: under 0.8 per 10 s it never reaches 5 in a real fight. Below 0.4 is a
    // problem, but only past 30 s: a trash pull that short cannot be judged on a 10 s rate.
    if (mt && mt.class === "warrior" && durMs >= LIMITS.minFightMs) {
      const n = spellCount(mt, "Sunder Armor");
      const shieldSlam = spellCount(mt, "Shield Slam");
      const rate = (n / secs) * 10;
      const lvl = rate >= LIMITS.sunderOk ? "ok"
        : rate < LIMITS.sunderFail && durMs >= LIMITS.sunderFailMinMs ? "fail" : "warn";
      out.push({
        id: "mt_sunder",
        lvl,
        title: "Main tank Sunder rate",
        num: rate.toFixed(1) + "/10 s",
        ev: mt.name + " cast Sunder Armor " + n + "× in " + Math.round(secs) + " s (Shield Slam " + shieldSlam
          + "×). Under 0.8 per 10 s the stack never reaches 5.",
      });
    }

    // A tank that barely casts is not holding the pull; the Sunder rate alone cannot say that. casts counts spells only
    // (no melee swings): a working tank casts 0.22-0.47 a second, the idle ones of the 165-fight census 0.16 or less.
    if (mt && durMs >= LIMITS.idleMinMs) {
      // casts is the bot's whole count; spells may be cut to a few rows (or none) to fit a big fight's payload.
      const spells = (mt.spells || []).filter((x) => x && (num(x.c) || 0) > 0);
      const casts = num(mt.casts) !== null ? num(mt.casts) : spells.reduce((s, x) => s + (num(x.c) || 0), 0);
      if (casts / secs < LIMITS.idleCastsPerSec) {
        const top = spells.slice().sort((a, b) => (num(b.c) || 0) - (num(a.c) || 0)).slice(0, 3);
        out.push({
          id: "mt_idle",
          lvl: "fail",
          title: "Main tank barely acted",
          num: casts + " casts in " + Math.round(secs) + " s",
          ev: mt.name + " cast " + casts + "× in " + Math.round(secs) + " s; top spells: "
            + (top.length ? top.map((x) => x.n + " " + x.c).join(", ") : "none") + ".",
          bot: mt.name,
        });
      }
    }

    const tankDeaths = [];
    const otherDeaths = [];
    deaths.forEach((d, i) => { (tankNames[d.name] ? tankDeaths : otherDeaths).push({ d, i }); });
    // The killer of each death of that life, by the same last-other-source rule the recaps use.
    const life = {};
    const deathEv = (list) => list.map((x) => {
      const killer = killerOf(x.d, life[x.d.name]);
      life[x.d.name] = x.d.t_ms;
      return x.d.name + " at " + (x.d.t_ms / 1000).toFixed(1) + " s to " + killer;
    }).join(" · ");

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

    // One or two deaths happen in a clean kill; three or a wipe is a real problem.
    const wiped = String(fight.outcome || "") === "wipe";
    // A wipe whose deaths the telemetry missed still fails: no death row would say so.
    if (wiped && !deaths.length) {
      out.push({
        id: "wipe",
        lvl: "fail",
        title: "The fight ended in a wipe",
        num: "",
        ev: "The fight ended in a wipe and no bot death was recorded.",
      });
    }
    if (otherDeaths.length) {
      out.push({
        id: "other_death",
        lvl: otherDeaths.length >= 3 || wiped ? "fail" : "warn",
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

    // A non-tank holding an enemy: 1-3 reports are a taunt bounce, a long stint inside 30 yd is lost threat. The lanes result
    // carries the thresholds and the named victims of the strip the card already drew.
    const lanes = extra && extra.lanes;
    if (lanes && Array.isArray(lanes.lanes) && lanes.lanes.length && durMs >= LIMITS.minFightMs) {
      const worst = lanes.ntWorst;
      const limits = lanes.lane || {};
      const longest = worst ? (num(worst.ntLongestMs) || 0) : 0;
      const secs = Math.round(longest / 1000);
      const warnMs = num(limits.warnMs), failMs = num(limits.failMs);
      const lvl = failMs !== null && longest >= failMs ? "fail" : warnMs !== null && longest >= warnMs ? "warn" : "ok";
      const badNames = (worst && Array.isArray(worst.badNames)) ? worst.badNames : [];
      if (lvl === "ok") {
        out.push({
          id: "lane_nontank",
          lvl,
          title: "Enemies stayed on the tanks",
          num: secs + " s",
          ev: secs > 0
            ? "Longest stretch on a non-tank within 30 yd: " + secs + " s (" + worst.label + ")."
            : "Longest stretch on a non-tank within 30 yd: none.",
        });
      } else {
        out.push({
          id: "lane_nontank",
          lvl,
          title: "Enemy on a non-tank: " + worst.label,
          num: secs + " s",
          ev: worst.label + " was on " + (badNames.join(", ") || "a non-tank") + " for " + secs + " s in a row ("
            + Math.round((num(worst.ntMs) || 0) / 1000) + " s in all) within 30 yd of its target."
            + " Under 8 s is a taunt bounce the 2 s samples cannot show.",
          bot: badNames[0],
        });
      }
    }

    // The plan's move steps between the pack before the fight's and its own that pass inside a later pack's ring: a scripted
    // wake means the walk wakes that pack, an aggro ring only narrows the margin.
    const strays = extra && Array.isArray(extra.strays) ? extra.strays : null;
    const packI = extra ? (num(extra.packI) || 0) : 0;
    if (strays && packI > 0) {
      const prevPackI = num(extra.prevPackI) || 0;
      const inRange = strays.filter((s) => s && num(s.i) !== null && num(s.i) > prevPackI && num(s.i) < packI);
      if (!inRange.length) {
        out.push({
          id: "plan_strays",
          lvl: "ok",
          title: "Route to this pack stays out of other packs' rings",
          num: "",
          ev: "No move point between the previous pack and this one falls inside a later pack's ring.",
        });
      } else {
        const ev = inRange.slice(0, 4).map((s) => "step " + s.i + " is " + (num(s.dist) || 0).toFixed(1) + " yd from "
          + (s.name || "a mob") + " (ring " + Math.round(num(s.ring) || 0) + " yd" + (s.wake ? ", scripted wake" : "") + ")")
          .join(" · ");
        out.push({
          id: "plan_strays",
          lvl: inRange.some((s) => s.wake) ? "fail" : "warn",
          title: "Route crosses " + inRange.length + " ring(s) on the way here",
          num: inRange.length,
          ev: ev + (inRange.length > 4 ? " · +" + (inRange.length - 4) + " more" : ""),
        });
      }
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

    // A red server rule on a pull shorter than 30 s judges a bot that barely had a global: warn, not fail.
    const shortPull = durMs < LIMITS.shortPullMs;
    (Array.isArray(warnings) ? warnings : []).forEach((w) => {
      const red = w.level === "red";
      out.push({
        id: "rule:" + w.rule + ":" + (w.bot || ""),
        lvl: red ? (shortPull ? "warn" : "fail") : w.level === "yellow" ? "warn" : "ok",
        title: String(w.text == null ? "" : w.text),
        num: red && shortPull ? "(short pull)" : "",
        ev: "Server rule " + w.rule,
        bot: w.bot,
      });
    });

    // Stable sort: level first, the build order inside a level.
    return out.map((c, i) => ({ c, i }))
      .sort((a, b) => (LEVEL[a.c.lvl] - LEVEL[b.c.lvl]) || (a.i - b.i))
      .map((x) => x.c);
  }

  function verdict(fight, layout, warnings, extra) {
    const list = checks(fight, layout, warnings, extra);
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
      const list = checks(det.fight, layout, det.warnings || [], det.extra || null);
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

  // Who killed a bot: the non-self hit with the greatest t_ms (the hits are stored newest-first, so array order
  // says nothing about time). The module's own killer field can name the dead bot (its
  // last event was its own spell or a self-damage tick); with no other source known the killer is unknown, never the victim.
  function killerOf(death, after) {
    if (!death) return "unknown";
    const end = num(death.t_ms) || 0;
    let best = null, bestT = -Infinity;
    (Array.isArray(death.hits) ? death.hits : []).forEach((h) => {
      if (!h || num(h.t_ms) === null || h.t_ms > end || (after !== undefined && h.t_ms <= after)) return;
      if (!h.a || h.a === death.name) return;
      if (best === null || h.t_ms > bestT) { best = h; bestT = h.t_ms; }
    });
    if (best) return String(best.a);
    const named = death.killer == null ? "" : String(death.killer);
    return named && named !== death.name ? named : "unknown";
  }

  // One hit's spell for the recap's SPELL column: the hit's own spell name when it carries one; else, when the fight
  // kept a cast log, the attacker's newest cast in the 3 s before the hit aimed at this bot (or at nobody); a logged
  // fight where nobody cast at the bot means a white hit ("melee"); with no log the spell is unknown (blank). A hit
  // from a fellow bot is never melee: its spell is unknown unless the hit names it.
  function spellForHit(h, bot, botNames, log) {
    if (!h) return null;
    if (h.s) return { name: h.s, sid: h.sid != null ? h.sid : null, t_ms: h.t_ms, likely: false };
    if (!Array.isArray(log) || !log.length) return null;
    const guid = bot && bot.guid != null ? String(bot.guid) : null;
    const cast = log.map((e) => e || {}).filter((e) => e.k === "cast" && num(e.t) !== null
      && e.t <= h.t_ms && e.t >= h.t_ms - 3000 && e.n === h.a
      && (!e.tg || (guid !== null && String(e.tg) === guid) || (e.tn && e.tn === bot.name)))
      .sort((x, y) => x.t - y.t).pop();
    if (cast) return { name: cast.s || "a spell", sid: cast.sid != null ? cast.sid : null, t_ms: cast.t, likely: true };
    if (h.a && h.a !== bot.name && (!botNames || !botNames[h.a])) return { name: "melee", sid: null, t_ms: h.t_ms, likely: false };
    return null;
  }

  function deathRecaps(fight) {
    const deaths = (fight && fight.deaths) || [];
    const bots = (fight && fight.bots) || [];
    const previous = {};
    return deaths.map((d, i) => {
      const after = previous[d.name];
      const b = bots.find((x) => x.name === d.name);
      const botNames = {};
      bots.forEach((x) => { if (x && x.name) botNames[x.name] = true; });
      const log = fight && fight.log;
      const hits = (d.hits || [])
        .filter((h) => h.t_ms <= d.t_ms && (after === undefined || h.t_ms > after))
        .map((h) => {
          const sp = spellForHit(h, { name: d.name, guid: b && b.guid != null ? b.guid : undefined }, botNames, log);
          return { t_ms: h.t_ms, a: h.a, d: h.d, s: h.s, sid: h.sid,
            sp: sp ? sp.name : "", spSid: sp ? sp.sid : null, spT: sp ? sp.t_ms : null, spLikely: !!(sp && sp.likely) };
        });
      previous[d.name] = d.t_ms;
      const total = hits.reduce((s, h) => s + (num(h.d) || 0), 0);
      // The killing spell: the killing blow's spell when it names or matches one, else the last hit that names one.
      // Without either, the module could not see the instant casts of phase 1, so the newest boss cast in the 3 s
      // before the death aimed at this bot (or at nobody) is shown, labelled likely.
      let spell = null, likely = false;
      let killing = null;
      hits.forEach((h) => { if (!killing || h.t_ms > killing.t_ms) killing = h; });
      if (killing && killing.sp && killing.sp !== "melee") spell = { name: killing.sp, sid: killing.spSid, t_ms: killing.spT, likely: killing.spLikely };
      const killedByMelee = !!(killing && killing.sp === "melee");
      if (spell) likely = !!spell.likely;
      for (let j = hits.length - 1; !spell && j >= 0; j--) {
        if (hits[j].s) { spell = { name: hits[j].s, sid: hits[j].sid != null ? hits[j].sid : null, t_ms: hits[j].t_ms, likely: false }; break; }
      }
      if (!spell && !killedByMelee && Array.isArray(log)) {
        const guid = b && b.guid != null ? String(b.guid) : null;
        const cast = log.map((e) => e || {}).filter((e) => e.k === "cast" && num(e.t) !== null
          && e.t <= d.t_ms && e.t >= d.t_ms - 3000
          // only the killing blow's own attacker: another mob's cast near the death is not the killing spell
          && (!killing || !killing.a || !e.n || e.n === killing.a)
          && (!e.tg || (guid !== null && String(e.tg) === guid) || (e.tn && e.tn === d.name)))
          .sort((x, y) => x.t - y.t).pop();
        if (cast) { spell = { name: cast.s || "a spell", sid: cast.sid != null ? cast.sid : null, t_ms: cast.t, likely: true }; likely = true; }
      }
      return {
        i,
        name: d.name,
        guid: b && b.guid != null ? b.guid : undefined,
        t_ms: d.t_ms,
        killer: killerOf(d, after),
        x: d.x,
        y: d.y,
        hits,
        total,
        spanMs: hits.length ? d.t_ms - hits[0].t_ms : 0,
        spell,
        likely,
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
      i: r.i, pct: clampPct(h.t_ms, durMs), d: h.d, a: h.a, h: Math.min(44, 6 + h.d / 70), s: h.s || "",
    })));
    // The boss lane: every cast the module saw, from the finished fight's log (an older daemon has none).
    const bossCasts = [];
    (Array.isArray(fight.log) ? fight.log : []).forEach((e) => {
      if (!e || (e.k !== "cast" && e.k !== "ab") || num(e.t) === null) return;
      bossCasts.push({
        t_ms: e.t,
        pct: clampPct(e.t, durMs),
        n: e.n || "boss",
        s: e.s || "",
        sid: e.sid != null ? e.sid : null,
        target: e.tn || "",
      });
    });
    return { durMs, ticks, deaths, hits, bossCasts };
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
        // The boss's debuffs on this bot (a newer module): the tile's chips, the dispel colour and the MC tag.
        debuffs: (Array.isArray(b.db) ? b.db : []).map((d) => ({ id: d && d.id, n: (d && d.n) || "", st: num(d && d.st) || 1, l: num(d && d.l), dt: (d && d.dt) || "" })),
        mc: !!b.mc,
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

  const utcDay = (t) => {
    const n = num(t);
    if (n === null) return "";
    const d = new Date(n * 1000);
    return d.getUTCFullYear() + "-" + ("0" + (d.getUTCMonth() + 1)).slice(-2) + "-" + ("0" + d.getUTCDate()).slice(-2);
  };

  // Pack numbers count inside a run, not across the whole list: a night restarts at Pack 1 when the session does (a new bind is
  // a new run), and so does a new day. rows are newest first, as the daemon lists them; walking them backwards gives the oldest
  // fight of each group Pack 1.
  function packNumbers(rows) {
    const list = Array.isArray(rows) ? rows : [];
    const out = new Array(list.length);
    const count = {};
    for (let i = list.length - 1; i >= 0; i--) {
      const r = list[i] || {};
      const day = utcDay(r.start);
      const run = r.session != null && r.session !== "" ? String(r.session) : "";
      const group = day + "|" + run;
      count[group] = (count[group] || 0) + 1;
      out[i] = count[group];
    }
    return out;
  }

  // state tells a card whose detail has not arrived apart from one that will never arrive: {failed: {key: {n}}, loadingKey,
  // asked} where asked is how many of the newest rows the page fetches. A row whose detail failed for good says so; one the page
  // never asked (a long night's older rows) says that; everything else is still loading.
  function railCards(list, detailsByKey, layout, state) {
    const rows = Array.isArray(list) ? list : [];
    const st = state && typeof state === "object" ? state : null;
    const asked = st ? num(st.asked) || 0 : rows.length;
    const maxDur = rows.reduce((m, r) => Math.max(m, num(r.dur) || 0), 0);
    const packs = packNumbers(rows);
    return rows.map((row, i) => {
      const det = detailsByKey && detailsByKey[row.key];
      const fight = det && det.fight;
      const dur = num(row.dur) || 0;
      const engaged = fight && fight.engaged && fight.engaged[0];
      const dmg = fight && Array.isArray(fight.bots) ? fight.bots.reduce((s, b) => s + (num(b && b.dmg) || 0), 0) : null;
      const fail = st && st.failed && st.failed[row.key];
      const failed = !!(fail && (num(fail.n) || 0) >= (num(st.retry) || 2));
      const waiting = !det && !failed && (i < asked || row.key === (st && st.loadingKey) || row.key === (st && st.sel));
      return {
        key: row.key,
        // Pack N inside the fight's own day and run: the oldest of that group is Pack 1.
        label: "Pack " + packs[i],
        mob: row.boss_name || (fight && fight.boss_name) || (engaged && engaged.n) || "trash",
        step: num(row.step) || 0,
        durMs: dur,
        dmg: dmg,
        deaths: num(row.deaths) || 0,
        lenPct: maxDur > 0 ? (dur / maxDur) * 100 : 0,
        verdict: fight ? verdict(fight, layout, det.warnings || [], det.extra || null) : null,
        // ready | loading | failed | unasked: what the rail's verdict word says while no verdict exists.
        detail: det ? "ready" : failed ? "failed" : waiting ? "loading" : "unasked",
        // A row COMBAT_EVENTS opened, with no finished fight behind it yet: no bot rows, no detail to load.
        live: !!row.live,
      };
    });
  }

  // A live fight is one with a hostile unit engaged with the group: a hostile mob whose victim is a group member, or
  // damage the group dealt to a hostile inside the module's 10 s window (a bot's dps is that window's damage). Buffs,
  // consumables and heals cast on a parked raid keep the module's book open but are not a fight.
  function liveEngaged(bind) {
    const ids = {};
    ((bind && bind.bots) || []).forEach((b) => {
      if (!b) return;
      if (b.id) ids[String(b.id)] = true;
      else if (b.guid != null) ids[String(b.guid)] = true; // a report from before bots carried their raw guid
    });
    const mobs = Array.isArray(bind && bind.mobs) ? bind.mobs : [];
    for (const m of mobs) {
      if (m && m.reaction === "h" && m.victim && ids[String(m.victim)]) return true;
    }
    const window = bind && bind.combat && Array.isArray(bind.combat.bots) ? bind.combat.bots : [];
    return window.some((w) => w && (num(w.dps) || 0) > 0);
  }

  function liveSummary(bind) {
    if (!bind || !bind.combat || !liveEngaged(bind)) return null;
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
    roleOf, spellCount, fmtInt, mainTank, offTanks, checks, verdict, worstIndex, killerOf, packNumbers,
    spellForHit, deathRecaps, timeline, raidFrames, roleTables, botPanel, railCards, liveEngaged, liveSummary, liveChecks, liveTables,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightDebug = api;
})(typeof self !== "undefined" ? self : this);
