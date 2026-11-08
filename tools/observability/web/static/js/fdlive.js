"use strict";
// FdLive: the pure state of one live fight view (no DOM, no fetch, no timers). The page keeps one state per fight key and
// feeds it what the live stream (GET /api/v1/live) and its two catch-up routes (GET /api/v1/combat/live and the track route
// with x=1) return; fight() turns the state into the object every Fight debugger panel reads for a finished fight, so the
// same panels draw the running one. Every buffer is bounded and drops its oldest entry first.
(function (root) {
  const TICK_MS = 1000;         // a visible live view repaints at most this often
  const EVENTS_MAX = 1200;      // events held (the daemon's live row holds the same: 1000 log kinds + 200 others)
  const SAMPLES_MAX = 900;      // 2 s samples: 30 min
  const POS_MAX = 600;          // 0.5 s position frames: 5 min
  const WINDOWS = [30, 60, 120, 300, 0]; // seconds of the follow range; 0 = the whole fight
  const DEFAULT_WINDOW = 60;
  const CATCHUP_MIN_MS = 5000;  // one catch-up per view per this long
  const CLOCK_SLACK_MS = 10000; // the fight clock runs on the browser's clock for at most this long past its last header
  const AURA_NAMES_MAX = 200;
  const LOG_KINDS = { cast: true, interrupt: true, taunt: true, death: true };

  function create(commander) {
    return {
      commander: String(commander), key: null, gen: 0, auraNames: {}, startMs: 0, fightMs: 0, fightAtMs: 0, boss: 0, bossName: "",
      cur: 0, top: 0, gap: false, ended: false, outcome: "", unkept: false, events: [], samples: [], pos: [], version: 0,
      lastCatchUp: 0, wantCatchUp: false, memo: null, noLive: false, trackFor: null,
    };
  }

  function num(v) {
    return typeof v === "number" && isFinite(v) ? v : 0;
  }

  // The header fields every source carries under its own spelling: the events frame's `fight` object (start_ms, boss,
  // boss_name, fight_ms) and the catch-up answer's top level (the same names).
  function header(st, h, nowMs) {
    if (!h) return;
    if (num(h.start_ms)) st.startMs = h.start_ms;
    if (h.boss != null) st.boss = num(h.boss);
    if (typeof h.boss_name === "string") st.bossName = h.boss_name;
    if (h.fight_ms != null && !st.ended) {
      st.fightMs = num(h.fight_ms);
      st.fightAtMs = num(nowMs);
    }
  }

  // Another fight: everything of the old one goes, and the generation moves so a request asked for the old one is told apart.
  function reset(st, key, h, nowMs) {
    st.key = key == null ? null : String(key);
    st.gen++;
    st.startMs = 0; st.fightMs = 0; st.fightAtMs = 0; st.boss = 0; st.bossName = "";
    st.cur = 0; st.top = 0; st.gap = false; st.ended = false; st.outcome = ""; st.unkept = false;
    st.events = []; st.samples = []; st.pos = []; st.wantCatchUp = false; st.memo = null; st.trackFor = null;
    header(st, h, nowMs);
    st.version++;
  }

  // One event stored by its own index (the order of the list is the index order; an index already held is not stored twice).
  // An element that is not an object still takes its index, so the cursor stays contiguous.
  function addEvent(st, i, ev) {
    const o = ev && typeof ev === "object" ? ev : { k: "?" };
    const list = st.events;
    let at = list.length;
    while (at > 0 && list[at - 1].i > i) at--;
    if (at > 0 && list[at - 1].i === i) return;
    list.splice(at, 0, { i, t: num(o.t), ev: o });
    if (list.length > EVENTS_MAX) list.splice(0, list.length - EVENTS_MAX);
    if (o.k === "end") {
      st.ended = true;
      st.outcome = typeof o.o === "string" ? o.o : "";
      if (o.kept === false) st.unkept = true;
      if (num(o.dur)) st.fightMs = o.dur;
    }
  }

  // st.cur is the contiguous cursor: every index below it is held (or dropped by a cap). Events held past it (frames that came
  // while a catch-up was out) join it once the indexes between are in.
  function absorb(st) {
    // The list is in index order: start at the first event at or past the cursor (a binary search), not at the oldest held.
    let lo = 0, hi = st.events.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (st.events[mid].i < st.cur) lo = mid + 1; else hi = mid;
    }
    for (let k = lo; k < st.events.length && st.events[k].i === st.cur; k++) st.cur++;
    if (st.top < st.cur) st.top = st.cur;
    st.wantCatchUp = st.cur < st.top;
  }

  // One SSE `events` frame: the daemon's datagram plus `key`, `from` and `cur` (the index of its first event and the next
  // index after its last). "ok" appended or nothing new, "dup" nothing to do, "gap" events between st.cur and the frame
  // were missed (the frame's own events are kept by index; catch up from st.cur), "reset" another fight that this frame does
  // not start (catch up from 0). A hole only asks for a catch-up: st.gap says what the daemon reports as dropped, nothing else.
  function applyEventsFrame(st, f, nowMs) {
    if (!f || typeof f.key !== "string" || !f.key) return "dup"; // a daemon without the cursor: the catch-up route is not there either
    const from = num(f.from), cur = num(f.cur);
    let res = "ok";
    if (f.key !== st.key) {
      reset(st, f.key, f.fight, nowMs);
      if (from > 0) res = "reset";
    }
    if (cur <= st.cur) {
      if (f.fight) header(st, f.fight, nowMs);
      return "dup";
    }
    const evs = Array.isArray(f.events) ? f.events : [];
    for (let p = 0; p < evs.length; p++) {
      if (from + p >= st.cur) addEvent(st, from + p, evs[p]);
    }
    if (cur > st.top) st.top = cur;
    const hole = from > st.cur;
    if (!hole) st.cur = cur;
    absorb(st);
    header(st, f.fight, nowMs);
    st.version++;
    return hole ? (res === "reset" ? "reset" : "gap") : res;
  }

  // The answer of GET /api/v1/combat/live: everything the daemon's live row still holds from the asked cursor. A late answer
  // never moves the cursor back: it is merged by index with what the stream already delivered.
  function applyCatchUp(st, d, nowMs) {
    if (!d || typeof d.key !== "string") return;
    if (d.key !== st.key) reset(st, d.key, d, nowMs);
    (Array.isArray(d.events) ? d.events : []).forEach((e) => {
      if (e && typeof e.i === "number") addEvent(st, e.i, e.e);
    });
    st.cur = Math.max(st.cur, num(d.cur));
    if (st.top < st.cur) st.top = st.cur;
    absorb(st);
    st.gap = st.gap || !!d.gap;
    st.lastCatchUp = num(nowMs);
    header(st, d, nowMs);
    st.version++;
  }

  // What a request is asked under: the answer is dropped (accepts) when the view moved to another fight meanwhile.
  function token(st) {
    return { key: st.key, gen: st.gen };
  }

  // Whether an answer to a request that started under tok still belongs to the view. A view that had no fight when it asked
  // takes the answer when the fight it holds now is the answer's own.
  function accepts(st, tok, d) {
    if (!tok || tok.gen === st.gen) return true;
    return tok.key === null && !!d && d.key === st.key;
  }

  // Marks a catch-up as asked (the rate limit starts now, whatever the answer turns out to be).
  function markCatchUp(st, nowMs) {
    st.lastCatchUp = num(nowMs);
  }

  function needCatchUp(st, nowMs) {
    const initial = st.cur === 0 && !st.events.length;
    return (initial || st.wantCatchUp) && num(nowMs) - st.lastCatchUp >= CATCHUP_MIN_MS;
  }

  function dbIds(list) {
    const out = [];
    (Array.isArray(list) ? list : []).forEach((a) => {
      if (a && num(a.id)) out.push(a.id);
    });
    return out;
  }

  // One COMMANDERS binding (the SSE `commander` frame, every 2 s) as a sample: each bot's health, dps/hps of the report's
  // window and boss debuff ids, and the mobs near the lead.
  function applySample(st, bind, tsMs) {
    if (!st || !bind) return false;
    const t = num(tsMs);
    if (!t) return false;
    const last = st.samples.length ? st.samples[st.samples.length - 1].t : 0;
    if (t <= last) return false;
    const meters = {};
    const combat = bind.combat;
    if (combat && Array.isArray(combat.bots)) combat.bots.forEach((c) => { if (c) meters[c.g] = c; });
    const bots = {};
    (Array.isArray(bind.bots) ? bind.bots : []).forEach((b) => {
      if (!b || b.guid == null) return;
      const m = meters[b.guid];
      const mx = num(b.max_hp);
      const db = dbIds(b.db);
      (Array.isArray(b.db) ? b.db : []).forEach((a) => {
        if (a && num(a.id) && typeof a.n === "string" && a.n && !st.auraNames[a.id] && Object.keys(st.auraNames).length < AURA_NAMES_MAX) {
          st.auraNames[a.id] = a.n;
        }
      });
      bots[b.guid] = { hp: mx ? Math.round(num(b.hp) * 100 / mx) : 0, a: b.alive === false ? 0 : 1, dps: m ? num(m.dps) : 0, hps: m ? num(m.hps) : 0, db };
    });
    // A mob's victim `v` is a string guid here (the commander frame's `victim` is the bot's id as a decimal string) and a number in
    // the track's rows (`v` is the bound bot's guid); both are kept as strings, which is also what a bot's id is.
    const mobs = (Array.isArray(bind.mobs) ? bind.mobs : []).filter((m) => m && m.id != null).map((m) => ({ id: String(m.id), e: num(m.entry), hp: num(m.hp), v: m.victim != null ? String(m.victim) : "" }));
    st.samples.push({ t, bots, mobs, w: combat ? num(combat.win_s) : 0 });
    if (st.samples.length > SAMPLES_MAX) st.samples.splice(0, st.samples.length - SAMPLES_MAX);
    st.version++;
    return true;
  }

  // The x=1 track answer (frames[].bots rows [guid,x,y,hp,alive,map,dps,hps,[db ids]], frame.w): samples for the time before
  // the page was watching. Merged with what the stream already added (one sample per report second), oldest first.
  function applyTrackX(st, json) {
    if (!st || !json || !Array.isArray(json.frames)) return 0;
    const have = {};
    st.samples.forEach((s) => { have[Math.floor(s.t / 1000)] = true; });
    let added = 0;
    json.frames.forEach((fr) => {
      if (!fr || !num(fr.t) || have[fr.t]) return;
      const bots = {};
      (Array.isArray(fr.bots) ? fr.bots : []).forEach((r) => {
        if (!Array.isArray(r) || r.length < 3) return;
        bots[r[0]] = { hp: num(r[3]), a: r[4] === 0 ? 0 : 1, dps: num(r[6]), hps: num(r[7]), db: Array.isArray(r[8]) ? r[8].filter((x) => num(x)) : [] };
      });
      const mobs = (Array.isArray(fr.mobs) ? fr.mobs : []).map((m) => ({ id: String(m.id), e: num(m.e), hp: num(m.hp), v: m.v ? String(m.v) : "" }));
      st.samples.push({ t: fr.t * 1000, bots, mobs, w: num(fr.w) });
      have[fr.t] = true;
      added++;
    });
    if (added) {
      st.samples.sort((a, b) => a.t - b.t);
      if (st.samples.length > SAMPLES_MAX) st.samples.splice(0, st.samples.length - SAMPLES_MAX);
      st.version++;
    }
    return added;
  }

  // One SSE `pos` frame (0.5 s): where the bots and mobs stood. Bots as [guid, x, y, hp %, alive], mobs as [id, x, y, hp %].
  function applyPos(st, frame) {
    if (!st || !frame) return false;
    const t = num(frame.ts_ms);
    if (!t) return false;
    if (st.pos.length && t <= st.pos[st.pos.length - 1].t) return false;
    const bots = (Array.isArray(frame.bots) ? frame.bots : []).filter((b) => b && b.g != null)
      .map((b) => [b.g, num(b.x), num(b.y), num(b.mhp) ? Math.round(num(b.hp) * 100 / b.mhp) : 0, b.a === 1 ? 1 : 0]);
    const mobs = (Array.isArray(frame.mobs) ? frame.mobs : []).filter((m) => m && m.id != null)
      .map((m) => [String(m.id), num(m.x), num(m.y), num(m.hp)]);
    st.pos.push({ t, bots, mobs });
    if (st.pos.length > POS_MAX) st.pos.splice(0, st.pos.length - POS_MAX);
    st.version++;
    return true;
  }

  // How long the fight has run: the last header's fight time plus the browser's clock since it arrived, for at most
  // CLOCK_SLACK_MS (the daemon's keepalive is 5 s); frozen once the end event came.
  function nowFightMs(st, nowMs) {
    if (!st || !st.key || !st.fightAtMs) return st && st.ended ? st.fightMs : 0;
    if (st.ended) return st.fightMs;
    return st.fightMs + Math.max(0, Math.min(num(nowMs) - st.fightAtMs, CLOCK_SLACK_MS));
  }

  // The follow range in fight milliseconds; winS 0 is the whole fight (null).
  function range(st, nowMs, winS) {
    if (!winS) return null;
    const now = nowFightMs(st, nowMs);
    return { from: Math.max(0, now - winS * 1000), to: now };
  }

  // Aura rows [t_ms from the fight's start, guid, spell id, 1 apply | -1 remove] from the sampled boss-debuff lists: an id
  // in a bot's list that the bot's previous sample did not have is applied at that sample's time, one that went missing is
  // removed. A bot absent from a sample keeps its state. An aura applied before the start is on from the pull: its
  // application is moved to 0 at the first sample inside the fight that still holds it, or just before its removal.
  function auraRowsFrom(samples, startMs) {
    const state = {};   // guid -> { id: true }
    const skipped = {}; // guid:id -> an application before the start was dropped
    const rows = [];
    (Array.isArray(samples) ? samples : []).forEach((s) => {
      const rel = s.t - startMs;
      Object.keys(s.bots || {}).forEach((g) => {
        const now = {};
        ((s.bots[g] && s.bots[g].db) || []).forEach((id) => { now[id] = true; });
        const prev = state[g] || {};
        Object.keys(now).forEach((id) => {
          if (prev[id]) {
            // Held since before the start and still held at the first sample inside the fight: on from the pull.
            if (rel >= 0 && skipped[g + ":" + id]) { delete skipped[g + ":" + id]; rows.push([0, Number(g), Number(id), 1]); }
            return;
          }
          if (rel < 0) skipped[g + ":" + id] = true; else rows.push([rel, Number(g), Number(id), 1]);
        });
        Object.keys(prev).forEach((id) => {
          if (now[id]) return;
          if (skipped[g + ":" + id]) {
            // Carried in from before the start: it ran from the pull until this removal (a removal before the start is dropped whole).
            delete skipped[g + ":" + id];
            if (rel > 0) { rows.push([0, Number(g), Number(id), 1]); rows.push([rel, Number(g), Number(id), -1]); }
            return;
          }
          if (rel >= 0) rows.push([rel, Number(g), Number(id), -1]);
        });
        state[g] = now;
      });
    });
    return rows;
  }

  // Whether any sample of any bot listed the most boss debuffs the report carries (4 per bot): a debuff may have been left out.
  const DB_SLOTS = 4;
  function slotsFull(samples) {
    return (Array.isArray(samples) ? samples : []).some((s) => Object.keys(s.bots || {}).some((g) => ((s.bots[g] && s.bots[g].db) || []).length >= DB_SLOTS));
  }

  function auraRows(st) {
    return auraRowsFrom(st.samples, st.startMs);
  }

  // The newest sampled health of the boss, in percent (null: no sample has him).
  function bossHp(st) {
    if (!st || !st.boss) return null;
    for (let i = st.samples.length - 1; i >= 0; i--) {
      const m = st.samples[i].mobs.find((x) => x.e === st.boss);
      if (m) return m.hp;
    }
    return null;
  }

  // The synthetic fight object the panels read: the same shape a filed fight has, drawn from the live state. Memoised on the
  // state's version and the second, so a repaint within one second costs nothing.
  function fight(st, bind, nowMs) {
    const sec = Math.floor(num(nowMs) / TICK_MS);
    if (st.memo && st.memo.version === st.version && st.memo.sec === sec && st.memo.bind === bind) return st.memo.fight;
    const log = [];
    const deaths = [];
    st.events.forEach((e) => {
      const ev = e.ev;
      if (!LOG_KINDS[ev.k]) return;
      log.push(ev);
      if (ev.k === "death") deaths.push({ name: ev.n, t_ms: num(ev.t), x: num(ev.x), y: num(ev.y), killer: ev.by, hits: [] });
    });
    const parts = String(st.key || "").split("\u0000");
    const bots = (bind && Array.isArray(bind.bots) ? bind.bots : []).filter((b) => b && b.guid != null).map((b) => ({ name: b.name, guid: b.guid, role: b.role }));
    const out = {
      id: parts[2] || "", key: st.key, live: true, start: Math.floor(st.startMs / 1000), dur_ms: nowFightMs(st, nowMs),
      boss: st.boss, boss_name: st.bossName, outcome: st.ended ? st.outcome : "live", bots, log, deaths,
      auras: auraRows(st), aura_src: "sampled", aura_slots_full: slotsFull(st.samples), log_dropped: st.gap ? 1 : 0,
    };
    st.memo = { version: st.version, sec, bind, fight: out };
    return out;
  }

  // Whether a filed fight (the SSE `fight` summary) is the one this live view shows. The live row and the filed fight share
  // combat.Key(commander, session, id); defensively the id and the session parts alone also match.
  function handoff(st, summary) {
    if (!st || !st.key || !summary) return false;
    if (summary.key === st.key) return true;
    const a = st.key.split("\u0000");
    const b = String(summary.key || "").split("\u0000");
    if (a.length === 3 && b.length === 3) return a[1] === b[1] && a[2] === b[2];
    return false;
  }

  const api = {
    TICK_MS, EVENTS_MAX, SAMPLES_MAX, POS_MAX, WINDOWS, DEFAULT_WINDOW, CATCHUP_MIN_MS, CLOCK_SLACK_MS,
    create, reset, applyEventsFrame, applyCatchUp, token, accepts, markCatchUp, needCatchUp, applySample, applyTrackX, applyPos,
    nowFightMs, range, auraRowsFrom, auraRows, slotsFull, bossHp, fight, handoff,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdLive = api;
})(typeof self !== "undefined" ? self : this);
