// Tortoise WoW Observability Dashboard Client
(function() {
  'use strict';

  // Zone registry matching bundled webp maps and DBC area IDs
  const ZONE_CONFIG = {
    12: { name: 'Elwynn Forest', map: 0, file: 'elwynn.webp' },
    14: { name: 'Durotar', map: 1, file: 'durotar.webp' },
    17: { name: 'The Barrens', map: 1, file: 'barrens.webp' },
    3277: { name: 'Warsong Gulch', map: 489, file: 'warsonggulch.webp' },
    1: { name: 'Dun Morogh', map: 0, file: 'dunmorogh.webp' },
    215: { name: 'Mulgore', map: 1, file: 'mulgore.webp' },
    85: { name: 'Tirisfal Glades', map: 0, file: 'tirisfal.webp' },
    141: { name: 'Teldrassil', map: 1, file: 'teldrassil.webp' },
    40: { name: 'Westfall', map: 0, file: 'westfall.webp' },
    44: { name: 'Redridge Mountains', map: 0, file: 'redridge.webp' },
    148: { name: 'Darkshore', map: 1, file: 'darkshore.webp' },
    33: { name: 'Stranglethorn Vale', map: 0, file: 'stranglethorn.webp' },
    3358: { name: 'Arathi Basin', map: 529, file: 'arathibasin.webp' },
    2597: { name: 'Alterac Valley', map: 30, file: 'alteracvalley.webp' }
  };

  const state = {
    activeTab: 'map',
    currentZoneId: 12, // Default Elwynn
    bots: [],
    anomalies: [],
    server: {
      online: false,
      uptime: 0,
      diff: 0,
      humans: 0,
      bots: 0,
      states: { combat: 0, moving: 0, resting: 0, dead: 0, idle: 0 }
    },
    showTrails: true,
    roleFilter: 'all',
    searchQuery: '',
    selectedBotGuid: null,
    anomalyTypeFilter: 'all',
    anomalySeverityFilter: 'all',
    wsConnected: false
  };

  // DOM Elements
  const el = {
    statusPill: document.getElementById('status-pill'),
    statusText: document.getElementById('status-text'),
    statusDot: document.getElementById('status-dot'),
    uptimeVal: document.getElementById('uptime-val'),
    diffVal: document.getElementById('diff-val'),
    humansVal: document.getElementById('humans-val'),
    botsVal: document.getElementById('bots-val'),
    zoneSelect: document.getElementById('zone-select'),
    mapImg: document.getElementById('map-img'),
    mapOverlay: document.getElementById('map-overlay'),
    mapCanvas: document.getElementById('map-canvas'),
    mapTooltip: document.getElementById('map-tooltip'),
    toggleTrails: document.getElementById('toggle-trails'),
    roleFilter: document.getElementById('role-filter'),
    botSearch: document.getElementById('bot-search'),
    botDrawer: document.getElementById('bot-drawer'),
    drawerContent: document.getElementById('drawer-content'),
    closeDrawer: document.getElementById('close-drawer'),
    anomaliesTable: document.getElementById('anomalies-table-body'),
    anomaliesCount: document.getElementById('anomalies-count'),
    typeFilter: document.getElementById('anomaly-type-filter'),
    severityFilter: document.getElementById('anomaly-severity-filter'),
    clearAnomalies: document.getElementById('clear-anomalies'),
    rosterTable: document.getElementById('roster-table-body'),
    stateCombat: document.getElementById('state-combat'),
    stateMoving: document.getElementById('state-moving'),
    stateResting: document.getElementById('state-resting'),
    stateIdle: document.getElementById('state-idle'),
    stateDead: document.getElementById('state-dead'),
    tabBtns: document.querySelectorAll('.tab-btn'),
    tabViews: document.querySelectorAll('.tab-view')
  };

  // Format seconds to hh:mm:ss
  function formatUptime(seconds) {
    if (!seconds) return '00:00:00';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  }

  // Tab Navigation
  el.tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const tab = btn.dataset.tab;
      state.activeTab = tab;
      el.tabBtns.forEach(b => b.classList.toggle('active', b.dataset.tab === tab));
      el.tabViews.forEach(v => v.style.display = v.id === `tab-${tab}` ? 'block' : 'none');
      if (tab === 'map') renderMap();
      if (tab === 'roster') renderRoster();
    });
  });

  // Zone Selector Change
  if (el.zoneSelect) {
    el.zoneSelect.addEventListener('change', (e) => {
      const zid = parseInt(e.target.value, 10);
      state.currentZoneId = zid;
      loadZoneMap(zid);
    });
  }

  function loadZoneMap(zoneId) {
    const zone = ZONE_CONFIG[zoneId];
    if (zone && el.mapImg) {
      el.mapImg.src = `/maps/${zone.file}`;
      renderMap();
    }
  }

  // Trail Toggle
  if (el.toggleTrails) {
    el.toggleTrails.addEventListener('change', (e) => {
      state.showTrails = e.target.checked;
      renderMap();
    });
  }

  // Filter Event Listeners
  if (el.roleFilter) {
    el.roleFilter.addEventListener('change', (e) => {
      state.roleFilter = e.target.value;
      renderMap();
      renderRoster();
    });
  }

  if (el.botSearch) {
    el.botSearch.addEventListener('input', (e) => {
      state.searchQuery = e.target.value.toLowerCase().trim();
      renderMap();
      renderRoster();
    });
  }

  if (el.closeDrawer) {
    el.closeDrawer.addEventListener('click', () => {
      state.selectedBotGuid = null;
      el.botDrawer.style.display = 'none';
    });
  }

  if (el.typeFilter) {
    el.typeFilter.addEventListener('change', (e) => {
      state.anomalyTypeFilter = e.target.value;
      renderAnomalies();
    });
  }

  if (el.severityFilter) {
    el.severityFilter.addEventListener('change', (e) => {
      state.anomalySeverityFilter = e.target.value;
      renderAnomalies();
    });
  }

  if (el.clearAnomalies) {
    el.clearAnomalies.addEventListener('click', () => {
      state.anomalies = [];
      renderAnomalies();
    });
  }

  // Initial Fetch of Anomaly History
  async function fetchAnomalies() {
    try {
      const res = await fetch('/api/v1/anomalies');
      if (res.ok) {
        const data = await res.json();
        state.anomalies = data || [];
        renderAnomalies();
      }
    } catch (err) {
      console.error('Failed to fetch anomalies:', err);
    }
  }

  // WebSocket Connection
  function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/api/v1/stream`;
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      state.wsConnected = true;
      updateServerStatus();
      console.log('[WS] Connected to telemetry stream');
    };

    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.event === 'heartbeat') {
          handleHeartbeat(msg.data);
        } else if (msg.event === 'anomaly') {
          handleAnomaly(msg.data);
        }
      } catch (e) {
        console.error('Error handling WS message:', e);
      }
    };

    ws.onclose = () => {
      state.wsConnected = false;
      updateServerStatus();
      console.log('[WS] Connection closed, retrying in 2s...');
      setTimeout(initWebSocket, 2000);
    };

    ws.onerror = (err) => {
      console.error('[WS] Error:', err);
      ws.close();
    };
  }

  function handleHeartbeat(hb) {
    state.server.online = true;
    state.server.uptime = hb.uptime;
    state.server.diff = hb.diff;
    state.server.humans = hb.humans;
    state.server.bots = hb.bots;
    state.server.states = hb.states || state.server.states;
    state.bots = hb.bot_list || [];

    updateServerStatus();
    renderStateRatios();

    if (state.activeTab === 'map') renderMap();
    if (state.activeTab === 'roster') renderRoster();
    if (state.selectedBotGuid) updateBotDrawer();
  }

  function handleAnomaly(a) {
    state.anomalies.unshift(a);
    if (state.anomalies.length > 1000) {
      state.anomalies.pop();
    }
    renderAnomalies();
  }

  function updateServerStatus() {
    if (!state.wsConnected || !state.server.online) {
      el.statusText.textContent = state.wsConnected ? 'Server Offline' : 'Disconnected';
      el.statusDot.className = 'status-dot offline';
      el.statusPill.style.borderColor = 'rgba(239, 68, 68, 0.3)';
      el.statusPill.style.color = '#f87171';
    } else if (state.server.diff > 100) {
      el.statusText.textContent = `Lagging (${state.server.diff.toFixed(1)}ms)`;
      el.statusDot.className = 'status-dot lagging';
      el.statusPill.style.borderColor = 'rgba(245, 158, 11, 0.3)';
      el.statusPill.style.color = '#fbbf24';
    } else {
      el.statusText.textContent = 'Server Online';
      el.statusDot.className = 'status-dot pulse';
      el.statusPill.style.borderColor = 'rgba(16, 185, 129, 0.25)';
      el.statusPill.style.color = '#34d399';
    }

    el.uptimeVal.textContent = formatUptime(state.server.uptime);
    el.diffVal.textContent = `${(state.server.diff || 0).toFixed(1)}ms`;
    el.humansVal.textContent = state.server.humans || 0;
    el.botsVal.textContent = state.server.bots || 0;
  }

  function renderStateRatios() {
    const s = state.server.states;
    const cPct = ((s.combat || 0) * 100).toFixed(1);
    const mPct = ((s.moving || 0) * 100).toFixed(1);
    const rPct = ((s.resting || 0) * 100).toFixed(1);
    const iPct = ((s.idle || 0) * 100).toFixed(1);
    const dPct = ((s.dead || 0) * 100).toFixed(1);

    if (el.stateCombat) el.stateCombat.style.width = `${cPct}%`;
    if (el.stateMoving) el.stateMoving.style.width = `${mPct}%`;
    if (el.stateResting) el.stateResting.style.width = `${rPct}%`;
    if (el.stateIdle) el.stateIdle.style.width = `${iPct}%`;
    if (el.stateDead) el.stateDead.style.width = `${dPct}%`;

    document.getElementById('state-combat-txt').textContent = `${cPct}%`;
    document.getElementById('state-moving-txt').textContent = `${mPct}%`;
    document.getElementById('state-resting-txt').textContent = `${rPct}%`;
    document.getElementById('state-idle-txt').textContent = `${iPct}%`;
    document.getElementById('state-dead-txt').textContent = `${dPct}%`;
  }

  // 2D Map Rendering
  function renderMap() {
    if (!el.mapOverlay) return;

    // Clear overlay markers
    el.mapOverlay.innerHTML = '';

    // Setup Canvas for breadcrumb trails
    const canvas = el.mapCanvas;
    const ctx = canvas ? canvas.getContext('2d') : null;
    if (canvas && ctx) {
      canvas.width = canvas.offsetWidth;
      canvas.height = canvas.offsetHeight;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }

    const currentZone = state.currentZoneId;

    // Filter bots in current zone
    const botsInZone = state.bots.filter(b => {
      if (b.zone !== currentZone && b.map !== ZONE_CONFIG[currentZone]?.map) {
        // Fallback match: if bot's zone matches selected zone
        if (b.zone !== currentZone) return false;
      }
      if (state.roleFilter !== 'all' && b.role.toLowerCase() !== state.roleFilter) return false;
      if (state.searchQuery && !b.name.toLowerCase().includes(state.searchQuery)) return false;
      return true;
    });

    // Draw breadcrumb trails on canvas
    if (state.showTrails && ctx && canvas) {
      botsInZone.forEach(b => {
        if (!b.trail || b.trail.length < 2) return;
        ctx.beginPath();
        let color = '#3b82f6';
        if (b.role === 'healer') color = '#10b981';
        if (b.role === 'dps') color = '#ef4444';

        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.setLineDash([4, 4]);
        ctx.globalAlpha = 0.5;

        for (let i = 0; i < b.trail.length; i++) {
          const pt = b.trail[i];
          const px = (pt.pct_x / 100.0) * canvas.width;
          const py = (pt.pct_y / 100.0) * canvas.height;
          if (i === 0) ctx.moveTo(px, py);
          else ctx.lineTo(px, py);
        }
        ctx.stroke();
      });
      ctx.globalAlpha = 1.0;
      ctx.setLineDash([]);
    }

    // Render bot dots
    botsInZone.forEach(b => {
      // Must have valid map percentages
      if (b.pct_x <= 0 || b.pct_x >= 100 || b.pct_y <= 0 || b.pct_y >= 100) return;

      const marker = document.createElement('div');
      marker.className = `bot-marker ${b.state === 'dead' ? 'dead' : b.role.toLowerCase()}`;
      marker.style.left = `${b.pct_x}%`;
      marker.style.top = `${b.pct_y}%`;
      marker.dataset.guid = b.guid;

      // Hover tooltip
      marker.addEventListener('mouseenter', (e) => {
        showTooltip(e, b);
      });
      marker.addEventListener('mouseleave', () => {
        hideTooltip();
      });

      // Click selection
      marker.addEventListener('click', () => {
        selectBot(b.guid);
      });

      el.mapOverlay.appendChild(marker);
    });
  }

  function showTooltip(e, bot) {
    if (!el.mapTooltip) return;
    const hpPct = bot.max_hp ? Math.round((bot.hp / bot.max_hp) * 100) : 100;
    const powerPct = bot.max_power ? Math.round((bot.power / bot.max_power) * 100) : 100;

    let roleBadgeColor = '#3b82f6';
    if (bot.role === 'healer') roleBadgeColor = '#10b981';
    if (bot.role === 'dps') roleBadgeColor = '#ef4444';

    el.mapTooltip.innerHTML = `
      <div style="font-weight: 700; font-size: 0.85rem; margin-bottom: 4px; display: flex; align-items: center; justify-content: space-between; gap: 8px;">
        <span>${bot.name} (Lvl ${bot.level} ${bot.class})</span>
        <span style="font-size: 0.7rem; padding: 1px 6px; border-radius: 4px; background: ${roleBadgeColor}33; color: ${roleBadgeColor}; text-transform: uppercase;">${bot.role}</span>
      </div>
      <div style="font-size: 0.75rem; color: var(--text-muted); margin-bottom: 6px;">
        State: <strong style="color: #fff;">${bot.state}</strong> | Target: <strong style="color: #f87171;">${bot.target || 'None'}</strong>
      </div>
      <div style="margin-bottom: 4px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.7rem; margin-bottom: 2px;">
          <span>HP</span><span>${bot.hp} / ${bot.max_hp} (${hpPct}%)</span>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill progress-hp" style="width: ${hpPct}%"></div></div>
      </div>
      <div>
        <div style="display: flex; justify-content: space-between; font-size: 0.7rem; margin-bottom: 2px;">
          <span>Power</span><span>${bot.power} / ${bot.max_power} (${powerPct}%)</span>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill progress-mana" style="width: ${powerPct}%"></div></div>
      </div>
    `;

    el.mapTooltip.style.left = `${bot.pct_x}%`;
    el.mapTooltip.style.top = `${bot.pct_y}%`;
    el.mapTooltip.style.display = 'block';
  }

  function hideTooltip() {
    if (el.mapTooltip) el.mapTooltip.style.display = 'none';
  }

  function selectBot(guid) {
    state.selectedBotGuid = guid;
    updateBotDrawer();
  }

  function updateBotDrawer() {
    if (!state.selectedBotGuid || !el.botDrawer) return;
    const bot = state.bots.find(b => b.guid === state.selectedBotGuid);
    if (!bot) {
      el.botDrawer.style.display = 'none';
      return;
    }

    const hpPct = bot.max_hp ? Math.round((bot.hp / bot.max_hp) * 100) : 100;
    const powerPct = bot.max_power ? Math.round((bot.power / bot.max_power) * 100) : 100;

    el.drawerContent.innerHTML = `
      <div style="display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 12px;">
        <h3 style="font-size: 1.25rem; font-weight: 700; color: #fff;">${bot.name}</h3>
        <span class="badge ${bot.role === 'tank' ? 'badge-info' : bot.role === 'healer' ? 'badge-warn' : 'badge-error'}">${bot.role.toUpperCase()}</span>
      </div>
      <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 0.8rem; color: var(--text-muted); margin-bottom: 16px;">
        <div>Class: <strong style="color: #fff;">${bot.class}</strong></div>
        <div>Level: <strong style="color: #fff;">${bot.level}</strong></div>
        <div>State: <strong style="color: #fff;">${bot.state}</strong></div>
        <div>Zone: <strong style="color: #fff;">${bot.zone} (Map ${bot.map})</strong></div>
        <div style="grid-column: span 2;">Target: <strong style="color: #f87171;">${bot.target || 'None'}</strong></div>
        <div style="grid-column: span 2;">Strategy: <strong style="color: #818cf8;">${bot.strategy || 'None'}</strong></div>
        <div style="grid-column: span 2;" class="mono">Coords: (${bot.x.toFixed(1)}, ${bot.y.toFixed(1)}, ${bot.z.toFixed(1)})</div>
      </div>
      <div style="margin-bottom: 10px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>Health</span><span>${bot.hp} / ${bot.max_hp}</span>
        </div>
        <div class="progress-bar-bg" style="height: 8px;"><div class="progress-bar-fill progress-hp" style="width: ${hpPct}%"></div></div>
      </div>
      <div style="margin-bottom: 16px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>Power</span><span>${bot.power} / ${bot.max_power}</span>
        </div>
        <div class="progress-bar-bg" style="height: 8px;"><div class="progress-bar-fill progress-mana" style="width: ${powerPct}%"></div></div>
      </div>
    `;

    el.botDrawer.style.display = 'block';
  }

  // Anomalies Table Rendering
  function renderAnomalies() {
    if (!el.anomaliesTable) return;
    el.anomaliesTable.innerHTML = '';

    const filtered = state.anomalies.filter(a => {
      if (state.anomalyTypeFilter !== 'all' && a.type !== state.anomalyTypeFilter) return false;
      if (state.anomalySeverityFilter !== 'all' && a.severity !== state.anomalySeverityFilter) return false;
      return true;
    });

    if (el.anomaliesCount) {
      el.anomaliesCount.textContent = `${filtered.length} incidents`;
    }

    if (filtered.length === 0) {
      el.anomaliesTable.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-muted); padding: 24px;">No incidents recorded</td></tr>`;
      return;
    }

    filtered.forEach(a => {
      const tr = document.createElement('tr');
      const timeStr = a.time_str || new Date(a.ts * 1000).toLocaleTimeString();
      const sevBadge = a.severity === 'ERROR' ? 'badge-error' : a.severity === 'WARN' ? 'badge-warn' : 'badge-info';

      tr.innerHTML = `
        <td class="mono" style="font-size: 0.8rem; color: var(--text-muted);">${timeStr}</td>
        <td><span class="badge ${sevBadge}">${a.type}</span></td>
        <td style="font-weight: 600;">${a.bot}</td>
        <td class="mono" style="font-size: 0.8rem;">Zone ${a.zone}</td>
        <td style="color: #f87171;">${a.target || '-'}</td>
        <td class="mono" style="font-size: 0.8rem;">${a.last_action || '-'}</td>
        <td style="color: var(--text-muted);">${a.details}</td>
      `;
      el.anomaliesTable.appendChild(tr);
    });
  }

  // Roster Table Rendering
  function renderRoster() {
    if (!el.rosterTable) return;
    el.rosterTable.innerHTML = '';

    const filtered = state.bots.filter(b => {
      if (state.roleFilter !== 'all' && b.role.toLowerCase() !== state.roleFilter) return false;
      if (state.searchQuery && !b.name.toLowerCase().includes(state.searchQuery)) return false;
      return true;
    });

    if (filtered.length === 0) {
      el.rosterTable.innerHTML = `<tr><td colspan="8" style="text-align: center; color: var(--text-muted); padding: 24px;">No active bots found</td></tr>`;
      return;
    }

    filtered.forEach(b => {
      const tr = document.createElement('tr');
      const hpPct = b.max_hp ? Math.round((b.hp / b.max_hp) * 100) : 100;
      const roleClass = b.role === 'tank' ? 'badge-info' : b.role === 'healer' ? 'badge-warn' : 'badge-error';

      tr.innerHTML = `
        <td style="font-weight: 600; cursor: pointer; color: #818cf8;" onclick="window.dashboardSelectBot(${b.guid})">${b.name}</td>
        <td>${b.class}</td>
        <td><span class="badge ${roleClass}">${b.role.toUpperCase()}</span></td>
        <td>${b.level}</td>
        <td style="width: 140px;">
          <div style="font-size: 0.7rem; margin-bottom: 2px;">${b.hp}/${b.max_hp} (${hpPct}%)</div>
          <div class="progress-bar-bg"><div class="progress-bar-fill progress-hp" style="width: ${hpPct}%"></div></div>
        </td>
        <td><span class="badge ${b.state === 'combat' ? 'badge-error' : 'badge-info'}">${b.state}</span></td>
        <td style="color: #f87171;">${b.target || '-'}</td>
        <td class="mono" style="font-size: 0.8rem;">Zone ${b.zone}</td>
      `;
      el.rosterTable.appendChild(tr);
    });
  }

  window.dashboardSelectBot = selectBot;

  // Initialize
  loadZoneMap(state.currentZoneId);
  fetchAnomalies();
  initWebSocket();

})();
