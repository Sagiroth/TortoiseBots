"use strict";
// Boss abilities: the pure half of the dashboard's boss panel. It turns the committed boss table
// (/data/boss_abilities.json, written by tools/boss_data.py from data/boss/boss_abilities.csv), one overlay mob row, the
// live/finished fight events and the phase the boss was last seen in into what the page draws: the phase, every known
// ability with when it was last seen and its next script-timer window, the current cast, the boss's auras flags and adds.
// No DOM, no fetch at load, no markup decisions outside render: unit-tested by internal/commandersjs/bosspanel_check.js.
//
// Table shape (the converter's JSON, one boss per entry; the CSV's a/b/c/d map to first/repeatMin/repeatMax/flags):
//   { "bosses": [ { "entry": 14507, "name": "Venoxis", "map": 309, "order": 0,
//       "abilities": [ { "spell": 23861, "name": "Poison Cloud", "first": 0, "repeatMin": 0, "repeatMax": 0,
//                        "flags": "p=snake;ground" } ],
//       "phases": [ { "name": "snake", "rule": "hp<50", "order": 1 } ],
//       "adds": [ { "entry": 14884, "role": "summon", "priority": 0 } ],
//       "notes": [ "Poison Cloud needs the snake phase." ] } ] }
// Raw CSV rows ({key,a,b,c,d}) and a bare array are accepted too, so the page survives whichever build wrote the file.
(function (root) {
  const num = (v) => (typeof v === "number" && isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && isFinite(Number(v)) ? Number(v) : null);
  const str = (v) => (typeof v === "string" ? v : v == null ? "" : String(v));
  const arr = (v) => (Array.isArray(v) ? v : v == null ? [] : [v]);
  const first = (...vals) => {
    for (const v of vals) if (v !== undefined && v !== null && v !== "") return v;
    return undefined;
  };

  const FLAG_WORD = {
    n: "untargetable",
    i: "cannot be attacked",
    h: "flying",
    d: "feigning death",
    e: "evading",
    v: "not visible",
  };
  const FLAG_ORDER = ["n", "i", "h", "d", "e", "v"];

  // ---- the table -------------------------------------------------------------------------------------------------------

  function ability(row) {
    if (!row || typeof row !== "object") return null;
    const spell = num(first(row.spell, row.sid, row.id, row.key, row.entry));
    if (spell === null) return null;
    const rawFlags = str(first(row.flags, typeof row.d === "string" ? row.d : "", row.flag, ""));
    const flags = {};
    rawFlags.split(";").map((s) => s.trim()).filter(Boolean).forEach((f) => {
      const i = f.indexOf("="), k = (i < 0 ? f : f.slice(0, i)).trim().toLowerCase(), v = i < 0 ? "" : f.slice(i + 1).trim();
      if (k === "p") flags.phase = v;
      else if (k === "t") flags.target = v;
      else if (k === "hp") flags.hpBelow = num(v);
      else if (/^hp\d/.test(k)) flags.hpBelow = num(k.slice(2));
      else if (k === "summon") flags.summon = num(v);
      else if (k === "aura") flags.aura = true;
      else if (k === "self") flags.self = true;
      else if (k === "ground") flags.ground = true;
      else if (k === "mark") flags.mark = true;
      else if (k === "int") flags.interruptible = true;
    });
    // The converter's JSON names one field per flag instead of a flags string; those fields win when both name a flag.
    const fieldPhase = str(row.phase).trim();
    if (fieldPhase) flags.phase = fieldPhase;
    const fieldTarget = str(row.target).trim();
    if (fieldTarget) flags.target = fieldTarget;
    if (row.aura) flags.aura = true;
    if (row.self) flags.self = true;
    if (row.ground) flags.ground = true;
    if (row.mark) flags.mark = true;
    if (row.int) flags.interruptible = true;
    const fieldSummon = num(row.summon);
    if (fieldSummon) flags.summon = fieldSummon;
    const fieldHp = num(row.hp);
    if (fieldHp) flags.hpBelow = fieldHp;
    return {
      sid: spell,
      name: str(first(row.name, row.n, row.spellName, row.spell_name, "spell " + spell)),
      first: num(first(row.first, row.a, row.firstMs, row.first_ms)) || 0,
      repeatMin: num(first(row.repeatMin, row.repeat_min, row.b, row.repeatMinMs, row.repeat_min_ms)) || 0,
      repeatMax: num(first(row.repeatMax, row.repeat_max, row.c, row.repeatMaxMs, row.repeat_max_ms)) || 0,
      rawFlags,
      flags,
      source: str(row.source),
    };
  }

  // The table (tools/boss_data.py) writes a rule as an object ({kind:"hp",hp:50}, {kind:"aura",aura:id},
  // {kind:"flag",flag:"i"}, {kind:"start"}); the matcher reads the text form ("hp<50", "aura=id", "flag=i", "start").
  function ruleText(rule) {
    if (rule && typeof rule === "object") {
      const kind = str(rule.kind).toLowerCase();
      if (kind === "hp" && num(rule.hp) !== null) return "hp<" + num(rule.hp);
      if (kind === "aura" && num(rule.aura) !== null) return "aura=" + num(rule.aura);
      if (kind === "flag" && str(rule.flag)) return "flag=" + str(rule.flag);
      if (kind === "start") return "start";
      return "";
    }
    return str(rule);
  }

  function phase(row) {
    if (row == null) return null;
    if (typeof row === "string") return { name: row, rule: "", order: 0 };
    if (typeof row !== "object") return null;
    const name = str(first(row.name, row.phase, row.key));
    const rule = ruleText(first(row.rule, row.a, ""));
    if (!name) return null;
    return { name, rule, order: num(first(row.order, row.b)) || 0 };
  }

  function add(row) {
    if (!row || typeof row !== "object") return null;
    const entry = num(first(row.entry, row.key, row.a));
    if (entry === null) return null;
    return {
      entry,
      name: str(first(row.name, row.n, row.creature, "")),
      role: str(first(row.role, row.a === undefined ? "" : row.a, "")),
      priority: num(first(row.priority, row.b)) || 0,
    };
  }

  function note(row) {
    if (row == null) return "";
    if (typeof row === "string") return row;
    return str(first(row.text, row.note, row.key, ""));
  }

  function boss(row, i) {
    if (!row || typeof row !== "object") return null;
    const entry = num(first(row.entry, row.id, row.creature, row.entryId, row.entry_id));
    if (entry === null) return null;
    return {
      entry,
      name: str(first(row.name, row.boss, row.n, "creature " + entry)),
      map: num(first(row.map, row.mapId, row.map_id)),
      mapKey: typeof row.map === "string" && !isFinite(Number(row.map)) ? row.map : "",
      order: num(first(row.order, row.i)) == null ? i : num(first(row.order, row.i)),
      abilities: arr(first(row.abilities, row.spells, row.ability)).map(ability).filter(Boolean),
      phases: arr(first(row.phases, row.phaseRows, row.phase)).map(phase).filter(Boolean),
      adds: arr(first(row.adds, row.add)).map(add).filter(Boolean),
      notes: arr(first(row.notes, row.note)).map(note).filter(Boolean),
    };
  }

  // Accepts the converter's object, a bare boss array, or an entry-keyed object. Unknown shapes give an empty index.
  function normalize(data) {
    let rows = [];
    if (Array.isArray(data)) rows = data;
    else if (data && typeof data === "object") {
      const b = data.bosses || data.boss || data.entries;
      if (Array.isArray(b)) rows = b;
      else if (b && typeof b === "object") rows = Object.keys(b).map((k) => (b[k] && typeof b[k] === "object" && b[k].entry == null ? Object.assign({ entry: k }, b[k]) : b[k]));
      else if (num(data.entry) !== null) rows = [data];
      else rows = Object.keys(data).map((k) => (data[k] && typeof data[k] === "object" && num(data[k].entry) === null ? Object.assign({ entry: k }, data[k]) : data[k]));
    }
    const bosses = rows.map(boss).filter(Boolean).sort((a, b) => (a.map || 0) - (b.map || 0) || (a.order - b.order) || a.entry - b.entry);
    const byEntry = {};
    bosses.forEach((b) => { if (!byEntry[b.entry]) byEntry[b.entry] = b; });
    const bySpell = {};
    const bySpellAll = {};
    bosses.forEach((b) => b.abilities.forEach((a, i) => {
      const slot = { boss: b, ability: a, index: i };
      if (!(a.sid in bySpell)) bySpell[a.sid] = slot;
      (bySpellAll[a.sid] = bySpellAll[a.sid] || []).push(slot);
    }));
    return { bosses: bosses, byEntry: byEntry, bySpell: bySpell, bySpellAll: bySpellAll };
  }

  let cache = null, loading = null, loaded = null, rawDoc = null;
  // Fetch the table once; a missing file (an old daemon) resolves to null and the page draws no boss panels.
  function load(url) {
    if (cache) return Promise.resolve(cache);
    if (loading) return loading;
    if (typeof fetch !== "function") return Promise.resolve(null);
    loading = fetch(url || "/data/boss_abilities.json", { credentials: "same-origin" })
      .then((r) => (r && r.ok ? r.json() : null))
      .then((d) => { rawDoc = d; loaded = normalize(d); cache = loaded.bosses.length ? loaded : null; return cache; })
      .catch(() => { cache = null; return null; })
      .then((v) => { loading = null; return v; });
    return loading;
  }
  function data() { return cache; }
  // The document as fetched: maps[] and codeAnswers[] the normalized index drops, which the coverage view needs.
  function raw() { return rawDoc; }
  function bossFor(table, entry) {
    const t = table === undefined ? cache : table;
    if (!t) return null;
    const e = num(entry);
    return e === null ? null : t.byEntry[e] || null;
  }
  function abilityBySpell(table, sid, casterEntry) {
    const t = table === undefined ? cache : table;
    const s = num(sid);
    if (!t || s === null) return null;
    const e = num(casterEntry);
    const slots = t.bySpellAll ? t.bySpellAll[s] : null;
    if (e !== null && slots && slots.length > 1) {
      const hit = slots.find((slot) => slot.boss.entry === e);
      if (hit) return hit;
    }
    return t.bySpell[s] || null;
  }
  // ---- phases ----------------------------------------------------------------------------------------------------------

  function ruleMatches(rule, mob) {
    const r = str(rule).trim().toLowerCase();
    if (!r) return false;
    if (r.indexOf("hp<") === 0) {
      const n = num(r.slice(3)), hp = num(mob && mob.hp);
      return n !== null && hp !== null && hp < n;
    }
    if (r.indexOf("aura=") === 0) {
      const id = num(r.slice(5));
      return id !== null && arr(mob && mob.au).some((a) => a && num(a.id) === id);
    }
    if (r === "start") return true;
    if (r.indexOf("flag=") === 0) {
      const f = r.slice(5).trim();
      return !!f && str(mob && mob.uf).toLowerCase().indexOf(f) >= 0;
    }
    return false;
  }

  // The label of the first rule (in order) that matches. An hp< phase latches: a boss does not heal back into an earlier
  // phase, so an hp rule with a higher threshold than the one already shown is ignored while the boss is alive. A more severe
  // threshold (p1 at 20% -> p2 at 10%) and an aura or flag phase still apply. prior is the phase this fight last showed for
  // the boss (the page keeps it per fight; an aura or flag phase is re-read every report).
  function hpRule(rule) {
    const m = /^\s*hp<(\d+(?:\.\d+)?)/i.exec(rule || "");
    return m ? Number(m[1]) : null;
  }
  function phaseOf(b, mob, prior) {
    const rules = (b && b.phases) || [];
    if (!rules.length) return { phase: null, latched: false };
    const sorted = rules.map((p, i) => ({ p: p, i: i })).sort((a, b2) => (a.p.order - b2.p.order) || (a.i - b2.i)).map((x) => x.p);
    const hit = sorted.find((p) => ruleMatches(p.rule, mob));
    const hp = num(mob && mob.hp);
    const alive = hp === null || hp > 0;
    const prev = prior ? sorted.find((p) => p.name === prior) : null;
    const prevHp = prev ? hpRule(prev.rule) : null;
    if (prev && alive) {
      if (!hit) return { phase: prev.name, latched: prevHp !== null };
      const hitHp = hpRule(hit.rule);
      if (prevHp !== null && hitHp !== null) {
        if (hitHp < prevHp) return { phase: hit.name, latched: false };
        return { phase: prev.name, latched: true };
      }
      return { phase: hit.name, latched: false };
    }
    if (hit) return { phase: hit.name, latched: false };
    return { phase: null, latched: false };
  }

  // ---- the model -------------------------------------------------------------------------------------------------------

  function auraList(mob) {
    return arr(mob && mob.au).map((a) => {
      if (!a || typeof a !== "object") return null;
      const id = num(a.id);
      if (id === null) return null;
      return { id, name: str(first(a.n, a.name, "spell " + id)), stacks: num(first(a.st, a.stacks)) || 1, leftMs: num(first(a.l, a.leftMs)) };
    }).filter(Boolean);
  }

  function flagText(uf) {
    const s = str(uf);
    return FLAG_ORDER.filter((f) => s.indexOf(f) >= 0).map((f) => FLAG_WORD[f]).filter(Boolean);
  }

  // The fight events (live ring or a finished fight's log). t is ms since the fight began; sid is the spell.
  function eventList(events) {
    return arr(events).filter((e) => e && typeof e === "object").map((e) => ({
      k: str(e.k),
      sid: num(first(e.sid, e.spell, e.spellId, e.spell_id)),
      name: str(first(e.s, e.name, e.spellName)),
      t: num(first(e.t, e.t_ms, e.tMs)),
      tg: str(first(e.tg, e.targetGuid, e.target_guid)),
      tn: str(first(e.tn, e.targetName, e.target_name)),
      e: num(e.e),
      n: str(e.n),
      by: str(e.by),
      m: str(e.m),
    }));
  }

  function model(table, mob, events, fightStartMs, nowMs, priorPhase) {
    const t = table === undefined ? cache : table;
    if (!t || !mob) return null;
    const b = bossFor(t, mob.entry);
    if (!b) return null;
    const evs = eventList(events);
    const start = num(fightStartMs) || 0, now = num(nowMs) == null ? start : num(nowMs);
    const elapsed = Math.max(0, now - start);
    const ph = phaseOf(b, mob, priorPhase);
    const seenAt = {};
    const countsFor = (e, sid) => {
      if (e.sid !== sid) return false;
      if (e.sid === null || e.t === null) return false;
      if (e.k !== "cast" && e.k !== "ab" && e.k !== "da" && e.k !== "dr") return false;
      const slots = t.bySpellAll ? t.bySpellAll[sid] : null;
      if (!slots || slots.length <= 1) return true;
      return e.e === null || e.e === b.entry || !slots.some((s) => s.boss.entry === e.e);
    };
    evs.forEach((e) => {
      if (!countsFor(e, e.sid)) return;
      if (!(e.sid in seenAt) || e.t > seenAt[e.sid]) seenAt[e.sid] = e.t;
    });
    const currentSid = num(mob.cid);
    const left = num(mob.cleft);
    const total = num(mob.ctot);
    const targetName = str(first(mob.ctn, mob.ctargetName, mob.castTargetName, mob.ctarget, ""));
    let lastCast = null;
    evs.forEach((e) => { if (e.k === "cast" && countsFor(e, e.sid) && (lastCast === null || e.t > lastCast.t)) lastCast = e; });

    const abilities = b.abilities.map((a, i) => {
      const seenAtMs = a.sid in seenAt ? seenAt[a.sid] : null;
      const castingNow = currentSid === a.sid && left !== null && left > 0;
      // A cast the report carries right now is a sighting even when no event has arrived yet (the live panel before the
      // first event datagram): last is 0 s and the next window counts from this moment.
      const at = seenAtMs !== null ? seenAtMs : castingNow ? Math.max(0, now - start) : null;
      const seen = at !== null;
      const flags = a.flags || {};
      const inPhase = !flags.phase || !ph.phase || flags.phase === ph.phase;
      const lastMs = seen ? Math.max(0, now - (start + at)) : null;
      let next = null;
      if (!flags.hpBelow) {
        if (seen) {
          if (a.repeatMax > 0) next = { lo: at + a.repeatMin, hi: at + a.repeatMax, at: elapsed };
        } else if (a.first > 0) next = { lo: a.first, hi: a.first, at: elapsed };
      }
      let casting = null;
      if (castingNow) {
        const tn = (lastCast && lastCast.sid === a.sid && lastCast.tn) || targetName;
        casting = {
          leftMs: left,
          frac: total && total > 0 ? Math.max(0, Math.min(1, 1 - left / total)) : 0,
          target: tn,
        };
      }
      return {
        sid: a.sid, name: a.name, flags: a.flags, rawFlags: a.rawFlags,
        lastMs, seen, inPhase, next, casting, color: i,
        hpBelow: flags.hpBelow == null ? null : flags.hpBelow,
      };
    });

    const adds = b.adds.map((x) => {
      const live = arr(first(mob.adds, mob.add)).find((m) => m && num(m.entry) === x.entry);
      return {
        entry: x.entry, name: (live && str(live.name)) || x.name || "", role: x.role, priority: x.priority,
        alive: live ? num(live.hp) !== 0 : null, hp: live ? num(live.hp) : null, count: live ? 1 : 0,
      };
    });
    const liveCounts = {};
    arr(first(mob.adds, mob.add)).forEach((m) => { const e = num(m && m.entry); if (e !== null) liveCounts[e] = (liveCounts[e] || 0) + 1; });
    adds.forEach((x) => { if (liveCounts[x.entry]) x.count = liveCounts[x.entry]; });

    return {
      boss: { entry: b.entry, name: b.name, map: b.map, mapKey: b.mapKey, order: b.order },
      phase: ph.phase,
      phaseLatched: !!ph.latched,
      auras: auraList(mob),
      flags: str(mob.uf),
      flagWords: flagText(mob.uf),
      adds: adds,
      notes: b.notes.slice(),
      hp: num(mob.hp),
      abilities: abilities,
      mark: markIds(t),
    };
  }

  // Every spell id a `mark` ability names: the map draws the "marked" icon from the data, never from a spell id in JS.
  function markIds(table) {
    const t = table === undefined ? cache : table;
    const out = {};
    if (!t) return out;
    t.bosses.forEach((b) => b.abilities.forEach((a) => { if (a.flags && a.flags.mark) out[a.sid] = a.name; }));
    return out;
  }

  // The boss table's hp thresholds for the panel's bar ticks: every phase and one-shot hp<N, ascending.
  function hpTicks(b) {
    if (!b) return [];
    const set = {};
    (b.phases || []).forEach((p) => {
      const m = /^\s*hp<(\d+(?:\.\d+)?)/i.exec(p.rule || "");
      if (m) set[Number(m[1])] = true;
    });
    (b.abilities || []).forEach((a) => { if (a.flags && a.flags.hpBelow != null) set[a.flags.hpBelow] = true; });
    return Object.keys(set).map(Number).sort((x, y) => x - y);
  }

  // ---- render ----------------------------------------------------------------------------------------------------------

  function esc(v) {
    return String(v == null ? "" : v).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  function secs(ms) {
    return ms == null ? "" : (ms / 1000).toFixed(1) + " s";
  }

  function nextText(a) {
    if (!a.next) return a.hpBelow != null ? "HP " + a.hpBelow + "%" : "—";
    const lo = Math.round(a.next.lo / 1000), hi = Math.round(a.next.hi / 1000);
    const late = typeof a.next.at === "number" && a.next.at > a.next.hi + 5000;
    return "≈ " + (lo === hi ? lo + " s" : lo + "–" + hi + " s") + (late ? " (late)" : "");
  }

  function castHtml(a) {
    if (!a.casting) return "";
    const pct = Math.round(a.casting.frac * 100);
    return '<span class="fd-boss-cast"><span class="fd-boss-castbar"><i style="width:' + pct + '%"></i></span>'
      + '<span class="fd-boss-casttext">' + esc(a.name) + (a.casting.target ? " → " + esc(a.casting.target) : "")
      + ' <b>' + secs(a.casting.leftMs) + "</b></span></span>";
  }

  // The coverage chip in front of an ability name (web/static/js/coverage.js forBoss answers it): one letter, its status in
  // the title as one line per need, and the aria label. No coverage map (the rows are not loaded yet) leaves the row as it was.
  const COV_GLYPH = { answered: "✓", partly: "◐", missing: "✗", notneeded: "–", unknown: "?" };
  function covChip(cov, sid, e) {
    if (!cov) return "";
    const c = cov[sid] || cov[String(sid)];
    if (!c) return "";
    const status = COV_GLYPH[c.status] ? c.status : "unknown";
    return '<span class="fd-cov fd-cov-' + status + '" title="' + e(c.title || "") + '" aria-label="' + e("coverage: " + status) + '">'
      + COV_GLYPH[status] + "</span>";
  }

  function abilityRow(a, cov, e) {
    const cls = "fd-boss-ability" + (a.seen ? "" : " unseen") + (a.inPhase ? "" : " otherphase") + (a.casting ? " casting" : "");
    const last = a.seen ? secs(a.lastMs) + " ago" : "not seen";
    return '<li class="' + cls + '"><i class="fd-boss-swatch bc-' + (a.color % 8) + '"></i>'
      + '<span class="fd-boss-an">' + covChip(cov, a.sid, e) + esc(a.name) + "</span>"
      + '<span class="fd-boss-last">' + esc(last) + "</span>"
      + '<span class="fd-boss-next">' + esc(nextText(a)) + "</span>"
      + '<span class="fd-boss-castwrap">' + castHtml(a) + "</span></li>";
  }

  // The table JSON plus a model (and optionally the live boss row for the hp bar) as the panel's HTML. opts.esc may replace the
  // escaper; opts.open says whether the panel starts unfolded. Empty string for a boss the table does not know.
  function render(table, panel, opts) {
    const t = table === undefined ? cache : table;
    if (!panel) return "";
    const o = opts || {}, e = typeof o.esc === "function" ? o.esc : esc;
    const b = bossFor(t, panel.boss && panel.boss.entry);
    if (!b) return "";
    const hp = panel.hp == null ? null : Math.max(0, Math.min(100, panel.hp));
    const phase = panel.phase ? '<span class="fd-boss-phase">' + e(panel.phase) + "</span>" : "";
    const ticks = hpTicks(b).filter((n) => n > 0 && n < 100)
      .map((n) => '<b class="fd-boss-tick" style="left:' + (100 - n) + '%" title="' + e("phase or one-shot at " + n + "%") + '"></b>').join("");
    const bar = hp == null ? "" : '<span class="fd-boss-hp"><i class="fd-boss-hpf" style="width:' + hp + '%"></i>' + ticks + "</span>";
    const chips = (panel.auras || []).map((a) => '<span class="fd-boss-aura" title="' + e(a.name) + (a.leftMs != null ? ", " + secs(a.leftMs) + " left" : ", permanent") + '">'
      + e(a.name) + (a.stacks > 1 ? ' <b>×' + a.stacks + "</b>" : "") + (a.leftMs != null ? ' <em>' + Math.ceil(a.leftMs / 1000) + "s</em>" : "") + "</span>").join("");
    const flags = (panel.flagWords || []).map((w) => '<span class="fd-boss-flag">' + e(w) + "</span>").join("");
    const adds = (panel.adds || []).map((a) => {
      const role = a.role ? " · " + e(a.role) : "";
      const state = a.alive === null ? "" : a.alive ? " · alive" + (a.hp != null ? " " + Math.round(a.hp) + "%" : "") : " · dead";
      const count = a.count > 1 ? " ×" + a.count : "";
      const pri = a.priority > 0 ? " · kill " + a.priority : "";
      return '<span class="fd-boss-add' + (a.priority > 0 ? " pri" : "") + '">' + e(a.name || (a.entry ? "creature " + a.entry : "")) + role + state + count + pri + "</span>";
    }).join("");
    const notes = (panel.notes || []).map((n) => '<li class="fd-boss-note">' + e(n) + "</li>").join("");
    const inPhase = (panel.abilities || []).filter((a) => a.inPhase);
    const other = (panel.abilities || []).filter((a) => !a.inPhase);
    const seen = (a) => a.seen;
    const rows = inPhase.map((a) => abilityRow(a, o.coverage, e)).join("");
    const hidden = other.length
      ? '<details class="fd-boss-other"><summary>' + ("Other phases (" + other.length + ")") + '</summary><ul class="fd-boss-abilities">' + other.map((a) => abilityRow(a, o.coverage, e)).join("") + "</ul></details>"
      : "";
    const covLink = o.coverage && b.mapKey
      ? '<a class="fd-boss-cov" href="#/coverage/' + e(encodeURIComponent(b.mapKey)) + "?boss=" + e(b.entry) + '">coverage</a>'
      : "";
    return '<details class="fd-boss-panel" data-entry="' + e(b.entry) + '"' + (o.open ? " open" : "") + ">"
      + '<summary class="fd-boss-head"><span class="fd-boss-name">' + e(b.name) + "</span>" + (" " + bar) + phase
      + covLink
      + '<span class="fd-boss-seen">' + (inPhase.some(seen) ? "seen " + (inPhase.filter(seen).length) + "/" + inPhase.length : "") + "</span></summary>"
      + '<div class="fd-boss-body">'
      + (chips ? '<div class="fd-boss-chips">' + chips + "</div>" : "")
      + (flags ? '<div class="fd-boss-flags">' + flags + "</div>" : "")
      + (rows ? '<ul class="fd-boss-abilities">' + rows + "</ul>" : '<p class="empty-hint">No ability known for this boss.</p>')
      + hidden
      + (adds ? '<div class="fd-boss-adds"><span class="fd-boss-lab">Adds</span>' + adds + "</div>" : "")
      + (notes ? '<ul class="fd-boss-notes">' + notes + "</ul>" : "")
      + "</div></details>";
  }

  // ---- helpers the page uses on the map and in the frames ---------------------------------------------------------------

  // The short words of a uf string ("n" -> "untargetable").
  function flagsText(uf) { return flagText(uf); }

  // The dashed map links of the data's adds: one per reported add row that stands next to its boss. sum (a summoner guid) wins
  // over the data's entry link when the module sends both.
  function addLinks(table, mobs) {
    const t = table === undefined ? cache : table;
    const list = arr(mobs);
    if (!t) return [];
    const byId = {};
    list.forEach((m) => { if (m && m.id != null) byId[String(m.id)] = m; });
    const bossEntry = {};
    t.bosses.forEach((b) => { bossEntry[b.entry] = b; });
    const out = [];
    list.forEach((m) => {
      if (!m || !m.rank && !m.entry) return;
      const sum = str(m.sum);
      let boss = sum && byId[sum] ? byId[sum] : null;
      if (!boss && m.entry) {
        for (const b of t.bosses) {
          if (b.adds.some((a) => a.entry === num(m.entry))) { boss = list.find((x) => x && num(x.entry) === b.entry); break; }
        }
      }
      if (!boss || boss === m || !boss.entry) return;
      if (boss.rank !== "b") return;
      const row = (bossEntry[num(boss.entry)] || { adds: [] }).adds.find((a) => a.entry === num(m.entry));
      out.push({ from: m, to: boss, priority: row ? row.priority : 0, role: row ? row.role : "" });
    });
    return out;
  }

  // The first debuff's colour letter of a bot's db chips (the map's one dot above it).
  function debuffColor(db) {
    const d = arr(db)[0];
    return d ? str(first(d.dt, d.d, "")) : "";
  }

  // Whether a bot carries a mark ability's aura (data-driven: no spell id may live in JS).
  function markedBy(table, bot) {
    const ids = markIds(table);
    const hit = arr(bot && bot.db).find((a) => a && num(first(a.id, a.sid)) in ids);
    return hit ? ids[num(first(hit.id, hit.sid))] : "";
  }

  const api = {
    normalize, load, data, raw, bossFor, abilityBySpell, phaseOf, markIds, hpTicks,
    model, render, flagsText, addLinks, debuffColor, markedBy,
    FLAG_WORD, _esc: esc,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.BossPanel = api;
})(typeof self !== "undefined" ? self : this);
