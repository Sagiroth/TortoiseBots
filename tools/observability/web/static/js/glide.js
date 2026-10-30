"use strict";
// Glide: the browser-side interpolation between the reported positions. Each unit keeps the last few samples in server time,
// and the map draws it a little in the past (the clock offset plus a delay built from the report gap), so at any moment there
// is almost always a later sample to walk towards: the dots move steadily instead of stopping at every report and jumping on
// the next. Free of the page so a test can run it under node (internal/commandersjs). The map draw (app.js) reads it through
// Glide.apply; the report path feeds it through LiveView.noteReport.
(function (root) {
  const CAP_MS = 2500;    // a longer silence (a lost report, a paused page) is a hole to snap over, never a slow walk across
  const FRAME_MS = 16;    // the scheduler redraws the map the viewer is looking at at most this often (about 60 fps)
  const GRID_FRAME_MS = 64; // every other visible map glides at most this often (about 15 fps)
  const SAMPLES = 4;      // samples kept per unit: the two around the render time and the one before them
  const OFFSETS = 10;     // arrival-minus-server offsets kept: the smallest is the clock offset (network spikes do not move it)
  const GAPS = 8;         // sample gaps the render delay is built from
  const DELAY_FACTOR = 1.25; // how many report gaps the map is behind
  const DELAY_MIN_MS = 300, DELAY_MAX_MS = 2600;
  const BACK_MS = 30000;  // a frame this much older than the newest server time is a clock reset, not a stall: a stalled
  // report or a reordered position frame (seconds late under load) is dropped, never a snap of the whole card backwards
  const PRUNE_MS = 15000; // a unit no report has carried for this long is gone from the map
  const POS_WINDOW_MS = 3000; // while position frames run, only they say how far behind the map is drawn, and only they move
  // the units they carry: the 2 s report's position of such a unit is ignored (it reaches the page through the refresh gate,
  // without a server time on the WebSocket, so it is stamped late and is older than the newest position frame)
  const SLEW = 0.25;      // the render clock runs at 0.75x..1.25x while it catches up with a changed delay or clock offset
  const SLEW_JUMP_MS = 3000; // further behind than this (a hidden tab, a new clock), it jumps forward; it never goes back
  const TAU = Math.PI * 2;
  // The same jump rule as the trails (commanders.js, the daemon's JumpLimitYd): 12 yd of slack plus 20 yd for each second,
  // seconds counted from 1 to 30.
  const SLACK_YD = 12;
  const YD_PER_SEC = 20;
  const MAX_GAP_SEC = 30;

  function jumpLimitYd(gapMs) {
    const sec = Math.min(MAX_GAP_SEC, Math.max(1, (Number(gapMs) || 0) / 1000));
    return SLACK_YD + YD_PER_SEC * sec;
  }

  // The render delay of a state with no gap yet: as for the idle 2 s feed.
  const initialDelay = () => Math.min(DELAY_MAX_MS, Math.max(DELAY_MIN_MS, DELAY_FACTOR * 2000));

  // One glide state for one commander's card: the units' samples and the clock that turns the page's arrival times into the
  // server's.
  function create() {
    return {
      units: {},
      offsets: [], offset: 0, offsetTs: null,
      gaps: [], delay: initialDelay(),
      lastClock: null, newestClock: null, session: null, posAt: null,
      rt: null, rtAt: null,
    };
  }

  // The game server restarted or its clock was moved, or a new datagram session took over: the samples, the offset window
  // and the gap history all belong to the old clock. They go, and the frame that brought the news is a snap.
  function resetClock(st) {
    st.units = {};
    st.offsets = [];
    st.offset = 0;
    st.offsetTs = null;
    st.gaps = [];
    st.delay = initialDelay();
    st.lastClock = null;
    st.newestClock = null;
    st.posAt = null;
    st.rt = null;
    st.rtAt = null;
  }

  // On every report: a unit the report did not carry for PRUNE_MS is gone (the report lists every unit the card draws, so a
  // rotating pack leaves no trace on the card).
  function prune(st, nowMs) {
    const units = st && st.units;
    if (!units) return;
    Object.keys(units).forEach((k) => {
      const u = units[k];
      if (u && typeof u.lastSeen === "number" && nowMs - u.lastSeen > PRUNE_MS) delete units[k];
    });
  }

  const botKey = (guid) => "b:" + String(guid);
  const mobKey = (id) => "m:" + String(id);

  function median(list) {
    const a = list.slice().sort((x, y) => x - y), n = a.length;
    if (!n) return 0;
    return n % 2 ? a[(n - 1) / 2] : (a[n / 2 - 1] + a[n / 2]) / 2;
  }

  // The server time of a sample: the datagram's own ts_ms when it has one (a position frame always does, the 2 s report while
  // the stream carries it), else the arrival time moved onto the card's server clock. A frame far behind the newest server
  // time, or from another datagram session, means the clock was reset: the state forgets the old one first.
  function clockOf(st, nowMs, tsMs, session) {
    const sid = session == null ? null : String(session);
    const hasTs = typeof tsMs === "number" && isFinite(tsMs);
    const t = hasTs ? tsMs : nowMs - (st.offset || 0);
    if ((sid != null && st.session != null && sid !== st.session) || (st.newestClock != null && t < st.newestClock - BACK_MS)) {
      resetClock(st);
    }
    if (sid != null) st.session = sid;
    if (hasTs && st.offsetTs !== tsMs) {
      st.offsetTs = tsMs;
      st.offsets.push(nowMs - tsMs);
      if (st.offsets.length > OFFSETS) st.offsets.shift();
      st.offset = Math.min.apply(null, st.offsets);
    }
    if (st.newestClock == null || t > st.newestClock) st.newestClock = t;
    return t;
  }

  // How far behind the newest report the map draws: 1.25× the median gap of the last few, never under 300 ms (in combat, a
  // 0.5 s feed, about 0.6 s behind) and never over 2.6 s (the idle 2 s feed sits at about 2.5 s).
  function delayOf(st) {
    const g = st.gaps.length ? st.gaps : [2000];
    return Math.min(DELAY_MAX_MS, Math.max(DELAY_MIN_MS, DELAY_FACTOR * median(g.slice(-GAPS))));
  }

  const renderTime = (st, nowMs) => nowMs - (st.offset || 0) - (st.delay || delayOf(st));

  // The card's render clock as the map draws it: renderTime, but never backwards. A changed delay (a fight starts or ends and
  // the feed gap with it) or clock offset moves the target; the clock then runs a little faster or slower until it is there,
  // instead of jumping, and a target behind the clock is waited for, never walked back to. Far behind (a hidden tab, the first
  // frame of a new clock) it jumps forward at once.
  function clock(st, nowMs) {
    if (!st) return 0;
    const target = renderTime(st, nowMs);
    if (typeof st.rt !== "number" || typeof st.rtAt !== "number") {
      st.rt = target;
      st.rtAt = nowMs;
      return target;
    }
    const dt = nowMs - st.rtAt;
    if (dt <= 0) return st.rt;
    const free = st.rt + dt, err = target - free;
    const next = err > SLEW_JUMP_MS ? target : free + Math.max(-SLEW * dt, Math.min(SLEW * dt, err));
    st.rt = Math.max(st.rt, next);
    st.rtAt = nowMs;
    return st.rt;
  }

  // Once per report: the gap the render delay follows, and the report's own server time when it has one. Only a newer server
  // time feeds it (a reordered frame never moves the clock back), and while position frames run only they do: the 2 s report
  // still moves the dots, but its cadence must not stretch the delay the position stream set. The datagram session rides
  // along only when the caller knows it (a clock reset, never a boolean); the pos flag only marks position frames.
  function sampled(st, nowMs, tsMs, session, pos) {
    if (!st) return;
    const sid = typeof session === "string" || typeof session === "number" ? session : undefined;
    const t = clockOf(st, nowMs, tsMs, sid);
    if (pos) st.posAt = nowMs;
    prune(st, nowMs);
    if (typeof st.lastClock !== "number") {
      st.lastClock = t;
      return;
    }
    if (t <= st.lastClock) return;
    const live = !!pos || st.posAt == null || nowMs - st.posAt > POS_WINDOW_MS;
    if (live) {
      st.gaps.push(t - st.lastClock);
      if (st.gaps.length > GAPS) st.gaps.shift();
      st.delay = delayOf(st);
      st.lastClock = t;
    }
  }

  // The angle the short way round, in [0, 2π): a unit turning from 350° to 10° passes 0, not 180.
  function angleLerp(a, b, f) {
    let d = (b - a) % TAU;
    if (d > Math.PI) d -= TAU;
    if (d < -Math.PI) d += TAU;
    return ((a + d * f) % TAU + TAU) % TAU;
  }

  const faceOf = (s) => (typeof s.f === "number" && isFinite(s.f) ? s.f : undefined);

  function pointAt(s, t) {
    if (!s.length) return null;
    if (s.length === 1 || t <= s[0].t) return { x: s[0].x, y: s[0].y, face: faceOf(s[0]) };
    const last = s[s.length - 1];
    if (t >= last.t) return { x: last.x, y: last.y, face: faceOf(last) };
    let i = 0;
    while (i < s.length - 2 && s[i + 1].t <= t) i += 1;
    const a = s[i], b = s[i + 1], span = b.t - a.t, f = span > 0 ? Math.min(1, Math.max(0, (t - a.t) / span)) : 1;
    const fa = faceOf(a), fb = faceOf(b);
    return {
      x: a.x + (b.x - a.x) * f,
      y: a.y + (b.y - a.y) * f,
      face: fa != null && fb != null ? angleLerp(fa, fb, f) : fa != null ? fa : fb,
    };
  }

  // The position drawn for a unit now (at the given render time, else at renderTime: the checks read it unclamped).
  function at(st, id, nowMs, rt) {
    const u = st && st.units && st.units[String(id)];
    if (!u || !u.samples || !u.samples.length) return null;
    return pointAt(u.samples, typeof rt === "number" ? rt : renderTime(st, nowMs));
  }

  // A new sample for one unit. It snaps (no interpolation towards it) when the map changed, the unit died, or the distance is
  // more than walking or riding explains (a summon, a portal, a spirit release). A sample the daemon already superseded is
  // dropped, so a late report never walks the dot backwards. `pos` marks a position frame's sample: for POS_WINDOW_MS after
  // one, the 2 s report only keeps the unit on the card (its position is older than the frame's, and stamped late), and a unit
  // the frames do not carry (a new one, a mob off their watch list) is moved by the report as before.
  function note(st, id, x, y, map, alive, nowMs, tsMs, face, pos) {
    if (!st || id == null) return;
    if (!st.units) st.units = {};
    const key = String(id), live = alive !== false, f = typeof face === "number" && isFinite(face) ? face : undefined;
    const prev = st.units[key];
    if (!pos && prev && typeof prev.posAt === "number" && nowMs - prev.posAt <= POS_WINDOW_MS) { prev.lastSeen = nowMs; return; }
    const t = clockOf(st, nowMs, tsMs), posAt = pos ? nowMs : prev ? prev.posAt : undefined;
    if (!prev) {
      st.units[key] = { samples: [{ t: t, x: x, y: y, f: f }], map: map, alive: live, lastSeen: nowMs, posAt: posAt };
      return;
    }
    const last = prev.samples[prev.samples.length - 1];
    if (t <= last.t) { prev.map = map; prev.alive = live; prev.lastSeen = nowMs; prev.posAt = posAt; return; }
    const gap = t - last.t;
    const snap = map !== prev.map || !live || gap > CAP_MS || Math.hypot(last.x - x, last.y - y) > jumpLimitYd(gap);
    const samples = snap ? [] : prev.samples.slice(-(SAMPLES - 1));
    samples.push({ t: t, x: x, y: y, f: f });
    st.units[key] = { samples: samples, map: map, alive: live, lastSeen: nowMs, posAt: posAt };
  }

  // Whether any unit is still moving (the page keeps drawing every frame while this is true).
  function active(st, nowMs) {
    const units = (st && st.units) || {}, t = st ? clock(st, nowMs) : 0;
    return Object.keys(units).some(k => {
      const u = units[k], s = u.samples;
      if (!s || s.length < 2 || u.alive === false) return false;
      const a = s[s.length - 2], b = s[s.length - 1];
      if (a.x === b.x && a.y === b.y && a.f === b.f) return false;
      return t < b.t;
    });
  }

  // A shallow copy of a binding whose bots' and mobs' x/y (and a boss's facing) are the interpolated positions (the reported
  // ones where nothing is kept), all at the card's one render clock. The binding itself is never changed: the state list is
  // what the next report replaces.
  function apply(bind, st, nowMs) {
    if (!bind) return bind;
    const rt = clock(st, nowMs);
    const bots = Array.isArray(bind.bots)
      ? bind.bots.map((b) => {
        const p = b ? at(st, botKey(b.guid), nowMs, rt) : null;
        return p ? Object.assign({}, b, { x: p.x, y: p.y }) : b;
      })
      : bind.bots;
    const mobs = Array.isArray(bind.mobs)
      ? bind.mobs.map((m) => {
        const p = m ? at(st, mobKey(m.id), nowMs, rt) : null;
        if (!p) return m;
        const o = Object.assign({}, m, { x: p.x, y: p.y });
        if (typeof p.face === "number") o.o = p.face;
        return o;
      })
      : bind.mobs;
    return Object.assign({}, bind, { bots: bots, mobs: mobs });
  }

  const api = {
    CAP_MS, FRAME_MS, GRID_FRAME_MS, BACK_MS, PRUNE_MS, POS_WINDOW_MS, SLEW, SLEW_JUMP_MS,
    SAMPLES, OFFSETS, GAPS, DELAY_FACTOR, DELAY_MIN_MS, DELAY_MAX_MS,
    SLACK_YD, YD_PER_SEC, MAX_GAP_SEC,
    jumpLimitYd, create, botKey, mobKey, median, angleLerp, sampled, delayOf, renderTime, clock, at, note, active, apply, prune,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.Glide = api;
})(typeof self !== "undefined" ? self : this);
