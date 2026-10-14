"use strict";
// Commanders tab: what a unit is doing and the box shown when the pointer rests on one, the threat colours, the status icons, the
// events (deaths, wipes, boss kills, objects opened or looted) with their feed, and the run line of a card. Pure logic (no page, no
// markup) so a test can run it under node (internal/commandersjs); app.js draws it and builds the boxes with textContent.
//
// It reads the fields of the COMMANDERS report that a newer module adds (all optional; an older module leaves them out and the
// functions then say nothing rather than guess):
//   bot:  st (c combat, n non-combat, d dead, r reaction), act (newest action), res (its verdict), cast (spell being cast), cc (s stunned,
//         c confused, f fleeing, r rooted), rest (d drinking, e eating), mv (moving), tname (name of its target), target (guid)
//   mob:  lvl, rank (b boss, e elite, r rare), cc, cast, victim (guid), reaction (h n f), kind (k v g u)
//   binding: bound_at (unix seconds the binding began)
(function (root) {
  const CC_WORD = { s: "stunned", c: "confused", f: "fleeing", r: "rooted" };
  const CC_LONG = { s: "stunned", c: "crowd controlled (polymorph, sleep or similar)", f: "feared or running away", r: "rooted" };
  const CC_ICON = { s: "stun", c: "sheep", f: "arrow", r: "root" };
  const RANK_WORD = { b: "Boss", e: "Elite", r: "Rare" };
  const REACTION_WORD = { h: "Hostile", n: "Neutral", f: "Friendly" };
  const KIND_WORD = { k: "critter", v: "civilian", g: "guard", u: "cannot be attacked" };
  const RES_WORD = { O: "done", F: "failed", I: "impossible", U: "useless", P: "waiting for a prerequisite", V: "vetoed" };
  const BOSS_GONE_HP = 40;      // a boss that leaves the list at or under this health was killed (above it: it left sight or reset)
  const BOSS_FORGET_SEC = 90;   // a boss not seen for this long is forgotten without a kill
  const EVENTS_MAX = 80;
  const OPEN_VERBS = { open: 1, use: 1, loot: 1 };

  const str = (s) => (typeof s === "string" ? s : "");
  const num = (n) => typeof n === "number" && isFinite(n);
  const pct = (a, b) => (b > 0 ? Math.max(0, Math.min(100, Math.round((a / b) * 100))) : 0);

  // ---- what a character is doing ---------------------------------------------------------------------------------------

  // The engine's action names that read as an activity of their own; any other name is shown raw in the details, not guessed at.
  const ACTION_WORD = {
    melee: "fighting", attack: "fighting", "attack anything": "fighting", follow: "following", stay: "holding position", "move to": "moving",
    drink: "drinking", eat: "eating", loot: "looting", revive: "reviving", release: "releasing its spirit", flee: "fleeing",
    "reach melee": "closing in", "reach spell": "moving into range", "reach ranged": "moving into range", "auto shot": "shooting", shoot: "shooting",
  };
  function actionPhrase(act) { return ACTION_WORD[str(act).toLowerCase()] || ""; }

  // What a bound bot is doing, as {icon, word, text}, or null when the module does not say (an older module). o = {targetName}.
  // icon is one of dead | cc | cast | rest | fight | move | idle; word is the short form for a column, text the full phrase.
  function activity(b, o) {
    if (!b) return null;
    if (b.alive === false) return { icon: "dead", word: "dead", text: "dead" };
    if (b.st == null) return null;
    const tgt = str((o && o.targetName) || b.tname);
    if (CC_WORD[b.cc]) return { icon: "cc", word: CC_WORD[b.cc], text: CC_LONG[b.cc] };
    if (b.cast) return { icon: "cast", word: "casting", text: "casting " + b.cast + (tgt ? " on " + tgt : "") };
    if (b.rest === "d") return { icon: "rest", word: "drinking", text: "drinking" };
    if (b.rest === "e") return { icon: "rest", word: "eating", text: "eating" };
    if (b.st === "c") return { icon: "fight", word: "fighting", text: "fighting" + (tgt ? " " + tgt : "") };
    if (b.mv) return { icon: "move", word: "moving", text: "moving" };
    const phrase = actionPhrase(b.act);
    if (phrase) return { icon: phrase === "fleeing" ? "cc" : "idle", word: phrase, text: phrase };
    return { icon: "idle", word: "idle", text: "idle" };
  }

  // ---- ids and distances -----------------------------------------------------------------------------------------------

  function byId(list) {
    const o = {};
    (list || []).forEach((u) => { if (u && u.id) o[u.id] = u; });
    return o;
  }

  // Yards in the plane between two units with game x, y; null for a missing one.
  function dist(a, b) {
    if (!a || !b || !num(a.x) || !num(a.y) || !num(b.x) || !num(b.y)) return null;
    return Math.hypot(a.x - b.x, a.y - b.y);
  }

  // ---- the hover box ---------------------------------------------------------------------------------------------------

  const HEALTH_LOW = 30;

  function hpText(b) { return b.max_hp > 0 ? pct(b.hp, b.max_hp) + " % (" + b.hp + " / " + b.max_hp + ")" : ""; }

  // A box is {title, sub, tone, rows: [[label, value], ...]}: plain strings only; the page puts them in with textContent.
  // ctx = {lead (the lead bot), bots (on this map), mobs (on this map)}.
  function describeBot(b, ctx) {
    const c = ctx || {}, mobs = byId(c.mobs), rows = [];
    const dead = b.alive === false;
    const target = b.target && mobs[b.target] ? mobs[b.target] : null;
    const tname = (target && target.name) || str(b.tname);
    const act = activity(b, { targetName: tname });
    const sub = [b.role ? (b.role === "dps" ? "DPS" : b.role[0].toUpperCase() + b.role.slice(1)) : "", str(b.class), b.level ? "level " + b.level : ""].filter(Boolean).join(" · ");
    rows.push(["Health", dead ? "dead" : hpText(b) || "unknown"]);
    if (act) rows.push(["Doing", act.text]);
    if (!dead && (tname || b.target)) rows.push(["Target", tname || "a unit that is not listed"]);
    if (!dead && b.cc) rows.push(["Crowd control", CC_LONG[b.cc] || b.cc]);
    if (b.max_power > 0 && !dead) rows.push([b.power_type === "rage" || b.power_type === "energy" ? b.power_type[0].toUpperCase() + b.power_type.slice(1) : "Mana", pct(b.power, b.max_power) + " %"]);
    if (!dead && b.act) rows.push(["Last action", b.act + (RES_WORD[b.res] ? " (" + RES_WORD[b.res] + ")" : "")]);
    const d = b === c.lead ? null : dist(b, c.lead);
    if (b === c.lead) rows.push(["Role", "the lead of the party"]);
    else if (d != null) rows.push(["From the lead", Math.round(d) + " yd"]);
    return { title: str(b.name) || "Bot", sub, tone: dead ? "dead" : "bot", rows };
  }

  // A mob: name (or its entry), level, rank, reaction, health, what it casts, whom it attacks, crowd control, distance to the lead.
  function describeMob(m, ctx) {
    const c = ctx || {}, bots = byId(c.bots), rows = [];
    const title = str(m.name) || (m.entry ? "Creature " + m.entry : "Unit");
    const sub = [m.lvl ? "level " + m.lvl : "", RANK_WORD[m.rank] || "", REACTION_WORD[m.reaction] || "", KIND_WORD[m.kind] || ""].filter(Boolean).join(" · ");
    rows.push(["Health", num(m.hp) ? m.hp + " %" : "unknown"]);
    if (m.cast) rows.push(["Casting", m.cast]);
    if (m.victim) {
      const v = bots[m.victim];
      rows.push(["Attacking", v ? v.name : "someone outside the party"]);
    } else {
      rows.push(["Attacking", "nobody"]);
    }
    if (m.cc) rows.push(["Crowd control", CC_LONG[m.cc] || m.cc]);
    const d = dist(m, c.lead);
    if (d != null) rows.push(["From the lead", Math.round(d) + " yd"]);
    if (m.entry) rows.push(["Entry", String(m.entry)]);
    return { title, sub, tone: m.rank === "b" ? "boss" : m.reaction === "h" ? "hostile" : m.reaction === "f" ? "friendly" : "neutral", rows };
  }

  function describeCommander(name, pos, ctx) {
    const rows = [["Role", "the commander (a player, not on the map of a bot)"]];
    const d = dist(pos, (ctx || {}).lead);
    if (d != null) rows.push(["From the lead", Math.round(d) + " yd"]);
    return { title: str(name), sub: "Commander", tone: "commander", rows };
  }

  function describeEvent(e, startSec) {
    const l = feedLine(e, startSec);
    return { title: l.text, sub: l.time ? "at " + l.time + " into the run" : "", tone: "event", rows: [] };
  }

  // Where the box goes: right and below the pointer, flipped to the left or above when it would leave the panel, kept inside it.
  function boxPosition(px, py, bw, bh, pw, ph, gap) {
    const g = gap == null ? 14 : gap;
    let left = px + g, top = py + g;
    if (left + bw > pw) left = px - g - bw;
    if (top + bh > ph) top = py - g - bh;
    return { left: Math.max(2, Math.min(left, Math.max(2, pw - bw - 2))), top: Math.max(2, Math.min(top, Math.max(2, ph - bh - 2))) };
  }

  // ---- bars, colours and icons -----------------------------------------------------------------------------------------

  function hpClass(p) { return p > 60 ? "good" : p > HEALTH_LOW ? "mid" : "low"; }

  // The health bar of a bot or a mob: {pct, cls} or null (dead, or unknown).
  function hpBar(u, kind) {
    if (!u || u.alive === false) return null;
    const p = kind === "bot" ? (u.max_hp > 0 ? pct(u.hp, u.max_hp) : null) : (num(u.hp) ? Math.max(0, Math.min(100, u.hp)) : null);
    if (p == null || (kind !== "bot" && p <= 0)) return null;
    return { pct: p, cls: kind === "bot" ? hpClass(p) : "mob" };
  }

  // Whom a mob attacks: "tank" (a bot with the tank role), "other" (another bot) or "none" (nobody, or not a bot of this party).
  function threatClass(m, bots) {
    if (!m || !m.victim) return "none";
    const v = byId(bots)[m.victim];
    if (!v) return "none";
    return v.role === "tank" ? "tank" : "other";
  }

  // The ids of what the living tanks target, and of the hostile mobs nobody fights yet that some bot has picked (the pull target).
  function targetMarks(mobs, bots) {
    const tank = {}, pull = {}, listed = byId(mobs);
    (bots || []).forEach((b) => {
      if (b.alive === false || !b.target || !listed[b.target]) return;
      if (b.role === "tank") tank[b.target] = true;
      if (!listed[b.target].victim && listed[b.target].reaction === "h") pull[b.target] = true;
    });
    return { tank, pull };
  }

  // The icons over a unit, in order: [{icon, title}]. icon: skull | spark | sheep | stun | arrow | root | cup.
  function statusIcons(u, kind) {
    const out = [];
    if (!u) return out;
    if (kind === "bot" && u.alive === false) return [{ icon: "skull", title: "dead" }];
    if (CC_WORD[u.cc]) out.push({ icon: CC_ICON[u.cc], title: CC_WORD[u.cc] });
    if (u.cast) out.push({ icon: "spark", title: "casting " + u.cast });
    if (kind === "bot" && u.rest) out.push({ icon: "cup", title: u.rest === "d" ? "drinking" : "eating" });
    return out;
  }

  // ---- events: deaths, wipes, boss kills, objects opened or looted ------------------------------------------------------

  // Memory per commander. Everything is derived from the reports as they come (deaths from the bots, a boss kill from a boss unit that
  // leaves the list nearly dead or at zero, an object from an accepted open / use / loot order), so no module event stream is needed.
  function newEvents() { return { by: {} }; }

  function mem(ev, key, nowSec) {
    return ev.by[key] || (ev.by[key] = { first: nowSec, alive: {}, wiped: false, bosses: {}, orders: {}, started: false, list: [], seq: 0 });
  }

  function push(m, e) {
    e.id = ++m.seq;
    m.list.push(e);
    if (m.list.length > EVENTS_MAX) m.list.shift();
    return e;
  }

  // Take one fresh report of a binding in; returns the events it produced.
  function noteEvents(ev, bind, nowSec) {
    const m = mem(ev, bind.commander, nowSec), made = [];
    const bots = (bind.bots || []), lead = bots.find((b) => b.lead) || bots[0] || null;
    const at = (u) => ({ x: u && num(u.x) ? u.x : null, y: u && num(u.y) ? u.y : null, map: u ? (u.map != null ? u.map : bind.mobs_map) : null });

    // deaths and wipes
    let anyAlive = false;
    bots.forEach((b) => {
      const k = b.guid || b.name, was = m.alive[k], now = b.alive !== false;
      if (now) anyAlive = true;
      if (m.started && was === true && !now) made.push(push(m, { t: nowSec, kind: "death", name: str(b.name), ...at(b) }));
      m.alive[k] = now;
    });
    if (bots.length) {
      if (!anyAlive && !m.wiped && m.started) { made.push(push(m, { t: nowSec, kind: "wipe", name: "", ...at(lead) })); m.wiped = true; }
      if (anyAlive) m.wiped = false;
    }

    // boss kills
    if (Array.isArray(bind.mobs) && bind.mobs_map != null) {
      const here = {};
      bind.mobs.forEach((mob) => { if (mob && mob.id) here[mob.id] = mob; });
      Object.keys(m.bosses).forEach((id) => {
        const b = m.bosses[id], cur = here[id];
        if (cur && cur.hp > 0) return;
        if (!m.wiped && ((cur && cur.hp <= 0) || b.hp <= BOSS_GONE_HP)) made.push(push(m, { t: nowSec, kind: "boss", name: b.name, x: b.x, y: b.y, map: b.map }));
        delete m.bosses[id];
      });
      bind.mobs.forEach((mob) => {
        if (mob && mob.id && mob.rank === "b" && mob.hp > 0) m.bosses[mob.id] = { name: str(mob.name) || (mob.entry ? "creature " + mob.entry : "boss"), hp: mob.hp, x: mob.x, y: mob.y, map: bind.mobs_map, seen: nowSec };
      });
      Object.keys(m.bosses).forEach((id) => { if (!here[id] && nowSec - m.bosses[id].seen > BOSS_FORGET_SEC) delete m.bosses[id]; });
    }

    // objects: an accepted open, use or loot order made after the first report of the binding
    (bind.orders || []).forEach((o) => {
      const verb = str(o.verb).toLowerCase(), key = o.at + "|" + verb + "|" + str(o.detail);
      if (m.orders[key]) return;
      m.orders[key] = true;
      if (!m.started || !o.ok || !OPEN_VERBS[verb]) return;
      const entry = /\bentry:(\d+)/i.exec(str(o.detail));
      made.push(push(m, { t: o.at || nowSec, kind: verb === "loot" ? "loot" : "open", name: entry ? "object " + entry[1] : str(o.detail).slice(0, 28), ...at(lead) }));
    });
    m.started = true;
    return made;
  }

  function eventsOf(ev, commander) { return (ev.by[commander] && ev.by[commander].list) || []; }
  function firstSeen(ev, commander) { return ev.by[commander] ? ev.by[commander].first : 0; }

  // Forget the commanders that are no longer listed.
  function pruneEvents(ev, commanders) {
    const live = {};
    commanders.forEach((c) => { live[c] = true; });
    Object.keys(ev.by).forEach((k) => { if (!live[k]) delete ev.by[k]; });
  }

  function clockText(sec) {
    sec = Math.max(0, Math.round(sec || 0));
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
    const p = (n) => String(n).padStart(2, "0");
    return h ? h + ":" + p(m) + ":" + p(s) : p(m) + ":" + p(s);
  }

  // One line of the feed: {time (since the run began), text, kind}.
  function feedLine(e, startSec) {
    const time = startSec && e.t >= startSec ? clockText(e.t - startSec) : "";
    const name = e.name || "";
    const text = e.kind === "death" ? name + " died"
      : e.kind === "wipe" ? "Wipe: the whole party is down"
      : e.kind === "boss" ? "Boss down: " + name
      : e.kind === "open" ? "Opened " + (name || "an object")
      : e.kind === "loot" ? "Looted " + (name || "a corpse or chest")
      : name || e.kind;
    return { time, text, kind: e.kind };
  }

  // The events to pin on a map: the ones that happened on mapId with a position, newest last, at most max.
  function pinsOn(list, mapId, max) {
    return (list || []).filter((e) => e.map === mapId && num(e.x) && num(e.y)).slice(-(max == null ? 30 : max));
  }

  // ---- the run line of a card ------------------------------------------------------------------------------------------

  // {elapsed (seconds), text}: the run timer from the binding's start (module) or its first report here, and the bosses: how many were
  // killed, out of the ones seen so far, and the next one seen alive. The module reports no dungeon plan, so no section count.
  function runInfo(bind, ev, nowSec) {
    const m = ev.by[bind.commander];
    const start = bind.bound_at > 0 ? bind.bound_at : m ? m.first : nowSec;
    const elapsed = Math.max(0, nowSec - start);
    const down = [], seen = [];
    ((m && m.list) || []).forEach((e) => { if (e.kind === "boss" && down.indexOf(e.name) < 0) down.push(e.name); });
    const alive = m ? Object.keys(m.bosses).map((k) => m.bosses[k]) : [];
    alive.forEach((b) => seen.push(b.name));
    const parts = ["Run " + clockText(elapsed)];
    const brief = runBrief(bind, nowSec);
    if (brief && brief.text) parts.push(brief.text);
    if (down.length || alive.length) parts.push("bosses " + down.length + " / " + (down.length + alive.length) + " seen");
    if (alive.length) parts.push("next: " + alive[0].name);
    else if (down.length) parts.push("last: " + down[down.length - 1]);
    return { elapsed, start, down, next: alive.length ? alive[0].name : "", text: parts.join(" · ") };
  }

  // The run leader's run of a binding (`.bot run`; the "observe" mode: the bots clear, the commander watches), in words:
  // {text: "step 78/205 · ancient stone keeper · pull · resting", paused, reason, changedAgo (seconds, or null), state}; null for a
  // binding without a run (or an older module). nowSec is the game server's clock.
  function runBrief(bind, nowSec) {
    const r = bind && bind.run;
    if (!r || typeof r !== "object") return null;
    const leg = str(r.leg).replace(/[_\-]+/g, " ").trim();
    const parts = [];
    if (num(r.steps) && r.steps > 0) parts.push("step " + (r.step || 0) + "/" + r.steps);
    if (leg && leg !== "-") parts.push(leg);
    if (r.state === "done") parts.push("done");
    else if (r.state === "paused") parts.push("paused");
    else {
      const what = [str(r.kind) !== "-" ? str(r.kind) : "", str(r.phase) !== "-" ? str(r.phase) : ""].filter(Boolean);
      if (what.length) parts.push(what.join(" · "));
    }
    const paused = r.state === "paused";
    const note = str(r.note) === "-" ? "" : str(r.note);
    return {
      text: parts.join(" · "), paused, reason: paused ? note : "", note, state: str(r.state),
      changedAgo: num(r.changed) && r.changed > 0 && nowSec ? Math.max(0, Math.round(nowSec - r.changed)) : null,
    };
  }

  const api = {
    runBrief,
    activity, actionPhrase, describeBot, describeMob, describeCommander, describeEvent, boxPosition, hpBar, hpClass, threatClass, targetMarks, statusIcons,
    newEvents, noteEvents, eventsOf, firstSeen, pruneEvents, clockText, feedLine, pinsOn, runInfo, dist, CC_WORD,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.UnitInfo = api;
})(typeof self !== "undefined" ? self : this);
