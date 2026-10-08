// Tortoise WoW Observability Dashboard
// State model: the WebSocket "snapshot" event is authoritative for the roster.
// Heartbeats update server gauges, "status" reports online/stale transitions,
// and a local watchdog marks the stream offline if pulses stop.
//
// Information architecture (one owner per question, no duplicate lists):
//   Overview  is everything running?      Bots      the one list of bots
//   Map       where are they?             Progress  are they levelling?
//   Activity  what happened?              Issues    what is broken?
//   Server    how is it configured?
// A bot's detail lives in ONE slide-over profile that any tab can open; the
// URL hash (#/bots?bot=123&p=gear) records the tab and the open profile.
(function() {
  'use strict';

  // Fallback zone dictionary (loaded dynamically from /data/zone_maps.json)
  let ZONE_CONFIG = {
    12: { name: 'Elwynn Forest', map: 0, file: 'elwynn.webp' },
    14: { name: 'Durotar', map: 1, file: 'durotar.webp' },
    17: { name: 'The Barrens', map: 1, file: 'barrens.webp' },
    3277: { name: 'Warsong Gulch', map: 489, file: 'warsonggulch.webp' }
  };

  const OFFLINE_AFTER_MS = 10000;

  // The selected tab survives a reload when the URL carries no hash: the
  // operator reloads mid-investigation and expects to land back where they
  // were. Only known tab keys are honoured.
  const TAB_STORAGE_KEY = 'tortoisebots.dashboard.tab';
  const BOTS_VIEW_STORAGE_KEY = 'tortoisebots.dashboard.botsview';

  const state = {
    activeTab: 'overview',
    currentZoneId: 12,
    mapView: 'world',
    worldMapId: 0,
    bots: [],
    // Every bot character in the database (online or offline), joined with the
    // live roster by guid to form the one Bots list.
    roster: [],
    rosterAt: 0,
    rosterError: false,
    anomalies: [],
    anomalyTotals: null,
    grinding: { bots_tracked: 0, bots_gaining_xp: 0, pct_gaining_xp: 0, median_xp_hour: 0, total_xp_hour: 0, deaths_per_min: 0, pct_died_5min: 0, state_counts: {}, level_bands: [] },
    serverInfo: null,
    server: {
      online: false,
      stale: true,
      uptime: 0,
      diff: 0,
      diff_avg: 0,
      diff_worst: 0,
      lag_p50: 0,
      lag_p95: 0,
      humans: 0,
      bots: 0
    },
    history: {
      t: [],
      bots: [],
      humans: [],
      diff: [],
      lag50: []
    },
    showTrails: true,
    // Bots tab
    botsPresence: 'all',
    botsView: 'live',
    botsState: 'all',
    botsClass: 'all',
    botsIssueOnly: false,
    botsSort: { key: null, dir: 1 }, // null = online first, then by name
    anomalyTypeFilter: 'all',
    anomalySeverityFilter: 'all',
    wsConnected: false,
    snapshotSeq: 0,
    lastHeartbeatAt: 0,
    lastSnapshotAt: 0,
    issues: { active: [], resolved: [], counts_by_type: {} },
    issueTypeFilter: 'all',
    issueDurationFilter: 300,
    issueHistory: { t: [], series: {} },
    feedView: 'quests',
    issuesView: 'active',
    // Bot profile slide-over
    profileGuid: null,
    profile: null,
    profileTab: 'overview',
    profileRecent: null,
    armorySpellFilter: '',
    activity: null,
    lootFeed: [],
    questFeed: []
  };

  // Equipment slot IDs mirror core Player.h EquipmentSlots (0-18).
  const EQUIP_SLOT_NAMES = {
    0: 'Head', 1: 'Neck', 2: 'Shoulders', 3: 'Shirt', 4: 'Chest',
    5: 'Waist', 6: 'Legs', 7: 'Feet', 8: 'Wrists', 9: 'Hands',
    10: 'Ring 1', 11: 'Ring 2', 12: 'Trinket 1', 13: 'Trinket 2',
    14: 'Back', 15: 'Main Hand', 16: 'Off Hand', 17: 'Ranged', 18: 'Tabard'
  };

  // Classic paperdoll columns: left, right, weapons row.
  const GEAR_LEFT = [0, 1, 2, 14, 4, 3, 18, 8];
  const GEAR_RIGHT = [9, 5, 6, 7, 10, 11, 12, 13];
  const GEAR_WEAPONS = [15, 16, 17];

  // Race/class IDs from core SharedDefines.h (RACE_* 1-10, CLASS_* 1-11).
  const RACE_NAMES = {
    1: 'Human', 2: 'Orc', 3: 'Dwarf', 4: 'Night Elf', 5: 'Undead',
    6: 'Tauren', 7: 'Gnome', 8: 'Troll', 9: 'Goblin', 10: 'High Elf'
  };

  const CLASS_NAMES = {
    1: 'Warrior', 2: 'Paladin', 3: 'Hunter', 4: 'Rogue', 5: 'Priest',
    7: 'Shaman', 8: 'Mage', 9: 'Warlock', 11: 'Druid'
  };

  // Quality names follow core SharedDefines.h ItemQualities (0-6).
  const QUALITY_NAMES = ['Poor', 'Common', 'Uncommon', 'Rare', 'Epic', 'Legendary', 'Artifact'];

  // Quality colours for the gear-quality census (core ItemQualityColors).
  const QUALITY_COLORS = { 0: '#9d9d9d', 1: '#ffffff', 2: '#1eff00', 3: '#0070dd', 4: '#a335ee' };

  // One colour per macro state, shared by the Activity census, the roster
  // state badges and the map markers so "dead" is never three colours.
  const STATE_COLORS = {
    combat: '#f85149', moving: '#58a6ff', busy: '#d29922', stalled: '#e3852a',
    resting: '#2ea043', idle: '#9aa4b2', dead: '#8b949e'
  };
  const STATE_TITLES = {
    combat: 'In combat or pulling',
    moving: 'A movement generator owns the bot',
    busy: 'Standing still but doing real work (loot, cast, eat, or movement in the last 45 s)',
    stalled: 'Standing still for 45+ s with only a travel target or action-name changes — no progress',
    resting: 'Rest flag',
    idle: 'No activity for 45+ s — really doing nothing',
    dead: 'Dead'
  };
  const STATE_ORDER = ['combat', 'moving', 'busy', 'stalled', 'resting', 'idle', 'dead'];

  // Equipped-gear summary used by both the roster sweep (daemon) and the
  // armory paperdoll (profile rows): average item level over filled slots
  // 0-18 minus shirt (3) and tabard (18), plus the quality census.
  function gearSummary(items) {
    let pieces = 0, sum = 0;
    const quality = [0, 0, 0, 0, 0];
    (items || []).forEach(it => {
      if (!it || it.slot === 3 || it.slot === 18) return;
      pieces++;
      sum += it.item_level || 0;
      quality[Math.min(it.quality || 0, 4)]++;
    });
    return {
      pieces,
      item_level: pieces ? sum / pieces : 0,
      grey: quality[0], white: quality[1], green: quality[2], blue: quality[3], epic: quality[4]
    };
  }

  const SPELL_SCHOOLS = {
    0: 'Physical', 1: 'Holy', 2: 'Fire', 3: 'Nature', 4: 'Frost', 5: 'Shadow', 6: 'Arcane'
  };

  // Spell buckets for the spellbook tab. Pets/traps/mounts/professions are
  // data noise next to combat spells; grouping by nameSubtext + pet-teach
  // descriptions keeps e.g. hunter pet teaches out of the combat list.
  // class_spell comes from the operator's DBCs (class skill lines); the rank
  // and name rules below cover talents, custom spells, and DBC-less setups.
  function spellBucket(sp) {
    const sub = String(sp.subtext || '');
    const desc = String(sp.description || '');
    const name = String(sp.name || '');
    if (sp.class_spell) return 'Class spells';
    if (/Rank \d+/.test(sub)) return 'Class spells';
    if (/Apprentice|Journeyman|Expert|Artisan/.test(sub)) return 'Professions';
    if (/Trap|Totem|Seal|Blessing|Aura|Stance|Form|Aspect|Track/i.test(name + ' ' + sub)) return 'Auras & Forms';
    if (/Summon|Pet|Cat|Boar|Owl|Carrion|Crab|Gorilla|Raptor|Tallstrider|Turtle|Wolf|Bear|Spider|Bat|Hyena|Wind Serpent|Scorpid|Tamed/i.test(sub + ' ' + desc + ' ' + name)) return 'Pet & Minions';
    return 'Abilities';
  }

  // Client $tokens resolved from backend effect values ($sN = base+dice per
  // core CalculateSimpleValue, $oN = same-effect tick, $aN = radius/misc,
  // $tN = amplitude, $d/$r/$c from operator DBCs, $/10; = divide-by-10).
  // Unknown tokens stay visible so missing data is obvious, never silent.
  function spellText(sp) {
    let raw = String(sp.description || '').replace(/\s+/g, ' ').trim();
    if (!raw) return '';
    const vals = sp.values || [], misc = sp.misc || [], trig = sp.triggers || [];
    const num = v => (v === null || v === undefined) ? '?' : String(v);
    raw = raw.replace(/\$\/(\d+);s(\d)/g, (m, div, n) => {
      const v = vals[parseInt(n, 10) - 1];
      if (v === null || v === undefined) return m;
      const d = parseInt(div, 10) || 1;
      const q = v / d;
      return String(Math.round(q * 10) / 10);
    });
    raw = raw.replace(/\$([sSoOaAtT])(\d)/g, (m, kind, n) => {
      const i = parseInt(n, 10) - 1;
      const k = kind.toLowerCase();
      if (k === 's' || k === 'o') return num(vals[i]);
      if (k === 'a') return num(misc[i] !== undefined ? misc[i] : vals[i]);
      if (k === 't') return num(trig[i] !== undefined ? trig[i] : vals[i]);
      return m;
    });
    raw = raw.replace(/\$d\b/g, sp.duration_ms ? fmtDur(sp.duration_ms) : '$d');
    raw = raw.replace(/\$r\b/g, sp.range_yd ? `${sp.range_yd} yd` : '$r');
    raw = raw.replace(/\$c\b/g, sp.cast_ms ? fmtDur(sp.cast_ms) : '$c');
    return raw;
  }

  function fmtDur(ms) {
    if (ms < 0) return 'permanent';
    if (ms < 1000) return `${ms} ms`;
    const s = ms / 1000;
    if (s < 60) return `${Math.round(s * 10) / 10} sec`;
    const m = Math.floor(s / 60);
    return m >= 60 ? `${Math.floor(m / 60)}h ${m % 60}m` : `${m} min`;
  }

  function spellHint(sp) {
    const resolved = spellText(sp);
    if (!resolved) return '';
    const plain = resolved.split('.')[0].slice(0, 160);
    if (/\$/.test(plain)) return '';
    return plain;
  }

  // Full hover card for a spell row: name + rank, school, resolved text.
  function spellTooltip(sp) {
    const name = sp.name || `Spell ${sp.spell}`;
    const sub = sp.subtext ? `<span style="color: var(--text-muted);">${esc(sp.subtext)}</span>` : '';
    const hint = (spellHint(sp) || '').trim();
    const full = (spellText(sp) || '').trim();
    const text = hint || full;
    let html = `<div class="tip-name">${esc(name)}${sub ? ` ${sub}` : ''}</div>`;
    html += `<div class="tip-sub">${esc(spellSchoolName(sp.school))}</div>`;
    if (sp.origin === 'starting') {
      html += `<div class="tip-sub">Starting spell — granted on login, not stored in character_spell</div>`;
    }
    if (text) {
      html += `<div class="tip-stat" style="margin-top: 4px; max-width: 280px;">${esc(text)}</div>`;
    }
    return html;
  }

  // Skill names from core SharedDefines.h SkillType enum. No DBC or client
  // data is shipped; unknown IDs fall back to "Skill <id>".
  const SKILL_NAMES = {
    6: 'Frost', 8: 'Fire', 26: 'Arms', 38: 'Combat', 39: 'Subtlety', 40: 'Poisons',
    43: 'Swords', 44: 'Axes', 45: 'Bows', 46: 'Guns', 50: 'Beast Mastery', 51: 'Survival',
    54: 'Maces', 55: 'Two-Handed Swords', 56: 'Holy', 78: 'Shadow', 95: 'Defense',
    98: 'Language: Common', 101: 'Racial: Dwarven', 109: 'Language: Orcish',
    111: 'Language: Dwarven', 113: 'Language: Darnassian', 115: 'Language: Taurahe',
    118: 'Dual Wield', 124: 'Racial: Tauren', 125: 'Racial: Orc', 126: 'Racial: Night Elf',
    129: 'First Aid', 134: 'Feral Combat', 136: 'Staves', 137: 'Language: Thalassian',
    138: 'Language: Draconic', 139: 'Language: Demon Tongue', 140: 'Language: Titan',
    141: 'Language: Old Tongue', 142: 'Survival', 148: 'Riding: Horse', 149: 'Riding: Wolf',
    150: 'Riding: Tiger', 152: 'Riding: Ram', 155: 'Swimming', 160: 'Two-Handed Maces',
    162: 'Unarmed', 163: 'Marksmanship', 164: 'Blacksmithing', 165: 'Leatherworking',
    171: 'Alchemy', 172: 'Two-Handed Axes', 173: 'Daggers', 176: 'Thrown',
    182: 'Herbalism', 183: 'Generic (DND)', 184: 'Retribution', 185: 'Cooking', 186: 'Mining',
    188: 'Pet: Imp', 189: 'Pet: Felhunter', 197: 'Tailoring', 202: 'Engineering',
    203: 'Pet: Spider', 204: 'Pet: Voidwalker', 205: 'Pet: Succubus', 206: 'Pet: Infernal',
    207: 'Pet: Doomguard', 208: 'Pet: Wolf', 209: 'Pet: Cat', 210: 'Pet: Bear',
    211: 'Pet: Boar', 212: 'Pet: Crocilisk', 213: 'Pet: Carrion Bird', 214: 'Pet: Crab',
    215: 'Pet: Gorilla', 217: 'Pet: Raptor', 218: 'Pet: Tallstrider', 220: 'Racial: Undead',
    226: 'Crossbows', 228: 'Wands', 229: 'Polearms', 236: 'Pet: Scorpid', 237: 'Arcane',
    251: 'Pet: Turtle', 253: 'Assassination', 256: 'Fury', 257: 'Protection',
    261: 'Beast Training', 267: 'Protection', 270: 'Pet Talents', 293: 'Plate Mail',
    313: 'Language: Gnomish', 315: 'Language: Troll', 333: 'Enchanting', 354: 'Demonology',
    355: 'Affliction', 356: 'Fishing', 373: 'Enhancement', 374: 'Restoration',
    375: 'Elemental Combat', 393: 'Skinning', 413: 'Mail', 414: 'Leather', 415: 'Cloth',
    433: 'Shield', 473: 'Fist Weapons', 533: 'Riding: Raptor', 553: 'Riding: Mechanostrider',
    554: 'Riding: Undead Horse', 573: 'Restoration', 574: 'Balance', 593: 'Destruction',
    594: 'Holy', 613: 'Discipline', 633: 'Lockpicking', 653: 'Pet: Bat', 654: 'Pet: Hyena',
    655: 'Pet: Owl', 656: 'Pet: Wind Serpent', 673: 'Language: Gutterspeak',
    713: 'Riding: Kodo', 733: 'Racial: Troll', 753: 'Racial: Gnome', 754: 'Racial: Human',
    755: 'Jewelcrafting', 758: 'Pet Event (RC)', 762: 'Riding'
  };

  // Combat grouping for the Skills tab: weapons + defense first.
  const COMBAT_SKILL_IDS = [43, 44, 45, 46, 54, 55, 118, 136, 160, 162, 172, 173, 176, 226, 228, 229, 473, 95];
  const ARMOR_SKILL_IDS = [293, 413, 414, 415, 433];
  const PROFESSION_SKILL_IDS = [129, 164, 165, 171, 182, 185, 186, 197, 202, 333, 356, 393, 755];

  const SKILL_ICONS = {
    // Weapons & Combat
    43: 'inv_sword_04',
    44: 'inv_axe_02',
    45: 'inv_weapon_bow_05',
    46: 'inv_weapon_rifle_01',
    54: 'inv_mace_01',
    55: 'inv_sword_07',
    95: 'ability_defend',
    118: 'ability_dualwield',
    136: 'inv_staff_08',
    160: 'inv_mace_04',
    162: 'ability_gouge',
    172: 'inv_axe_09',
    173: 'inv_weapon_shortblade_05',
    176: 'inv_throwingknife_04',
    226: 'inv_weapon_crossbow_01',
    228: 'inv_wand_01',
    229: 'inv_spear_06',
    473: 'inv_misc_monsterclaw_04',
    // Armor
    293: 'inv_chest_plate01',
    413: 'inv_chest_chain',
    414: 'inv_chest_leather_09',
    415: 'inv_chest_cloth_21',
    433: 'inv_shield_04',
    // Professions & Secondary
    129: 'spell_holy_sealofsacrifice',
    164: 'trade_blacksmithing',
    165: 'trade_leatherworking',
    171: 'trade_alchemy',
    182: 'spell_nature_naturetouchgrow',
    185: 'inv_misc_food_15',
    186: 'trade_mining',
    197: 'trade_tailoring',
    202: 'trade_engineering',
    333: 'trade_engraving',
    356: 'trade_fishing',
    393: 'inv_misc_pelt_wolf_01',
    633: 'spell_nature_moonkey',
    755: 'inv_misc_gem_01',
    762: 'spell_nature_swiftness',
    // Class Specs / Magic Schools
    6: 'spell_frost_frostbolt02',
    8: 'spell_fire_fire',
    26: 'ability_warrior_savageblow',
    38: 'ability_backstab',
    39: 'ability_stealth',
    40: 'trade_brewpoison',
    50: 'ability_hunter_beasttaming',
    51: 'ability_hunter_swiftstrike',
    56: 'spell_holy_holybolt',
    78: 'spell_shadow_shadowbolt',
    134: 'ability_racial_bearform',
    142: 'ability_hunter_camouflage',
    163: 'ability_marksmanship',
    184: 'spell_holy_auraoflight',
    237: 'spell_holy_magicalsentry',
    253: 'ability_rogue_eviscerate',
    256: 'ability_warrior_innerrage',
    257: 'ability_warrior_defensivestance',
    267: 'spell_holy_devotionaura',
    354: 'spell_shadow_metamorphosis',
    355: 'spell_shadow_deathcoil',
    373: 'spell_nature_lightningoverload',
    374: 'spell_nature_magicimmunity',
    375: 'spell_nature_lightning',
    573: 'spell_nature_healingtouch',
    574: 'spell_nature_starfall',
    593: 'spell_shadow_rainoffire',
    594: 'spell_holy_guardianspirit',
    613: 'spell_holy_powerwordshield',
    // Languages & Misc
    98: 'inv_misc_book_07', 101: 'inv_misc_book_07', 109: 'inv_misc_book_07', 111: 'inv_misc_book_07',
    113: 'inv_misc_book_07', 115: 'inv_misc_book_07', 137: 'inv_misc_book_07',
    138: 'inv_misc_book_07', 139: 'inv_misc_book_07', 140: 'inv_misc_book_07',
    141: 'inv_misc_book_07', 313: 'inv_misc_book_07', 315: 'inv_misc_book_07',
    673: 'inv_misc_book_07',
  };

  function skillIconById(id) {
    return SKILL_ICONS[id] || 'inv_misc_questionmark';
  }

  // Item tooltip enums from core (ItemPrototype.h, SharedDefines.h). Hardcoded
  // on purpose: these are protocol constants, not data, and keep the dashboard
  // free of DBC/client assets.
  const ITEM_STAT_NAMES = {
    0: 'Mana', 1: 'Health', 3: 'Agility', 4: 'Strength',
    5: 'Intellect', 6: 'Spirit', 7: 'Stamina'
  };
  const ITEM_BONDING_NAMES = {
    0: '', 1: 'Binds when picked up', 2: 'Binds when equipped',
    3: 'Binds when used', 4: 'Quest Item', 6: 'Binds to account'
  };
  const ITEM_CLASS_NAMES = {
    0: 'Consumable', 1: 'Container', 2: 'Weapon', 3: 'Gem', 4: 'Armor',
    5: 'Reagent', 6: 'Projectile', 7: 'Trade Goods', 8: 'Generic',
    9: 'Recipe', 10: 'Money', 11: 'Quiver', 12: 'Quest', 13: 'Key',
    14: 'Permanent', 15: 'Junk'
  };
  const ITEM_SUBCLASS_NAMES = {
    '2-0': 'Axe', '2-1': 'Two-Handed Axe', '2-2': 'Bow', '2-3': 'Gun',
    '2-4': 'Mace', '2-5': 'Two-Handed Mace', '2-6': 'Polearm', '2-7': 'Sword',
    '2-8': 'Two-Handed Sword', '2-10': 'Staff', '2-13': 'Fist Weapon',
    '2-15': 'Dagger', '2-16': 'Thrown', '2-18': 'Crossbow', '2-19': 'Wand',
    '2-20': 'Fishing Pole', '4-0': 'Miscellaneous', '4-1': 'Cloth',
    '4-2': 'Leather', '4-3': 'Mail', '4-4': 'Plate', '4-6': 'Shield',
    '4-7': 'Libram', '4-8': 'Idol', '4-9': 'Totem'
  };
  const ITEM_TRIGGER_NAMES = {
    0: 'Use:', 1: 'Equip:', 2: 'Chance on hit:', 4: 'Soulstone:', 5: 'Use:'
  };
  const INVTYPE_NAMES = {
    1: 'Head', 2: 'Neck', 3: 'Shoulder', 4: 'Shirt', 5: 'Chest', 6: 'Waist',
    7: 'Legs', 8: 'Feet', 9: 'Wrist', 10: 'Hands', 11: 'Finger', 12: 'Trinket',
    13: 'Weapon', 14: 'Shield', 15: 'Ranged', 16: 'Back', 17: 'Two-Hand',
    18: 'Bag', 19: 'Tabard', 20: 'Robe', 21: 'Main Hand', 22: 'Off Hand',
    23: 'Held In Off-hand', 24: 'Ammo', 25: 'Thrown', 26: 'Ranged', 27: 'Quiver', 28: 'Relic'
  };

  const ISSUE_TYPES = ['STUCK', 'DEAD_LONG', 'UNREACHABLE_TARGET'];
  const ISSUE_LABELS = { STUCK: 'Stuck', DEAD_LONG: 'Dead long', UNREACHABLE_TARGET: 'Unreachable' };
  const ISSUE_COLORS = { STUCK: '#d29922', DEAD_LONG: '#8b949e', UNREACHABLE_TARGET: '#a371f7' };
  const ISSUE_HISTORY_MAX = 300;

  const HIST_MAX = 300; // 2s samples -> 10 minutes

  // WoW class colors (https://wowpedia.fandom.com/wiki/Class_colors)
  const CLASS_COLORS = {
    warrior: '#c79c6e', paladin: '#f58cba', hunter: '#abd473', rogue: '#fff569',
    priest: '#ffffff', shaman: '#0070de', mage: '#69ccf0', warlock: '#9482c9',
    druid: '#ff7d0a', unknown: '#8b949e'
  };

  function classColor(cls) {
    const key = String(cls || '').toLowerCase().trim();
    return CLASS_COLORS[key] || CLASS_COLORS.unknown;
  }

  // Power bar label follows the bot's resource: rage, energy, focus, mana.
  function powerLabel(b) {
    const t = (b && b.power_type) || 'power';
    return t.charAt(0).toUpperCase() + t.slice(1);
  }

  function hexToRgba(hex, alpha) {
    const h = hex.replace('#', '');
    const full = h.length === 3 ? h.split('').map(c => c + c).join('') : h;
    const n = parseInt(full, 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`;
  }

  // DOM Elements
  const el = {
    statusPill: document.getElementById('status-pill'),
    statusLabel: document.getElementById('status-label'),
    metricTick: document.getElementById('metric-tick'),
    metricBotsOnline: document.getElementById('metric-bots-online'),
    metricHumansOnline: document.getElementById('metric-humans-online'),
    metricUptime: document.getElementById('metric-uptime'),
    snapshotAgeVal: document.getElementById('snapshot-age-val'),

    // Shell & navigation
    menuItems: document.querySelectorAll('.sidebar-menu .menu-item[data-tab]'),
    tabViews: document.querySelectorAll('.tab-view'),
    sidebar: document.getElementById('sidebar'),
    sidebarBackdrop: document.getElementById('sidebar-backdrop'),
    hamburgerBtn: document.getElementById('hamburger-btn'),
    pageTitle: document.getElementById('page-title'),
    quickFind: document.getElementById('quick-find'),
    quickFindInput: document.getElementById('quick-find-input'),
    quickFindResults: document.getElementById('quick-find-results'),

    // Map
    zoneFilterInput: document.getElementById('zone-filter-input'),
    zoneFilterCount: document.getElementById('zone-filter-count'),
    zoneSelect: document.getElementById('zone-select'),
    mapImg: document.getElementById('map-img'),
    zoneNoart: document.getElementById('zone-noart'),
    zoneViewport: document.getElementById('zone-viewport'),
    mapOverlay: document.getElementById('map-overlay'),
    mapCanvas: document.getElementById('map-canvas'),
    mapTooltip: document.getElementById('map-tooltip'),
    mapLegend: document.getElementById('map-legend'),
    toggleTrails: document.getElementById('toggle-trails'),
    zoneView: document.getElementById('zone-view'),
    worldView: document.getElementById('world-view'),
    worldViewport: document.getElementById('world-viewport'),
    worldImg: document.getElementById('world-img'),
    worldCanvas: document.getElementById('world-canvas'),
    worldOverlay: document.getElementById('world-overlay'),
    worldTab0: document.getElementById('world-tab-0'),
    worldTab1: document.getElementById('world-tab-1'),
    worldCount0: document.getElementById('world-count-0'),
    worldCount1: document.getElementById('world-count-1'),
    worldZones: document.getElementById('world-zones'),
    worldChipsWrapper: document.getElementById('world-chips-wrapper'),
    toggleZoneChips: document.getElementById('toggle-zone-chips'),
    zoneChipsCount: document.getElementById('zone-chips-count'),
    worldOffmap: document.getElementById('world-offmap'),

    // Bots
    botSearch: document.getElementById('bot-search'),
    presenceSeg: document.getElementById('bots-presence'),
    viewSeg: document.getElementById('bots-view'),
    stateChips: document.getElementById('state-chips'),
    classChips: document.getElementById('class-chips'),
    rosterHead: document.getElementById('roster-head'),
    rosterTable: document.getElementById('roster-table-body'),
    rosterCount: document.getElementById('roster-count'),
    rosterNote: document.getElementById('roster-note'),

    // Progress
    progressKpis: document.getElementById('progress-kpis'),
    levelBands: document.getElementById('level-bands'),
    levelTimeline: document.getElementById('level-timeline-body'),

    // Activity
    activityMetrics: document.getElementById('activity-metrics'),
    activityWindow: document.getElementById('activity-window'),
    feedSeg: document.getElementById('feed-seg'),
    lootTable: document.getElementById('loot-table-body'),
    lootQualityFilter: document.getElementById('loot-quality-filter'),
    lootClassFilter: document.getElementById('loot-class-filter'),
    lootMinLevel: document.getElementById('loot-min-level'),
    lootBotSearch: document.getElementById('loot-bot-search'),
    questsTable: document.getElementById('quests-table-body'),
    questBotSearch: document.getElementById('quest-bot-search'),

    // Bot profile slide-over
    profileDrawer: document.getElementById('profile-drawer'),
    profileBackdrop: document.getElementById('profile-backdrop'),
    closeProfile: document.getElementById('close-profile'),
    profileTabs: document.getElementById('profile-tabs'),
    armoryMapBtn: document.getElementById('armory-map-btn'),
    armoryOnline: document.getElementById('armory-online'),
    armoryName: document.getElementById('armory-name'),
    armorySubtitle: document.getElementById('armory-subtitle'),
    armorySpecInfo: document.getElementById('armory-spec-info'),
    armoryHpBar: document.getElementById('armory-hp-bar'),
    armoryHpText: document.getElementById('armory-hp-text'),
    armoryPowBar: document.getElementById('armory-pow-bar'),
    armoryPowText: document.getElementById('armory-pow-text'),
    armoryXpWrap: document.getElementById('armory-xp-wrap'),
    armoryXpBar: document.getElementById('armory-xp-bar'),
    armoryXpText: document.getElementById('armory-xp-text'),
    armoryMoney: document.getElementById('armory-money'),
    armoryIlvl: document.getElementById('armory-ilvl'),
    armoryPlayed: document.getElementById('armory-played'),
    armoryError: document.getElementById('armory-error'),
    armoryBody: document.getElementById('armory-body'),
    gearLeft: document.getElementById('armory-gear-left'),
    gearRight: document.getElementById('armory-gear-right'),
    gearWeapons: document.getElementById('armory-gear-weapons'),
    gearSummary: document.getElementById('armory-gear-summary'),

    // Incidents
    anomaliesTable: document.getElementById('anomalies-table-body'),
    anomaliesTotals: document.getElementById('anomaly-totals-line'),
    typeFilter: document.getElementById('anomaly-type-filter'),
    severityFilter: document.getElementById('anomaly-severity-filter'),
    clearAnomalies: document.getElementById('clear-anomalies'),

    // Issues
    issuesSeg: document.getElementById('issues-seg'),
    issueActive: document.getElementById('issue-active'),
    issuePersistent: document.getElementById('issue-persistent'),
    issueWatch: document.getElementById('issue-watch'),
    issueResolved: document.getElementById('issue-resolved'),
    issueDeaths: document.getElementById('issue-deaths'),
    issuesTable: document.getElementById('issues-table-body'),
    issueTypeFilter: document.getElementById('issue-type-filter'),
    issueDurationFilter: document.getElementById('issue-duration-filter'),
    issuesResolved: document.getElementById('issues-resolved'),
    issueZones: document.getElementById('issue-zones'),
    issueChart: document.getElementById('issue-chart'),
    issueLegend: document.getElementById('issue-legend'),
    issuesCount: document.getElementById('issues-count'),
    metricIssues: document.getElementById('metric-issues'),
    metricIssuesSub: document.getElementById('metric-issues-sub'),

    // Overview
    activityPanel: document.getElementById('activity-panel'),
    fleetHealth: document.getElementById('fleet-health'),

    // Chart & Console
    activityChart: document.getElementById('activity-chart'),
    tickChart: document.getElementById('tick-chart'),
    tickNow: document.getElementById('tick-now'),
    consoleBody: document.getElementById('console-body'),
    consoleRate: document.getElementById('console-rate'),
    refreshBtn: document.getElementById('refresh-btn')
  };

  // All dynamic values rendered via innerHTML must pass through this.
  function esc(value) {
    if (value === null || value === undefined) return '';
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function formatUptime(seconds) {
    seconds = Math.max(0, parseInt(seconds, 10) || 0);
    if (seconds < 60) return `${seconds}s`;
    const m = Math.floor(seconds / 60);
    const h = Math.floor(m / 60);
    const remM = m % 60;
    if (h > 0) return `${h}h ${remM}m`;
    return `${m}m`;
  }

  // English names for DBC areas that have bounds but no extracted artwork
  // (mostly Turtle custom zones). Sources: Shyalya locales_area.sql for 12,
  // web research for Balor/Northwind, provisional deDE translations for
  // 5601/5602. Areas with no name anywhere keep the "Zone N" fallback.
  let EXTRA_ZONE_NAMES = {};
  // ZONE_BOUNDS (from /data/zones.json, the DBC bounds) is assigned by
  // fetchZoneBounds below and doubles as the last-resort name source:
  // zone_maps.json only covers zones with artwork, so custom zones like
  // Moonwhisper (5642) would otherwise render as "Zone 5642".

  function getZoneName(zoneId, mapId) {
    if (zoneId === null || zoneId === undefined) return '-';
    const zid = Number(zoneId);
    const zone = ZONE_CONFIG[zid];
    if (zone) return displayZoneName(zone.name);
    const extra = EXTRA_ZONE_NAMES[zid];
    if (extra) return displayZoneName(extra.name);
    if (ZONE_BOUNDS) {
      // area_id repeats across maps (e.g. 721 = GnomereganEntrance on map 0
      // vs Gnomeregan on map 90): prefer the caller's map, else the record
      // whose key map matches, else first match.
      const mid = mapId === undefined || mapId === null ? NaN : Number(mapId);
      let fallback = null;
      for (const key in ZONE_BOUNDS) {
        const zb = ZONE_BOUNDS[key];
        if (!zb || Number(zb.area_id) !== zid || !zb.name) continue;
        if (Number(zb.map_id) === mid) return displayZoneName(zb.name);
        if (!fallback && Number(key.split('_')[0]) === mid) fallback = zb;
        if (!fallback) fallback = zb;
      }
      if (fallback) return displayZoneName(fallback.name);
    }
    return `Zone ${zoneId}`;
  }

  // DBC area names are CamelCase internals (SwampOfSorrows, ThunderBluff,
  // AhnQiraj, GMIsland, ScarletMonastery2f). Insert spaces at case and
  // letter/digit boundaries so every text surface reads the same. Short
  // all-caps runs stay glued (AhnQiraj -> "Ahn Qiraj", not "Ahn Q Ira j")
  // and small words keep natural casing ("Swamp of Sorrows").
  function displayZoneName(name) {
    if (!name) return name;
    // Keep a trailing floor/digit suffix glued ("ScarletMonastery2f" ->
    // "Scarlet Monastery 2f"); only a letter-followed-by-digit run splits.
    // "RuinsofAhnQiraj" carries a DBC typo for "Ruins of"; fix it here so
    // the one label reads correctly everywhere.
    const spaced = String(name)
      .replace(/^RuinsofAhnQiraj$/, 'Ruins of Ahn Qiraj')
      .replace(/([a-z])([A-Z])/g, '$1 $2')
      .replace(/([a-zA-Z])(\d+[a-z]*)$/g, '$1 $2')
      .replace(/\s+/g, ' ')
      .trim();
    const words = spaced.split(' ');
    return words
      .map((w, idx) => (/^(of|the)$/i.test(w) && idx > 0 ? w.toLowerCase() : w))
      .join(' ');
  }

  // GetSelectedUnit() returns the bot itself while it has no hostile target
  // (follow/idle/wander states), so the wire target is the bot's own name.
  // Render that as "self", never as if the bot were fighting itself.
  function displayTarget(b) {
    const t = b && b.target;
    if (!t) return '-';
    if (b && b.name && t === b.name) return 'self';
    return t;
  }
  // XP bar: next_xp == 0 means "no data" (old emitter, max level) — render
  // nothing rather than a fake 0% or 100% bar.
  function xpPct(b) {
    if (!b || !b.next_xp) return null;
    const pct = Math.round((b.xp || 0) / b.next_xp * 100);
    return Math.min(100, Math.max(0, pct));
  }

  function fmtXpRate(v) {
    if (!v || v <= 0) return '–';
    if (v >= 1000000) return `${(v / 1000000).toFixed(1)}M/h`;
    if (v >= 1000) return `${(v / 1000).toFixed(1)}k/h`;
    return `${Math.round(v)}/h`;
  }

  function fmtGainAge(sec) {
    if (sec === null || sec === undefined || sec < 0) return 'no gain yet';
    if (sec < 60) return `${Math.round(sec)}s ago`;
    const m = Math.floor(sec / 60);
    if (m < 60) return `${m}m ago`;
    return `${Math.floor(m / 60)}h ${m % 60}m ago`;
  }

  // The telemetry role is the AI combat-role signal (forced role, combat
  // strategies, else talent/gear marks), not a group slot.
  function roleLabel(b) {
    const role = (b && b.role) || 'dps';
    return role.toUpperCase();
  }

  function appendConsoleLog(time, tag, text, level = 'info') {
    if (!el.consoleBody) return;
    const line = document.createElement('div');
    line.className = 'log-line';
    const tagClass = level === 'warn' ? 'warn' : level === 'error' ? 'error' : '';
    // text may contain intentional markup from callers; all telemetry-derived
    // strings are escaped before they reach this point.
    line.innerHTML = `<span class="log-time">[${esc(time)}]</span> <span class="log-tag ${tagClass}">[${esc(tag)}]</span> ${text}`;
    el.consoleBody.appendChild(line);
    while (el.consoleBody.children.length > 200) {
      el.consoleBody.removeChild(el.consoleBody.firstChild);
    }
    el.consoleBody.scrollTop = el.consoleBody.scrollHeight;
  }

  // ---- Navigation & routing ----------------------------------------------
  // The URL hash is the single source of truth for "where am I":
  //   #/<tab>                      a tab
  //   #/<tab>?bot=<guid>&p=<ptab>  a tab with a bot profile open on <ptab>
  // so the Back button, reloads and pasted links all land on the same view.
  const TABS = {
    overview: { hash: 'overview', title: 'Overview' },
    roster: { hash: 'bots', title: 'Bots' },
    map: { hash: 'map', title: 'Map' },
    progress: { hash: 'progress', title: 'Progress' },
    activity: { hash: 'activity', title: 'Activity' },
    issues: { hash: 'issues', title: 'Issues' },
    server: { hash: 'server', title: 'Server' }
  };
  const HASH_TO_TAB = Object.fromEntries(Object.entries(TABS).map(([k, v]) => [v.hash, k]));
  const PROFILE_TABS = ['overview', 'gear', 'talents', 'spells', 'professions', 'skills', 'bags', 'progress'];

  function openNavDrawer() {
    if (!el.sidebar) return;
    el.sidebar.classList.add('open');
    if (el.sidebarBackdrop) el.sidebarBackdrop.classList.add('active');
    if (el.hamburgerBtn) el.hamburgerBtn.setAttribute('aria-expanded', 'true');
  }

  function closeNavDrawer() {
    if (!el.sidebar) return;
    el.sidebar.classList.remove('open');
    if (el.sidebarBackdrop) el.sidebarBackdrop.classList.remove('active');
    if (el.hamburgerBtn) el.hamburgerBtn.setAttribute('aria-expanded', 'false');
  }

  function parseRoute() {
    const [path, query] = window.location.hash.replace(/^#\/?/, '').split('?');
    const q = new URLSearchParams(query || '');
    const bot = parseInt(q.get('bot'), 10);
    return { tab: HASH_TO_TAB[path] || null, bot: Number.isFinite(bot) ? bot : null, ptab: q.get('p') };
  }

  function routeHash(r) {
    const q = new URLSearchParams();
    if (r.bot !== null && r.bot !== undefined) {
      q.set('bot', r.bot);
      if (r.ptab && r.ptab !== 'overview') q.set('p', r.ptab);
    }
    const qs = q.toString();
    return `#/${TABS[r.tab].hash}${qs ? `?${qs}` : ''}`;
  }

  // Change the route: switching tab closes the profile unless a bot is given;
  // switching only the profile tab replaces the history entry.
  function go(partial, opts) {
    const next = Object.assign({ tab: state.activeTab, bot: state.profileGuid, ptab: state.profileTab }, partial);
    if (partial.tab && !('bot' in partial)) next.bot = null;
    const hash = routeHash(next);
    if (opts && opts.replace) {
      history.replaceState(null, '', hash);
      applyRoute();
    } else if (window.location.hash === hash) {
      applyRoute();
    } else {
      window.location.hash = hash; // hashchange -> applyRoute
    }
  }

  let routeApplied = false;
  function applyRoute() {
    const r = parseRoute();
    const tab = r.tab || state.activeTab;
    if (tab !== state.activeTab || !routeApplied) showTab(tab);
    routeApplied = true;
    if (r.bot !== null) {
      state.profileTab = PROFILE_TABS.includes(r.ptab) ? r.ptab : 'overview';
      showProfile(r.bot);
    } else {
      hideProfile();
    }
  }

  function paintNavActive() {
    const tab = TABS[state.activeTab];
    el.menuItems.forEach(item => item.classList.toggle('active', item.dataset.tab === state.activeTab));
    if (el.pageTitle) el.pageTitle.textContent = tab.title;
    document.title = `${tab.title} — TortoiseBots`;
    try { localStorage.setItem(TAB_STORAGE_KEY, state.activeTab); } catch (e) {}
  }

  // Per-tab render. Hidden tabs are rebuilt on their own activation, never on
  // every 2 s snapshot.
  function renderTab(tab) {
    if (tab === 'overview') { renderOverviewCharts(); renderStates(); renderAttention(); }
    else if (tab === 'map') renderMap();
    else if (tab === 'roster') renderBotsTab();
    else if (tab === 'progress') renderProgress();
    else if (tab === 'activity') renderActivityTab();
    else if (tab === 'issues') { renderIssues(); renderAnomalies(); }
    else if (tab === 'server') renderServerPanel();
  }

  function showTab(tab) {
    if (!TABS[tab]) tab = 'overview';
    state.activeTab = tab;
    closeNavDrawer();
    el.tabViews.forEach(v => {
      v.style.display = v.id === `tab-${tab}` ? 'block' : 'none';
    });
    paintNavActive();
    renderTab(tab);
    if (tab === 'roster' && Date.now() - state.rosterAt > 15000) fetchRoster();
    if (tab === 'activity') fetchFeed();
    if (tab === 'issues') fetchAnomalyTotals();
  }

  el.menuItems.forEach(item => item.addEventListener('click', closeNavDrawer));
  if (el.hamburgerBtn) {
    el.hamburgerBtn.addEventListener('click', () => {
      if (el.sidebar && el.sidebar.classList.contains('open')) closeNavDrawer(); else openNavDrawer();
    });
  }
  if (el.sidebarBackdrop) {
    el.sidebarBackdrop.addEventListener('click', closeNavDrawer);
  }
  window.addEventListener('hashchange', applyRoute);

  function isTyping(t) {
    return !!t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
  }

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      closeNavDrawer();
      if (state.profileGuid !== null && !isTyping(e.target)) closeProfile();
    } else if (e.key === '/' && !isTyping(e.target) && el.quickFindInput) {
      e.preventDefault();
      el.quickFindInput.focus();
      el.quickFindInput.select();
    }
  });

  let resizeTimer = null;
  function onWindowResize() {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      if (state.activeTab === 'map') renderMap();
      else if (state.activeTab === 'overview') renderOverviewCharts();
      else if (state.activeTab === 'issues') renderIssueChart();
    }, 100);
  }
  window.addEventListener('resize', onWindowResize);
  window.addEventListener('orientationchange', onWindowResize);

  if (el.refreshBtn) {
    el.refreshBtn.addEventListener('click', () => {
      fetchBots(true);
      fetchAnomalies();
      fetchIssues(true);
      fetchActivity();
      fetchRoster();
      if (state.activeTab === 'activity') fetchFeed();
      if (state.profileGuid !== null) fetchProfile(state.profileGuid);
    });
  }

  // Segmented control: switches the view of ONE panel.
  function paintSeg(seg, value) {
    seg.querySelectorAll('.seg-btn[data-v]').forEach(b => b.classList.toggle('active', b.dataset.v === value));
  }

  function bindSeg(seg, onChange) {
    if (!seg) return;
    seg.addEventListener('click', (e) => {
      const btn = e.target.closest('.seg-btn[data-v]');
      if (!btn) return;
      paintSeg(seg, btn.dataset.v);
      onChange(btn.dataset.v);
    });
  }

  // Find-a-bot (topbar, or press "/"): opens the profile from any tab.
  function initQuickFind() {
    const input = el.quickFindInput, box = el.quickFindResults;
    if (!input || !box) return;
    let items = [];
    let active = 0;
    const close = () => { box.hidden = true; };
    const paint = () => {
      box.querySelectorAll('.qf-row').forEach((r, i) => r.classList.toggle('active', i === active));
    };
    const render = () => {
      const q = input.value.trim().toLowerCase();
      if (!q) { close(); return; }
      const startsWith = r => (r.name.toLowerCase().startsWith(q) ? 0 : 1);
      items = botRows()
        .filter(r => r.name.toLowerCase().includes(q))
        .sort((a, b) => startsWith(a) - startsWith(b) || Number(b.online) - Number(a.online) || a.name.localeCompare(b.name))
        .slice(0, 8);
      active = 0;
      box.innerHTML = items.length
        ? items.map(r => `<div class="qf-row" role="option" data-guid="${esc(r.guid)}"><span class="qf-name" style="color: ${classColor(r.cls)};">${esc(r.name)}</span><span class="qf-meta">L${esc(r.level)} ${esc(capClass(r.cls))} · ${r.online ? 'online' : 'offline'}</span></div>`).join('')
        : '<div class="qf-empty">No bot matches.</div>';
      box.hidden = false;
      paint();
    };
    const pick = (guid) => {
      close();
      input.value = '';
      input.blur();
      openProfile(guid);
    };
    input.addEventListener('input', render);
    input.addEventListener('focus', render);
    input.addEventListener('blur', close);
    input.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        if (!items.length) return;
        active = (active + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
        paint();
      } else if (e.key === 'Enter') {
        if (items[active]) pick(items[active].guid);
      } else if (e.key === 'Escape') {
        input.value = '';
        close();
        input.blur();
      }
    });
    // mousedown (not click): the input's blur would hide the list first.
    box.addEventListener('mousedown', (e) => {
      const row = e.target.closest('.qf-row[data-guid]');
      if (!row) return;
      e.preventDefault();
      pick(parseInt(row.dataset.guid, 10));
    });
  }

  // Zone Selector & Filtering
  function zoneContinentName(mapId) {
    if (mapId === 0) return 'Eastern Kingdoms';
    if (mapId === 1) return 'Kalimdor';
    return 'Other maps';
  }

  // All zones the map can actually show: artwork zones from ZONE_CONFIG
  // plus live-only zones from the current roster and the DBC bounds, keyed
  // by numeric zone id. Without this a custom zone opened from a chip
  // (e.g. Moonwhisper 5642) has no <option>, so the select cannot hold it
  // and a later filter keystroke ejects the user to the first artwork zone.
  function zoneSelectEntries() {
    const entries = new Map();
    Object.entries(ZONE_CONFIG).forEach(([id, z]) => {
      entries.set(Number(id), z);
    });
    const addLive = (zid, mapId) => {
      const id = Number(zid);
      if (!Number.isFinite(id) || entries.has(id)) return;
      entries.set(id, { name: getZoneName(id, mapId), map: Number(mapId) || 0 });
    };
    state.bots.forEach(b => addLive(b.zone, b.map));
    if (ZONE_BOUNDS) {
      for (const key in ZONE_BOUNDS) {
        const zb = ZONE_BOUNDS[key];
        if (zb) addLive(zb.area_id, zb.map_id);
      }
    }
    return [...entries.entries()].map(([id, z]) => [String(id), z]);
  }

  function populateZoneSelect(filter = '') {
    if (!el.zoneSelect) return;
    const q = filter.trim().toLowerCase();
    el.zoneSelect.innerHTML = '';
    const worldOpt = document.createElement('option');
    worldOpt.value = 'world';
    worldOpt.textContent = '🌍 World (one continent at a time)';
    el.zoneSelect.appendChild(worldOpt);
    const sorted = zoneSelectEntries().sort((a, b) => a[1].name.localeCompare(b[1].name));
    const matching = sorted.filter(([id, z]) => !q || z.name.toLowerCase().includes(q));
    const groups = [[0, []], [1, []], [-1, []]];
    matching.forEach(([id, z]) => {
      const g = z.map === 0 ? groups[0] : z.map === 1 ? groups[1] : groups[2];
      g[1].push([id, z]);
    });
    groups.forEach(([mapId, entries]) => {
      if (!entries.length) return;
      const og = document.createElement('optgroup');
      og.label = zoneContinentName(mapId);
      entries.forEach(([id, z]) => {
        const opt = document.createElement('option');
        opt.value = id;
        opt.textContent = displayZoneName(z.name);
        og.appendChild(opt);
      });
      el.zoneSelect.appendChild(og);
    });
    if (el.zoneFilterCount) {
      el.zoneFilterCount.textContent = q ? `${matching.length}/${sorted.length} zones` : `${sorted.length} zones`;
    }
    if (state.mapView === 'world') {
      el.zoneSelect.value = 'world';
      return;
    }
    // The open zone always has an <option> now (zoneSelectEntries covers
    // live/bounds zones), so hold it even when the text filter excludes it —
    // filtering the list must not eject the user from the zone they are in.
    const current = String(state.currentZoneId);
    if (![...el.zoneSelect.options].some(o => o.value === current)) {
      const opt = document.createElement('option');
      opt.value = current;
      opt.textContent = getZoneName(state.currentZoneId);
      el.zoneSelect.appendChild(opt);
    }
    el.zoneSelect.value = current;
  }

  function initZoneSelector() {
    fetch('/data/zone_maps.json')
      .then(r => r.json())
      .then(cfg => {
        if (!cfg || Object.keys(cfg).length === 0) return;
        ZONE_CONFIG = cfg;
        populateZoneSelect(el.zoneFilterInput ? el.zoneFilterInput.value : '');
        loadZoneMap(state.currentZoneId);
      })
      .catch(() => {});

    if (el.zoneFilterInput) {
      el.zoneFilterInput.addEventListener('input', (e) => populateZoneSelect(e.target.value));
    }

    if (el.zoneSelect) {
      el.zoneSelect.addEventListener('change', (e) => {
        if (e.target.value === 'world') { setMapView('world'); return; }
        const zid = parseInt(e.target.value, 10);
        state.currentZoneId = zid;
        setMapView('zone', zid);
        loadZoneMap(zid);
      });
    }
    fetchZoneBounds();
    fetch('/data/zone_names.json')
      .then(r => r.json())
      .then(cfg => {
        if (cfg && Object.keys(cfg).length > 0) {
          EXTRA_ZONE_NAMES = cfg;
          if (state.activeTab === 'map') renderMap();
          if (state.activeTab === 'overview') renderAttention();
        }
      })
      .catch(() => {});
    if (el.worldTab0) el.worldTab0.addEventListener('click', () => setWorldTab(0));
    if (el.worldTab1) el.worldTab1.addEventListener('click', () => setWorldTab(1));
    if (el.toggleZoneChips && el.worldChipsWrapper) {
      el.toggleZoneChips.addEventListener('click', () => {
        const isExpanded = el.worldChipsWrapper.classList.toggle('expanded');
        el.toggleZoneChips.setAttribute('aria-expanded', isExpanded ? 'true' : 'false');
      });
    }
  }

  function loadZoneMap(zoneId) {
    const zone = ZONE_CONFIG[zoneId];
    const art = zone && zone.file;
    if (el.mapImg) {
      if (art) {
        if (el.mapImg.getAttribute('src') !== `/maps/${zone.file}`) el.mapImg.src = `/maps/${zone.file}`;
        el.mapImg.style.display = 'block';
      } else {
        el.mapImg.removeAttribute('src');
        el.mapImg.style.display = 'none';
        resetZoneArt();
      }
    }
    if (el.zoneViewport && !art) el.zoneViewport.style.aspectRatio = '1002 / 668';
    if (el.zoneNoart) {
      if (art) {
        el.zoneNoart.style.display = 'none';
      } else {
        el.zoneNoart.style.display = 'block';
        el.zoneNoart.innerHTML = `No map artwork for <strong style="color: #fff;">${esc(getZoneName(zoneId))}</strong> — dots positioned by DBC bounds.`;
      }
    }
    // No renderMap() here: every caller that switches zone goes through
    // setMapView (which renders) or switchTab('map') (which renders), so this
    // used to be a second full 500-dot render per zone switch.
  }

  // Zone art is not uniformly trimmed to the drawing: several files carry a
  // black border (a 3:2 map padded to a 4:3 canvas, or a letterboxed band).
  // Dots are placed at a percentage of the artwork, so a percentage of the
  // whole file drifts towards the padded edges — the bottom band of a 4:3 file
  // alone moves a dot ~10% down the map. Measure the artwork once per loaded
  // image and fit the viewport to it; a map that already fills its file (and
  // blank/garbage art) keeps the plain full-image fit.
  function artRegion(img) {
    const cw = img.naturalWidth, ch = img.naturalHeight;
    if (!cw || !ch) return null;
    let px;
    try {
      const c = document.createElement('canvas');
      c.width = cw; c.height = ch;
      const cx = c.getContext('2d', { willReadFrequently: true });
      cx.drawImage(img, 0, 0);
      px = cx.getImageData(0, 0, cw, ch).data;
    } catch (e) {
      return null; // canvas unavailable (tainted/blocked): keep the full image
    }
    let l = cw, t = ch, r = -1, b = -1, ink = 0;
    for (let y = 0; y < ch; y++) {
      for (let x = 0; x < cw; x++) {
        const i = (y * cw + x) * 4;
        if (px[i] + px[i + 1] + px[i + 2] <= 24) continue;
        ink++;
        if (x < l) l = x;
        if (x > r) r = x;
        if (y < t) t = y;
        if (y > b) b = y;
      }
    }
    // Require a real drawing: a dark or blank map is not a bordered one, and
    // trimming it would move the dots through the art instead of the padding.
    if (r < 0 || ink < cw * ch * 0.1 || (r - l + 1) < cw * 0.5 || (b - t + 1) < ch * 0.5) return null;
    if (l === 0 && t === 0 && r === cw - 1 && b === ch - 1) return null;
    return { cw, ch, l, t, w: r - l + 1, h: b - t + 1 };
  }

  // Drop the artwork-fit inline styles (kept when a zone has no map file).
  function resetZoneArt() {
    const img = el.mapImg;
    if (!img) return;
    img.style.position = '';
    img.style.left = '';
    img.style.top = '';
    img.style.width = '';
    img.style.height = '';
    img.style.objectFit = '';
    img.style.maxWidth = '';
    img.style.maxHeight = '';
  }

  function fitZoneArt() {
    const img = el.mapImg, vp = el.zoneViewport;
    if (!img || !vp || !img.naturalWidth) return;
    const art = artRegion(img);
    resetZoneArt();
    if (!art) {
      vp.style.aspectRatio = `${img.naturalWidth} / ${img.naturalHeight}`;
      return;
    }
    // The viewport shows exactly the artwork, so a dot at pct_x/pct_y lands on
    // the same percentage of the drawing; the image is scaled so its artwork
    // region fills the viewport and the padding is clipped.
    vp.style.aspectRatio = `${art.w} / ${art.h}`;
    img.style.position = 'absolute';
    img.style.left = `${-art.l / art.w * 100}%`;
    img.style.top = `${-art.t / art.h * 100}%`;
    img.style.width = `${art.cw / art.w * 100}%`;
    img.style.height = `${art.ch / art.h * 100}%`;
    img.style.objectFit = 'fill';
    img.style.maxWidth = 'none';
    img.style.maxHeight = 'none';
  }

  if (el.mapImg) {
    el.mapImg.addEventListener('load', () => {
      fitZoneArt();
      // The fit resizes the viewport, so the trail canvas must re-measure.
      if (state.activeTab === 'map') renderMap();
    });
    // The default src may already be decoded before this listener attaches.
    if (el.mapImg.complete && el.mapImg.naturalWidth) fitZoneArt();
  }

  // World view: both continents from WorldMapArea.dbc bounds (data/zones.json).
  let ZONE_BOUNDS = null; // key "map_area" -> {map_id, area_id, name, loc_*}
  function fetchZoneBounds() {
    fetch('/data/zones.json')
      .then(r => r.json())
      .then(cfg => {
        if (cfg && Object.keys(cfg).length > 0) {
          ZONE_BOUNDS = cfg;
          if (state.mapView === 'world') renderMap();
        }
      })
      .catch(() => {});
  }

  function continentBounds(mapId) {
    if (!ZONE_BOUNDS) return null;
    // The area_id 0 record is the continent itself: its rect is exactly what
    // the client world-map art (azeroth.webp / kalimdor.webp) spans, so dots
    // projected against it land on the artwork. Aggregate fallback only.
    const whole = ZONE_BOUNDS[mapId + '_0'];
    if (whole && whole.loc_left > whole.loc_right && whole.loc_top > whole.loc_bottom) {
      return { left: whole.loc_left, right: whole.loc_right, top: whole.loc_top, bottom: whole.loc_bottom };
    }
    let left = -Infinity, right = Infinity, top = -Infinity, bottom = Infinity, n = 0;
    for (const key in ZONE_BOUNDS) {
      const z = ZONE_BOUNDS[key];
      if (z.map_id !== mapId || z.area_id === 0) continue;
      if (!(z.loc_left > z.loc_right) || !(z.loc_top > z.loc_bottom)) continue;
      if (z.loc_left > left) left = z.loc_left;
      if (z.loc_right < right) right = z.loc_right;
      if (z.loc_top > top) top = z.loc_top;
      if (z.loc_bottom < bottom) bottom = z.loc_bottom;
      n++;
    }
    if (!n || left <= right || top <= bottom) return null;
    return { left, right, top, bottom };
  }

  function projectWorld(mapId, x, y) {
    const c = continentBounds(mapId);
    if (!c) return null;
    return {
      x: ((c.left - y) / (c.left - c.right)) * 100,
      y: ((c.top - x) / (c.top - c.bottom)) * 100
    };
  }


  function worldBots(mapId) {
    return state.bots.filter(b => b.map === mapId);
  }

  function setMapView(view, zoneId) {
    state.mapView = view;
    if (el.worldView) el.worldView.style.display = view === 'world' ? 'block' : 'none';
    if (el.zoneView) el.zoneView.style.display = view === 'world' ? 'none' : 'block';
    if (el.zoneSelect) el.zoneSelect.value = view === 'world' ? 'world' : String(zoneId !== undefined ? zoneId : state.currentZoneId);
    paintWorldTabs();
    renderMap();
  }

  function paintWorldTabs() {
    const mapId = state.worldMapId || 0;
    [[el.worldTab0, 0], [el.worldTab1, 1]].forEach(([tab, id]) => {
      if (!tab) return;
      const active = id === mapId && state.mapView === 'world';
      tab.style.borderColor = active ? 'var(--accent-green-bright)' : '';
      tab.style.color = active ? '#fff' : '';
    });
  }

  function setWorldTab(mapId) {
    state.worldMapId = mapId;
    const files = { 0: '/maps/azeroth.webp', 1: '/maps/kalimdor.webp' };
    if (el.worldImg && el.worldImg.getAttribute('src') !== files[mapId]) el.worldImg.src = files[mapId];
    setMapView('world');
  }


  function renderWorld() {
    const mapId = state.worldMapId || 0;
    const bots = renderWorldPanel(mapId);
    const off = state.bots.filter(b => b.map !== 0 && b.map !== 1);
    if (el.worldOffmap) {
      el.worldOffmap.textContent = off.length
        ? `${off.length} bot${off.length === 1 ? '' : 's'} in instances or other maps (see the Bots list).`
        : '';
    }
    renderMapLegend(bots);
  }

  // Zones on the active continent that currently hold bots, busiest first.
  // Empty zones vanish on the next render; click drills into the zone view.
  function renderWorldZones(mapId) {
    if (!el.worldZones) return;
    el.worldZones.innerHTML = '';
    const counts = new Map();
    state.bots.forEach(b => {
      if (b.map !== mapId) return;
      counts.set(`${b.map}_${b.zone}`, (counts.get(`${b.map}_${b.zone}`) || 0) + 1);
    });
    if (el.zoneChipsCount) {
      el.zoneChipsCount.textContent = counts.size;
    }
    [...counts.entries()].sort((a, b) => b[1] - a[1]).forEach(([key, n]) => {
      const [mapIdStr, zoneIdStr] = key.split('_');
      const zoneId = Number(zoneIdStr), zidMap = Number(mapIdStr);
      const chip = document.createElement('button');
      chip.className = 'btn';
      chip.style.cssText = 'padding: 4px 10px; font-size: 0.78rem; font-weight: 600; cursor: pointer;';
      chip.innerHTML = `${esc(getZoneName(zoneId, zidMap))} <span style="opacity: 0.6;">${n}</span>`;
      chip.addEventListener('click', () => openZone(zoneId));
      el.worldZones.appendChild(chip);
    });
  }

  function renderWorldPanel(mapId) {
    const canvas = el.worldCanvas;
    const overlay = el.worldOverlay;
    const viewport = el.worldViewport;
    if (!canvas || !overlay) return [];
    overlay.innerHTML = '';
    const bots = worldBots(mapId);
    const c = continentBounds(mapId);
    if (!c) return bots;
    const w = c.left - c.right, h = c.top - c.bottom;
    if (viewport && w > 0 && h > 0) viewport.style.aspectRatio = `${w} / ${h}`;
    const rect = canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return bots;
    if (canvas.width !== Math.floor(rect.width) || canvas.height !== Math.floor(rect.height)) {
      canvas.width = Math.floor(rect.width);
      canvas.height = Math.floor(rect.height);
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) return bots;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (state.showTrails) {
      bots.forEach(b => {
        if (!b.trail || b.trail.length < 2) return;
        ctx.beginPath();
        let started = false;
        b.trail.forEach(pt => {
          const p = projectWorld(mapId, pt.x, pt.y);
          if (!p) return;
          const px = (p.x / 100) * canvas.width, py = (p.y / 100) * canvas.height;
          if (!started) { ctx.moveTo(px, py); started = true; }
          else ctx.lineTo(px, py);
        });
        ctx.strokeStyle = hexToRgba(classColor(b.class), 0.45);
        ctx.lineWidth = 2;
        ctx.stroke();
      });
    }
    const issuesByGuid = issueSet();
    bots.forEach(b => {
      const p = projectWorld(mapId, b.x, b.y);
      if (!p) return;
      overlay.appendChild(makeBotDot(b, issuesByGuid[b.guid], p.x, p.y));
      if (b.guid === state.profileGuid) overlay.appendChild(makeSelectRing(p.x, p.y));
    });
    return bots;
  }


  if (el.toggleTrails) {
    el.toggleTrails.addEventListener('change', (e) => {
      state.showTrails = e.target.checked;
      renderMap();
    });
  }

  // Chart helpers -----------------------------------------------------------

  function prepCanvas(canvas) {
    if (!canvas || !canvas.parentElement) return null;
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    if (w <= 0 || h <= 0) return null;
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
      canvas.width = Math.floor(w * dpr);
      canvas.height = Math.floor(h * dpr);
    }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    return { ctx, w, h };
  }

  function drawGrid(ctx, w, h, padTop, padBottom, maxVal) {
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = padTop + (h - padTop - padBottom) * (i / 4);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }
    ctx.fillStyle = '#6e7681';
    ctx.font = '10px "JetBrains Mono", monospace';
    for (let i = 0; i <= 4; i++) {
      const val = maxVal * (1 - i / 4);
      const y = padTop + (h - padTop - padBottom) * (i / 4);
      ctx.fillText(String(Math.round(val)), 3, i === 0 ? y + 9 : y - 2);
    }
  }

  function drawTimeAxis(ctx, w, h) {
    ctx.fillStyle = '#6e7681';
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillText('10m ago', 3, h - 2);
    ctx.textAlign = 'right';
    ctx.fillText('now', w - 2, h - 2);
    ctx.textAlign = 'left';
  }

  // Population timeline (bots + players), one sample per heartbeat.
  function renderActivityChart() {
    const canvas = el.activityChart;
    const prepared = prepCanvas(canvas);
    if (!prepared) return;
    const { ctx, w, h } = prepared;

    const bots = state.history.bots;
    const humans = state.history.humans;
    const padTop = 12, padBottom = 16;

    if (bots.length === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '12px Inter';
      ctx.fillText('Collecting telemetry...', 12, h / 2);
      drawTimeAxis(ctx, w, h);
      return;
    }

    const maxVal = Math.max(10, ...bots, ...humans) * 1.15;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = bots.length > 1 ? w / (bots.length - 1) : 0;
    const xFor = i => bots.length > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    // Bots: filled area + line
    if (bots.length > 1) {
      ctx.beginPath();
      ctx.moveTo(xFor(0), h - padBottom);
      bots.forEach((v, i) => ctx.lineTo(xFor(i), yFor(v)));
      ctx.lineTo(xFor(bots.length - 1), h - padBottom);
      ctx.closePath();
      const grad = ctx.createLinearGradient(0, 0, 0, h);
      grad.addColorStop(0, 'rgba(88, 166, 255, 0.28)');
      grad.addColorStop(1, 'rgba(88, 166, 255, 0.0)');
      ctx.fillStyle = grad;
      ctx.fill();
    }

    ctx.beginPath();
    bots.forEach((v, i) => {
      if (i === 0) ctx.moveTo(xFor(i), yFor(v));
      else ctx.lineTo(xFor(i), yFor(v));
    });
    ctx.strokeStyle = '#58a6ff';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Players line
    ctx.beginPath();
    humans.forEach((v, i) => {
      if (i === 0) ctx.moveTo(xFor(i), yFor(v));
      else ctx.lineTo(xFor(i), yFor(v));
    });
    ctx.strokeStyle = '#2ea043';
    ctx.lineWidth = 2;
    ctx.stroke();

    if (bots.length === 1) {
      [[bots[0], '#58a6ff'], [humans[0], '#2ea043']].forEach(([v, color]) => {
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(xFor(0), yFor(v || 0), 3, 0, Math.PI * 2);
        ctx.fill();
      });
    }

    // End labels
    const last = bots.length - 1;
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillStyle = '#58a6ff';
    ctx.textAlign = 'right';
    ctx.fillText(String(bots[last]), w - 3, yFor(bots[last]) - 4);
    ctx.fillStyle = '#2ea043';
    ctx.fillText(String(humans[last]), w - 3, yFor(humans[last]) - 4);
    ctx.textAlign = 'left';

    drawTimeAxis(ctx, w, h);
  }

  // World tick timeline with nominal (50ms) and lag (100ms) references.
  // Player lag thresholds (server side, before the player's own ping): under
  // 100 ms keeps a typical 50 ms-ping player under 150 ms in total.
  const LAG_GOOD_MS = 100;
  const LAG_BAD_MS = 150;
  const lagColor = v => v > LAG_BAD_MS ? '#f85149' : v > LAG_GOOD_MS ? '#d29922' : '#3fb950';

  function renderTickChart() {
    const canvas = el.tickChart;
    const prepared = prepCanvas(canvas);
    if (!prepared) return;
    const { ctx, w, h } = prepared;

    const series = state.history.diff;
    const median = state.history.lag50;
    const padTop = 8, padBottom = 14;

    if (series.length === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '11px Inter';
      ctx.fillText('Collecting telemetry...', 12, h / 2);
      return;
    }

    const maxVal = Math.max(LAG_BAD_MS, ...series) * 1.1;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = series.length > 1 ? w / (series.length - 1) : 0;
    const xFor = i => series.length > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = 'rgba(210, 153, 34, 0.6)';
    ctx.beginPath(); ctx.moveTo(0, yFor(LAG_GOOD_MS)); ctx.lineTo(w, yFor(LAG_GOOD_MS)); ctx.stroke();
    ctx.strokeStyle = 'rgba(248, 81, 73, 0.6)';
    ctx.beginPath(); ctx.moveTo(0, yFor(LAG_BAD_MS)); ctx.lineTo(w, yFor(LAG_BAD_MS)); ctx.stroke();
    ctx.setLineDash([]);

    const line = (values, color) => {
      ctx.beginPath();
      values.forEach((v, i) => {
        if (i === 0) ctx.moveTo(xFor(i), yFor(v));
        else ctx.lineTo(xFor(i), yFor(v));
      });
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.stroke();
    };
    line(median, 'rgba(88, 166, 255, 0.8)');
    line(series, '#f0883e');

    if (series.length === 1) {
      ctx.fillStyle = '#f0883e';
      ctx.beginPath();
      ctx.arc(xFor(0), yFor(series[0]), 3, 0, Math.PI * 2);
      ctx.fill();
    }

    const last = Math.round(series[series.length - 1]);
    const lastMedian = Math.round(median[median.length - 1] || 0);
    if (el.tickNow) {
      el.tickNow.textContent = `p95 ${last} ms · median ${lastMedian} ms`;
      el.tickNow.style.color = lagColor(last);
    }
  }

  function renderOverviewCharts() {
    renderActivityChart();
    renderTickChart();
  }

  // ---- Overview ------------------------------------------------------------
  // Overview answers "is everything running?": KPI tiles, the two timelines,
  // the live-state census (each row jumps to those bots) and a short list of
  // what needs attention. It holds no bot table and no XP/gear analysis.

  // The one authoritative state census (counts + %, same snapshot as the Bots
  // list); a row is a shortcut into the Bots list filtered to that state.
  function renderStates() {
    const host = el.activityPanel;
    if (!host) return;
    const g = state.grinding || {};
    const counts = g.state_counts || {};
    const n = g.bots_tracked || state.bots.length || 0;
    if (!n) {
      host.innerHTML = '<div class="empty-hint">Waiting for bot roster...</div>';
      return;
    }
    host.innerHTML = STATE_ORDER.map(key => {
      const c = counts[key] || 0;
      const pct = Math.round((c / n) * 100);
      return `<div class="comp-row clickable" data-state="${key}" title="${esc(STATE_TITLES[key] || key)} — list these bots"><span class="comp-name">${key}</span><span class="comp-bar-bg"><span class="comp-bar" style="width: ${pct}%; background: ${STATE_COLORS[key]};"></span></span><span class="comp-count">${c} · ${pct}%</span></div>`;
    }).join('');
  }

  function renderAttention() {
    const host = el.fleetHealth;
    if (!host) return;
    if (!state.bots.length && !state.issues.active.length) {
      host.innerHTML = '<div class="empty-hint">Waiting for bot roster...</div>';
      return;
    }
    const active = state.issues.active.slice().sort((a, b) => (b.duration_sec || 0) - (a.duration_sec || 0));
    // A missing max_hp means "unknown", not "full health".
    let low = 0;
    state.bots.forEach(b => {
      const pct = b.max_hp ? b.hp / b.max_hp : null;
      if (b.state !== 'dead' && b.hp !== 0 && pct !== null && pct < 0.35) low++;
    });
    let html = active.length ? '' : '<div class="attention-ok">✓ No active issues</div>';
    html += active.slice(0, 6).map(i => `
      <div class="attention-row clickable" data-guid="${esc(i.guid)}">
        <span class="badge ${i.severity === 'persistent' ? 'badge-error' : 'badge-warn'}">${esc(ISSUE_LABELS[i.type] || i.type)}</span>
        <strong class="bot-name">${esc(i.bot)}</strong>
        <span class="attention-meta">${esc(getZoneName(i.zone, i.map))} · ${esc(fmtDuration(i.duration_sec))}</span>
      </div>`).join('');
    if (active.length > 6) html += `<a class="card-link attention-row" href="#/issues">+${active.length - 6} more issues →</a>`;
    if (low) html += `<div class="attention-row"><span class="badge badge-warn">Low HP</span><span>${low} bot${low === 1 ? '' : 's'} below 35% health</span></div>`;
    host.innerHTML = html;
  }

  // ---- Progress ------------------------------------------------------------
  // Progress answers "are the bots levelling?": the pool XP/death rollup, the
  // level bands with gear item level, and the level-up timeline.
  function kpiTile(label, value, sub, valueClass, title) {
    return `<div class="kpi"${title ? ` title="${esc(title)}"` : ''}><div class="kpi-label">${esc(label)}</div><div class="kpi-value ${valueClass || ''}">${value}</div><div class="kpi-sub">${esc(sub)}</div></div>`;
  }

  function renderProgressKpis() {
    const host = el.progressKpis;
    if (!host) return;
    const g = state.grinding || {};
    if (!g.bots_tracked) {
      host.innerHTML = '<div class="empty-hint">Waiting for XP samples...</div>';
      return;
    }
    host.innerHTML =
      kpiTile('Gaining XP', `${esc(g.bots_gaining_xp)}/${esc(g.bots_tracked)}`, `${Math.round(g.pct_gaining_xp || 0)}% in 10 min`, 'kpi-green', 'Quest turn-ins, discovery and kills all count, not just combat') +
      kpiTile('Median XP / hour', esc(fmtXpRate(g.median_xp_hour)), 'all tracked bots', '', 'Idle bots included') +
      kpiTile('Total XP / hour', esc(fmtXpRate(g.total_xp_hour)), 'whole pool', '') +
      kpiTile('Deaths / min', esc((g.deaths_per_min || 0).toFixed(1)), 'last 10 min', (g.deaths_per_min || 0) > 0 ? 'kpi-amber' : '', 'BOT_DEATH bot deaths per minute') +
      kpiTile('Died in 5 min', `${Math.round(g.pct_died_5min || 0)}%`, 'of the pool', '', 'Distinct bots, not death events');
  }

  // One row per level band with the band's bot count and the average equipped
  // item level from the daemon's gear sweep, with its ratio to the band's
  // level so "is gear keeping up?" is one glance. "–" until the first sweep
  // covers a band.
  function renderLevelBands() {
    const bands = el.levelBands;
    if (!bands) return;
    const g = state.grinding || {};
    if (!g.bots_tracked) {
      bands.innerHTML = '<div class="empty-hint">Waiting for XP samples...</div>';
      return;
    }
    const lb = Array.isArray(g.level_bands) ? g.level_bands : [];
    if (!lb.length) {
      bands.innerHTML = '<div class="empty-hint">No level bands yet.</div>';
      return;
    }
    const max = Math.max(1, ...lb.map(b => b.count || 0));
    const label = b => (b.lo === b.hi ? `L${b.lo}` : `${b.lo}-${b.hi}`);
    const gear = b => {
      if (!b.gear_bots) return '<span class="comp-gear" title="No gear sweep has covered these bots yet">–</span>';
      const mid = (b.lo + b.hi) / 2;
      const ratio = mid > 0 ? b.avg_item_level / mid : 0;
      return `<span class="comp-gear" title="Average equipped item level of the ${b.gear_bots} swept bots in this band, and its ratio to level ${mid}">ilvl ${b.avg_item_level.toFixed(1)} · ${ratio.toFixed(2)}×</span>`;
    };
    bands.innerHTML = lb.map(b => `
      <div class="comp-row"><span class="comp-name">${esc(label(b))}</span><span class="comp-bar-bg"><span class="comp-bar" style="width: ${Math.round(((b.count || 0) / max) * 100)}%; background: #2ea043;"></span></span><span class="comp-count">${b.count || 0}</span>${gear(b)}</div>`).join('');
  }

  function renderProgress() {
    renderProgressKpis();
    renderLevelBands();
    renderLevelTimeline();
  }

  // Art-less zones (custom zones like Northwind) open a dark zone view with
  // an explanatory badge: bounds exist so dots still plot, only paint is missing.
  // Leaves the profile alone when already on the map; showOnMap passes the bot
  // so the profile stays open on the map.
  function openZone(zoneId, botGuid) {
    state.currentZoneId = zoneId;
    setMapView('zone', zoneId);
    loadZoneMap(zoneId);
    if (state.activeTab !== 'map' || botGuid !== undefined) go(botGuid !== undefined ? { tab: 'map', bot: botGuid } : { tab: 'map' });
  }

  // Server panel: effective running settings from the SERVER_INFO payload —
  // live getters, never config files. Each setting is one key/value row
  // (label left, value right, monospace values, ON/OFF badges aligned) laid
  // out as sections that reflow into columns, so no label ever wraps mid-word
  // and nothing overflows the card.
  function flagBadge(v) {
    return v === '1' || v === 1 || v === true
      ? '<span class="badge badge-success">on</span>'
      : '<span class="badge">off</span>';
  }

  // rows: [label, valueHtml, confKeyOrHint]
  function settingsSection(title, rows) {
    return `<div class="settings-col">
      <div class="section-label" style="margin: 0 0 8px;">${esc(title)}</div>
      <div class="settings-rows">${rows.map(([label, value, hint]) =>
        `<div class="setting-row"${hint ? ` title="${esc(hint)}"` : ''}><span class="setting-key">${esc(label)}</span><span class="setting-val">${value}</span></div>`).join('')}</div>
    </div>`;
  }

  function renderServerPanel() {
    const host = document.getElementById('server-panel');
    if (!host) return;
    const info = state.serverInfo;
    if (!info) {
      host.innerHTML = '<div class="empty-hint">Waiting for server info (sent at startup, then every 5 min)...</div>';
      return;
    }
    const rates = info.rates || {}, bots = info.bots || {}, diag = info.diagnostics || {};
    const num = v => (v === undefined || v === null || v === '' ? '–' : esc(v));
    const cfg = k => `AiPlayerbot.${k}`;
    host.innerHTML = `
      <div class="server-versions">
        <span><span class="setting-dim">TortoiseBots</span> <strong class="mono">${esc(info.module_version || '?')}</strong></span>
        <span><span class="setting-dim">core</span> <strong class="mono">${esc(info.core_revision || '?')}</strong> <span class="setting-dim">${esc(info.core_date || '')}</span></span>
        <span><span class="setting-dim">max level</span> <strong class="mono">${num(info.max_level)}</strong></span>
      </div>
      <div class="settings-grid">
        ${settingsSection('RATES · CORE WORLD', [
          ['XP kill', num(rates.xp_kill), 'CONFIG_FLOAT_RATE_XP_KILL'],
          ['XP elite', num(rates.xp_kill_elite), 'CONFIG_FLOAT_RATE_XP_KILL_ELITE'],
          ['XP quest', num(rates.xp_quest), 'CONFIG_FLOAT_RATE_XP_QUEST'],
          ['XP explore', num(rates.xp_explore), 'CONFIG_FLOAT_RATE_XP_EXPLORE'],
          ['Drop money', num(rates.drop_money), 'CONFIG_FLOAT_RATE_DROP_MONEY'],
          ['Honor', num(rates.honor), 'CONFIG_FLOAT_RATE_HONOR'],
          ['Rep gain', num(rates.rep_gain), 'CONFIG_FLOAT_RATE_REPUTATION_GAIN'],
          ['Talent', num(rates.talent), 'CONFIG_FLOAT_RATE_TALENT']
        ])}
        ${settingsSection('BOTS · POOL', [
          ['Min/max random', `${num(bots.min_random)} / ${num(bots.max_random)}`, `${cfg('MinRandomBots')} / ${cfg('MaxRandomBots')}`],
          ['Update interval', `${num(bots.update_interval)} ms`, cfg('RandomBotUpdateInterval')],
          ['Max level', num(bots.max_level), cfg('RandomBotMaxLevel')],
          ['Active alone', num(bots.active_alone), cfg('botActiveAlone')],
          ['Pool budget', `${num(bots.pool_budget_us)} µs / ${num(bots.pool_budget_gate_ms)} ms`, `${cfg('PoolTickBudgetUs')} / ${cfg('PoolBudgetWhenTickOverMs')}`]
        ])}
        ${settingsSection('BOTS · TUNING', [
          ['XP rate', num(rates.bot_xp_mult), cfg('XPRate')],
          ['Loot rate uncommon', num(bots.bot_loot_uncommon), cfg('BotLootRateUncommon')],
          ['Loot rate rare', num(bots.bot_loot_rare), cfg('BotLootRateRare')],
          ['AH market enabled', flagBadge(bots.ah_market), cfg('AhMarketEnabled')],
          ['Auto-learn trainer spells', flagBadge(bots.auto_learn_trainer_spells), cfg('AutoLearnTrainerSpells')],
          ['Auto-learn quest spells', flagBadge(bots.auto_learn_quest_spells), cfg('AutoLearnQuestSpells')],
          ['Level-up mounts', flagBadge(bots.level_up_mounts), cfg('LevelUpMounts')],
          ['Turtle mount at level', num(bots.turtle_mount_at_level), cfg('TurtleMountAtLevel')]
        ])}
        ${settingsSection('BOTS · GROUPS', [
          ['Group nearby', flagBadge(bots.group_nearby), cfg('RandomBotGroupNearby')],
          ['Raid nearby', flagBadge(bots.raid_nearby), cfg('RandomBotRaidNearby')],
          ['Invite player', flagBadge(bots.invite_player), cfg('RandomBotInvitePlayer')],
          ['Timed logout', flagBadge(bots.timed_logout), cfg('RandomBotTimedLogout')],
          ['Force active near', flagBadge(bots.force_active_near), cfg('ForceActiveWhenNearPlayer')],
          ['Limit combat', flagBadge(bots.limit_combat), cfg('LimitCombatActivity')],
          ['Auto quests', flagBadge(bots.auto_do_quests), cfg('AutoDoQuests')]
        ])}
        ${settingsSection('BOTS · WORLD', [
          ['Avoid towns', flagBadge(bots.avoid_towns), cfg('AvoidHostileTowns')],
          ['Leave zones', flagBadge(bots.leave_zones), cfg('LeaveOutgrownZones')],
          ['LFT', flagBadge(bots.lft), cfg('RandomBotLftEnabled')],
          ['Battlegrounds', flagBadge(bots.bg), cfg('RandomBotBgEnabled')],
          ['AH buyer', flagBadge(bots.ah_buyer), cfg('AhMarketBuyer')],
          ['Random levels off', flagBadge(bots.disable_random_levels), cfg('DisableRandomLevels')],
          ['Level ladder', flagBadge(bots.level_ladder), cfg('LevelLadder')],
          ['Activity priorities off', flagBadge(bots.disable_activity), cfg('DisableActivityPriorities')]
        ])}
        ${settingsSection('DIAGNOSTICS · LOGS', [
          ['PerfMon', flagBadge(diag.perf_mon), cfg('PerfMonEnabled')],
          ['bot_events log', flagBadge(diag.bot_events), 'forwarded activity stream (bot_events.csv)'],
          ['unreachable log', flagBadge(diag.unreachable), 'unreachable_targets.csv'],
          ['deaths log', flagBadge(diag.deaths), 'deaths.csv']
        ])}
      </div>`;
  }

  // Pool gear summary for the diagnostic report: average equipped item level
  // over the swept bots plus the quality census of their equipped pieces.
  function gearDiagLine() {
    const geared = state.bots.filter(b => b.gear && b.gear.pieces > 0);
    if (!geared.length) return 'no gear sweep yet';
    let sum = 0;
    const q = [0, 0, 0, 0, 0];
    geared.forEach(b => {
      sum += b.gear.item_level;
      q[0] += b.gear.grey; q[1] += b.gear.white; q[2] += b.gear.green;
      q[3] += b.gear.blue; q[4] += b.gear.epic;
    });
    return `bots=${geared.length} avg_ilvl=${(sum / geared.length).toFixed(1)} pieces=${q.reduce((a, b) => a + b, 0)} grey=${q[0]} white=${q[1]} green=${q[2]} blue=${q[3]} epic=${q[4]}`;
  }

  // Compact plain-text diagnostic report for bug reports (GitHub/Discord).
  // Versions + effective settings + pool health only — no IPs, hosts,
  // account names, or passwords anywhere in this payload.
  function buildDiagReport() {
    const info = state.serverInfo || {};
    const rates = info.rates || {}, bots = info.bots || {}, diag = info.diagnostics || {};
    const g = state.grinding || {};
    // Unknown (key absent from the emitter's payload) reads "?", not "off", so
    // a report from an older server cannot claim a setting is disabled.
    const on = v => (v === '1' || v === 1 || v === true) ? 'on'
      : (v === '0' || v === 0 || v === false) ? 'off' : '?';
    return [
      '# TortoiseBots diagnostic report',
      `- module: ${info.module_version || '?'} / core: ${info.core_revision || '?'} (${info.core_date || '?'}) / max level: ${info.max_level || '?'}`,
      `- rates: xp_kill=${rates.xp_kill ?? '?'} xp_elite=${rates.xp_kill_elite ?? '?'} xp_quest=${rates.xp_quest ?? '?'} xp_explore=${rates.xp_explore ?? '?'} drop_money=${rates.drop_money ?? '?'} honor=${rates.honor ?? '?'} rep=${rates.rep_gain ?? '?'} talent=${rates.talent ?? '?'} bot_xp_mult=${rates.bot_xp_mult ?? '?'}`,
      `- bots: min/max=${bots.min_random ?? '?'}/${bots.max_random ?? '?'} update=${bots.update_interval ?? '?'} maxlvl=${bots.max_level ?? '?'} group=${on(bots.group_nearby)} raid=${on(bots.raid_nearby)} invite=${on(bots.invite_player)} timed_logout=${on(bots.timed_logout)} no_rand_levels=${on(bots.disable_random_levels)} ladder=${on(bots.level_ladder)} quests=${on(bots.auto_do_quests)} no_activity=${on(bots.disable_activity)} alone=${bots.active_alone ?? '?'} pool=${bots.pool_budget_us ?? '?'}us/${bots.pool_budget_gate_ms ?? '?'}ms ah=${on(bots.ah_buyer)} lft=${on(bots.lft)} bg=${on(bots.bg)}`,
      `- tuning: xp_rate=${rates.bot_xp_mult ?? '?'} loot_uncommon=${bots.bot_loot_uncommon ?? '?'} loot_rare=${bots.bot_loot_rare ?? '?'} ah_market=${on(bots.ah_market)} autolearn_trainer=${on(bots.auto_learn_trainer_spells)} autolearn_quest=${on(bots.auto_learn_quest_spells)} levelup_mounts=${on(bots.level_up_mounts)} turtle_mount_lvl=${bots.turtle_mount_at_level ?? '?'}`,
      `- diag: perfmon=${on(diag.perf_mon)} bot_events=${on(diag.bot_events)} unreachable=${on(diag.unreachable)} deaths=${on(diag.deaths)}`,
      `- pool: tracked=${g.bots_tracked || 0} gaining=${g.bots_gaining_xp || 0} (${Math.round(g.pct_gaining_xp || 0)}%) median_xp/h=${Math.round(g.median_xp_hour || 0)} total_xp/h=${Math.round(g.total_xp_hour || 0)} deaths/min=${(g.deaths_per_min || 0).toFixed(1)} died5m=${Math.round(g.pct_died_5min || 0)}% states=${['combat', 'moving', 'busy', 'stalled', 'resting', 'idle', 'dead'].map(k => `${k}=${((g.state_counts || {})[k]) || 0}`).join(' ')}`,
      `- levels: ${(Array.isArray(g.level_bands) ? g.level_bands : []).map(b => `${b.lo === b.hi ? `L${b.lo}` : `${b.lo}-${b.hi}`}=${b.count || 0}${b.gear_bots ? `/ilvl${b.avg_item_level.toFixed(1)}` : ''}`).join(' ')}`,
      `- gear: ${gearDiagLine()}`,
      `- issues: active=${state.issues.active.length} persistent=${state.issues.active.filter(i => i.severity === 'persistent').length}`,
      `- server: online=${state.server.online} stale=${state.server.stale} uptime=${state.server.uptime}s tick=${state.server.diff}ms humans=${state.server.humans} bots=${state.server.bots}`,
      `- activity: ${(() => { const sm = (state.activity && state.activity.summary) || null; const c = sm && sm.counters; if (!c) return 'no data'; return `window=since ${sm.since_str || '?'} quests=${c.quests_rewarded || 0} handins=${c.quest_handins || 0} open=${c.open_quests || 0} loot=${c.loot_items || 0} notable=${c.notable_loot || 0} money=+${c.money_earned || 0}c/-${c.money_spent || 0}c sold=${c.sold_value || 0}c bought=${c.bought_value || 0}c kills=${c.kills || 0} deaths=${c.deaths || 0} ghost=${c.ghost_seconds || 0}s trainers=${c.trainer_visits || 0} spells=${c.spells_learned || 0} vendors=${c.vendor_visits || 0} repairs=${c.repairs || 0} giveups=${c.giveups || 0} gather=${c.gathering || 0} skin=${c.skinning || 0} ah=${c.ah_listings || 0}/${c.ah_bids || 0}`; })()}`,
    ].join('\n');
  }

  function initDiagButton() {
    const btn = document.getElementById('copy-diag-btn');
    if (!btn || btn.dataset.bound) return;
    btn.dataset.bound = '1';
    btn.addEventListener('click', async () => {
      const text = buildDiagReport();
      try {
        await navigator.clipboard.writeText(text);
        btn.textContent = 'Copied!';
      } catch (e) {
        btn.textContent = 'Copy failed';
        window.prompt('Diagnostic report (copy manually):', text);
      }
      setTimeout(() => { btn.textContent = 'Copy diagnostic report'; }, 2000);
    });
  }

  function updateSnapshotAge() {
    if (!el.snapshotAgeVal) return;
    if (!state.lastSnapshotAt) {
      el.snapshotAgeVal.textContent = '–';
      el.snapshotAgeVal.className = 'kpi-value snapshot-offline';
      return;
    }
    const age = (Date.now() - state.lastSnapshotAt) / 1000;
    el.snapshotAgeVal.textContent = age < 10 ? `${age.toFixed(1)}s` : `${Math.round(age)}s`;
    el.snapshotAgeVal.className = 'kpi-value ' +
      (age < 6 ? 'snapshot-fresh' : age < 10 ? 'snapshot-stale' : 'snapshot-offline');
  }

  function pushHistory() {
    const s = state.server;
    state.history.t.push(Date.now());
    state.history.bots.push(s.online ? s.bots : 0);
    state.history.humans.push(s.online ? s.humans : 0);
    // Older modules send no lag percentiles; fall back to the tick average.
    state.history.diff.push(s.lag_p95 || s.diff_avg || s.diff || 0);
    state.history.lag50.push(s.lag_p50 || s.diff_avg || s.diff || 0);
    if (state.history.t.length > HIST_MAX) {
      state.history.t.shift();
      state.history.bots.shift();
      state.history.humans.shift();
      state.history.diff.shift();
      state.history.lag50.shift();
    }
  }

  function setStatusPill() {
    if (!el.statusPill || !el.statusLabel) return;
    const s = state.server;
    el.statusPill.className = `status-pill ${s.online ? (s.stale ? 'stale' : 'online') : 'offline'}`;
    const word = s.online ? (s.stale ? 'snapshot stale' : 'running') : 'offline';
    // While the server is offline the last counts are history, not live state.
    el.statusLabel.innerHTML = `<span class="pill-word">${esc(word)} · </span><strong>${s.online ? s.bots : 0}</strong> bots<span class="pill-players"> · <strong>${s.online ? s.humans : 0}</strong> players</span>`;
  }

  function updateOverviewMetrics() {
    const s = state.server;
    const lag = s.lag_p95 || s.diff_avg || s.diff || 0;
    if (el.metricBotsOnline) el.metricBotsOnline.textContent = s.online ? s.bots : 0;
    if (el.metricHumansOnline) el.metricHumansOnline.textContent = s.online ? s.humans : 0;
    if (el.metricUptime) el.metricUptime.textContent = s.online ? `up ${formatUptime(s.uptime)}` : 'offline';
    if (el.metricTick) {
      el.metricTick.textContent = s.online ? `${Math.round(lag)} ms` : '–';
      el.metricTick.className = `kpi-value ${!s.online ? '' : lag > LAG_BAD_MS ? 'kpi-red' : lag > LAG_GOOD_MS ? 'kpi-amber' : 'kpi-green'}`;
    }
    const tickSub = document.getElementById('metric-tick-avg');
    if (tickSub && s.online) tickSub.textContent = s.lag_p50
      ? `median ${Math.round(s.lag_p50)} · worst tick ${Math.round(s.diff_worst)} ms`
      : `worst tick ${Math.round(s.diff_worst || s.diff || 0)} ms`;
    updateSnapshotAge();
    setStatusPill();
  }

  // A pulsing ring around the selected bot so it is easy to spot on the map
  // (the dot itself is clipped to a triangle, so the ring is its own element).
  function makeSelectRing(pctX, pctY) {
    const ring = document.createElement('div');
    ring.className = 'bot-select-ring';
    ring.style.left = `${pctX}%`;
    ring.style.top = `${pctY}%`;
    return ring;
  }

  function makeBotDot(b, issue, pctX, pctY) {
    const dot = document.createElement('div');
    dot.className = 'bot-dot';
    if (issue) dot.classList.add(issue.severity === 'persistent' ? 'issue-persistent' : 'issue-watch');
    if (b.guid === state.profileGuid) dot.classList.add('selected');
    // A corpse must not read as a live bot: the fill stays the class colour,
    // the dimming carries the state (same "dead" tone as STATE_COLORS).
    if (b.state === 'dead') dot.classList.add('corpse');
    dot.dataset.guid = b.guid;
    dot.style.left = `${pctX}%`;
    dot.style.top = `${pctY}%`;
    dot.style.backgroundColor = classColor(b.class);
    const deg = (b.o || 0) * (180 / Math.PI);
    dot.style.transform = `translate(-50%, -50%) rotate(${-deg}deg)`;
    return dot;
  }

  function positionMapTooltip(e) {
    if (!el.mapTooltip) return;
    const pad = 14;
    const tipRect = el.mapTooltip.getBoundingClientRect();
    let left = e.clientX + pad;
    let top = e.clientY + pad;
    if (left + tipRect.width > window.innerWidth - 6)
      left = e.clientX - tipRect.width - pad;
    if (top + tipRect.height > window.innerHeight - 6)
      top = e.clientY - tipRect.height - pad;
    el.mapTooltip.style.left = `${Math.max(6, left)}px`;
    el.mapTooltip.style.top = `${Math.max(6, top)}px`;
  }

  function showMapTooltip(b, e) {
    if (!el.mapTooltip) return;
    const issue = issueSet()[b.guid];
    const issueLine = issue
      ? `<br><span style="color: var(--accent-red);">Issue:</span> ${esc(ISSUE_LABELS[issue.type] || issue.type)} (${esc(fmtDuration(issue.duration_sec))})`
      : '';
    el.mapTooltip.innerHTML = `
      <strong style="color: #fff;">${esc(b.name)}</strong> (${esc(b.class)} Lvl ${esc(b.level)})<br>
      <span style="color: var(--text-muted);">Role:</span> ${esc(roleLabel(b))}<br>
      <span style="color: var(--text-muted);">Status:</span> ${esc(b.state || 'idle')}<br>
      ${b.killer ? `<span style="color: var(--text-muted);">Killed by:</span> ${esc(b.killer)}${b.killer_level ? ` (${b.killer_level})` : ''}<br>` : ''}
      <span style="color: var(--text-muted);">Zone:</span> ${esc(getZoneName(b.zone, b.map))}<br>
      <span style="color: var(--text-muted);">Target:</span> ${esc(displayTarget(b))}
      ${issueLine}
    `;
    el.mapTooltip.style.display = 'block';
    positionMapTooltip(e);
  }

  function hideMapTooltip() {
    if (el.mapTooltip) el.mapTooltip.style.display = 'none';
  }

  // One delegated listener set per map overlay. Both overlays rebuild up to
  // 500 dots every 2 s snapshot; per-dot listeners (3 each) were the bulk of
  // the map render cost.
  function bindMapOverlay(overlay) {
    if (!overlay || overlay.dataset.delegated) return;
    overlay.dataset.delegated = '1';
    const dotFor = e => (e.target.closest ? e.target.closest('.bot-dot') : null);
    const botFor = node => state.bots.find(b => b.guid === parseInt(node.dataset.guid, 10)) || null;
    overlay.addEventListener('mouseover', e => {
      const node = dotFor(e);
      if (!node) return;
      const b = botFor(node);
      if (b) showMapTooltip(b, e);
    });
    overlay.addEventListener('mousemove', e => {
      if (dotFor(e)) positionMapTooltip(e);
    });
    overlay.addEventListener('mouseout', e => {
      const node = dotFor(e);
      if (node && !node.contains(e.relatedTarget)) hideMapTooltip();
    });
    overlay.addEventListener('click', e => {
      const node = dotFor(e);
      if (!node) return;
      e.stopPropagation();
      openProfile(parseInt(node.dataset.guid, 10));
    });
  }

  // 2D Map Rendering
  function renderMap() {
    [0, 1].forEach(id => {
      const countEl = id === 0 ? el.worldCount0 : el.worldCount1;
      if (countEl) {
        const n = worldBots(id).length;
        countEl.textContent = n ? `· ${n}` : '';
      }
    });
    const chipMap = state.mapView === 'world'
      ? (state.worldMapId || 0)
      : ((ZONE_CONFIG[state.currentZoneId] || {}).map ?? state.worldMapId ?? 0);
    renderWorldZones(chipMap);
    if (state.mapView === 'world') { renderWorld(); return; }
    if (!el.mapOverlay) return;
    el.mapOverlay.innerHTML = '';

    const canvas = el.mapCanvas;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const rect = canvas.getBoundingClientRect();
    if (canvas.width !== rect.width || canvas.height !== rect.height) {
      canvas.width = rect.width;
      canvas.height = rect.height;
    }
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    const zoneBots = state.bots.filter(b => b.zone === state.currentZoneId && b.projected);

    if (state.showTrails) {
      zoneBots.forEach(b => {
        if (!b.trail || b.trail.length < 2) return;
        ctx.beginPath();
        b.trail.forEach((pt, idx) => {
          const px = (pt.pct_x / 100) * canvas.width;
          const py = (pt.pct_y / 100) * canvas.height;
          if (idx === 0) ctx.moveTo(px, py);
          else ctx.lineTo(px, py);
        });
        ctx.strokeStyle = hexToRgba(classColor(b.class), 0.45);
        ctx.lineWidth = 2;
        ctx.stroke();
      });
    }

    const issuesByGuid = issueSet();
    zoneBots.forEach(b => {
      el.mapOverlay.appendChild(makeBotDot(b, issuesByGuid[b.guid], b.pct_x, b.pct_y));
      if (b.guid === state.profileGuid) el.mapOverlay.appendChild(makeSelectRing(b.pct_x, b.pct_y));
    });

    renderMapLegend(zoneBots);
  }

  function renderMapLegend(zoneBots) {
    if (!el.mapLegend) return;
    const present = [...new Set(zoneBots.map(b => b.class))].sort();
    if (present.length === 0) {
      el.mapLegend.innerHTML = '<span style="color: var(--text-muted); font-size: 0.75rem;">No bots in this zone</span>';
      return;
    }
    el.mapLegend.innerHTML = present.map(cls =>
      `<span><i style="background: ${classColor(cls)};"></i>${esc(cls.charAt(0).toUpperCase() + cls.slice(1))}</span>`
    ).join('');
  }

  // ---- Bots: the one list of bots -------------------------------------------
  // Every bot character in the database (online or offline) joined with the
  // live roster by guid. Live telemetry wins for anything it carries; offline
  // bots show database values (level, spec, gold) and a dash for the rest.
  // Three column sets (Live / Progress / Activity) replace what used to be
  // separate roster, armory and per-bot activity tables.

  function capClass(c) {
    const s = String(c || '');
    return s ? s.charAt(0).toUpperCase() + s.slice(1).toLowerCase() : '–';
  }

  function makeBotRow(db, live) {
    return {
      guid: live ? live.guid : db.guid,
      name: live ? live.name : db.name,
      cls: String((live && live.class) || (db ? classNameById(db.class) : '') || 'unknown').toLowerCase(),
      level: live ? live.level : db.level,
      spec: db ? (db.spec || '') : '',
      money: db ? (parseInt(db.money, 10) || 0) : 0,
      online: !!live || !!(db && db.online),
      live,
      db
    };
  }

  function botRows() {
    const liveByGuid = new Map(state.bots.map(b => [b.guid, b]));
    const known = new Set();
    const rows = state.roster.map(d => {
      known.add(d.guid);
      return makeBotRow(d, liveByGuid.get(d.guid) || null);
    });
    state.bots.forEach(b => { if (!known.has(b.guid)) rows.push(makeBotRow(null, b)); });
    return rows;
  }

  const DASH = '<span class="cell-muted">–</span>';

  function stateTag(s) {
    const key = s || 'idle';
    return `<span class="state-tag" title="${esc(STATE_TITLES[key] || key)}"><i style="background: ${STATE_COLORS[key] || '#8b949e'};"></i>${esc(key)}</span>`;
  }

  // Compact bar cell: caption above a thin bar.
  function barCell(text, pct, color, title) {
    return `<div class="cell-bar"${title ? ` title="${esc(title)}"` : ''}><div class="cell-bar-text">${esc(text)}</div><div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${pct}%; background: ${color};"></div></div></div>`;
  }

  // Swept average equipped item level, with the quality split in the tooltip
  // ("–" until the first sweep covers the bot).
  function gearCell(b) {
    const g = b && b.gear;
    if (!g) return DASH;
    const title = `${g.pieces} equipped pieces · ${g.grey} grey / ${g.white} white / ${g.green} green / ${g.blue} blue / ${g.epic} epic`;
    return `<span class="mono" title="${esc(title)}">${esc(g.item_level.toFixed(1))}</span>`;
  }

  // Counter cell for the Activity view (session counters; offline bots have none).
  const actNum = (key) => ({
    sort: r => (r.live && r.live.activity ? r.live.activity[key] || 0 : -1),
    cell: r => (r.live && r.live.activity ? `<span class="mono">${esc(r.live.activity[key] || 0)}</span>` : DASH)
  });

  const BOT_COLS = {
    name: {
      label: 'Name',
      sort: r => r.name.toLowerCase(),
      cell: (r, ctx) => {
        const issue = ctx.issues[r.guid];
        const badge = issue
          ? ` <span class="badge ${issue.severity === 'persistent' ? 'badge-error' : 'badge-warn'}" title="${esc(ISSUE_LABELS[issue.type] || issue.type)}">${esc(fmtDuration(issue.duration_sec))}</span>`
          : '';
        return `<a class="bot-name bot-link" style="color: ${classColor(r.cls)};" href="${esc(routeHash({ tab: 'roster', bot: r.guid, ptab: state.profileTab }))}">${esc(r.name)}</a>${badge}`;
      }
    },
    class: {
      label: 'Class',
      sort: r => `${r.cls} ${r.spec}`.toLowerCase(),
      cell: r => `${esc(capClass(r.cls))}${r.spec ? ` <span class="cell-muted">${esc(r.spec)}</span>` : ''}`
    },
    level: { label: 'Lvl', sort: r => r.level || 0, cell: r => esc(r.level) },
    state: {
      label: 'Status',
      title: 'Live state from telemetry',
      sort: r => (r.live ? r.live.state || 'idle' : 'zzz'),
      cell: r => (r.live ? stateTag(r.live.state) : `<span class="cell-muted">${r.online ? '–' : 'offline'}</span>`)
    },
    hp: {
      label: 'Health',
      sort: r => (r.live && r.live.max_hp ? r.live.hp / r.live.max_hp : -1),
      cell: r => {
        const b = r.live;
        if (!b) return DASH;
        const pct = b.max_hp ? Math.min(100, Math.round((b.hp / b.max_hp) * 100)) : 100;
        return barCell(`${b.hp}/${b.max_hp} · ${pct}%`, pct, pct < 35 ? 'var(--accent-red)' : 'var(--accent-green-bright)');
      }
    },
    power: {
      label: 'Power',
      sort: r => (r.live && r.live.max_power ? r.live.power / r.live.max_power : -1),
      cell: r => {
        const b = r.live;
        if (!b) return DASH;
        const pct = b.max_power ? Math.min(100, Math.round((b.power / b.max_power) * 100)) : 0;
        const color = b.power_type === 'rage' ? '#f85149' : b.power_type === 'energy' ? '#e3b341' : 'var(--accent-blue-bright)';
        return barCell(`${powerLabel(b)} ${b.power}/${b.max_power}`, pct, color);
      }
    },
    target: {
      label: 'Target',
      sort: r => (r.live ? displayTarget(r.live).toLowerCase() : ''),
      cell: r => (r.live && r.live.target
        ? `<span class="target-cell">${esc(displayTarget(r.live))}</span>${r.live.target_level ? ` <span class="cell-muted">L${esc(r.live.target_level)}</span>` : ''}`
        : DASH)
    },
    zone: {
      label: 'Zone',
      sort: r => (r.live ? getZoneName(r.live.zone, r.live.map).toLowerCase() : ''),
      cell: r => (r.live ? `<span class="mono">${esc(getZoneName(r.live.zone, r.live.map))}</span>` : DASH)
    },
    xp: {
      label: 'XP',
      title: 'Progress into the current level, XP per hour and the age of the last gain (live bots)',
      sort: r => (r.live && r.live.next_xp ? (r.live.xp || 0) / r.live.next_xp : -1),
      cell: r => {
        const b = r.live;
        const pct = b ? xpPct(b) : null;
        if (pct === null) return DASH;
        const rate = fmtXpRate(b.xp_per_hour);
        return barCell(`${pct}% · ${rate}`, pct, 'var(--accent-yellow)', `${b.xp}/${b.next_xp} XP · ${rate} · last gain ${fmtGainAge(b.xp_gain_age_sec)}`);
      }
    },
    gear: {
      label: 'Gear',
      title: 'Average item level of equipped gear (shirt and tabard excluded)',
      sort: r => (r.live && r.live.gear ? r.live.gear.item_level : -1),
      cell: r => gearCell(r.live)
    },
    gold: { label: 'Gold', sort: r => r.money, cell: r => (r.db ? `<span class="mono">${formatMoney(r.money)}</span>` : DASH) },
    quests: Object.assign({ label: 'Quests', title: 'Quest rewards this game-server session' }, actNum('quests_rewarded')),
    loot: Object.assign({ label: 'Loot', title: 'Looted items this session' }, actNum('loot_items')),
    notable: Object.assign({ label: 'Notable', title: 'Green or better loot' }, actNum('notable_loot')),
    kills: Object.assign({ label: 'Kills' }, actNum('kills')),
    deaths: Object.assign({ label: 'Deaths' }, actNum('deaths')),
    money: {
      label: 'Money +/-',
      title: 'Copper earned / spent this session',
      sort: r => (r.live && r.live.activity ? r.live.activity.money_earned || 0 : -1),
      cell: r => {
        const a = r.live && r.live.activity;
        return a ? `<span class="mono" style="font-size: 0.74rem;">+${formatMoney(a.money_earned)}<br>-${formatMoney(a.money_spent)}</span>` : DASH;
      }
    },
    vendors: Object.assign({ label: 'Vendors' }, actNum('vendor_visits')),
    repairs: Object.assign({ label: 'Repairs' }, actNum('repairs')),
    giveups: Object.assign({ label: 'Give-ups', title: 'Targets or goals the bot abandoned' }, actNum('giveups')),
    gather: {
      label: 'Gather/Skin',
      sort: r => (r.live && r.live.activity ? (r.live.activity.gathering || 0) + (r.live.activity.skinning || 0) : -1),
      cell: r => {
        const a = r.live && r.live.activity;
        return a ? `<span class="mono">${esc(a.gathering || 0)}/${esc(a.skinning || 0)}</span>` : DASH;
      }
    },
    ah: {
      label: 'AH',
      title: 'Auction listings / bids',
      sort: r => (r.live && r.live.activity ? (r.live.activity.ah_listings || 0) + (r.live.activity.ah_bids || 0) : -1),
      cell: r => {
        const a = r.live && r.live.activity;
        return a ? `<span class="mono">${esc(a.ah_listings || 0)}/${esc(a.ah_bids || 0)}</span>` : DASH;
      }
    }
  };

  const BOT_VIEWS = {
    live: ['name', 'class', 'level', 'state', 'hp', 'power', 'target', 'zone'],
    progress: ['name', 'class', 'level', 'xp', 'gear', 'gold'],
    activity: ['name', 'class', 'level', 'quests', 'loot', 'notable', 'kills', 'deaths', 'money', 'vendors', 'repairs', 'giveups', 'gather', 'ah']
  };

  function renderBotChips(base, issues, stateOk, classOk) {
    const chip = (kind, value, label, count, dot, active, extra) =>
      `<button class="chip${active ? ' active' : ''}${extra || ''}" data-kind="${kind}" data-v="${esc(value)}">${dot ? `<i style="background: ${dot};"></i>` : ''}${esc(label)} <b>${count}</b></button>`;

    const forStates = base.filter(classOk);
    const stateCount = {};
    forStates.forEach(r => { if (r.live) { const k = r.live.state || 'idle'; stateCount[k] = (stateCount[k] || 0) + 1; } });
    const withIssues = base.filter(r => issues[r.guid]).length;
    el.stateChips.innerHTML =
      chip('state', 'all', 'Any state', forStates.length, '', state.botsState === 'all') +
      STATE_ORDER.filter(k => stateCount[k] || state.botsState === k)
        .map(k => chip('state', k, k[0].toUpperCase() + k.slice(1), stateCount[k] || 0, STATE_COLORS[k], state.botsState === k)).join('') +
      (withIssues || state.botsIssueOnly ? chip('issues', '1', 'With issues', withIssues, '#f85149', state.botsIssueOnly, ' chip-warn') : '');

    const forClasses = base.filter(stateOk);
    const classCount = {};
    forClasses.forEach(r => { classCount[r.cls] = (classCount[r.cls] || 0) + 1; });
    el.classChips.innerHTML =
      chip('class', 'all', 'All classes', forClasses.length, '', state.botsClass === 'all') +
      Object.entries(classCount).sort((a, b) => b[1] - a[1])
        .map(([c, n]) => chip('class', c, capClass(c), n, classColor(c), state.botsClass === c)).join('');
  }

  function renderRoster() {
    if (!el.rosterTable) return;
    paintSeg(el.presenceSeg, state.botsPresence);
    paintSeg(el.viewSeg, state.botsView);

    const rows = botRows();
    const issues = issueSet();
    const query = (el.botSearch ? el.botSearch.value : '').toLowerCase().trim();
    const presenceOk = r => state.botsPresence === 'all' || (state.botsPresence === 'online') === r.online;
    const queryOk = r => !query || r.name.toLowerCase().includes(query) || r.cls.includes(query) || r.spec.toLowerCase().includes(query);
    const stateOk = r => state.botsState === 'all' || (r.live && (r.live.state || 'idle') === state.botsState);
    const classOk = r => state.botsClass === 'all' || r.cls === state.botsClass;
    const issueOk = r => !state.botsIssueOnly || !!issues[r.guid];

    const base = rows.filter(r => presenceOk(r) && queryOk(r));
    renderBotChips(base, issues, stateOk, classOk);
    const list = base.filter(r => stateOk(r) && classOk(r) && issueOk(r));

    const cols = BOT_VIEWS[state.botsView] || BOT_VIEWS.live;
    const sortKey = cols.includes(state.botsSort.key) ? state.botsSort.key : null;
    const dir = state.botsSort.dir;
    const sortFn = sortKey ? BOT_COLS[sortKey].sort : r => 0;
    list.sort((a, b) => {
      if (!sortKey && a.online !== b.online) return a.online ? -1 : 1;
      const av = sortFn(a), bv = sortFn(b);
      if (av < bv) return -1 * dir;
      if (av > bv) return 1 * dir;
      return a.name.localeCompare(b.name);
    });

    const online = rows.filter(r => r.online).length;
    if (el.rosterCount) el.rosterCount.textContent = `${list.length === rows.length ? rows.length : `${list.length} of ${rows.length}`} bots`;
    if (el.rosterNote) {
      el.rosterNote.textContent = `${online} online · ${rows.length - online} offline` +
        (state.rosterError ? ' · offline bots unavailable (database list failed)' : '');
    }

    el.rosterHead.innerHTML = '<tr>' + cols.map(k => {
      const c = BOT_COLS[k];
      const active = k === sortKey;
      return `<th class="sortable${active ? ' sort-active' : ''}" tabindex="0" data-sort="${k}"${c.title ? ` title="${esc(c.title)}"` : ''}>${esc(c.label)}${active ? (dir > 0 ? ' ▲' : ' ▼') : ''}</th>`;
    }).join('') + '</tr>';

    if (!list.length) {
      el.rosterTable.innerHTML = `<tr><td colspan="${cols.length}" class="table-empty">${rows.length ? 'No bots match these filters.' : 'Waiting for bots…'}</td></tr>`;
      return;
    }
    const ctx = { issues };
    el.rosterTable.innerHTML = list.map(r =>
      `<tr class="row-link${r.online ? '' : ' row-offline'}${r.guid === state.profileGuid ? ' row-selected' : ''}" data-guid="${esc(r.guid)}">${cols.map(k => `<td>${BOT_COLS[k].cell(r, ctx)}</td>`).join('')}</tr>`
    ).join('');
  }

  function renderBotsTab() {
    renderRoster();
  }

  // Offline bots come from the database; refreshed on Bots activation, by the
  // Refresh button and once a minute (it is one indexed query, never per tick).
  let rosterInflight = false;
  function fetchRoster() {
    if (rosterInflight) return;
    rosterInflight = true;
    fetch('/api/v1/armory/bots')
      .then(jsonOrThrow)
      .then(bots => {
        if (!Array.isArray(bots)) throw new Error('bad payload');
        state.roster = bots;
        state.rosterAt = Date.now();
        state.rosterError = false;
      })
      .catch(() => { state.rosterError = true; })
      .finally(() => {
        rosterInflight = false;
        if (state.activeTab === 'roster') renderRoster();
      });
  }

  function initBots() {
    const saved = (() => { try { return localStorage.getItem(BOTS_VIEW_STORAGE_KEY); } catch (e) { return null; } })();
    if (saved && BOT_VIEWS[saved]) state.botsView = saved;

    if (el.botSearch) el.botSearch.addEventListener('input', renderRoster);
    bindSeg(el.presenceSeg, v => { state.botsPresence = v; renderRoster(); });
    bindSeg(el.viewSeg, v => {
      state.botsView = v;
      try { localStorage.setItem(BOTS_VIEW_STORAGE_KEY, v); } catch (e) {}
      renderRoster();
    });

    const onChip = (e) => {
      const btn = e.target.closest('.chip[data-kind]');
      if (!btn) return;
      const v = btn.dataset.v;
      if (btn.dataset.kind === 'state') state.botsState = state.botsState === v ? 'all' : v;
      else if (btn.dataset.kind === 'class') state.botsClass = state.botsClass === v ? 'all' : v;
      else state.botsIssueOnly = !state.botsIssueOnly;
      renderRoster();
    };
    el.stateChips.addEventListener('click', onChip);
    el.classChips.addEventListener('click', onChip);

    const sortBy = (th) => {
      const key = th.dataset.sort;
      if (state.botsSort.key === key) state.botsSort.dir *= -1;
      else state.botsSort = { key, dir: 1 };
      renderRoster();
    };
    el.rosterHead.addEventListener('click', e => { const th = e.target.closest('th.sortable'); if (th) sortBy(th); });
    el.rosterHead.addEventListener('keydown', e => {
      const th = e.target.closest('th.sortable');
      if (th && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); sortBy(th); }
    });

    // The name link navigates by itself (so ctrl-click works); any other cell
    // of the row opens the profile too.
    el.rosterTable.addEventListener('click', e => {
      if (e.target.closest('a')) return;
      const tr = e.target.closest('tr[data-guid]');
      if (tr && !window.getSelection().toString()) openProfile(parseInt(tr.dataset.guid, 10));
    });

    // Overview shortcuts: a state row lists those bots, an issue row opens its bot.
    if (el.activityPanel) {
      el.activityPanel.addEventListener('click', e => {
        const row = e.target.closest('[data-state]');
        if (!row) return;
        state.botsState = row.dataset.state;
        state.botsPresence = 'online';
        state.botsClass = 'all';
        state.botsIssueOnly = false;
        go({ tab: 'roster' });
      });
    }
    if (el.fleetHealth) {
      el.fleetHealth.addEventListener('click', e => {
        const row = e.target.closest('[data-guid]');
        if (row) openProfile(parseInt(row.dataset.guid, 10));
      });
    }
  }

  function fmtClock(ts) {
    if (!ts) return '–';
    return new Date(ts * 1000).toLocaleTimeString();
  }

  // The daemon's wall clock, derived from the counter window it reports as
  // both an epoch (since) and its own rendering (since_str): re-parsing that
  // string locally and offsetting by the epoch difference cancels both zones.
  // Payload fields that carry only an epoch still display the server's hour,
  // never the viewer's.
  function daemonClock(ts) {
    const s = state.activity && state.activity.summary;
    if (s && s.since && s.since_str) {
      const parsed = Date.parse(s.since_str.replace(' ', 'T'));
      if (Number.isFinite(parsed)) {
        return new Date(ts * 1000 + (parsed - s.since * 1000)).toLocaleTimeString();
      }
    }
    return fmtClock(ts);
  }

  // Feed rows carry the daemon's own rendered timestamp (time_str), so every
  // table on the dashboard shares one time base. Formatting the epoch in the
  // viewer's zone (the fallback) would make the feeds disagree with the
  // incident table whenever the browser is not on the server's clock.
  function feedClock(item) {
    if (item && item.time_str) return item.time_str.slice(11, 19);
    return daemonClock(item ? item.at : 0);
  }

  // Activity counters are session-scoped: they count the current game-server
  // session and are kept across a dashboard restart. Every panel that shows
  // them repeats this window so "19 loot" cannot read as a lifetime total.
  function counterWindow() {
    const s = state.activity && state.activity.summary;
    return (s && s.since_str) ? `since ${s.since_str.slice(11, 16)}` : 'this session';
  }

  function counterWindowTitle() {
    const s = state.activity && state.activity.summary;
    const start = (s && s.since_str) ? ` since ${s.since_str}` : '';
    return `Counters cover the current game-server session${start}. They are kept across a dashboard restart and reset when the game server restarts.`;
  }

  function botLink(guid, name, cls) {
    return `<a class="bot-link"${cls ? ` style="color: ${classColor(cls)};"` : ''} href="${esc(routeHash({ tab: state.activeTab, bot: guid, ptab: state.profileTab }))}">${esc(name)}</a>`;
  }

  function statGroup(title, rows) {
    return `<div class="stat-group"><h3>${esc(title)}</h3>${rows.map(([label, value, hint]) =>
      `<div class="stat-row"${hint ? ` title="${esc(hint)}"` : ''}><span>${esc(label)}</span><strong>${value}</strong></div>`).join('')}</div>`;
  }

  // Activity counters, grouped by theme: what bots did (quests, combat, loot,
  // gathering) and the money loop (earned/spent, vendors, repairs, auction
  // house). Per-bot numbers live in the Bots list (Activity columns).
  function renderActivityMetrics() {
    const host = el.activityMetrics;
    if (!host) return;
    const summary = state.activity && state.activity.summary;
    const c = summary && summary.counters;
    if (!c) {
      host.innerHTML = '<div class="empty-hint">Waiting for activity data...</div>';
      return;
    }
    const num = v => esc(v || 0);
    if (el.activityWindow) el.activityWindow.textContent = `POOL COUNTERS · ${counterWindow().toUpperCase()} · ${summary.bots_tracked || 0} TRACKED BOTS`;
    host.title = counterWindowTitle();
    host.innerHTML =
      statGroup('Quests', [
        ['Rewarded', num(c.quests_rewarded)],
        ['Hand-ins', num(c.quest_handins)],
        ['Finished, not handed in', num(c.open_quests)]
      ]) +
      statGroup('Combat', [
        ['Kills', num(c.kills)],
        ['Deaths', num(c.deaths)],
        ['Ghost time', esc(fmtDuration(c.ghost_seconds))],
        ['Give-ups', num(c.giveups)]
      ]) +
      statGroup('Loot & gathering', [
        ['Loot items', num(c.loot_items)],
        ['Notable loot', num(c.notable_loot), 'Green or better'],
        ['Gathering', num(c.gathering)],
        ['Skinning', num(c.skinning)],
        ['Skill-ups', num(c.skillups)]
      ]) +
      statGroup('Money', [
        ['Earned', formatMoney(c.money_earned)],
        ['Spent', formatMoney(c.money_spent)],
        ['Sold', `${num(c.items_sold)} · ${formatMoney(c.sold_value)}`],
        ['Bought', `${num(c.items_bought)} · ${formatMoney(c.bought_value)}`]
      ]) +
      statGroup('Services', [
        ['Vendor visits', num(c.vendor_visits)],
        ['Repairs', `${num(c.repairs)} · ${formatMoney(c.repair_cost)}`],
        ['Trainer visits', num(c.trainer_visits)],
        ['Spells learned', num(c.spells_learned)],
        ['AH listings / bids', `${num(c.ah_listings)} / ${num(c.ah_bids)}`]
      ]);
  }

  // Friendly names for the raw quest event ids the emitter forwards.
  const QUEST_EVENT_LABELS = {
    QuestRewarded: 'Rewarded', AcceptQuestAction: 'Accepted', QuestCompleted: 'Completed',
    QuestUpdateCompleteAction: 'Completed', QuestDropped: 'Dropped', TalkToQuestGiverAction: 'Hand-in'
  };
  const questEventLabel = e => QUEST_EVENT_LABELS[e] || e || 'quest';

  function classLevelCell(cls, level) {
    return `${esc(capClass(cls))} <span class="cell-muted">L${esc(level)}</span>`;
  }

  // Feeds: one panel, three views (quests / loot / live console).
  function showFeed(view) {
    state.feedView = view;
    paintSeg(el.feedSeg, view);
    document.querySelectorAll('[data-feed]').forEach(n => { n.hidden = n.dataset.feed !== view; });
    fetchFeed();
  }

  function fetchFeed() {
    if (state.feedView === 'quests') fetchQuestFeed();
    else if (state.feedView === 'loot') fetchLootFeed();
  }

  function lootFeedParams() {
    const parts = ['limit=200'];
    parts.push(`min_quality=${encodeURIComponent((el.lootQualityFilter && el.lootQualityFilter.value) || '2')}`);
    if (el.lootClassFilter && el.lootClassFilter.value) parts.push(`class=${encodeURIComponent(el.lootClassFilter.value)}`);
    if (el.lootMinLevel && el.lootMinLevel.value) parts.push(`min_level=${encodeURIComponent(el.lootMinLevel.value)}`);
    if (el.lootBotSearch && el.lootBotSearch.value.trim()) parts.push(`bot=${encodeURIComponent(el.lootBotSearch.value.trim())}`);
    return parts.join('&');
  }

  function fetchLootFeed() {
    if (!el.lootTable || state.activeTab !== 'activity' || state.feedView !== 'loot') return;
    fetch(`/api/v1/activity/loot?${lootFeedParams()}`)
      .then(jsonOrThrow)
      .then(rows => {
        state.lootFeed = Array.isArray(rows) ? rows : [];
        renderLootFeed();
      })
      .catch(() => {});
  }

  function lootItemCell(r) {
    if (r.source === 'money') return `<span class="mono">${formatMoney(r.money)}</span>`;
    const q = Number(r.quality) || 0;
    return `<span class="quality-text-${q}" title="${esc(qualityName(q))}">${esc(r.item || 'Item')}</span>${r.value ? ` <span class="mono cell-muted">(${formatMoney(r.value)})</span>` : ''}`;
  }

  function renderLootFeed() {
    if (!el.lootTable) return;
    const rows = state.lootFeed || [];
    if (!rows.length) {
      el.lootTable.innerHTML = '<tr><td colspan="6" class="table-empty">No loot rows match.</td></tr>';
      return;
    }
    el.lootTable.innerHTML = rows.map(r => `<tr>
      <td class="mono cell-time">${esc(feedClock(r))}</td>
      <td>${botLink(r.guid, r.bot, r.class)}</td>
      <td>${classLevelCell(r.class, r.level)}</td>
      <td><span class="badge badge-info">${esc(r.source || '-')}</span></td>
      <td>${lootItemCell(r)}</td>
      <td class="mono">${esc(getZoneName(r.zone, r.map))}</td>
    </tr>`).join('');
  }

  function fetchQuestFeed() {
    if (!el.questsTable || state.activeTab !== 'activity' || state.feedView !== 'quests') return;
    const parts = ['limit=200'];
    if (el.questBotSearch && el.questBotSearch.value.trim()) parts.push(`bot=${encodeURIComponent(el.questBotSearch.value.trim())}`);
    fetch(`/api/v1/activity/quests?${parts.join('&')}`)
      .then(jsonOrThrow)
      .then(rows => {
        state.questFeed = Array.isArray(rows) ? rows : [];
        renderQuestFeed();
      })
      .catch(() => {});
  }

  function renderQuestFeed() {
    if (!el.questsTable) return;
    const rows = state.questFeed || [];
    if (!rows.length) {
      el.questsTable.innerHTML = '<tr><td colspan="5" class="table-empty">No quest events match.</td></tr>';
      return;
    }
    el.questsTable.innerHTML = rows.map(r => `<tr>
      <td class="mono cell-time">${esc(feedClock(r))}</td>
      <td>${botLink(r.guid, r.bot, r.class)}</td>
      <td>${classLevelCell(r.class, r.level)}</td>
      <td><span class="badge badge-info">${esc(questEventLabel(r.event))}</span></td>
      <td>${esc(r.quest || '-')}${r.quest_id ? ` <span class="mono cell-muted">#${esc(r.quest_id)}</span>` : ''}</td>
    </tr>`).join('');
  }

  // Level-up timeline (Progress tab), newest first, capped so the DOM stays small.
  function renderLevelTimeline() {
    if (!el.levelTimeline) return;
    const feed = (state.activity && Array.isArray(state.activity.level_feed)) ? state.activity.level_feed.slice() : [];
    if (!feed.length) {
      el.levelTimeline.innerHTML = '<tr><td colspan="4" class="table-empty">No level-ups recorded yet.</td></tr>';
      return;
    }
    feed.sort((a, b) => (b.at || 0) - (a.at || 0));
    el.levelTimeline.innerHTML = feed.slice(0, 300).map(e => `<tr>
      <td class="mono cell-time">${esc(feedClock(e))}</td>
      <td>${botLink(e.guid, e.bot, e.class)}</td>
      <td>${esc(capClass(e.class))}</td>
      <td><span class="badge badge-success">L${esc(e.level)}</span></td>
    </tr>`).join('');
  }

  function renderActivityTab() {
    renderActivityMetrics();
    renderQuestFeed();
    renderLootFeed();
  }

  function fetchActivity() {
    fetch('/api/v1/activity')
      .then(jsonOrThrow)
      .then(data => {
        if (!data || !data.summary) return;
        state.activity = data;
        populateLootClassFilter();
        if (state.activeTab === 'activity') { renderActivityMetrics(); fetchFeed(); }
        if (state.activeTab === 'progress') renderLevelTimeline();
        if (state.profileGuid !== null && state.profileTab === 'progress' && state.profile) renderArmoryPanel(state.profile, 'progress');
      })
      .catch(() => {});
  }

  function populateLootClassFilter() {
    if (!el.lootClassFilter) return;
    const set = new Set();
    state.bots.forEach(b => { if (b.class) set.add(String(b.class).toLowerCase()); });
    if (state.activity && Array.isArray(state.activity.bots)) state.activity.bots.forEach(b => { if (b.class) set.add(String(b.class).toLowerCase()); });
    const classes = [...set].sort();
    const cur = el.lootClassFilter.value || '';
    el.lootClassFilter.innerHTML = '<option value="">All classes</option>' +
      classes.map(c => `<option value="${esc(c)}">${esc(capClass(c))}</option>`).join('');
    el.lootClassFilter.value = classes.includes(cur) ? cur : '';
  }

  let lootFilterTimer = null;
  let questFilterTimer = null;

  function initActivity() {
    bindSeg(el.feedSeg, showFeed);
    if (el.lootQualityFilter) el.lootQualityFilter.addEventListener('change', fetchLootFeed);
    if (el.lootClassFilter) el.lootClassFilter.addEventListener('change', fetchLootFeed);
    if (el.lootMinLevel) el.lootMinLevel.addEventListener('change', fetchLootFeed);
    if (el.lootBotSearch) el.lootBotSearch.addEventListener('input', () => {
      clearTimeout(lootFilterTimer);
      lootFilterTimer = setTimeout(fetchLootFeed, 300);
    });
    if (el.questBotSearch) el.questBotSearch.addEventListener('input', () => {
      clearTimeout(questFilterTimer);
      questFilterTimer = setTimeout(fetchQuestFeed, 300);
    });
  }

  function armoryActivity(guid) {
    if (state.activity && Array.isArray(state.activity.bots)) {
      const b = state.activity.bots.find(x => x.guid === guid);
      if (b && b.activity) return b.activity;
    }
    const live = state.bots.find(x => x.guid === guid);
    return (live && live.activity) || {};
  }

  function progressRow(label, valueHtml) {
    return `<div class="stat-card-row"><span>${esc(label)}</span><strong class="mono">${valueHtml}</strong></div>`;
  }

  function renderArmoryProgress(p) {
    const host = document.getElementById('armory-content-progress');
    if (!host) return;
    const guid = p && p.summary ? p.summary.guid : null;
    const a = armoryActivity(guid);
    const num = v => esc(v || 0);
    let html = '';
    if (!a || Object.keys(a).length === 0) {
      html += `<div class="empty-hint">No activity recorded for this bot yet.</div>`;
    } else {
      html += `
      <div class="empty-hint" style="margin-bottom: 12px;">Counters cover the current game-server session (${esc(counterWindow())}). They are kept across a dashboard restart and reset when the game server restarts.</div>
      <div class="armory-stat-card">
        <div class="stat-card-title">Quests</div>
        ${progressRow('Rewarded', num(a.quests_rewarded))}
        ${progressRow('Accepted', num(a.quests_accepted))}
        ${progressRow('Completed', num(a.quests_completed))}
        ${progressRow('Hand-ins', num(a.quest_handins))}
        ${progressRow('Open', num(a.open_quests))}
      </div>
      <div class="armory-stat-card">
        <div class="stat-card-title">Loot & Money</div>
        ${progressRow('Loot items', num(a.loot_items))}
        ${progressRow('Loot value', formatMoney(a.loot_value))}
        ${progressRow('Notable loot', num(a.notable_loot))}
        ${progressRow('Items sold', num(a.items_sold))}
        ${progressRow('Sold value', formatMoney(a.sold_value))}
        ${progressRow('Items bought', num(a.items_bought))}
        ${progressRow('Bought value', formatMoney(a.bought_value))}
        ${progressRow('Money earned', formatMoney(a.money_earned))}
        ${progressRow('Money spent', formatMoney(a.money_spent))}
      </div>
      <div class="armory-stat-card">
        <div class="stat-card-title">Combat</div>
        ${progressRow('Kills', num(a.kills))}
        ${progressRow('Deaths', num(a.deaths))}
        ${progressRow('Ghost time', esc(fmtDuration(a.ghost_seconds)))}
        ${progressRow('Levels gained', num(a.levels_gained))}
        ${progressRow('Events', num(a.events))}
      </div>
      <div class="armory-stat-card">
        <div class="stat-card-title">Services</div>
        ${progressRow('Trainer visits', num(a.trainer_visits))}
        ${progressRow('Spells learned', num(a.spells_learned))}
        ${progressRow('Vendor visits', num(a.vendor_visits))}
        ${progressRow('Repairs', num(a.repairs))}
        ${progressRow('Repair cost', formatMoney(a.repair_cost))}
        ${progressRow('Give-ups', num(a.giveups))}
      </div>
      <div class="armory-stat-card">
        <div class="stat-card-title">Gathering & AH</div>
        ${progressRow('Gathering', num(a.gathering))}
        ${progressRow('Skinning', num(a.skinning))}
        ${progressRow('Skill-ups', num(a.skillups))}
        ${progressRow('AH listings', num(a.ah_listings))}
        ${progressRow('AH bids', num(a.ah_bids))}
      </div>
      <div class="armory-stat-card" title="${esc(counterWindowTitle())}">
        <div class="stat-card-title">This session (${esc(counterWindow())})</div>
        ${progressRow('First seen', esc(daemonClock(a.first_seen)))}
        ${progressRow('Last event', esc(daemonClock(a.last_event)))}
      </div>`;
    }
    const feed = (state.activity && Array.isArray(state.activity.level_feed) ? state.activity.level_feed : [])
      .filter(e => e.guid === guid)
      .sort((x, y) => (x.at || 0) - (y.at || 0));
    if (feed.length) {
      const rows = feed.map((e, i) => {
        const gap = i === 0 ? '–' : fmtDuration((e.at || 0) - (feed[i - 1].at || 0));
        return `<tr><td class="mono" style="font-size: 0.75rem; color: var(--text-muted);">${esc(feedClock(e))}</td><td><span class="badge badge-success">L${esc(e.level)}</span></td><td class="mono" style="color: var(--text-muted);">${esc(gap)}</td></tr>`;
      }).reverse().join('');
      html += `<div class="section-label" style="margin: 14px 0 8px;">LEVEL TIMELINE · ${feed.length}</div><div style="overflow-x: auto;"><table class="data-table"><thead><tr><th>Time</th><th>Level</th><th>Time since last</th></tr></thead><tbody>${rows}</tbody></table></div>`;
    } else if (a && Object.keys(a).length > 0) {
      html += `<div class="empty-hint" style="margin-top: 12px;">No level-ups recorded for this bot yet.</div>`;
    }
    host.innerHTML = html;
  }

  // ---- Bot Armory -------------------------------------------------------
  // Text-first inspector over the read-only armory API. No image assets:
  // quality shows as text/border color (core ItemQualityColors); icon names
  // from item_display_info/spellicon surface as tooltip text only.

  function classNameById(id) {
    return CLASS_NAMES[id] || (id ? `Class ${id}` : 'Unknown');
  }

  function raceNameById(id) {
    return RACE_NAMES[id] || (id ? `Race ${id}` : 'Unknown');
  }

  function skillNameById(id) {
    return SKILL_NAMES[id] || `Skill ${id}`;
  }

  function qualityName(q) {
    return QUALITY_NAMES[q] || `Quality ${q}`;
  }

  function spellSchoolName(s) {
    if (s === null || s === undefined || s === 0) return 'Physical';
    return SPELL_SCHOOLS[s] || `School ${s}`;
  }

  function formatMoney(copper) {
    copper = Math.max(0, parseInt(copper, 10) || 0);
    const g = Math.floor(copper / 10000);
    const s = Math.floor((copper % 10000) / 100);
    const c = copper % 100;
    return `<span class="money-gold">${g}g</span> <span class="money-silver">${s}s</span> <span class="money-copper">${c}c</span>`;
  }

  function formatPlayed(sec) {
    sec = Math.max(0, parseInt(sec, 10) || 0);
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    if (d > 0) return `${d}d ${h}h played`;
    if (h > 0) return `${h}h ${m}m played`;
    return `${m}m played`;
  }

  function fmtNum(n) {
    if (n === null || n === undefined || n === '') return '–';
    const f = parseFloat(n);
    return isNaN(f) ? '–' : (Number.isInteger(f) ? String(f) : f.toFixed(1));
  }

  // ---- Bot profile (slide-over) ----------------------------------------------
  // One profile for every bot, reachable from every tab: it overlays the page
  // so closing it returns to exactly where the operator was. Live telemetry
  // fills what the database cannot (state, target, health), the database fills
  // everything for offline bots (gear, talents, spells, bags).

  function openProfile(guid) {
    go({ bot: guid });
  }

  function closeProfile() {
    go({ bot: null });
  }

  function liveBot(guid) {
    return state.bots.find(b => b.guid === guid) || null;
  }

  function knownBotName(guid) {
    const live = liveBot(guid);
    if (live) return live.name;
    if (state.profile && state.profile.summary && state.profile.summary.guid === guid) return state.profile.summary.name;
    const row = state.roster.find(r => r.guid === guid);
    return row ? row.name : '';
  }

  // The selection highlight (map ring, list row) follows the open profile.
  function refreshSelection() {
    if (state.activeTab === 'map') renderMap();
    else if (state.activeTab === 'roster') renderRoster();
  }

  function showProfile(guid) {
    const changed = state.profileGuid !== guid;
    state.profileGuid = guid;
    el.profileDrawer.classList.add('open');
    el.profileDrawer.setAttribute('aria-hidden', 'false');
    el.profileBackdrop.classList.add('active');
    // On the map the backdrop is click-through so another marker can be picked.
    el.profileBackdrop.classList.toggle('passive', state.activeTab === 'map');
    el.profileDrawer.classList.toggle('compact', state.activeTab === 'map');
    if (changed) {
      state.profile = null;
      state.profileRecent = null;
      const body = el.profileDrawer.querySelector('.profile-body');
      if (body) body.scrollTop = 0;
      if (el.armoryError) el.armoryError.style.display = 'none';
      fetchProfile(guid);
    }
    renderProfileHeader();
    renderArmoryPanel(state.profile, state.profileTab);
    ensureProfileRecent();
    if (changed) refreshSelection();
  }

  function hideProfile() {
    if (state.profileGuid === null) return;
    state.profileGuid = null;
    state.profile = null;
    state.profileRecent = null;
    el.profileDrawer.classList.remove('open');
    el.profileDrawer.setAttribute('aria-hidden', 'true');
    el.profileBackdrop.classList.remove('active');
    refreshSelection();
  }

  function fetchProfile(guid) {
    fetch(`/api/v1/armory/bot/${encodeURIComponent(guid)}`)
      .then(async r => {
        if (r.status === 401) throw new Error('HTTP 401');
        if (!r.ok) {
          const body = (await r.text()).trim().slice(0, 200);
          throw new Error(`HTTP ${r.status}${body ? `: ${body}` : ''}`);
        }
        return r.json();
      })
      .then(p => {
        if (state.profileGuid !== guid) return;
        state.profile = p;
        renderArmory();
        ensureProfileRecent();
      })
      .catch(e => {
        if (state.profileGuid !== guid) return;
        const msg = String((e && e.message) || 'fetch failed');
        if (el.armoryError) {
          el.armoryError.style.display = 'block';
          el.armoryError.innerHTML = msg.includes('401')
            ? 'Session expired — <a href="/login">log in again</a>, then reopen the bot.'
            : `Could not load the database profile of bot #${esc(guid)}: ${esc(msg)}`;
        }
      });
  }

  // The bot's latest quest and loot events (from the same bounded in-memory
  // feeds the Activity tab reads), merged into one short list.
  function ensureProfileRecent() {
    const guid = state.profileGuid;
    if (guid === null || (state.profileRecent && state.profileRecent.guid === guid)) return;
    const name = knownBotName(guid);
    if (!name) return;
    state.profileRecent = { guid, rows: null };
    const enc = encodeURIComponent(name);
    Promise.all([
      fetch(`/api/v1/activity/quests?bot=${enc}&limit=100`).then(jsonOrThrow).catch(() => []),
      fetch(`/api/v1/activity/loot?bot=${enc}&min_quality=0&limit=100`).then(jsonOrThrow).catch(() => [])
    ]).then(([quests, loot]) => {
      if (state.profileGuid !== guid) return;
      const rows = []
        .concat((quests || []).filter(r => r.guid === guid).map(r => ({ at: r.at, time: feedClock(r), html: `<span class="badge badge-info recent-kind">${esc(questEventLabel(r.event))}</span><span>${esc(r.quest || '-')}</span>` })))
        .concat((loot || []).filter(r => r.guid === guid).map(r => ({ at: r.at, time: feedClock(r), html: `<span class="badge badge-info recent-kind">${esc(r.source || 'loot')}</span><span>${lootItemCell(r)}</span>` })))
        .sort((a, b) => (b.at || 0) - (a.at || 0))
        .slice(0, 12);
      state.profileRecent = { guid, rows };
      renderProfileOverview();
    });
  }

  function profileBars(p, live) {
    const st = (p && p.stats) || {};
    // Online bots: telemetry is current, the database row lags.
    const hpMax = live && live.max_hp ? live.max_hp : (st.maxhealth || 0);
    const hpCur = live && live.max_hp ? live.hp : (st.health !== undefined ? st.health : hpMax);
    const hpPct = hpMax ? Math.min(100, Math.max(0, Math.round((hpCur / hpMax) * 100))) : 0;
    if (el.armoryHpBar) el.armoryHpBar.style.width = `${hpPct}%`;
    if (el.armoryHpText) el.armoryHpText.textContent = hpMax ? `${fmtNum(hpCur)} / ${fmtNum(hpMax)} HP` : '– HP';

    if (el.armoryPowBar && el.armoryPowText) {
      if (live && live.max_power) {
        const type = live.power_type || 'mana';
        el.armoryPowBar.className = `stat-bar-fill stat-bar-${type === 'rage' ? 'rage' : type === 'energy' ? 'energy' : 'mana'}`;
        el.armoryPowBar.style.width = `${Math.min(100, Math.round((live.power / live.max_power) * 100))}%`;
        el.armoryPowText.textContent = `${fmtNum(live.power)} / ${fmtNum(live.max_power)} ${powerLabel(live)}`;
      } else {
        // The armory tables carry only max values — no live current power —
        // so render max only, never a faked full bar.
        const cls = p && p.summary ? p.summary.class : 0;
        const label = cls === 1 ? 'Rage' : cls === 4 ? 'Energy' : 'Mana';
        const max = cls === 1 ? (st.maxpower2 || 100) : cls === 4 ? (st.maxpower4 || 100) : (st.maxpower1 || 0);
        el.armoryPowBar.className = `stat-bar-fill stat-bar-${cls === 1 ? 'rage' : cls === 4 ? 'energy' : 'mana'}`;
        el.armoryPowBar.style.width = '0%';
        el.armoryPowText.textContent = max ? `Max ${fmtNum(max)} ${label}` : '–';
      }
    }

    // XP bar: live telemetry only (the database has no per-level XP row).
    if (el.armoryXpWrap) {
      const pct = live ? xpPct(live) : null;
      el.armoryXpWrap.style.display = pct === null ? 'none' : '';
      if (pct !== null) {
        const rate = fmtXpRate(live.xp_per_hour);
        if (el.armoryXpBar) el.armoryXpBar.style.width = `${pct}%`;
        if (el.armoryXpText) el.armoryXpText.textContent = `${live.xp} / ${live.next_xp} XP (${pct}%)${rate !== '–' ? ` · ${rate}` : ''}`;
      }
    }
  }

  function renderProfileHeader() {
    const guid = state.profileGuid;
    const p = state.profile;
    const s = p && p.summary;
    const live = liveBot(guid);
    const clsName = s ? classNameById(s.class) : (live ? capClass(live.class) : '');
    const online = !!live || !!(s && s.online);

    if (el.armoryName) {
      el.armoryName.textContent = s ? s.name : (live ? live.name : '…');
      el.armoryName.style.color = clsName ? classColor(clsName) : '#fff';
    }
    if (el.armorySubtitle) {
      const level = live ? live.level : (s ? s.level : '');
      const zone = live ? `${getZoneName(live.zone, live.map)} (live)` : (s ? (s.online ? 'Online — position pending' : 'Offline') : '');
      el.armorySubtitle.textContent = [
        level !== '' ? `Level ${level}${s ? ` ${raceNameById(s.race)}` : ''} ${clsName}`.trim() : clsName,
        zone
      ].filter(Boolean).join(' · ');
    }
    if (el.armoryOnline) {
      el.armoryOnline.textContent = online ? 'Online' : 'Offline';
      el.armoryOnline.className = `badge ${online ? 'badge-success' : 'badge-warn'}`;
    }
    if (el.armoryMapBtn) el.armoryMapBtn.style.display = live ? '' : 'none';
    if (el.armoryMoney) el.armoryMoney.innerHTML = s ? formatMoney(s.money) : '';
    if (el.armoryPlayed) el.armoryPlayed.textContent = s ? formatPlayed(s.totaltime) : '';

    // Spec = the talent tree with the most points.
    if (el.armorySpecInfo) {
      const trees = (p && p.talents) || [];
      if (!trees.length) {
        el.armorySpecInfo.textContent = '';
      } else {
        const pts = trees.map(t => t.points || 0);
        const total = pts.reduce((a, b) => a + b, 0);
        let top = trees[0];
        trees.forEach(t => { if ((t.points || 0) > (top.points || 0)) top = t; });
        el.armorySpecInfo.textContent = `${total > 0 ? (top.name || 'Hybrid') : 'Unspecified'} (${pts.join(' / ')})`;
      }
    }
    profileBars(p, live);
  }

  const kv = (label, value) => `<div class="kv-row"><span>${esc(label)}</span><span>${value}</span></div>`;
  const kvMono = (value) => `<span class="mono">${esc(value || '-')}</span>`;

  function renderProfileOverview() {
    const host = document.getElementById('armory-content-overview');
    if (!host || state.profileTab !== 'overview' || state.profileGuid === null) return;
    const guid = state.profileGuid;
    const live = liveBot(guid);
    const mine = state.issues.active.filter(i => i.guid === guid);
    const past = state.issues.resolved.filter(i => i.guid === guid).slice(0, 5);
    let html = '';

    if (live) {
      const xp = xpPct(live);
      html += `<div class="profile-grid profile-section">
        <div class="kv-card">
          <div class="section-label">Right now</div>
          ${kv('Status', stateTag(live.state))}
          ${kv('Target', live.target ? `<strong class="target-cell">${esc(displayTarget(live))}</strong>${live.target_level ? ` <span class="cell-muted">L${esc(live.target_level)}</span>` : ''}` : DASH)}
          ${live.travel_purpose ? kv('Travel', `<strong>${esc(live.travel_purpose)}</strong><span class="cell-muted"> → ${esc(live.travel_to || '')}</span>`) : ''}
          ${kv('Last action', kvMono(live.last_action))}
          ${kv('Trigger', kvMono(live.last_trigger))}
          ${kv('Strategy', kvMono(live.strategy || 'default'))}
          ${kv('Combat role', `<span class="badge badge-info" title="AI combat role (forced role / combat strategies / talent-gear auto-detect), not a group slot">${esc(roleLabel(live))}</span>`)}
        </div>
        <div class="kv-card">
          <div class="section-label">Where &amp; how it is going</div>
          ${kv('Zone', `<span class="mono">${esc(getZoneName(live.zone, live.map))}</span>`)}
          ${xp === null ? '' : kv('XP', `<span class="mono">${esc(live.xp)} / ${esc(live.next_xp)} (${xp}%)</span>`)}
          ${xp === null ? '' : kv('XP per hour', `<span class="mono">${esc(fmtXpRate(live.xp_per_hour))}</span>`)}
          ${xp === null ? '' : kv('Last XP gain', `<span class="mono">${esc(fmtGainAge(live.xp_gain_age_sec))}</span>`)}
          ${live.gear ? kv('Gear item level', gearCell(live)) : ''}
        </div>
      </div>`;
    } else {
      html += `<div class="offline-note profile-section">Offline — no live status. Everything else comes from the database.</div>`;
    }

    if (mine.length || past.length) {
      html += `<div class="profile-section"><div class="section-label">Issues</div>
        ${mine.map(i => `<div class="issue-chip ${esc(i.severity)}"><span>${esc(ISSUE_LABELS[i.type] || i.type)} · ${esc(getZoneName(i.zone, i.map))}</span><span class="mono">${esc(fmtDuration(i.duration_sec))}</span></div>`).join('')}
        ${past.length ? `<div class="empty-hint">Recently resolved: ${past.map(i => `${esc(ISSUE_LABELS[i.type] || i.type)} (${esc(fmtDuration(i.duration_sec))})`).join(', ')}</div>` : ''}
      </div>`;
    }

    const recent = state.profileRecent && state.profileRecent.guid === guid ? state.profileRecent.rows : null;
    html += `<div class="profile-section"><div class="section-label">Recent events</div>${
      recent === null ? '<div class="empty-hint">Loading…</div>'
        : recent.length ? recent.map(r => `<div class="recent-row"><span class="recent-time mono">${esc(r.time)}</span>${r.html}</div>`).join('')
          : '<div class="empty-hint">No quest or loot events recorded this session.</div>'
    }</div>`;
    host.innerHTML = html;
  }

  // Everything that depends on the database profile: header, paperdoll, stats
  // and the active sub-tab.
  function renderArmory() {
    const p = state.profile;
    if (!p || !p.summary) return;
    if (el.armoryError) el.armoryError.style.display = 'none';
    renderProfileHeader();
    renderArmoryGear(p, liveBot(p.summary.guid));
    renderArmoryStats(p);
    renderArmoryPanel(p, state.profileTab);
  }

  // Cheap per-snapshot refresh: bars and the live overview, nothing heavier.
  function renderProfileLive() {
    if (state.profileGuid === null) return;
    renderProfileHeader();
    renderProfileOverview();
  }

  function showOnMap() {
    const b = liveBot(state.profileGuid);
    if (b) openZone(b.zone, b.guid);
  }

  function initProfile() {
    if (el.closeProfile) el.closeProfile.addEventListener('click', closeProfile);
    if (el.profileBackdrop) el.profileBackdrop.addEventListener('click', closeProfile);
    if (el.armoryMapBtn) el.armoryMapBtn.addEventListener('click', showOnMap);
    if (el.profileTabs) {
      el.profileTabs.addEventListener('click', e => {
        const btn = e.target.closest('.armory-subtab[data-subtab]');
        if (btn) go({ ptab: btn.dataset.subtab }, { replace: true });
      });
    }
  }

  function renderArmoryPanel(p, tab) {
    state.profileTab = tab;
    document.querySelectorAll('#profile-tabs .armory-subtab[data-subtab]').forEach(b => b.classList.toggle('active', b.dataset.subtab === tab));
    PROFILE_TABS.forEach(t => {
      const panel = document.getElementById(`armory-content-${t}`);
      if (panel) panel.style.display = t === tab ? 'block' : 'none';
    });
    if (tab === 'overview') { renderProfileOverview(); return; }
    if (!p) {
      const host = document.getElementById(`armory-content-${tab}`);
      if (host && tab !== 'gear') host.innerHTML = '<div class="empty-hint">Loading…</div>';
      return;
    }
    // Gear & stats render together with the profile (renderArmory).
    if (tab === 'bags') renderArmoryBags(p);
    else if (tab === 'talents') renderArmoryTalents(p);
    else if (tab === 'spells') renderArmorySpells(p);
    else if (tab === 'professions') renderArmoryProfessions(p);
    else if (tab === 'skills') renderArmorySkills(p);
    else if (tab === 'progress') renderArmoryProgress(p);
  }

  function itemSubclassName(cls, sub) {
    const key = `${cls}-${sub}`;
    return ITEM_SUBCLASS_NAMES[key] || ITEM_CLASS_NAMES[cls] || `Item ${cls}/${sub}`;
  }

  function itemStatName(t) {
    return ITEM_STAT_NAMES[t] || `Stat ${t}`;
  }

  function getItemIconUrl(icon) {
    if (!icon) return null;
    const name = String(icon).toLowerCase().trim();
    if (!name) return null;
    return `/static/icons/${name}.jpg`;
  }

  function itemTooltip(item) {
    if (!item) return 'Empty slot';
    try {
      const d = item.detail || {};
      const q = item.quality || 0;
      const lines = [];
      lines.push(`<div class="tip-name quality-text-${q}">${esc(item.name)}${item.count > 1 ? ` <span style="color:#fff;">×${esc(item.count)}</span>` : ''}</div>`);
      if (item.item_level) lines.push(`<div class="tip-sub">Item Level ${esc(item.item_level)}</div>`);
      if (ITEM_BONDING_NAMES[d.bonding]) lines.push(`<div class="tip-sub">${esc(ITEM_BONDING_NAMES[d.bonding])}</div>`);
      const slots = item.container_slots || d.container_slots;
      if (slots) lines.push(`<div class="tip-row"><span>${esc(slots)} Slot Bag</span></div>`);
      const typeName = item.inventory_type ? INVTYPE_NAMES[item.inventory_type] : itemSubclassName(d.class, d.subclass);
      if (typeName && !slots) lines.push(`<div class="tip-row"><span>${esc(typeName)}</span><span>${esc(itemSubclassName(d.class, d.subclass))}</span></div>`);
      if (d.required_level) lines.push(`<div class="tip-row"><span>Requires Level</span><span>${esc(d.required_level)}</span></div>`);
      if (d.armor) lines.push(`<div class="tip-row"><span>${esc(d.armor)} Armor</span></div>`);
      if (d.block) lines.push(`<div class="tip-row"><span>${esc(d.block)} Block</span></div>`);
      if (d.dmg_min1 || d.dmg_max1) {
        const speedSec = d.delay ? (d.delay / 1000) : 0;
        const speedText = speedSec ? `<span>Speed ${esc(speedSec.toFixed(2))}</span>` : '';
        lines.push(`<div class="tip-row"><span>${esc(Math.round(d.dmg_min1 || 0))} – ${esc(Math.round(d.dmg_max1 || 0))} Damage</span>${speedText}</div>`);
        if (speedSec > 0) {
          const dps = ((d.dmg_min1 + d.dmg_max1) / 2) / speedSec;
          lines.push(`<div class="tip-row"><span style="color: #8b949e;">(${dps.toFixed(1)} damage per second)</span></div>`);
        }
      }
      if (d.dmg_min2 || d.dmg_max2) {
        lines.push(`<div class="tip-row"><span>+${esc(Math.round(d.dmg_min2 || 0))} – ${esc(Math.round(d.dmg_max2 || 0))} Damage</span></div>`);
      }
      if (d.dmg_min3 || d.dmg_max3) {
        lines.push(`<div class="tip-row"><span>+${esc(Math.round(d.dmg_min3 || 0))} – ${esc(Math.round(d.dmg_max3 || 0))} Damage</span></div>`);
      }

      if (Array.isArray(item.enchantments) && item.enchantments.length > 0) {
        item.enchantments.forEach(e => {
          let extra = '';
          if (e.slot === 1) {
            if (e.charges > 0 && e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)} min / ${e.charges} charges)`;
            else if (e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)} min)`;
            else if (e.charges > 0) extra = ` (${e.charges} charges)`;
          }
          lines.push(`<div class="tip-enchant">${esc(e.description)}${extra}</div>`);
        });
      }

      let types = d.stat_types || [];
      if (typeof types === 'string') {
        try {
          const bin = atob(types);
          types = [];
          for (let i = 0; i < bin.length; i++) types.push(bin.charCodeAt(i));
        } catch (e) {
          types = [];
        }
      }
      const vals = d.stat_values || [];
      if (Array.isArray(types)) {
        types.forEach((t, i) => {
          const v = vals[i];
          if (!v) return;
          lines.push(`<div class="tip-stat">+${esc(v)} ${esc(itemStatName(t))}</div>`);
        });
      }

      if (Array.isArray(d.random_stats) && d.random_stats.length > 0) {
        d.random_stats.forEach(st => {
          if (st) lines.push(`<div class="tip-stat">${esc(st)}</div>`);
        });
      }

      const res = [['Holy', d.res_holy], ['Fire', d.res_fire], ['Nature', d.res_nature], ['Frost', d.res_frost], ['Shadow', d.res_shadow], ['Arcane', d.res_arcane]];
      res.forEach(([k, v]) => { if (v) lines.push(`<div class="tip-stat">+${esc(v)} ${k} Resistance</div>`); });

      const sids = d.spell_ids || [];
      let strg = d.spell_triggers || [];
      if (typeof strg === 'string') {
        try {
          const bin = atob(strg);
          strg = [];
          for (let i = 0; i < bin.length; i++) strg.push(bin.charCodeAt(i));
        } catch (e) {
          strg = [];
        }
      }
      const snames = d.spell_names || [];
      const sdescs = d.spell_descs || [];
      sids.forEach((sid, i) => {
        if (!sid) return;
        const triggerVal = Array.isArray(strg) ? strg[i] : (typeof strg === 'string' ? strg.charCodeAt(i) : 1);
        const label = ITEM_TRIGGER_NAMES[triggerVal] || 'Effect:';
        let text = (sdescs[i] || snames[i] || `Spell #${sid}`).trim();
        if (!text) return;
        if (text.startsWith(label)) {
          lines.push(`<div class="tip-proc">${esc(text)}</div>`);
        } else {
          lines.push(`<div class="tip-proc">${esc(label)} ${esc(text)}</div>`);
        }
      });

      if (d.max_durability) {
        lines.push(`<div class="tip-sub">Durability ${esc(d.max_durability)} / ${esc(d.max_durability)}</div>`);
      }
      if (d.description) lines.push(`<div class="tip-flavor">${esc(d.description.replace(/^"|"$/g, ''))}</div>`);
      lines.push(`<div class="tip-sub mono">#${esc(item.item_template)} · ${esc(qualityName(q))}</div>`);
      if (d.sell_price) lines.push(`<div class="tip-sub">Sells for ${formatMoney(d.sell_price)}</div>`);
      return lines.join('');
    } catch (err) {
      console.error('Failed to render item tooltip', err);
      return `<div class="tip-name quality-text-${item.quality || 0}">${esc(item.name || 'Item')}</div><div class="tip-sub mono">#${esc(item.item_template || '')}</div>`;
    }
  }

  function gearBySlot(p) {
    const m = {};
    (p.equipment || []).forEach(e => { m[e.slot] = e; });
    return m;
  }

  function gearRowLeft(slot, item) {
    const slotName = EQUIP_SLOT_NAMES[slot] || `Slot ${slot}`;
    const icon = item ? (item.icon || (item.detail && item.detail.icon)) : null;
    const iconUrl = getItemIconUrl(icon);
    const q = item ? item.quality : 0;

    const iconHtml = iconUrl
      ? `<img src="${iconUrl}" alt="${esc(slotName)}" onerror="this.onerror=null; this.parentElement.innerHTML='<span class=\\'gear-slot-initial\\'>${slotName[0]}</span>';" class="gear-icon-img">`
      : `<span class="gear-slot-initial">${slotName[0]}</span>`;

    if (!item) {
      const emptyTip = JSON.stringify({ name: `Empty ${slotName}`, quality: 0 });
      return `
        <div class="gear-slot-row left gear-empty" data-tip='${esc(emptyTip)}'>
          <div class="gear-icon-box quality-border-0">${iconHtml}</div>
          <div class="gear-info">
            <div class="gear-slot-label">${esc(slotName)}</div>
            <div class="gear-empty-text">Empty</div>
          </div>
        </div>`;
    }

    const count = item.count > 1 ? ` <span class="gear-ilvl">×${esc(item.count)}</span>` : '';
    const ilvl = item.item_level ? `<span class="gear-ilvl">iLvl ${esc(item.item_level)}</span>` : '';
    const enchantHtml = (item.enchantments && item.enchantments.length > 0)
      ? item.enchantments.map(e => {
          let extra = '';
          if (e.slot === 1) {
            if (e.charges > 0 && e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m, ${e.charges}ch)`;
            else if (e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m)`;
            else if (e.charges > 0) extra = ` (${e.charges}ch)`;
          }
          return `<div class="gear-enchant-text" title="${esc(e.description)}${extra}">✨ ${esc(e.description)}${extra}</div>`;
        }).join('')
      : '';

    return `
      <div class="gear-slot-row left quality-border-${esc(q)}" data-tip='${esc(JSON.stringify(item))}'>
        <div class="gear-icon-box quality-border-${esc(q)}">${iconHtml}</div>
        <div class="gear-info">
          <div class="gear-name quality-text-${esc(q)}">${esc(item.name)}${count}</div>
          ${enchantHtml}
          <div class="gear-subrow">
            <span>${esc(slotName)}</span>${ilvl}<span class="mono">#${esc(item.item_template)}</span>
          </div>
        </div>
      </div>`;
  }

  function gearRowRight(slot, item) {
    const slotName = EQUIP_SLOT_NAMES[slot] || `Slot ${slot}`;
    const icon = item ? (item.icon || (item.detail && item.detail.icon)) : null;
    const iconUrl = getItemIconUrl(icon);
    const q = item ? item.quality : 0;

    const iconHtml = iconUrl
      ? `<img src="${iconUrl}" alt="${esc(slotName)}" onerror="this.onerror=null; this.parentElement.innerHTML='<span class=\\'gear-slot-initial\\'>${slotName[0]}</span>';" class="gear-icon-img">`
      : `<span class="gear-slot-initial">${slotName[0]}</span>`;

    if (!item) {
      const emptyTip = JSON.stringify({ name: `Empty ${slotName}`, quality: 0 });
      return `
        <div class="gear-slot-row right gear-empty" data-tip='${esc(emptyTip)}'>
          <div class="gear-info" style="text-align: right;">
            <div class="gear-slot-label">${esc(slotName)}</div>
            <div class="gear-empty-text">Empty</div>
          </div>
          <div class="gear-icon-box quality-border-0">${iconHtml}</div>
        </div>`;
    }

    const count = item.count > 1 ? ` <span class="gear-ilvl">×${esc(item.count)}</span>` : '';
    const ilvl = item.item_level ? `<span class="gear-ilvl">iLvl ${esc(item.item_level)}</span>` : '';
    const enchantHtml = (item.enchantments && item.enchantments.length > 0)
      ? item.enchantments.map(e => {
          let extra = '';
          if (e.slot === 1) {
            if (e.charges > 0 && e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m, ${e.charges}ch)`;
            else if (e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m)`;
            else if (e.charges > 0) extra = ` (${e.charges}ch)`;
          }
          return `<div class="gear-enchant-text" title="${esc(e.description)}${extra}">✨ ${esc(e.description)}${extra}</div>`;
        }).join('')
      : '';

    return `
      <div class="gear-slot-row right quality-border-${esc(q)}" data-tip='${esc(JSON.stringify(item))}'>
        <div class="gear-info" style="text-align: right;">
          <div class="gear-name quality-text-${esc(q)}">${esc(item.name)}${count}</div>
          ${enchantHtml}
          <div class="gear-subrow" style="justify-content: flex-end;">
            <span>${esc(slotName)}</span>${ilvl}<span class="mono">#${esc(item.item_template)}</span>
          </div>
        </div>
        <div class="gear-icon-box quality-border-${esc(q)}">${iconHtml}</div>
      </div>`;
  }

  function gearRowWeapon(slot, item) {
    const slotName = EQUIP_SLOT_NAMES[slot] || `Slot ${slot}`;
    const icon = item ? (item.icon || (item.detail && item.detail.icon)) : null;
    const iconUrl = getItemIconUrl(icon);
    const q = item ? item.quality : 0;

    const iconHtml = iconUrl
      ? `<img src="${iconUrl}" alt="${esc(slotName)}" onerror="this.onerror=null; this.parentElement.innerHTML='<span class=\\'gear-slot-initial\\'>${slotName[0]}</span>';" class="gear-icon-img">`
      : `<span class="gear-slot-initial">${slotName[0]}</span>`;

    if (!item) {
      const emptyTip = JSON.stringify({ name: `Empty ${slotName}`, quality: 0 });
      return `
        <div class="gear-weapon-item gear-empty" data-tip='${esc(emptyTip)}'>
          <div class="gear-icon-box quality-border-0">${iconHtml}</div>
          <div class="gear-info" style="text-align: center;">
            <div class="gear-slot-label">${esc(slotName)}</div>
            <div class="gear-empty-text">Empty</div>
          </div>
        </div>`;
    }

    const enchantHtml = (item.enchantments && item.enchantments.length > 0)
      ? item.enchantments.map(e => {
          let extra = '';
          if (e.slot === 1) {
            if (e.charges > 0 && e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m, ${e.charges}ch)`;
            else if (e.duration > 0) extra = ` (${Math.ceil(e.duration / 60)}m)`;
            else if (e.charges > 0) extra = ` (${e.charges}ch)`;
          }
          return `<div class="gear-enchant-text" title="${esc(e.description)}${extra}">✨ ${esc(e.description)}${extra}</div>`;
        }).join('')
      : '';

    return `
      <div class="gear-weapon-item quality-border-${esc(q)}" data-tip='${esc(JSON.stringify(item))}'>
        <div class="gear-icon-box quality-border-${esc(q)}">${iconHtml}</div>
        <div class="gear-info" style="text-align: center;">
          <div class="gear-name quality-text-${esc(q)}">${esc(item.name)}</div>
          ${enchantHtml}
          <div class="gear-subrow" style="justify-content: center;">
            <span>${esc(slotName)}</span>
          </div>
        </div>
      </div>`;
  }

  // Equipped-gear summary above the paperdoll: average item level over the
  // filled slots (shirt/tabard excluded, same rule as the daemon's roster
  // sweep) plus the quality split of the pieces rendered below.
  // liveGear is the roster snapshot's swept gear rollup when the bot is online
  // (identical slot rule, DB truth); the profile's own equipment is the
  // fallback so offline bots still get a header stat.
  function renderGearSummary(p, liveGear) {
    const host = el.gearSummary;
    if (!host) return;
    const g = gearSummary(p.equipment);
    const header = liveGear && liveGear.pieces ? liveGear : g;
    if (el.armoryIlvl) {
      if (header.pieces) {
        const split = `${header.pieces} equipped pieces · ${header.grey} grey / ${header.white} white / ${header.green} green / ${header.blue} blue / ${header.epic} epic`;
        el.armoryIlvl.textContent = `iLvl ${Math.round(header.item_level)}`;
        el.armoryIlvl.title = `Average equipped item level ${header.item_level.toFixed(1)} · ${split}`;
      } else {
        el.armoryIlvl.textContent = '';
        el.armoryIlvl.title = '';
      }
    }
    if (!g.pieces) {
      host.innerHTML = '<span>No gear equipped.</span>';
      return;
    }
    const q = (n, qi, label) => (n ? `<span style="color: ${QUALITY_COLORS[qi]};">${n} ${label}</span>` : '');
    host.innerHTML =
      `<span>Average item level <strong>${g.item_level.toFixed(1)}</strong> over ${g.pieces} pieces</span>` +
      q(g.grey, 0, 'grey') + q(g.white, 1, 'white') + q(g.green, 2, 'green') +
      q(g.blue, 3, 'blue') + q(g.epic, 4, 'epic');
  }

  function renderArmoryGear(p, live) {
    const m = gearBySlot(p);
    if (el.gearLeft) el.gearLeft.innerHTML = GEAR_LEFT.map(s => gearRowLeft(s, m[s])).join('');
    if (el.gearRight) el.gearRight.innerHTML = GEAR_RIGHT.map(s => gearRowRight(s, m[s])).join('');
    if (el.gearWeapons) el.gearWeapons.innerHTML = GEAR_WEAPONS.map(s => gearRowWeapon(s, m[s])).join('');
    renderGearSummary(p, live && live.gear);
    bindItemTooltips(document.getElementById('profile-drawer'));
  }

  function armoryTipDiv() {
    let tip = document.getElementById('item-tooltip');
    if (!tip) {
      tip = document.createElement('div');
      tip.id = 'item-tooltip';
      tip.className = 'item-tooltip';
      tip.style.display = 'none';
      document.body.appendChild(tip);
    }
    return tip;
  }

  function bindItemTooltips(root) {
    if (!root) return;
    const tip = armoryTipDiv();
    root.querySelectorAll('[data-tip]').forEach(node => {
      if (node.dataset.tipBound) return;
      node.dataset.tipBound = '1';
      node.addEventListener('mouseenter', e => {
        let item = null;
        try { item = JSON.parse(node.dataset.tip); } catch (err) { item = null; }
        tip.innerHTML = itemTooltip(item);
        tip.style.display = 'block';
        const pad = 14;
        const r = tip.getBoundingClientRect();
        let left = e.clientX + pad, top = e.clientY + pad;
        if (left + r.width > window.innerWidth - 6) left = e.clientX - r.width - pad;
        if (top + r.height > window.innerHeight - 6) top = e.clientY - r.height - pad;
        tip.style.left = `${Math.max(6, left)}px`;
        tip.style.top = `${Math.max(6, top)}px`;
      });
      node.addEventListener('mouseleave', () => { tip.style.display = 'none'; });
    });
  }

  function bindSpellTooltips(root) {
    if (!root) return;
    const tip = armoryTipDiv();
    root.querySelectorAll('[data-sptip]').forEach(node => {
      if (node.dataset.sptipBound) return;
      node.dataset.sptipBound = '1';
      const posTip = e => {
        const pad = 14;
        const r = tip.getBoundingClientRect();
        let left = e.clientX + pad, top = e.clientY + pad;
        if (left + r.width > window.innerWidth - 6) left = e.clientX - r.width - pad;
        if (top + r.height > window.innerHeight - 6) top = e.clientY - r.height - pad;
        tip.style.left = `${Math.max(6, left)}px`;
        tip.style.top = `${Math.max(6, top)}px`;
      };
      node.addEventListener('mouseenter', e => {
        tip.innerHTML = node.dataset.sptip;
        tip.style.display = 'block';
        posTip(e);
      });
      node.addEventListener('mousemove', posTip);
      node.addEventListener('mouseleave', () => { tip.style.display = 'none'; });
    });
  }

  function bagItemRow(it) {
    const q = it.quality || 0;
    const iconUrl = getItemIconUrl(it.icon);
    const iconHtml = iconUrl
      ? `<img src="${iconUrl}" style="width: 20px; height: 20px; border-radius: 3px; vertical-align: middle; margin-right: 6px;" onerror="this.style.display='none';">`
      : '';
    return `<tr data-tip='${esc(JSON.stringify(it))}'><td class="mono">${esc(it.slot)}</td><td class="quality-text-${q}">${iconHtml}${esc(it.name)} <span class="badge quality-badge-${q}">${esc(qualityName(q))}</span></td><td class="mono">×${esc(it.count > 1 ? it.count : 1)}</td><td class="mono" style="color: var(--text-dim);">#${esc(it.item_template)}</td></tr>`;
  }

  function bagTable(items) {
    return `<div style="overflow-x: auto;"><table class="data-table"><thead><tr><th>Slot</th><th>Item</th><th>Count</th><th>Entry</th></tr></thead><tbody>${items.map(bagItemRow).join('')}</tbody></table></div>`;
  }

  function renderArmoryStats(p) {
    const host = document.getElementById('armory-content-stats');
    if (!host) return;
    const st = p.stats || {};

    const attrsCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">💪</span> Attributes</div>
        <div class="stat-card-row"><span>Strength</span><strong class="mono">${esc(fmtNum(st.strength))}</strong></div>
        <div class="stat-card-row"><span>Agility</span><strong class="mono">${esc(fmtNum(st.agility))}</strong></div>
        <div class="stat-card-row"><span>Stamina</span><strong class="mono">${esc(fmtNum(st.stamina))}</strong></div>
        <div class="stat-card-row"><span>Intellect</span><strong class="mono">${esc(fmtNum(st.intellect))}</strong></div>
        <div class="stat-card-row"><span>Spirit</span><strong class="mono">${esc(fmtNum(st.spirit))}</strong></div>
      </div>`;

    const meleeCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">⚔️</span> Melee</div>
        <div class="stat-card-row"><span>Attack Power</span><strong class="mono">${esc(fmtNum(st.attack_power))}</strong></div>
        <div class="stat-card-row"><span>Damage</span><strong class="mono" style="color: #fff;">${esc(st.melee_damage || '–')}</strong></div>
        <div class="stat-card-row"><span>Speed</span><strong class="mono">${st.melee_speed ? esc(fmtNum(st.melee_speed) + 's') : '–'}</strong></div>
        <div class="stat-card-row"><span>Crit Chance</span><strong class="mono">${esc(fmtNum(st.melee_crit_pct))}%</strong></div>
        <div class="stat-card-row"><span>Hit Chance</span><strong class="mono">${esc(fmtNum(st.melee_hit))}%</strong></div>
      </div>`;

    const rangedCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">🏹</span> Ranged</div>
        <div class="stat-card-row"><span>Ranged AP</span><strong class="mono">${esc(fmtNum(st.ranged_attack_power))}</strong></div>
        <div class="stat-card-row"><span>Damage</span><strong class="mono" style="color: #fff;">${esc(st.ranged_damage || '–')}</strong></div>
        <div class="stat-card-row"><span>Speed</span><strong class="mono">${st.ranged_speed ? esc(fmtNum(st.ranged_speed) + 's') : '–'}</strong></div>
        <div class="stat-card-row"><span>Crit Chance</span><strong class="mono">${esc(fmtNum(st.ranged_crit_pct))}%</strong></div>
        <div class="stat-card-row"><span>Hit Chance</span><strong class="mono">${esc(fmtNum(st.ranged_hit))}%</strong></div>
      </div>`;

    const baseSp = st.spell_damage || 0;
    const schools = [
      { name: 'Holy', icon: '✨', color: '#fde047', bonus: st.spell_dmg_holy || 0 },
      { name: 'Fire', icon: '🔥', color: '#fb923c', bonus: st.spell_dmg_fire || 0 },
      { name: 'Nature', icon: '🌿', color: '#4ade80', bonus: st.spell_dmg_nature || 0 },
      { name: 'Frost', icon: '❄️', color: '#38bdf8', bonus: st.spell_dmg_frost || 0 },
      { name: 'Shadow', icon: '💀', color: '#c084fc', bonus: st.spell_dmg_shadow || 0 },
      { name: 'Arcane', icon: '🔮', color: '#e879f9', bonus: st.spell_dmg_arcane || 0 },
    ];

    const schoolTipRows = schools.map(s => {
      const total = baseSp + s.bonus;
      const bonusText = s.bonus > 0 ? ` <span style="color: var(--text-muted); font-size: 0.72rem; font-weight: normal;">(+${s.bonus})</span>` : '';
      return `<div style="display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 3px 0;"><span style="color: var(--text-muted);">${s.icon} ${s.name}</span><strong class="mono" style="color: ${s.color};">+${total}${bonusText}</strong></div>`;
    }).join('');

    const spTipHtml = `
      <div style="font-weight: 700; color: #fff; margin-bottom: 6px; padding-bottom: 4px; border-bottom: 1px solid var(--border-color); font-size: 0.82rem;">Spell Power by School</div>
      <div style="display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 3px 0; margin-bottom: 4px; border-bottom: 1px dashed rgba(255,255,255,0.1);"><span style="color: var(--text-main); font-weight: 600;">Base Spell Power</span><strong class="mono" style="color: #3fb950;">+${baseSp}</strong></div>
      ${schoolTipRows}
    `;

    const healingPower = st.healing_power || baseSp;
    const healBonus = healingPower > baseSp ? healingPower - baseSp : 0;
    const healTipHtml = `
      <div style="font-weight: 700; color: #fff; margin-bottom: 6px; padding-bottom: 4px; border-bottom: 1px solid var(--border-color); font-size: 0.82rem;">Healing Power Breakdown</div>
      <div style="display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 3px 0;"><span style="color: var(--text-muted);">Base Spell Power</span><strong class="mono" style="color: #3fb950;">+${baseSp}</strong></div>
      ${healBonus > 0 ? `<div style="display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 3px 0;"><span style="color: var(--text-muted);">Pure Healing Bonus</span><strong class="mono" style="color: #3fb950;">+${healBonus}</strong></div>` : ''}
      <div style="display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 3px 0; margin-top: 4px; border-top: 1px dashed rgba(255,255,255,0.1);"><span style="color: var(--text-main); font-weight: 600;">Total Healing Power</span><strong class="mono" style="color: #3fb950;">+${healingPower}</strong></div>
    `;

    const spellCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">✨</span> Spell</div>
        <div class="stat-card-row" data-sptip="${esc(spTipHtml)}" style="cursor: help;">
          <span style="border-bottom: 1px dotted var(--text-dim); display: inline-flex; align-items: center; gap: 4px;">Spell Power <span style="font-size: 0.72rem; color: var(--text-muted);">ℹ️</span></span>
          <strong class="mono" style="color: #3fb950; font-size: 0.95rem;">+${esc(baseSp)}</strong>
        </div>
        <div class="stat-card-row" data-sptip="${esc(healTipHtml)}" style="cursor: help;">
          <span style="border-bottom: 1px dotted var(--text-dim); display: inline-flex; align-items: center; gap: 4px;">Healing <span style="font-size: 0.72rem; color: var(--text-muted);">ℹ️</span></span>
          <strong class="mono" style="color: #3fb950;">+${esc(healingPower)}</strong>
        </div>
        <div class="stat-card-row"><span>Spell Crit</span><strong class="mono">${esc(fmtNum(st.spell_crit_pct))}%</strong></div>
        <div class="stat-card-row"><span>Spell Hit</span><strong class="mono">${esc(fmtNum(st.spell_hit))}%</strong></div>
        <div class="stat-card-row"><span>Mana Regen</span><strong class="mono">${esc(st.mana_regen || 0)} MP5</strong></div>
      </div>`;

    const defenseCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">🛡️</span> Defense</div>
        <div class="stat-card-row"><span>Armor</span><strong class="mono">${esc(fmtNum(st.armor))}</strong></div>
        <div class="stat-card-row"><span>Dodge</span><strong class="mono">${esc(fmtNum(st.dodge_pct))}%</strong></div>
        <div class="stat-card-row"><span>Parry</span><strong class="mono">${esc(fmtNum(st.parry_pct))}%</strong></div>
        <div class="stat-card-row"><span>Block</span><strong class="mono">${esc(fmtNum(st.block_pct))}%</strong></div>
      </div>`;

    const resistCard = `
      <div class="armory-stat-card">
        <div class="stat-card-title"><span class="stat-icon">🔮</span> Resistance</div>
        <div class="stat-card-row"><span>🔮 Arcane</span><strong class="mono">${esc(st.res_arcane || 0)}</strong></div>
        <div class="stat-card-row"><span>🔥 Fire</span><strong class="mono">${esc(st.res_fire || 0)}</strong></div>
        <div class="stat-card-row"><span>🌿 Nature</span><strong class="mono">${esc(st.res_nature || 0)}</strong></div>
        <div class="stat-card-row"><span>❄️ Frost</span><strong class="mono">${esc(st.res_frost || 0)}</strong></div>
        <div class="stat-card-row"><span>💀 Shadow</span><strong class="mono">${esc(st.res_shadow || 0)}</strong></div>
        <div class="stat-card-row"><span>✨ Holy</span><strong class="mono">${esc(st.res_holy || 0)}</strong></div>
      </div>`;

    host.innerHTML = `
      <div class="armory-stat-cards-grid">
        ${attrsCard}
        ${meleeCard}
        ${rangedCard}
        ${spellCard}
        ${defenseCard}
        ${resistCard}
      </div>`;
    bindSpellTooltips(host);
  }


  function renderArmoryBags(p) {
    const host = document.getElementById('armory-content-bags');
    if (!host) return;
    const bp = p.backpack || [];
    let html = `<div class="bag-block"><div class="bag-head"><strong>Backpack</strong><span style="color: var(--text-muted);">${bp.length}/16</span><div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${Math.round((bp.length / 16) * 100)}%; background: var(--accent-blue-bright);"></div></div></div>`;
    html += bp.length ? bagTable(bp) : `<div class="empty-hint">Backpack is empty.</div>`;
    html += `</div>`;
    (p.bags || []).forEach(b => {
      const cap = b.container_slots || b.items.length;
      const pct = cap ? Math.min(100, Math.round((b.items.length / cap) * 100)) : 0;
      const bagIcon = b.icon || (b.detail && b.detail.icon);
      const bagIconUrl = getItemIconUrl(bagIcon);
      const bagIconHtml = bagIconUrl
        ? `<img src="${bagIconUrl}" style="width: 20px; height: 20px; border-radius: 3px; vertical-align: middle; margin-right: 6px;" onerror="this.style.display='none';">`
        : '';
      html += `<div class="bag-block"><div class="bag-head" data-tip='${esc(JSON.stringify(b))}' style="cursor: pointer;"><strong class="quality-text-${esc(b.quality)}">${bagIconHtml}${esc(b.name)}</strong><span style="color: var(--text-muted);">${b.items.length}/${cap || '?'} · slot ${esc(b.slot)}</span><div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${pct}%; background: var(--accent-blue-bright);"></div></div></div>`;
      html += b.items.length ? bagTable(b.items) : `<div class="empty-hint">Empty.</div>`;
      html += `</div>`;
    });
    if (!(p.bags || []).length) html += `<div class="empty-hint">No equipped bags.</div>`;
    const bb = p.buyback || [];
    if (bb.length) {
      html += `<div class="bag-block"><div class="bag-head"><strong>Sold to vendor (buyback)</strong><span style="color: var(--text-muted);">${bb.length} recoverable</span></div>${bagTable(bb)}</div>`;
    }
    host.innerHTML = html;
    bindItemTooltips(host);
  }
  function talentTooltipHtml(treeName, n, p) {
    const name = n.name || `Talent ${n.talent_id}`;
    const req = n.row * 5;
    let spDesc = '';
    if (n.spell_id && p && p.spells) {
      const sp = p.spells.find(s => s.spell === n.spell_id);
      if (sp) {
        spDesc = spellText(sp) || sp.description || '';
      }
    }
    if (!spDesc && n.description) {
      spDesc = spellText({ description: n.description }) || n.description;
    }
    if (!spDesc) {
      spDesc = `${treeName} talent (Rank ${n.rank}/${n.max_rank}).`;
    }
    return `<div class="tip-name" style="color: #e6cc80;">${esc(name)}</div><div class="tip-sub" style="color: var(--accent-yellow); font-weight: 600;">Rank ${n.rank}/${n.max_rank}</div>${req > 0 ? `<div class="tip-sub">Requires ${req} points in ${esc(treeName)}</div>` : ''}<div class="tip-stat" style="margin-top: 6px; color: #fff; max-width: 280px;">${esc(spDesc)}</div>${n.spell_id ? `<div class="tip-sub mono" style="margin-top: 4px; font-size: 0.7rem;">Spell #${n.spell_id}</div>` : ''}`;
  }

  function renderArmoryTalents(p) {
    const host = document.getElementById('armory-content-talents');
    if (!host) return;
    const trees = p.talents || [];
    const spent = trees.reduce((a, t) => a + (t.points || 0), 0);
    if (!trees.length) {
      host.innerHTML = `<div class="empty-hint">No talent points spent yet — this bot is level ${esc((p.summary || {}).level)} with ${esc((p.summary || {}).level >= 10 ? (p.summary.level - 9) : 0)} point(s) available. Trees appear here once points are allocated.</div>`;
      return;
    }
    const active = state.armoryTalentTab || 0;
    const tabs = trees.map((t, i) => `<button class="armory-subtab${i === active ? ' active' : ''}" data-ttab="${i}">${esc(t.name)} (${esc(t.points)})</button>`).join('');
    const tree = trees[Math.min(active, trees.length - 1)];
    // 1.12 talent frame: 7 tiers x 4 columns. Index by (row, col) so each
    // talent sits in its DBC tier/column cell; empty cells stay as gaps.
    const maxRow = Math.max(0, ...(tree.talents || []).map(n => n.row || 0));
    const grid = [];
    for (let r = 0; r <= Math.max(maxRow, 6); r++) {
      const cells = [];
      for (let c = 0; c < 4; c++) {
        const n = (tree.talents || []).find(t => (t.row || 0) === r && (t.col || 0) === c);
        if (!n) { cells.push(`<div class="talent-cell-empty"></div>`); continue; }
        const cls = n.rank > 0 ? (n.rank >= n.max_rank ? 'learned' : 'partial') : 'unlearned';
        const iconUrl = getItemIconUrl(n.icon);
        const iconHtml = iconUrl
          ? `<img src="${iconUrl}" alt="${esc(n.name)}" onerror="this.onerror=null; this.style.display='none';" class="talent-icon-img">`
          : `<div class="talent-icon-placeholder">${esc((n.name || 'T')[0])}</div>`;
        const tip = talentTooltipHtml(tree.name, n, p);
        cells.push(`<div class="talent-node-box ${cls}" data-sptip="${esc(tip)}"><div class="talent-node-icon-wrap">${iconHtml}<div class="talent-rank-badge">${esc(n.rank)}/${esc(n.max_rank)}</div></div><div class="talent-node-name">${esc(n.name || `Talent ${n.talent_id}`)}</div></div>`);
      }
      const need = r * 5;
      grid.push(`<div class="talent-tier"><div class="talent-tier-label">Tier ${r + 1}<span>req ${need}</span></div><div class="talent-tier-nodes">${cells.join('')}</div></div>`);
    }
    host.innerHTML = `<div class="empty-hint" style="margin-bottom: 10px;">${esc(spent)} point(s) spent</div><div class="armory-subtabs" style="border-bottom: none; padding-bottom: 0;">${tabs}</div><div class="talent-tree"><div class="talent-tree-head"><span>${esc(tree.name)}</span><span class="badge badge-info">${esc(tree.points)} pts</span></div>${grid.join('')}</div>`;
    bindSpellTooltips(host);
    host.querySelectorAll('[data-ttab]').forEach(btn => {
      btn.addEventListener('click', () => { state.armoryTalentTab = parseInt(btn.dataset.ttab, 10) || 0; renderArmoryTalents(p); });
    });
  }

  // One spellbook table. Passive spells carry no cast state of their own, so
  // they never inherit the persisted spellbook Active/Inactive flag.
  function spellTable(list) {
    const rows = list.map(sp => {
      const badge = sp.passive
        ? `<span class="badge">Passive</span>`
        : (sp.origin === 'starting'
          ? `<span class="badge badge-info">Starting</span>`
          : (sp.disabled ? `<span class="badge badge-warn">Disabled</span>` : (sp.active ? `<span class="badge badge-success">Active</span>` : `<span class="badge">Inactive</span>`)));
      const tip = spellTooltip(sp);
      const iconUrl = getItemIconUrl(sp.icon);
      const iconHtml = iconUrl
        ? `<img src="${iconUrl}" onerror="this.style.display='none';" class="spell-table-icon">`
        : '';
      return `<tr data-sptip="${esc(tip)}"><td class="mono" style="color: var(--text-dim);">#${esc(sp.spell)}</td><td><div style="display: flex; align-items: center; gap: 8px;">${iconHtml}<div><span style="font-weight: 600;">${esc(sp.name || `Spell ${sp.spell}`)}</span>${sp.subtext ? ` <span style="color: var(--text-muted);">${esc(sp.subtext)}</span>` : ''}</div></div></td><td>${esc(spellSchoolName(sp.school))}</td><td>${badge}</td></tr>`;
    }).join('');
    return `<div style="overflow-x: auto;"><table class="data-table"><thead><tr><th>ID</th><th>Spell</th><th>School</th><th>State</th></tr></thead><tbody>${rows}</tbody></table></div>`;
  }

  function renderArmorySpells(p) {
    const host = document.getElementById('armory-content-spells');
    if (!host) return;
    const q = (state.armorySpellFilter || '').toLowerCase().trim();
    const all = p.spells || [];
    const filtered = all.filter(sp => {
      if (!q) return true;
      return (sp.name || '').toLowerCase().includes(q) || String(sp.spell).includes(q);
    });
    const passiveTotal = filtered.filter(sp => sp.passive).length;
    const passiveNote = passiveTotal ? ` · ${passiveTotal} passive` : '';
    let html = `<div style="display: flex; gap: 10px; align-items: center; margin-bottom: 12px; flex-wrap: wrap;"><span style="color: var(--text-muted); font-size: 0.8rem;">${filtered.length}/${all.length} spells${passiveNote}</span><input id="armory-spell-filter" class="btn" style="padding: 6px 12px; min-width: 200px;" placeholder="Filter spells..." value="${esc(state.armorySpellFilter || '')}"></div>`;
    if (!filtered.length) {
      html += `<div class="empty-hint">No spells match.</div>`;
    } else {
      const groups = {};
      filtered.forEach(sp => {
        const g = spellBucket(sp);
        (groups[g] = groups[g] || []).push(sp);
      });
      ['Class spells', 'Abilities', 'Auras & Forms', 'Pet & Minions'].forEach(g => {
        const list = (groups[g] || []).slice(0, 500);
        if (!list.length) return;
        // Passives are never cast (talent effects, trigger-only auras), so they
        // never answer "can this bot use this spell" — keep them apart.
        const active = list.filter(sp => !sp.passive);
        const passive = list.filter(sp => sp.passive);
        const counts = passive.length ? ` <span style="font-weight: 400; color: var(--text-muted);">(${active.length} active · ${passive.length} passive)</span>` : '';
        html += `<div class="section-label" style="margin: 14px 0 8px;">${esc(g)} · ${list.length}${counts}</div>`;
        if (active.length) html += spellTable(active);
        if (passive.length) {
          html += `<div class="section-label" style="margin: 10px 0 6px; font-size: 0.7rem; color: var(--text-muted);">PASSIVE · ${passive.length}</div>`;
          html += spellTable(passive);
        }
      });
      const prof = (groups['Professions'] || []).slice(0, 500);
      if (prof.length) {
        html += `<div class="empty-hint" style="margin-top: 12px;">${prof.length} profession spell(s) moved to the <strong>Professions</strong> tab.</div>`;
      }
    }
    host.innerHTML = html;
    bindSpellTooltips(host);
    const input = document.getElementById('armory-spell-filter');
    if (input) {
      input.addEventListener('input', e => {
        state.armorySpellFilter = e.target.value;
        const pos = e.target.selectionStart;
        renderArmorySpells(p);
        const next = document.getElementById('armory-spell-filter');
        if (next) { next.focus(); try { next.setSelectionRange(pos, pos); } catch (err) {} }
      });
    }
  }

  function skillTable(list) {
    return `<div style="overflow-x: auto;"><table class="data-table"><thead><tr><th>Skill</th><th>Value</th><th>Max</th><th style="width: 40%;">Progress</th></tr></thead><tbody>${list.map(sk => {
      const pct = sk.max ? Math.min(100, Math.round((sk.value / sk.max) * 100)) : 0;
      const color = sk.max && sk.value >= sk.max ? 'var(--accent-green-bright)' : (pct >= 70 ? 'var(--accent-blue-bright)' : 'var(--accent-yellow)');
      const iconName = skillIconById(sk.skill);
      const iconUrl = getItemIconUrl(iconName);
      const iconHtml = iconUrl
        ? `<img src="${iconUrl}" onerror="this.style.display='none';" class="spell-table-icon">`
        : '';
      return `<tr>
        <td>
          <div style="display: flex; align-items: center; gap: 8px;">
            ${iconHtml}
            <div>
              <span style="font-weight: 600;">${esc(skillNameById(sk.skill))}</span>
              <span class="mono" style="color: var(--text-dim); font-size: 0.7rem; margin-left: 4px;">#${esc(sk.skill)}</span>
            </div>
          </div>
        </td>
        <td class="mono">${esc(sk.value)}</td>
        <td class="mono" style="color: var(--text-muted);">${esc(sk.max)}</td>
        <td>
          <div class="progress-bar-bg">
            <div class="progress-bar-fill" style="width: ${pct}%; background: ${color};"></div>
          </div>
        </td>
      </tr>`;
    }).join('')}</tbody></table></div>`;
  }
  // Professions tab: profession skills with live levels from character_skills,
  // plus the profession's own spells. Class spells stay in the Spells tab;
  // weapon/armor/language rows stay in Skills.
  function renderArmoryProfessions(p) {
    const host = document.getElementById('armory-content-professions');
    if (!host) return;
    const byId = {};
    (p.skills || []).forEach(sk => {
      if (!byId[sk.skill] || (sk.value || 0) > (byId[sk.skill].value || 0)) byId[sk.skill] = sk;
    });
    const profSkills = PROFESSION_SKILL_IDS.map(id => byId[id]).filter(Boolean);
    const spells = (p.spells || []).filter(sp => spellBucket(sp) === 'Professions');
    if (!profSkills.length && !spells.length) {
      host.innerHTML = `<div class="empty-hint">No professions learned yet.</div>`;
      return;
    }
    let html = '';
    if (profSkills.length) {
      html += `<div class="section-label" style="margin: 0 0 8px;">PROFESSIONS · ${profSkills.length}</div>${skillTable(profSkills)}`;
    }
    if (spells.length) {
      const active = spells.filter(sp => !sp.passive);
      const passive = spells.filter(sp => sp.passive);
      html += `<div class="section-label" style="margin: 14px 0 8px;">PROFESSION SPELLS · ${spells.length}</div>`;
      if (active.length) html += spellTable(active);
      if (passive.length) {
        html += `<div class="section-label" style="margin: 10px 0 6px; font-size: 0.7rem; color: var(--text-muted);">PASSIVE · ${passive.length}</div>`;
        html += spellTable(passive);
      }
    }
    host.innerHTML = html;
    bindSpellTooltips(host);
  }

  function renderArmorySkills(p) {
    const host = document.getElementById('armory-content-skills');
    if (!host) return;
    // Dedupe: character_skills has one row per skill, but UNION-style profile
    // assembly (or a stale double-read) can surface the same id twice. One
    // row per id, highest value wins; professions live in their own tab now.
    const byId = {};
    (p.skills || []).forEach(sk => {
      if (!byId[sk.skill] || (sk.value || 0) > (byId[sk.skill].value || 0)) byId[sk.skill] = sk;
    });
    const skills = Object.values(byId).filter(sk => !PROFESSION_SKILL_IDS.includes(sk.skill));
    if (!skills.length) {
      host.innerHTML = `<div class="empty-hint">No skill rows recorded for this bot.</div>`;
      return;
    }
    const groupOf = id => COMBAT_SKILL_IDS.includes(id) ? 0
      : (ARMOR_SKILL_IDS.includes(id) ? 1 : 2);

    const sorted = [...skills].sort((a, b) => groupOf(a.skill) - groupOf(b.skill) || skillNameById(a.skill).localeCompare(skillNameById(b.skill)));
    const groups = [[], [], []];
    sorted.forEach(sk => groups[groupOf(sk.skill)].push(sk));
    const titles = ['Weapons & Defense', 'Armor Proficiencies', 'Languages'];
    host.innerHTML = groups.map((g, i) => g.length ? `<div class="section-label" style="margin: 14px 0 8px;">${titles[i]}</div>${skillTable(g)}` : '').join('') || `<div class="empty-hint">No skills.</div>`;
  }

  // REST helpers: a 401 means the GM session expired, so send the operator to
  // the login page instead of silently rendering stale/empty data; any other
  // non-2xx is an error the caller's catch handles.
  function jsonOrThrow(r) {
    if (r.status === 401) {
      window.location.href = '/login';
      throw new Error('unauthorized');
    }
    if (!r.ok) throw new Error(`http ${r.status}`);
    return r.json();
  }

  function fetchAnomalies() {
    fetch('/api/v1/anomalies')
      .then(jsonOrThrow)
      .then(data => {
        state.anomalies = Array.isArray(data) ? data.slice(0, ANOMALY_MAX) : [];
        renderAnomalies();
      })
      .catch(() => {});
    fetchAnomalyTotals();
  }

  // Cumulative session totals behind GET /api/v1/anomalies/totals: the
  // rolling feed above loses whole-run counts (BOT_DEATH fills the 1000-row
  // window in ~24 min at 500-bot rates), so this line is the full picture.
  // Kept in totals state, not the incident rows: Clear wipes the window
  // (state.anomalies) but leaves these counters untouched.
  function fetchAnomalyTotals() {
    fetch('/api/v1/anomalies/totals')
      .then(jsonOrThrow)
      .then(data => {
        state.anomalyTotals = data || null;
        renderAnomalyTotals();
      })
      .catch(() => {});
  }

  function renderAnomalyTotals() {
    if (!el.anomaliesTotals) return;
    const t = state.anomalyTotals;
    if (!t || !t.totals) {
      el.anomaliesTotals.textContent = 'Session totals: no data yet.';
      return;
    }
    const order = ['ACTION_LOOP', 'UNREACHABLE_TARGET', 'BOT_DEATH', 'STUCK'];
    const parts = [];
    for (const k of order) {
      if (t.totals[k] != null) parts.push(`${k} ${t.totals[k]}`);
    }
    for (const k of Object.keys(t.totals || {})) {
      if (order.indexOf(k) === -1) parts.push(`${k} ${t.totals[k]}`);
    }
    const since = t.since_str ? ` since ${t.since_str.slice(0, 16)}` : '';
    const loops = topAction(t, 'ACTION_LOOP');
    const unreach = topAction(t, 'UNREACHABLE_TARGET');
    const extra = [loops, unreach].filter(Boolean).join(' · ');
    el.anomaliesTotals.textContent =
      `Session totals${since}: ${parts.length ? parts.join(' · ') : 'none'}` +
      (extra ? ` (top: ${extra})` : '');
  }

  // Most common last_action for one anomaly type, e.g. "move 41".
  function topAction(t, type) {
    const m = (t.by_action && t.by_action[type]) || null;
    if (!m) return '';
    let best = '', bestN = 0;
    for (const k of Object.keys(m)) {
      if (m[k] > bestN) { bestN = m[k]; best = k; }
    }
    return best ? `${type} top ${best} ${bestN}` : '';
  }

  // Same bound as the daemon's ring buffer: a long session must not grow the
  // array (and the table) past what the server itself keeps.
  const ANOMALY_MAX = 1000;

  function renderAnomalies() {
    if (!el.anomaliesTable) return;

    const filtered = state.anomalies.filter(a => {
      const sev = (a.severity || '').toLowerCase();
      if (state.anomalyTypeFilter !== 'all' && a.type !== state.anomalyTypeFilter) return false;
      if (state.anomalySeverityFilter !== 'all' && sev !== state.anomalySeverityFilter) return false;
      return true;
    });

    if (filtered.length === 0) {
      el.anomaliesTable.innerHTML = '<tr><td colspan="7" class="table-empty">No incidents recorded.</td></tr>';
      return;
    }

    // Newest first, independent of the order the server returned them in.
    const ordered = filtered.slice().sort((a, b) => (b.ts || 0) - (a.ts || 0));

    el.anomaliesTable.innerHTML = ordered.map(a => {
      const timeStr = a.time_str || (a.ts ? new Date(a.ts * 1000).toLocaleTimeString() : '-');
      const sev = (a.severity || 'info').toLowerCase();
      const sevBadge = sev === 'error' ? 'badge-error' : sev === 'warn' ? 'badge-warn' : 'badge-info';
      return `<tr>
        <td class="mono cell-time">${esc(timeStr)}</td>
        <td><span class="badge ${sevBadge}">${esc(sev.toUpperCase())}</span></td>
        <td class="mono">${esc(a.type)}</td>
        <td>${a.guid ? botLink(a.guid, a.bot || '-', a.class) : esc(a.bot || '-')}</td>
        <td class="mono">${esc(getZoneName(a.zone, a.map))}</td>
        <td class="cell-muted">${esc(a.details || a.last_action || '-')}</td>
        <td class="target-cell">${esc(a.target && a.bot && a.target === a.bot ? 'self' : (a.target || '-'))}</td>
      </tr>`;
    }).join('');
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
      fetch('/api/v1/anomalies', { method: 'DELETE' })
        .then(() => {
          state.anomalies = [];
          renderAnomalies();
        })
        .catch(() => {});
    });
  }

  function fetchIssues(force = false) {
    if (state.wsConnected && !force) return;
    fetch('/api/v1/issues')
      .then(jsonOrThrow)
      .then(data => { applyIssues(data); renderLive(); })
      .catch(() => {});
  }

  // REST fallback used by the Refresh button and while the socket is down.
  function fetchBots(force = false) {
    if (state.wsConnected && !force) return;
    fetch('/api/v1/bots')
      .then(jsonOrThrow)
      .then(data => {
        if (!Array.isArray(data)) return;
        state.bots = data;
        updateOverviewMetrics();
        renderLive();
      })
      .catch(() => {});
    fetch('/api/v1/grinding').then(jsonOrThrow).then(g => {
      if (g && typeof g.bots_tracked === 'number') {
        state.grinding = g;
        renderLive();
      }
    }).catch(() => {});
    fetch('/api/v1/server-info').then(r => {
      if (!r.ok) return null;
      return r.json();
    }).then(info => {
      if (info && !info.error) { state.serverInfo = info; renderServerPanel(); }
    }).catch(() => {});
  }

  function applyServerStatus(s) {
    if (!s) return;
    state.server.online = !!s.online;
    state.server.stale = !!s.stale;
    state.server.uptime = s.uptime || 0;
    state.server.diff = s.diff || 0;
    state.server.diff_avg = s.diff_avg || 0;
    state.server.diff_worst = s.diff_worst || 0;
    state.server.lag_p50 = s.lag_p50 || 0;
    state.server.lag_p95 = s.lag_p95 || 0;
    state.server.humans = s.humans || 0;
    state.server.bots = s.bots || 0;
  }

  // WebSocket Live Streaming
  let reconnectTimer = null;

  function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/api/v1/stream`;
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      state.wsConnected = true;
      // Daemon restarts reset its sequence: drop the high-water mark or every
      // post-restart snapshot is discarded as stale while gauges look alive.
      state.snapshotSeq = 0;
      if (el.consoleRate) el.consoleRate.innerHTML = '● connected';
      appendConsoleLog(new Date().toLocaleTimeString(), 'ws', 'Connected to live telemetry stream.');
    };

    ws.onmessage = (event) => {
      try {
        handleStreamMessage(JSON.parse(event.data));
      } catch (e) {}
    };

    ws.onclose = () => {
      state.wsConnected = false;
      if (el.consoleRate) el.consoleRate.innerHTML = '<span style="color: #f85149;">● disconnected</span>';
      if (reconnectTimer) clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(initWebSocket, 3000);
    };
  }

  // Re-render what a fresh roster/issue snapshot changes on the visible tab,
  // plus the open profile's live part. Hidden tabs wait for their activation.
  function renderLive() {
    const tab = state.activeTab;
    if (tab === 'overview') { renderStates(); renderAttention(); }
    else if (tab === 'map') renderMap();
    else if (tab === 'roster') renderRoster();
    else if (tab === 'progress') { renderProgressKpis(); renderLevelBands(); }
    renderProfileLive();
  }

  function handleStreamMessage(msg) {
    const time = new Date().toLocaleTimeString();

    if (msg.event === 'snapshot') {
      const data = msg.data;
      if (!data) return;
      if (typeof data.seq === 'number' && data.seq < state.snapshotSeq) return;
      state.snapshotSeq = data.seq || 0;

      const prevCount = state.bots.length;
      state.bots = Array.isArray(data.bots) ? data.bots : [];
      if (data.grinding) state.grinding = data.grinding;
      if (data.info) { state.serverInfo = data.info; renderServerPanel(); }
      state.lastSnapshotAt = Date.now();
      applyServerStatus(data.server);
      applyIssues(data.issues);
      updateOverviewMetrics();

      renderLive();

      if (state.bots.length !== prevCount) {
        appendConsoleLog(time, 'roster', `Snapshot #${esc(state.snapshotSeq)}: ${state.bots.length} bots active.`);
      }
    } else if (msg.event === 'heartbeat') {
      const d = msg.data;
      if (!d) return;
      state.lastHeartbeatAt = Date.now();
      state.server.online = true;
      state.server.stale = false;
      state.server.uptime = d.uptime || 0;
      state.server.diff = d.diff || 0;
      state.server.diff_avg = d.diff_avg || 0;
      state.server.diff_worst = d.diff_worst || 0;
      state.server.lag_p50 = d.lag_p50 || 0;
      state.server.lag_p95 = d.lag_p95 || 0;
      state.server.humans = d.humans || 0;
      state.server.bots = d.bots || 0;
      pushHistory();
      updateOverviewMetrics();
      if (state.activeTab === 'overview') renderOverviewCharts();
      // Heartbeats arrive every 2s; log sparsely to keep the console useful.
      if (!handleStreamMessage._lastPulse || Date.now() - handleStreamMessage._lastPulse > 30000) {
        handleStreamMessage._lastPulse = Date.now();
        appendConsoleLog(time, 'pulse', `diff=${esc(d.diff)}ms, players=${esc(d.humans)}, bots=${esc(d.bots)}`);
      }
    } else if (msg.event === 'status') {
      applyServerStatus(msg.data);
      updateOverviewMetrics();
    } else if (msg.event === 'server_info') {
      if (msg.data) { state.serverInfo = msg.data; renderServerPanel(); }
    } else if (msg.event === 'anomaly') {
      const a = msg.data;
      if (a) {
        state.anomalies.push(a);
        // Keep the array at the daemon's own ring-buffer bound.
        if (state.anomalies.length > ANOMALY_MAX) state.anomalies.splice(0, state.anomalies.length - ANOMALY_MAX);
        // Rebuild the (up to 1000-row) table only when it is on screen.
        if (state.activeTab === 'issues') { renderAnomalies(); renderIssues(); }
        // The rolling feed above just grew by one; the cumulative session
        // totals behind the totals line grew with it, so refetch them.
        fetchAnomalyTotals();
        const sev = (a.severity || 'info').toLowerCase();
        appendConsoleLog(time, a.type, `<span class="log-bot">${esc(a.bot || 'Bot')}</span>: ${esc(a.details || a.last_action || '')}`, sev);
      }
    }
  }

  // ---- Persistent issues --------------------------------------------------

  function fmtDuration(sec) {
    sec = Math.max(0, Math.round(sec || 0));
    if (sec < 60) return `${sec}s`;
    const m = Math.floor(sec / 60);
    if (m < 60) return `${m}m ${String(sec % 60).padStart(2, '0')}s`;
    return `${Math.floor(m / 60)}h ${String(m % 60).padStart(2, '0')}m`;
  }

  function issueSet() {
    const set = {};
    state.issues.active.forEach(i => { set[i.guid] = i; });
    return set;
  }

  function applyIssues(issues) {
    if (!issues) return;
    state.issues = {
      active: Array.isArray(issues.active) ? issues.active : [],
      resolved: Array.isArray(issues.resolved) ? issues.resolved : [],
      counts_by_type: issues.counts_by_type || {}
    };
    pushIssueHistory();
    updateIssueBadges();
    if (state.activeTab === 'issues') { renderIssues(); renderAnomalies(); }
  }

  function pushIssueHistory() {
    state.issueHistory.t.push(Date.now());
    ISSUE_TYPES.forEach(t => {
      if (!state.issueHistory.series[t]) state.issueHistory.series[t] = [];
      state.issueHistory.series[t].push(state.issues.counts_by_type[t] || 0);
    });
    if (state.issueHistory.t.length > ISSUE_HISTORY_MAX) {
      state.issueHistory.t.shift();
      ISSUE_TYPES.forEach(t => state.issueHistory.series[t].shift());
    }
  }

  function updateIssueBadges() {
    const active = state.issues.active.length;
    const persistent = state.issues.active.filter(i => i.severity === 'persistent').length;
    // The owner reads the persistent (>=10 min) subset only; watch episodes
    // must not inflate the sidebar badge. At zero the badge hides entirely
    // rather than sitting there as a blue "0" next to the label.
    if (el.issuesCount) {
      el.issuesCount.textContent = persistent;
      el.issuesCount.style.display = persistent > 0 ? '' : 'none';
      el.issuesCount.className = 'badge badge-error';
    }
    if (el.metricIssues) {
      el.metricIssues.textContent = active;
      el.metricIssues.className = `kpi-value ${persistent > 0 ? 'kpi-red' : active > 0 ? 'kpi-amber' : ''}`;
    }
    if (el.metricIssuesSub) el.metricIssuesSub.textContent = `${persistent} persistent`;
  }

  function renderIssues() {
    const active = state.issues.active;
    if (el.issueActive) el.issueActive.textContent = active.length;
    if (el.issuePersistent) el.issuePersistent.textContent = active.filter(i => i.severity === 'persistent').length;
    if (el.issueWatch) el.issueWatch.textContent = active.filter(i => i.severity === 'watch').length;
    if (el.issueDeaths) el.issueDeaths.textContent = state.anomalies.filter(a => a.type === 'BOT_DEATH').length;

    renderIssueTable();
    renderIssueZones();
    renderIssueChart();
    renderResolvedList();
  }

  function renderIssueTable() {
    if (!el.issuesTable) return;
    const minDur = state.issueDurationFilter;
    // A row without a duration is unknown, not "short": keep it rather than
    // silently hiding an episode the watch card above already counted.
    const filtered = state.issues.active
      .filter(i => (state.issueTypeFilter === 'all' || i.type === state.issueTypeFilter) &&
        (minDur === 0 || i.duration_sec == null || i.duration_sec >= minDur))
      .sort((a, b) => (b.duration_sec || 0) - (a.duration_sec || 0));

    if (filtered.length === 0) {
      el.issuesTable.innerHTML = '<tr><td colspan="7" class="table-empty">No matching issues.</td></tr>';
      return;
    }

    el.issuesTable.innerHTML = filtered.map(i => {
      // Anomaly-derived rows (UNREACHABLE_TARGET) never carry a trigger.
      // Details carry the emitter's unreachable description instead; snapshot
      // rows (STUCK) keep their trigger appended so no context is lost.
      const action = (i.type === 'UNREACHABLE_TARGET' && i.details)
        ? esc(i.details)
        : `${esc(i.action || i.details || '-')}${i.trigger ? ` <span class="cell-muted">· ${esc(i.trigger)}</span>` : ''}`;
      return `<tr>
        <td>${botLink(i.guid, i.bot, i.class)}</td>
        <td>${esc(capClass(i.class))}</td>
        <td><span class="badge ${i.severity === 'persistent' ? 'badge-error' : 'badge-warn'}">${esc(ISSUE_LABELS[i.type] || i.type)}</span></td>
        <td class="mono" style="color: ${i.severity === 'persistent' ? '#f85149' : '#d29922'};">${esc(fmtDuration(i.duration_sec))}</td>
        <td class="mono">${action}</td>
        <td class="mono">${esc(getZoneName(i.zone, i.map))}</td>
        <td class="target-cell">${esc(i.target && i.bot && i.target === i.bot ? 'self' : (i.target || '-'))}</td>
      </tr>`;
    }).join('');
  }

  function renderIssueZones() {
    if (!el.issueZones) return;
    const zones = new Map();
    state.issues.active.forEach(i => zones.set(`${i.map}_${i.zone}`, (zones.get(`${i.map}_${i.zone}`) || 0) + 1));
    if (zones.size === 0) {
      el.issueZones.innerHTML = '<div class="empty-hint">No active issues.</div>';
      return;
    }
    const entries = [...zones.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8);
    const max = entries[0][1] || 1;
    el.issueZones.innerHTML = entries.map(([key, n]) => {
      const [mapIdStr, zoneIdStr] = key.split('_');
      return `<div class="comp-row"><span class="comp-name" style="width:110px;">${esc(getZoneName(Number(zoneIdStr), Number(mapIdStr)))}</span><span class="comp-bar-bg"><span class="comp-bar" style="width:${Math.round((n / max) * 100)}%;background:#f85149;"></span></span><span class="comp-count">${n}</span></div>`;
    }).join('');
  }

  function renderIssueChart() {
    const prepared = prepCanvas(el.issueChart);
    if (!prepared) return;
    const { ctx, w, h } = prepared;
    const hist = state.issueHistory;
    const len = hist.t.length;

    if (el.issueLegend) {
      el.issueLegend.innerHTML = ISSUE_TYPES.map(t =>
        `<div class="legend-item"><span class="legend-color" style="background:${ISSUE_COLORS[t]};"></span>${esc(ISSUE_LABELS[t])}</div>`
      ).join('');
    }

    if (len === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '12px Inter';
      ctx.fillText('Collecting issue history...', 12, h / 2);
      return;
    }

    let maxVal = 1;
    for (let i = 0; i < len; i++) {
      let sum = 0;
      ISSUE_TYPES.forEach(t => { sum += (hist.series[t] || [])[i] || 0; });
      if (sum > maxVal) maxVal = sum;
    }
    maxVal = Math.ceil(maxVal * 1.2);

    const padTop = 10, padBottom = 12;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = len > 1 ? w / (len - 1) : 0;
    const xFor = i => len > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    let lower = new Array(len).fill(0);
    ISSUE_TYPES.forEach(t => {
      const series = hist.series[t] || [];
      const upper = lower.map((v, i) => v + (series[i] || 0));
      ctx.beginPath();
      for (let i = 0; i < len; i++) {
        const x = xFor(i), y = yFor(upper[i]);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      for (let i = len - 1; i >= 0; i--) ctx.lineTo(xFor(i), yFor(lower[i]));
      ctx.closePath();
      ctx.fillStyle = hexToRgba(ISSUE_COLORS[t], 0.5);
      ctx.fill();

      ctx.beginPath();
      for (let i = 0; i < len; i++) {
        const x = xFor(i), y = yFor(upper[i]);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = ISSUE_COLORS[t];
      ctx.lineWidth = 1.5;
      ctx.stroke();

      lower = upper;
    });
  }

  // Resolved card counts every archived episode; the list shows the newest
  // slice so the DOM stays small. Keep the two in sync with one label.
  function renderResolvedList() {
    if (!el.issuesResolved) return;
    const list = state.issues.resolved.slice(0, 12);
    if (el.issueResolved) el.issueResolved.textContent = state.issues.resolved.length ? `${list.length}/${state.issues.resolved.length}` : '0';
    if (list.length === 0) {
      el.issuesResolved.innerHTML = '<div class="empty-hint">No resolved episodes yet.</div>';
      return;
    }
    el.issuesResolved.innerHTML = list.map(i =>
      `<div class="resolved-row"><span class="badge badge-info">${esc(ISSUE_LABELS[i.type] || i.type)}</span><strong>${botLink(i.guid, i.bot, i.class)}</strong><span class="cell-muted">${esc(getZoneName(i.zone, i.map))}</span><span class="mono" style="margin-left: auto; color: #2ea043;">${esc(fmtDuration(i.duration_sec))}</span></div>`
    ).join('');
  }


  // Issues: one panel, three lists (active / resolved / incident feed).
  function showIssuesView(view) {
    state.issuesView = view;
    paintSeg(el.issuesSeg, view);
    document.querySelectorAll('[data-issues]').forEach(n => { n.hidden = n.dataset.issues !== view; });
  }

  function initIssues() {
    bindSeg(el.issuesSeg, showIssuesView);
    if (el.issueTypeFilter) {
      el.issueTypeFilter.addEventListener('change', (e) => {
        state.issueTypeFilter = e.target.value;
        renderIssueTable();
      });
    }
    if (el.issueDurationFilter) {
      el.issueDurationFilter.addEventListener('change', (e) => {
        state.issueDurationFilter = parseInt(e.target.value, 10) || 0;
        renderIssueTable();
      });
    }
  }

  // Local watchdog: the daemon cannot tell us it died, so the client decides
  // based on pulse age. This is what makes server-online trustworthy.
  setInterval(() => {
    updateSnapshotAge();
    const age = state.lastHeartbeatAt ? Date.now() - state.lastHeartbeatAt : Infinity;
    if (age > OFFLINE_AFTER_MS && state.server.online) {
      state.server.online = false;
      state.server.stale = true;
      updateOverviewMetrics();
      if (state.activeTab === 'overview') renderOverviewCharts();
      appendConsoleLog(new Date().toLocaleTimeString(), 'watchdog', 'No heartbeat for 10s: marking server offline.', 'warn');
    }
  }, 3000);

  // Init
  bindMapOverlay(el.mapOverlay);
  bindMapOverlay(el.worldOverlay);
  initZoneSelector();
  initQuickFind();
  initBots();
  initActivity();
  initProfile();
  initIssues();
  initDiagButton();
  renderServerPanel();
  fetchBots();
  fetchAnomalies();
  fetchIssues();
  fetchActivity();
  fetchRoster();
  setInterval(fetchActivity, 30000);
  setInterval(fetchRoster, 60000);
  initWebSocket();

  // First route: the URL hash wins; without one, return to the last tab used.
  if (!parseRoute().tab) {
    let tab = 'overview';
    try {
      const saved = localStorage.getItem(TAB_STORAGE_KEY);
      if (saved && TABS[saved]) tab = saved;
    } catch (e) {}
    history.replaceState(null, '', routeHash({ tab }));
  }
  applyRoute();
})();
