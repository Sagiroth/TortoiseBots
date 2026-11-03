"use strict";
// Ready check: the pure logic behind the Fight debugger's frames header. It turns a live binding (its bots, the plan's
// next stand, the raid flag) into per-bot readiness: dead, low on mana, off the stand, missing a raid buff the bound
// classes can provide, without a flask or elixir or food in a raid, with key cooldowns still running. The module's `rdy`
// facts are the buff and consumable bits (the wire: {"b":13,"c":6,"cr":2,"cn":3,"cl":142}), absent from an older module.
// No DOM, no fetch, no markup: unit-tested by internal/commandersjs/readycheck_check.js under node.
(function (root) {
  // A mana user under half cannot pull; under nine tenths is worth a check. The stand is where the plan gathers the raid,
  // so 15 yd is a straggler and 40 yd is somebody left behind.
  const READY = { manaFail: 0.5, manaWarn: 0.9, standWarnYd: 15, standFailYd: 40 };
  const BUFF = { sta: 1, int: 2, spi: 4, all: 8, ap: 16, mp5: 32 };
  const CONS = { flask: 1, elixir: 2, food: 4 };
  // A class's raid buff the raid may expect from it. Attack power and mana regeneration are assignments, not readiness.
  const PROVIDES = { priest: ["sta"], mage: ["int"], druid: ["all"] };
  const BUFF_ORDER = ["sta", "int", "all"];
  const BUFF_WORD = { sta: "stamina", int: "intellect", all: "all stats" };

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const isMana = (b) => !!b && b.power_type === "mana" && (num(b.max_power) || 0) > 0;

  function isRaid(bind) {
    const cv = typeof CmdView !== "undefined" ? CmdView : root.CmdView;
    return !!(cv && typeof cv.isRaid === "function" && cv.isRaid(bind));
  }

  // The next place the plan gathers the raid: its first stand step at or after the run's step.
  function standOf(plan, run) {
    if (!plan || !Array.isArray(plan.steps) || !run || num(run.step) == null) return null;
    const step = num(run.step);
    const s = plan.steps.find((st) => st && st.stand && num(st.i) != null && st.i >= step);
    if (!s) return null;
    return { i: s.i, x: num(s.x) || 0, y: num(s.y) || 0, z: num(s.z) || 0 };
  }

  // 3D yards to the stand, or null when the bot carries no position.
  function standDist(b, stand) {
    if (!b || !stand) return null;
    const d = Math.hypot((num(b.x) || 0) - stand.x, (num(b.y) || 0) - stand.y, (num(b.z) || 0) - stand.z);
    return isFinite(d) ? d : null;
  }

  // The raid buffs the alive bound bots can provide, in a fixed order (stamina, intellect, all stats).
  function expected(bind) {
    const seen = {};
    ((bind && bind.bots) || []).forEach((b) => {
      if (!b || b.alive === false) return;
      (PROVIDES[b.class] || []).forEach((k) => {
        if (k === "int" && !isMana(b)) return;
        seen[k] = true;
      });
    });
    return BUFF_ORDER.filter((k) => seen[k]);
  }

  // One bot, its reasons in the card's order. A dead bot is dead and nothing else; without rdy only the dead, mana and
  // stand rules run.
  function bot(b, ctx) {
    const c = ctx || {};
    const name = b && b.name ? String(b.name) : "";
    if (!b || b.alive === false) return { name, lvl: "fail", reasons: ["dead"] };
    const reasons = [];
    let lvl = "ok";
    const worse = (l) => { if (l === "fail" || (l === "warn" && lvl === "ok")) lvl = l; };
    const pct = isMana(b) ? (num(b.power) || 0) / b.max_power : null;
    const dist = standDist(b, c.stand);
    const manaFail = pct != null && pct < READY.manaFail;
    if (manaFail) { reasons.push("mana " + Math.round(pct * 100) + "%"); worse("fail"); }
    const standFail = dist != null && dist > READY.standFailYd;
    if (standFail) { reasons.push(Math.round(dist) + " yd from the stand"); worse("fail"); }
    if (!manaFail && pct != null && pct < READY.manaWarn) { reasons.push("mana " + Math.round(pct * 100) + "%"); worse("warn"); }
    if (!standFail && dist != null && dist > READY.standWarnYd) { reasons.push(Math.round(dist) + " yd from the stand"); worse("warn"); }
    const rdy = b.rdy && typeof b.rdy === "object" ? b.rdy : null;
    if (rdy) {
      const buffs = num(rdy.b) || 0;
      (Array.isArray(c.expect) ? c.expect : []).forEach((k) => {
        if (BUFF[k] && (buffs & BUFF[k]) === 0) { reasons.push("no " + BUFF_WORD[k]); worse("warn"); }
      });
      const cons = num(rdy.c) || 0;
      if (c.raid && (cons & (CONS.flask | CONS.elixir)) === 0) { reasons.push("no flask or elixir"); worse("warn"); }
      if (c.raid && (cons & CONS.food) === 0) { reasons.push("not well fed"); worse("warn"); }
      const cr = num(rdy.cr) || 0, cn = num(rdy.cn) || 0;
      if (cr < cn) {
        reasons.push(cr + " of " + cn + " key cooldowns ready (longest " + (num(rdy.cl) || 0) + "s)");
        worse("warn");
      }
    }
    return { name, lvl, reasons };
  }

  // A ready check is a deliberate gather. The raid is gathered when the plan's current step is a stand (the run waits there)
  // or the run is parked; walking or fighting the plan, every bot is naturally far from the next stand and that distance says
  // nothing about readiness, so the stand rule (and the header's verdict) waits for the gather.
  function gatheredAt(bind, plan) {
    const run = bind && bind.run;
    const step = num(run && run.step);
    if (step == null) return true;      // no run: nothing to gather at, the bot rules speak
    if (run.state === "paused") return true;
    const steps = plan && Array.isArray(plan.steps) ? plan.steps : null;
    if (!steps) return true; // no plan to say where the gather is: the bot rules speak, as before
    return steps.some((s) => s && num(s.i) === step && !!s.stand);
  }

  // The whole binding: every bot's level, the counts, the worst, the stand the raid is expected at, whether the raid is
  // gathered for a check, and whether any bot has the module's rdy facts at all.
  function raid(bind, plan) {
    const bots = Array.isArray(bind && bind.bots) ? bind.bots : [];
    const gathered = gatheredAt(bind, plan);
    const stand = gathered ? standOf(plan, bind && bind.run) : null;
    const expect = expected(bind);
    const inRaid = isRaid(bind);
    const rows = bots.map((b) => bot(b, { stand, expect, raid: inRaid }));
    const counts = { ok: 0, warn: 0, fail: 0 };
    rows.forEach((r) => { counts[r.lvl]++; });
    const lvl = counts.fail ? "fail" : counts.warn ? "warn" : "ok";
    return { bots: rows, counts, lvl, stand, gathered, known: bots.some((b) => !!b && !!b.rdy && typeof b.rdy === "object") };
  }

  const api = { READY, BUFF, CONS, PROVIDES, isMana, standOf, gatheredAt, bot, raid };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.ReadyCheck = api;
})(typeof self !== "undefined" ? self : this);
