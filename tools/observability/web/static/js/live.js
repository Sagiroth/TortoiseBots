"use strict";
// LiveView: the live stream (GET /api/v1/live, Server-Sent Events). The pure half — the URL of the asked commanders, the
// copy-on-write merge of a position frame, which refresh rates may move the dots, the error fallback, the once-per-frame
// scheduler and the report feed into Glide — runs under node (internal/commandersjs); connect() is the thin EventSource
// wrapper the page uses.
(function (root) {
  const MAX_COMMANDERS = 20;     // the daemon's MaxCommandersPerClient
  const FALLBACK_ERRORS = 3;     // errors within the window that send the page back to polling
  const FALLBACK_WINDOW_MS = 60000;
  const glide = () => (typeof Glide !== "undefined" ? Glide : require("./glide.js"));

  // "/api/v1/live?commander=a&commander=b": deduplicated, sorted, at most 20; null when there is nothing to ask for.
  function url(commanders) {
    const seen = {};
    const out = [];
    (Array.isArray(commanders) ? commanders : []).forEach((c) => {
      if (typeof c !== "string" || !c || seen[c]) return;
      seen[c] = true;
      out.push(c);
    });
    out.sort();
    if (!out.length) return null;
    return "/api/v1/live?" + out.slice(0, MAX_COMMANDERS).map((c) => "commander=" + encodeURIComponent(c)).join("&");
  }

  // Merge one pos frame into the standing list: a new list, a new binding and new bots/mobs arrays for the commander it
  // names (the state list is shared with what the page drew, so nothing is changed in place). Unknown guids and ids are
  // left as they are: a new unit arrives with the next full report.
  function applyPos(list, frame) {
    if (!Array.isArray(list) || !frame || typeof frame.commander !== "string") return list;
    const bi = list.findIndex((b) => b && b.commander === frame.commander);
    if (bi < 0) return list;
    const out = list.slice();
    const b = Object.assign({}, list[bi]);
    const bots = Array.isArray(b.bots) ? b.bots : [];
    const posBots = Array.isArray(frame.bots) ? frame.bots : [];
    if (posBots.length) {
      b.bots = bots.map((x) => {
        const p = x ? posBots.find((q) => q && q.g != null && x.guid === q.g) : null;
        if (!p) return x;
        return Object.assign({}, x, { x: p.x, y: p.y, z: p.z, hp: p.hp, max_hp: p.mhp, alive: p.a === 1, map: p.m });
      });
    }
    const mobs = Array.isArray(b.mobs) ? b.mobs : [];
    const posMobs = Array.isArray(frame.mobs) ? frame.mobs : [];
    if (posMobs.length) {
      b.mobs = mobs.map((m) => {
        const p = m ? posMobs.find((q) => q && String(q.id) === String(m.id)) : null;
        if (!p) return m;
        return Object.assign({}, m, { x: p.x, y: p.y, hp: p.hp, victim: p.v });
      });
    }
    out[bi] = b;
    return out;
  }

  // The dots only move at the two live rates: a slower pick or paused means the page shows what the reports say.
  function posAllowed(intervalSec) {
    return intervalSec === 1 || intervalSec === 2;
  }

  // Three errors within a minute: the stream is not working, fall back to polling.
  function fallback(errTimesMs, nowMs) {
    const cut = nowMs - FALLBACK_WINDOW_MS;
    return (Array.isArray(errTimesMs) ? errTimesMs : []).filter((t) => typeof t === "number" && t >= cut).length >= FALLBACK_ERRORS;
  }

  // One drawCmdMap per marked key per animation frame; a glide-only redraw of one key at most every Glide.FRAME_MS (the
  // position stream is faster than the screen). draw(key) is app.js's drawCmdMap.
  function scheduler(raf, now, draw) {
    const refresh = typeof raf === "function" ? raf : () => 0;
    const clock = typeof now === "function" ? now : () => Date.now();
    const paint = typeof draw === "function" ? draw : () => {};
    const frameMs = glide().FRAME_MS;
    const pending = {};
    const last = {};
    let queued = false;
    function frame() {
      queued = false;
      const t = clock();
      Object.keys(pending).forEach((k) => {
        const glideOnly = pending[k];
        delete pending[k];
        if (glideOnly && last[k] != null && t - last[k] < frameMs) return;
        last[k] = t;
        paint(k);
      });
    }
    function schedule() {
      if (queued) return;
      queued = true;
      refresh(frame);
    }
    return {
      mark(key, glideOnly) {
        if (key == null) return;
        const k = String(key);
        const g = !!glideOnly;
        if (!(k in pending)) pending[k] = g;
        else pending[k] = pending[k] && g; // a full draw wins over a glide-only one
        schedule();
      },
      frame: frame,
    };
  }

  // Feed one full report into the per-card glide states (commander -> state, created on first use): the gap first, then
  // every bot and mob of that binding.
  function noteReport(states, list, nowMs) {
    if (!states) return;
    const G = glide();
    (Array.isArray(list) ? list : []).forEach((bind) => {
      if (!bind || typeof bind.commander !== "string") return;
      const st = states[bind.commander] || (states[bind.commander] = G.create());
      G.sampled(st, nowMs);
      (bind.bots || []).forEach((b) => {
        if (b && b.guid != null) G.note(st, G.botKey(b.guid), b.x, b.y, b.map, b.alive !== false, nowMs);
      });
      (bind.mobs || []).forEach((m) => {
        if (m && m.id != null) G.note(st, G.mobKey(m.id), m.x, m.y, bind.mobs_map, true, nowMs);
      });
    });
  }

  // ---- the EventSource wrapper (thin: no state the tests need) ----
  let source = null;
  let sourceUrl = null;
  let reopenTimer = null;
  let handlersNow = null;

  const NAMES = ["snapshot", "commander", "pos", "events", "fight_live", "fight", "resync"];

  function open(u) {
    if (typeof EventSource === "undefined") return;
    const src = new EventSource(u);
    source = src;
    sourceUrl = u;
    NAMES.forEach((name) => {
      src.addEventListener(name, (e) => {
        let data = null;
        try {
          data = JSON.parse(e.data);
        } catch (err) {
          return;
        }
        const h = handlersNow && handlersNow[name];
        if (!h) return;
        try {
          h(data);
        } catch (err) {
          /* one broken handler must not stop the stream */
        }
      });
    });
    src.onerror = () => {
      const h = handlersNow && handlersNow.error;
      if (h) {
        try {
          h();
        } catch (err) {
          /* ignore */
        }
      }
    };
  }

  // One source for the asked commanders; a changed set (or tab) closes and reopens it after 500 ms of quiet.
  function connect(commanders, handlers) {
    handlersNow = handlers || {};
    const next = url(commanders);
    if (next === sourceUrl && source) return source;
    if (reopenTimer) return source;
    if (source) {
      const target = next;
      reopenTimer = setTimeout(() => {
        reopenTimer = null;
        close();
        if (target) open(target);
      }, 500);
      return source;
    }
    if (next) open(next);
    return source;
  }

  function close() {
    if (reopenTimer) {
      clearTimeout(reopenTimer);
      reopenTimer = null;
    }
    if (source) source.close();
    source = null;
    sourceUrl = null;
  }

  const api = {
    MAX_COMMANDERS, FALLBACK_ERRORS, FALLBACK_WINDOW_MS,
    url, applyPos, posAllowed, fallback, scheduler, noteReport, connect, close,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.LiveView = api;
})(typeof self !== "undefined" ? self : this);
