"use strict";
// Commanders page, Fight debugger mode: the raid tabs' plain logic, free of the page so a test can run it under node
// (internal/commandersjs). The strip lists every commander as a tab (in combat first, then by name) with the state of its
// run, its dead count and a red dot for a new problem fight; the All raids tab fetches each commander's fight summaries;
// opening a tab puts its name in the URL hash. Nothing here builds markup or fetches on its own: app.js draws and owns the
// timers, and FdTabs.newFights is the one fights cache both the summary grid and the open tab read.
(function (root) {
  const GONE_MS = 5 * 60 * 1000;   // a commander that disappears keeps its greyed tab this long
  const FIGHTS_MS = 10000;         // fight summaries are refetched at most this often
  const TRACK_RETRY_MS = 30000;    // a fight's kept positions are asked for again this long after a failed ask
  const SEEN_MAX = 400;            // seen fight keys kept in the browser
  const SEEN_KEY = "tbdash.fdtabs.seen.v1";
  const PAGE_KEY = "tbdash.cmdpage.v1";

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : 0);
  const text = (v) => (v == null ? "" : String(v));

  // The tab order: a commander in combat first, then by name. Ties keep their report order.
  function sortBinds(list) {
    return (Array.isArray(list) ? list : [])
      .filter((b) => b && b.commander)
      .slice()
      .sort((a, b) => {
        const ca = a.combat ? 0 : 1, cb = b.combat ? 0 : 1;
        if (ca !== cb) return ca - cb;
        return a.commander < b.commander ? -1 : a.commander > b.commander ? 1 : 0;
      });
  }

  function deadCount(bind) {
    return ((bind && bind.bots) || []).filter((b) => b && b.alive === false).length;
  }

  // walking / fighting / wiped / done / idle, from what the binding already says: the party's deaths, the run's state and
  // the words CmdView.stateWord gives the Live header ("moving" is walking here).
  function stateOf(bind, word) {
    const bots = (bind && bind.bots) || [];
    if (bots.length && bots.every((b) => b && b.alive === false)) return "wiped";
    if (bind && bind.run && bind.run.state === "done") return "done";
    const w = text(word);
    if (w === "fighting" || w === "attacking" || w === "pulling") return "fighting";
    if (w === "moving" || w === "walking") return "walking";
    return "idle";
  }

  // ---- the newest fight and its red dot ----------------------------------------------------------------------------
  // GET /api/v1/combat/fights is newest first. A summary has no bot rows, so its verdict is the one the daemon can tell
  // without them: a wipe, any death, or a rule that fired. The open tab's own rail judges the fight with FightDebug once
  // its detail is loaded; this summary word only marks the tab and the All raids card.
  function rowEnd(row) {
    return num(row && row.start) + num(row && row.dur) / 1000;
  }

  function fightKey(row) {
    return row && (row.key || row.id) ? text(row.key || row.id) : "";
  }

  function problem(row) {
    if (!row) return false;
    return row.outcome === "wipe" || num(row.deaths) > 0 || num(row.warnings) > 0;
  }

  function verdictWord(row) {
    return row ? (problem(row) ? "problem" : "clean") : "";
  }

  // The seen keys are fight keys (commander + session + fight id), so a page reload remembers what was already looked at.
  function markSeen(seen, rows) {
    const s = seen && typeof seen === "object" ? seen : {};
    (Array.isArray(rows) ? rows : []).forEach((r) => { const k = fightKey(r); if (k) s[k] = 1; });
    const keys = Object.keys(s);
    if (keys.length > SEEN_MAX) keys.slice(0, keys.length - SEEN_MAX).forEach((k) => { delete s[k]; });
    return s;
  }

  function loadSeen(store) {
    try {
      const v = JSON.parse(store && store.getItem(SEEN_KEY));
      return v && typeof v === "object" && !Array.isArray(v) ? v : {};
    } catch (e) { return {}; }
  }

  function saveSeen(store, seen) {
    try { if (store) store.setItem(SEEN_KEY, JSON.stringify(seen || {})); } catch (e) { /* a storage that throws only forgets */ }
  }

  // rows: [{key, name, place, plan, state, dead, gone}], opts: {fights: {commander: list}, seen: {fightKey: 1}}.
  function model(rows, opts) {
    const o = opts || {}, fights = o.fights || {}, seen = o.seen || {};
    return (Array.isArray(rows) ? rows : []).map((r) => {
      const entry = fights[r.key];
      // app.js keeps {list, at} per commander; a bare list is accepted too (the tests and any future caller).
      const list = Array.isArray(entry) ? entry : entry && Array.isArray(entry.list) ? entry.list : null;
      const last = list && list.length ? list[0] : null;
      const k = fightKey(last);
      return Object.assign({}, r, { last, verdict: verdictWord(last), dot: !!(last && problem(last) && !(k && seen[k])) });
    });
  }

  // Only the open commander tab fetches fight details, track or its plan; All raids and the other tabs do not.
  function detailAllowed(mode, open, key) {
    return mode === "debug" && !!open && open === key;
  }

  // ---- the fights summaries both the All raids grid and the open tab read ------------------------------------------
  // cache/pending are the page's own objects (commander -> {list, at} / callbacks of the one request in flight), so the
  // debugger, the Deaths panel and the grid share one answer. list() returns true when it started a request.
  function newFights(opts) {
    const o = opts || {}, cache = o.cache || {}, pending = o.pending || {};
    const now = o.now || (() => Date.now()), ms = o.ms || FIGHTS_MS;

    function list(key, cb) {
      const c = cache[key];
      if (c && now() - c.at < ms) { if (cb) cb(c.list); return false; }
      if (pending[key]) { if (cb) pending[key].push(cb); return false; }
      const cbs = pending[key] = [];
      if (cb) cbs.push(cb);
      const done = (rows) => {
        // A failed fetch is remembered for the same window as an answer (a failing daemon is not asked again on every draw);
        // it keeps the rows already shown.
        const list = Array.isArray(rows) ? rows : cache[key] ? cache[key].list : null;
        cache[key] = { list: list, at: now() };
        const out = pending[key] || [];
        delete pending[key];
        out.forEach((f) => { try { f(list); } catch (e) { /* a drawing error must not strand the queue */ } });
      };
      let p;
      try { p = Promise.resolve(o.fetchRows ? o.fetchRows(key) : null); } catch (e) { p = Promise.reject(e); }
      p.then(done).catch(() => done(null));
      return true;
    }

    function detail(key, fightKey, allowed, fetchDetail) {
      if (!allowed || !fightKey || !fetchDetail) return null;
      try { return Promise.resolve(fetchDetail(fightKey)); } catch (e) { return Promise.reject(e); }
    }

    return { cache, pending, list, detail };
  }

  // ---- a fight's kept positions (GET /api/v1/commanders/track) -----------------------------------------------------
  // entry is the page's own value for one fight: undefined (never asked), 'loading' (an ask in flight), an object (the answer,
  // kept for the page's life) or null (the ask failed). failedAt is when that failure happened.
  function trackDue(entry, failedAt, nowMs) {
    if (entry === undefined) return true;
    if (entry !== null) return false;
    return typeof failedAt === "number" && nowMs - failedAt >= TRACK_RETRY_MS;
  }

  const api = {
    GONE_MS, FIGHTS_MS, TRACK_RETRY_MS, SEEN_MAX, SEEN_KEY, PAGE_KEY,
    sortBinds, deadCount, stateOf, rowEnd, fightKey, problem, verdictWord, markSeen, loadSeen, saveSeen, model,
    detailAllowed, newFights, trackDue,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdTabs = api;
})(typeof self !== "undefined" ? self : this);
