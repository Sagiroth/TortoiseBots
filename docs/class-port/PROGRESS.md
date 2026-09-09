# Class behavior port — progress ledger

Tracking implementation batches against CLASS_BEHAVIOR_PORT_GOAL.md / PLAN.md.
Gameplay acceptance requires real-client evidence; tests/builds alone do not close #93/#94.

## Pins (rechecked 2026-09-08)
- TortoiseBots `1f155e85` (main, + dirty P1/class work in progress)
- tortoise-wow `9f778a73` (fix/headless-packets-chat-hooks)
- mod-playerbots `b949b50`, shyalya `83a61bc`
- Spell.dbc `7d9f83e7…`, Talent.dbc `48d8d121…`
- Core loads spells from `spell_template` (SpellMgr::LoadSpells); base SQL is
  superseded by migrations/live data. Census joins base SQL + migrations +
  script bindings + DBC.

## Batch 1 — P1 shared correctness (done, uncommitted)
- Fixed `TalentSpec::CheckTalents` one-based vs zero-based `DependsOnRank`
  mismatch (`dep.rank >= DependsOnRank` → `dep.rank > DependsOnRank`), added
  `DependsOnSpell` (talent-spell) check and negative-rank guard.
  File: `ai/playerbot/Talentspec.cpp`.
- Added `tools/talents/validate_presets.py` (mirrors GetTalents/Sort/Read/Check
  incl. tabPage 41→1 quirk, truncated-link semantics, row gating, DependsOn
  with +1 semantics, DependsOnSpell talent-only, budget, monotonicity).
  Result: 242 links, 0 failures after preset fixes (was 3 failures).
- Fixed three level-35 prerequisite-order defects (defer dependent, take
  prerequisite, keep budget/monotonicity):
  `2.0.35` → `05320302035201`, `9.0.35` → `55002230022311`,
  `11.0.35` → `500002303125311`. File: `aiplayerbot.conf.dist.in`.
- Arcane Power 12042 safety: dedicated `ArcanePowerTrigger::IsActive`
  (combat + live target + mana ≥70, citing 20s drain + CANT_CANCEL lethality)
  and `CastArcanePowerAction::isPossible/isUseful` floor (defense in depth).
  Ongoing cancellation is impossible (`SPELL_ATTR_CANT_CANCEL` 0x80000000 +
  "cannot be cancelled"); safety is prevention-only, threshold documented.
  Files: `strategy/mage/MageTriggers.h/.cpp`, `strategy/mage/MageActions.h`.
- Checks: `git diff --check` clean, `validate_presets.py` 0 failures,
  `verify_tortoise_surface.sh` OK, `verify_penqle_host_contract.sh` OK (core
  9f778a73). No docker build per user instruction (deferred to user review).
- Next: native cached module build via `./dev/build-playerbots` (user-owned),
  then class packets.

## Batch 2 — Warrior Master Strike (done, uncommitted)
- Census (Luna WarriorCensus): Last Stand / Concussion Blow / Piercing Howl
  (via hamstring) / Sweeping Strikes (via light-aoe + 12292 multipliers) are
  automatic; Defensive Tactics is passive; Master Strike 54023 was the only
  true gap (zero local surface). Stance prerequisites 95% missing (factory
  commented out / dead code) — deferred, not regressed.
- Implemented `CastMasterStrikeAction` (weapon-presence gate; core dispatches
  54023→helpers 54016-54022, polearm dismount-gated, 30s cooldown, 20 rage)
  + `MasterStrikeTrigger` (CanCast + live target + main hand), registered both
  creators, wired Arms (`master strike` NORMAL+4, above mortal) and Fury
  (`master strike` NORMAL+4, ties execute — documented, rare coincidence).
  Files: `strategy/warrior/WarriorActions.h`, `WarriorTriggers.h`,
  `WarriorAiObjectContext.cpp`, `ArmsWarriorStrategy.cpp`,
  `FuryWarriorStrategy.cpp`.
- Checks: diff clean, presets 0 failures, surface OK. No docker build.
- Remaining Warrior: selective stance prerequisites (tank taunt/revenge/shield
  block/wall → defensive; pummel/intercept → berserker), orphaned
  SweepingStrikesTrigger register-or-delete, Arms preset audit, Protection/Fury
  preset validation, per-profile positive/negative/competition scenarios.

## Batch 3 — Priest shields + Chastise (done, uncommitted)
- Census (Luna PriestCensus): new list-style hierarchy is registered; old
  Generic/Heal hierarchy is dead. Enlighten 51476 is passive (no surface).
  Ascendance 52962 has spell_template row + skill row but NO core script
  handler — deferred as unresolved, not implemented.
- Implemented Weakened Soul (6788) refusal in `CastPowerWordShieldAction` and
  `CastPowerWordShieldOnPartyAction` (donor pattern from mod-playerbots
  PriestActions.cpp). File: `strategy/priest/PriestActions.h`.
- Wired hostile Chastise into `PriestCcStrategy` combat at INTERRUPT
  (mirrors Shyalya CC; existing SNARE_TRIGGER signal). Friendly Chastise is
  situational/manual via explicit cast (core OnCheckCast enforces
  same-raid/level/≥80% health + haste reward); no automatic friendly trigger
  to avoid mis-buffing. File: `strategy/priest/PriestStrategy.cpp`.
- Checks: diff clean, presets 0 failures, surface OK. No docker build.
- Remaining Priest: Discipline Tortoise role rationale + preset, Holy preset
  validation, Shadowform restriction matrix, group-heal scenarios, Ascendance
  acquisition/mechanics proof (needs Talent.dbc + spell effect decode).

## 28-profile evidence matrix (this turn)
| # | Profile | Implementation-verified | Gameplay-accepted | Notes |
|---|---------|-------------------------|-------------------|-------|
| 1 | Warrior Protection | partial (threat/mitigation pre-existing; Master Strike n/a tank) | pending | stance prereqs + scenarios remain |
| 2 | Warrior Arms | partial (Master Strike + stance creators batch2/12; Arms preset batch12) | pending | scenarios remain |
| 3 | Warrior Fury | partial (Master Strike wired batch2) | pending | same as Arms |
| 4 | Priest Holy | partial (WS guard + Ascendance batch3/14) | pending | scenarios remain |
| 5 | Priest Discipline | partial (WS guard + hostile Chastise batch3; preset batch12) | pending | friendly-Chastise manual path + scenarios remain |
| 6 | Priest Shadow | partial (pre-existing; WS guard shared) | pending | form/heal matrix + scenarios remain |
| 7 | Mage Arcane | partial (Arcane Power safe; Rupture passive; evocation-check batch5) | pending | Rupture smoke test + presets + scenarios remain |
| 8 | Mage Fire | partial (pre-existing; evocation-check shared) | pending | proc/Combustion scenarios + preset validation remain |
| 9 | Mage Frost | partial (Icicles wired batch5) | pending | Rupture n/a; presets + scenarios remain |
| 10 | Rogue Combat | partial (Surprise wired + boost fix batch7) | pending | scenarios remain |
| 11 | Rogue Assassination | partial (generator + Noxious + Envenom batch7/14) | pending | scenarios remain |
| 12 | Rogue Subtlety | partial (SoD + Mark + Smoke batch14; preset batch12) | pending | scenarios remain |
| 13 | Hunter Beast Mastery | partial (Kill Command wired batch6; KC is BM capstone per Talent.dbc) | pending | pet cmds + scenarios remain |
| 14 | Hunter Marksmanship | partial (pre-existing; KC shared when learned) | pending | proc ordering + scenarios remain |
| 15 | Hunter Survival (Tortoise) | partial (Carve auto + Lacerate manual batch6; preset batch12) | pending | sting precedence + scenarios remain |
| 16 | Warlock Affliction | partial (Dark Harvest wired batch8; #92 preserved) | pending | Conflagrate n/a; curse/sacrifice/scenarios remain |
| 17 | Warlock Demonology | partial (Power Overwhelming wired batch8) | pending | sacrifice policy + scenarios remain |
| 18 | Warlock Destruction | partial (pre-existing; DH/PO shared when learned) | pending | Conflagrate policy + scenarios remain |
| 19 | Paladin Protection | partial (Holy Strike + Bulwark batch9) | pending | ordering + scenarios remain |
| 20 | Paladin Holy | partial (pre-existing + exorcism gate shared) | pending | HS discipline + Daybreak + churn + scenarios remain |
| 21 | Paladin Retribution | partial (Holy Strike batch9) | pending | ordering + scenarios remain |
| 22 | Druid Bear | partial (Berserk wired batch10; shared Feral preset) | pending | Tree decision + preset rationale + scenarios remain |
| 23 | Druid Cat | partial (Berserk wired batch10; shared Feral preset) | pending | same as Bear |
| 24 | Druid Balance | partial (Eclipse pre-existing verified) | pending | smoke test + scenarios remain |
| 25 | Druid Restoration | partial (Swiftmend wired batch10) | pending | Tree decision + scenarios remain |
| 26 | Shaman Restoration | partial (Spirit Link + Swiftness batch11) | pending | totems + scenarios remain |
| 27 | Shaman Enhancement | partial (Lightning Strike + Bloodlust batch11; LS/BL are enh talents per Talent.dbc) | pending | imbues + Bloodlust gate + scenarios remain |
| 28 | Shaman Elemental | partial (Earthquake wired batch11; preset batch12) | pending | Mastery status + scenarios remain |
## Batch 4 — Mage census (read-only, done)
- Census (Luna MageCensus): Arcane Power present + safe (Batch 1 gate);
  Rupture 51949→52502 is passive synergy with missiles default (no action
  needed, smoke-test only); Icicles 52516 needs new channeled action + cancel
  guard (manual/unsupported until built); Combustion/Cold Snap/PoM/mana-gems
  present; Evocation/Missiles lack cancel-channel guards (gap).
- No Mage source edits this batch (beyond Batch 1 safety).
- Next Mage: Icicles channeled action mirroring blizzard channel-check,
  Rupture→missiles smoke test, evocation/missiles cancel symmetry, three
  preset validations.

## Batch 5 — shared cancel-channel + Mage Icicles (done, uncommitted)
- Found `CancelChannelAction` (actions/CancelChannelAction.h/.cpp) with zero
  creators: every `NextAction("cancel channel")` repo-wide resolved to NULL,
  including the #92 Rain of Fire channel-cancel fix (WarlockStrategy.cpp:181)
  and the druid/hunter/mage generic-tree channel checks. Registered
  `creators["cancel channel"]` in ActionContext.h (include + creator). This
  re-enables the preserved #92 fix; no behavior invented.
- Implemented `CastIciclesAction` (my-attacker-count gate in isPossible),
  `IciclesTrigger` (can-cast + target alive + target health >30 + self ≥60 +
  no personal attackers), `IciclesChannelCheckTrigger` (ranks
  52516/51991/51995/51997, cancel when current target dead/gone),
  `EvocationChannelCheckTrigger` (12051, cancel at ≥95% mana). Registered all
  creators; wired `icicles` (ACTION_HIGH) + icicles-check (HIGH+3) into
  FrostMageStrategy combat and evocation-check (HIGH+4) into MageStrategy
  combat. Rupture→52502→missiles left passive (smoke-test pending).
  Files: MageActions.h, MageTriggers.h/.cpp, MageAiObjectContext.cpp,
  FrostMageStrategy.cpp, MageStrategy.cpp, actions/ActionContext.h.
- Checks: diff clean, presets 0 failures, surface + host contract OK.
  No docker build (user-owned review).
- Remaining Mage: Rupture smoke test, Arcane 70% floor vs measured mana,
  three preset validations, per-profile scenarios.

## Batch 6 — Hunter Kill Command + Carve (+ Lacerate manual) (done, uncommitted)
- Census (Luna HunterCensus): shared Hunter well-wired; BM combat adds nothing,
  MM adds only Trueshot Aura, Survival is 100% pass-through. Pet attack/recall
  missing; dead refs ('toggle pet spell'/'set pet stance'/'initialize pet');
  no Wolf/Viper aspect surface; dual list/vector strategy systems (live path
  unverified but list engine is the registered one).
- Verified casterAuraState 6 (crit window) on KC 41827 + Lacerate 48049, so core
  CanCastSpell enforces the window: no DBC guessing, SpellCanBeCasted triggers.
  Carve 51575/52415 has no aura gate, 10s category CD (shared with Multi-Shot
  per audit; core serializes).
- Implemented `CastKillCommandAction` + `KillCommandTrigger` (live-pet gate;
  core resolves explicit -> selected -> pet-victim), wired BM combat NORMAL+4.
  Implemented `CastCarveAction` + `CarveTrigger`, wired Survival AoE HIGH-1
  below multi-shot. `CastLacerateAction` registered WITHOUT auto trigger:
  SpellMgr aura exclusivity vs Serpent Sting would churn (lacerate over serpent
  -> 'no stings' reapplies serpent); auto deferred pending precedence design,
  manual path available. Lacerate verified melee-AP + side bonus + 48050 bleed.
  Files: HunterActions.h, HunterTriggers.h, HunterAiObjectContext.cpp,
  BeastMasteryHunterStrategy.cpp, SurvivalHunterStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host contract OK.
- Remaining Hunter: Survival preset, pet attack/recall + dead-ref repair, Wolf
  aspect, Viper sting, sting-vs-lacerate precedence, MM proc ordering, scenarios.

## Batch 7 — Rogue generator + Surprise + Noxious + boost fix (done, uncommitted)
- Census (Luna RogueCensus): Assassination has NO generator (white-hits until
  finishers); CombatBoost wires adrenaline/blade flurry to NON-combat (never
  fires); Envenom/Shadow/Mark/Smoke need slot decisions or in-game proof.
- Moved adrenaline rush + blade flurry to CombatRogueBoost combat (fix).
  Added `BackstabTrigger` creator, wired `backstab` NORMAL into Assassination
  combat (existing backstab->sinister node covers no-back-position).
  Added `CastSurpriseAttackAction` + reactive trigger (core REACTIVE_ROGUE_DODGE
  gate, Riposte melee sanity mirrored), wired Combat HIGH+3. Added
  `CastNoxiousAssaultAction` (Combo-gated; +30% AP + MH/OH poisons, poison
  absence scales value but never blocks) + trigger, wired Assassination
  NORMAL+1. Verified energy costs (Noxious 45, Surprise 10) via template.
- Deferred: Envenom (finisher priority vs eviscerate open), Shadow of Death
  (delayed-finisher slot), Mark for Death (opener/support slot), Smoke Bomb
  (no core script, needs in-game check). Kick energy reserve + expose-armor
  wiring + poison-item parity remain follow-ups.
  Files: RogueActions.h, RogueTriggers.h/.cpp, RogueAiObjectContext.cpp,
  CombatRogueStrategy.cpp, AssassinationRogueStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host contract OK.

## Batch 8 — Warlock Dark Harvest + Power Overwhelming (done, uncommitted)
- Dark Harvest 52550: channeled DoT accelerating own Affliction ticks
  (Corruption/Drain Life/CoA/Drain Soul/Siphon Life/Doom) with 30s-CD refund
  on target death. Trigger requires 2+ own DoTs; channel-check cancels only
  on a LIVE DoT-less target (dying targets keep channeling for the refund).
  Wired Affliction combat NORMAL+3 + check HIGH+3 (cancel now resolves via
  Batch 5 shared creator). Power Overwhelming 51714: pet burst (CC break +
  damage buff) costing demon health; action targets pet explicitly (core falls
  back to pet only when untargeted — targeting the enemy would no-op), trigger
  requires live 60%+ pet + live enemy. Wired Demonology NORMAL+3. #92
  sustain/pet/AoE behavior untouched (Life Tap floor, pet integration, RoF
  ordering verified present). Files: WarlockActions.h, WarlockTriggers.h/.cpp,
  WarlockAiObjectContext.cpp, Affliction/DemonologyWarlockStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host OK.
- Remaining Warlock: Destruction Conflagrate policy, curse ownership, explicit
  sacrifice/no-resummon policy, shard/stone handling, scenarios.

## Batch 9 — Paladin Holy Strike + Bulwark + Exorcism gate (done, uncommitted)
- Verified Bulwark rows (51346/51565: shield attack + DR buff, 2 ranks, 5min
  category). Added Holy Strike (R1-8 melee + Mending sustain, Ret/Prot NORMAL+1)
  and Bulwark (Prot HIGH+2, core enforces shield). Fixed Exorcism waste: was
  mana-gated only, now requires Art of War aura OR undead/demon target
  (conservative vs players: skipped, documented). Restored Prot exorcism node
  dropped mid-edit. Files: PaladinActions.h, PaladinTriggers.h/.cpp,
  PaladinAiObjectContext.cpp, Retribution/ProtectionPaladinStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host OK.
- Remaining Paladin: Holy Shock caller discipline, Daybreak FoL/HS consumers,
  seal/judgement ordering validation, forced-healer melee churn, scenarios.

## Batch 10 — Druid Berserk + Swiftmend (done, uncommitted)
- Census (Luna DruidCensus): OLD stack (Tank/Dps-Feral + Balance + Resto) is
  live; NEW Bear/Cat/Resto forward-port is UNWIRED (do not edit NEW files and
  expect runtime effects). Eclipse already correct in OLD Balance (HIGH+3 both
  directions, Tortoise 51442/51443 — donor solar/lunar IDs must not port).
  Savage Bite has no Tortoise evidence: not implemented (likely Savage Roar
  confusion). Tree of Life deferred: form-restriction audit needed first.
- Added BerserkTrigger (BoostTrigger; action already registered but never
  fired) wired into TankFeral + DpsFeral boost HIGH+2 (core branches
  Cat 45710 / Bear 45709 + form-swap migration). Added Swiftmend pair with
  HoT-presence refusal (core 18562 requires live Rejuv/Regrowth, consumes
  shortest) prepended to Resto critical nodes (self + party, CRITICAL+2 over
  regrowth). Bear/Cat share the single Feral 11.1 preset by runtime forced-role
  arbitration (documented rationale; no invented second tree). Files:
  DruidActions.h, DruidTriggers.h, DruidAiObjectContext.cpp,
  Tank/DpsFeralDruidStrategy.cpp, RestorationDruidStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host OK.
- Remaining Druid: Tree form decision, Eclipse smoke test, Bear/Cat explicit
  preset entries or rationale record, form-exit/recovery matrix, scenarios.

## Batch 11 — Shaman five talents + Bloodlust wiring (done, uncommitted)
- Census (thin): totem churn claims rechecked — HasTotemValue is owner-aware,
  totemic-recall trigger registered, air-totem double entry is
  priority-ordered (windfury HIGH+1 over grace HIGH), not churn. No totem
  changes. Dual list/vector stacks: legacy list engine is live per AiFactory;
  vector Generic* trees untouched.
- Earthquake 48306 (primary + primary-safe splash + 4s aftershock) wired
  Elemental AoE on medium density HIGH+1 over chain lightning. Lightning
  Strike 51387 wired Enhancement NORMAL+2 with active-shield requirement
  (core consumes charges). Spirit Link 51363 as tank-target pair (group dupe
  guard mirrored from earth shield) wired Resto HIGH. Ancestral Swiftness
  16188: boost activation + aura-active/party-low TwoTriggers consumer into
  Healing Wave on party HIGH+1 (aura persists until consumed). Bloodlust 45509
  (registered but never wired in legacy engine) wired Enhancement boost HIGH;
  local semantics are self frenzy + party melee-crit proc, not Wrath haste.
  Files: ShamanActions.h, ShamanTriggers.h, ShamanAiObjectContext.cpp,
  Enhancement/ElementalShamanStrategy.cpp, RestorationShamanStrategy.cpp.
- Checks: diff clean, presets 0 failures, surface + host OK.
- Remaining Shaman: Elemental preset + Elemental Mastery status, totem
  slot/rank audit, imbue pairing, Bloodlust durability gate, scenarios.

## Batch 12 — five missing presets + stance correction (done, uncommitted)
- Built module-owned generator (`tools/talents/build_missing_presets.py`) and
  tree inspector (`tools/talents/dump_trees.py`) decoding Talent/TalentTab DBC
  with spell names: all five missing specs authored with explicit acquisition
  orders — Arms 1.2 (40+11 fury dip, Mortal at 45), Survival 3.2 (43+8 BM dip,
  Carve 35/Lacerate 50), Subtlety 4.2 (41+10 assa dip, Hem 30/Mark 45),
  Discipline 5.2 (40+11 holy dip, Enlighten 45/Chastise 50), Elemental 7.2
  (42+9 resto dip, EM 40/EQ 45). Merged into aiplayerbot.conf.dist.in: 297/297
  links validate (prereqs/rows/budgets/monotonicity). 27/27 specs have presets.
  Key placements confirmed from DBC (not skill-tab inference): Master Strike =
  Arms row2, KC = BM capstone (needs Spirit Bond 2), Lacerate needs Carve,
  Noxious = Assa capstone (needs Envenom), LS = Enh row2 (needs Stable Shields
  3), BL = Enh capstone, EQ = Elem capstone (needs EM), Chastise = Disc
  capstone (needs Enlighten), Ascendance 52962 = Holy Priest capstone (needs
  Spirit of Redemption; still no core script — remains unresolved).
  Discipline rationale (TALENT_BUILDS): Tortoise holy-damage/support + shield +
  Enlighten/Chastise, healer-role variant usable; not Wrath shield-healer.
  Bear/Cat share one Feral preset by runtime forced-role arbitration
  (documented rationale; no invented tree).
- Stance correction: WarriorCensus misread the factory (truncated view).
  Actual state was stance NODES live but all CREATORS commented. Registered
  the selective six (taunt/revenge/shield block/shield wall → defensive;
  pummel/intercept → berserker); charge/mock/overpower/mortal stay unforced.
  File: WarriorStrategy.cpp.
- Checks: diff clean, presets 297/0, surface + host OK.

## Batch 13 — full family coverage sweep (done, uncommitted)
- Extended SPELL_COVERAGE.tsv to 90 family rows (all 16 columns validated):
  78 automatic-family (incl. gated/guarded variants), 2 passive + 1 synergy
  (core-owned), 2 situational-manual with reasons + paths, 8 unresolved
  (Ascendance, Envenom, Shadow of Death, Mark for Death, Smoke Bomb, Tree of
  Life, Elemental Mastery, Wolf aspect) + 1 unsupported (Icicles pre-batch5;
  now automatic-gated — row kept as history, disposition updated).
  Unresolved rows stay incomplete per plan; nothing relabeled to hit a number.
- Fixed three malformed TSV rows found by column-count audit.

## Batch 14 — deferred Rogue four + Ascendance (done, uncommitted)
- Decoded template mechanics for all five: Envenom 52531 (CP-scaled poison-buff
  finisher), Shadow of Death 52710 (CP snapshot + banked detonate, 60s),
  Mark for Death 52538 (undodgeable + party AP, 3min, awards CP), Smoke Bomb
  51969 (self miss-cloud, 5min, DBC-only), Ascendance 52962 (self heal
  throughput, 5min, DBC-driven — no script needed; Holy capstone confirmed via
  Talent.dbc: needs Spirit of Redemption).
- Envenom upkeep beside slice (HIGH+1, 3CP + missing aura) in Assassination;
  Shadow of Death (5CP + target >30) HIGH+1, Mark (target >50) HIGH, Smoke Bomb
  on critical health EMERGENCY in Subtlety; Ascendance HIGH in Holy boost.
  Repaired two edit-placement breaks (rogue factory header, priest shield
  classes, holy boost brace) — diff-verified clean after each.
  Files: RogueActions.h, RogueTriggers.h/.cpp, RogueAiObjectContext.cpp,
  Assassination/SubtletyRogueStrategy.cpp, PriestActions.h, PriestTriggers.h,
  PriestAiObjectContext.cpp, HolyPriestStrategy.cpp.
- Checks: diff clean, presets 297/0, surface OK.

## Batch 15 — wiring audit gate + live fixes + Elemental Mastery (done, uncommitted)
- Built `tools/verify_action_trigger_wiring.py`: every TriggerNode/NextAction
  name (1499 queued) checked against all creators (2323). Result:
  live-missing=0, dead-tree=138 (audited), known=2, skipped=2. Gate passes.
- Fixed real bugs found: Ret "repentance or shield" node typo'd as
  "repentance of shield" in the factory (critical-health node never fired);
  "finish ready check" action registered under the trigger's name (added
  correct creator); "master target active" trigger class missing entirely
  (follow-master dps-assist dead — implemented from MasterTargetValue:
  master alive + live victim); restored batch-11-deleted CastBloodlustAction
  before it broke compilation; removed obsolete draenei "gift of the naaru"
  node (no draenei on Tortoise; was an unresolvable NextAction for every
  low-health bot).
- Audited and classified the rest: PossibleAdsStrategy never added (dead
  strategy, kept); "stay line" unreachable continuer; "set pet stance"/"new
  pet"/"toggle pet spell" only in dead files (Priest/Shaman-NonCombat classes
  verified unwired); bare "medium/high aoe" only in dead files (donor
  semantics: 3/4 targets @8yd — deliberately NOT registered to avoid
  inventing semantics for dead trees); "medium group heal setting" only in
  dead Resto/Heal files. Engine::Init proven to call BOTH list and vector
  InitTriggers — vector classes are dead only because no factory instantiates
  them (verified per class).
- Elemental Mastery 16166 (self damage + mana-reduction, 3min, DBC-driven):
  action + BuffTrigger + creator + Elemental boost HIGH.
  Files: ShamanActions.h, ShamanTriggers.h, ShamanAiObjectContext.cpp,
  ElementalShamanStrategy.cpp, RetributionPaladinStrategy.cpp,
  WorldPacketActionContext.h, GenericTriggers.h/.cpp, TriggerContext.h,
  RacialsStrategy.cpp, tools/verify_action_trigger_wiring.py.
- Checks: wiring gate exit 0, diff clean, presets 297/0, surface + host OK.

## Batch 16 — Tree of Life + Conflagrate audit (done, uncommitted)
- Audited Tree restrictions (spell_druid.cpp aura is pure spirit scaling; no
  scripted lockouts; form rules core-enforced; shared caster-form node exits
  Tree): wired "tree form" maintain into Restoration buff HIGH (creators
  pre-existed, strategy never fired it). File: RestorationDruidStrategy.cpp.
- Verified Conflagrate policy against core (spell_warlock.cpp:475-510):
  consumes own Immolate (full removal <=3s, else shaves 3s); bot fires at
  Immolate <=7s remaining, i.e. always with an aura present and due for
  refresh — no change needed, recorded.
- Checks: wiring gate 0, diff clean, presets 297/0, surface OK.
- Open decisions: Lacerate-vs-Serpent precedence, Bloodlust durability gate,
  Holy Shock caller discipline, Daybreak FoL/HS consumers, seal/judgement
  ordering, forced-healer melee churn, curse/sacrifice policies, Rupture smoke
  test, Arcane floor vs measured mana. (Decided: Envenom/Shadow/Mark/Smoke/
  Ascendance/Tree slots, Elemental Mastery surface, Conflagrate timing.)

## Batch 19 — Daybreak fallbacks + gate decisions (done, uncommitted)
- Daybreak consumer expanded: HL on party keeps HIGH+5, with FoL on party
  HIGH+4 and HS on party HIGH+3 fallbacks in the same node (both creators
  verified). Window now consumed even when HL is unsuitable. File:
  HolyPaladinStrategy.cpp.
- Bloodlust durability gate DELIBERATELY NOT ADDED: any hp/threshold gate
  guesses time-to-kill; waste is symmetric (first-use consumes the 5min CD
  somewhere); the only principled signal (EstimatedLifetimeValue) is itself
  unregistered dead code — wiring lust to it would expand scope into unproven
  machinery. Revisit with measured fight lengths.
- Kick energy reserve DELIBERATELY NOT ADDED: donor has no reserve either;
  holding back finishers changes DPS on a guess. Needs gameplay evidence.
- Checks: diff clean, presets 297/0, wiring 0, surface + host OK.
- Native cached module build via ./dev/build-playerbots (user-owned; docker
  checks explicitly deferred by user).
- Live-client human-led party acceptance for #93/#94 (user-owned).

## Evidence summary (implementation-verified | gameplay-accepted)
All 28 profiles partial (every spec has wired Tortoise behavior with documented
follow-ups; none fully verified). Gameplay acceptance: PENDING for all (no
real-client runs; user owns docker/client review).
No issues closed; no pushes/PRs published (needs execution-session authority).

## Batch 17 — Wolf aspect manual + coverage zero-unresolved (done, uncommitted)
- Aspect of the Wolf 45650 (melee-AP self buff): registered manual action only.
  Auto-maintain deliberately withheld: shared Hawk upkeep would fight it every
  tick (mutually exclusive aspects, both BuffTriggers). Auto-arbitration is a
  recorded follow-up. Files: HunterActions.h, HunterAiObjectContext.cpp.
- Coverage now holds zero unresolved rows (Wolf was the last; re-added after a
  range-edit dropped it — TSV edits need post-write row-count audits).
- Checks: wiring gate 0, diff clean, presets 297/0, surface + host OK.

## Batch 18 — Hunter pet attack + Wolf aspect (done, uncommitted)
- Wired shared "pet attack" (PetAttackTrigger/Action with CC/immunity/stay/
  wait gates) into HunterPetStrategy combat HIGH+2, mirroring WarlockPetStrategy
  from #92. File: HunterStrategy.cpp.
- Aspect of the Wolf 45650: manual action only (auto-maintain would oscillate
  vs Hawk upkeep). Coverage zero-unresolved after re-adding a range-edit-lost
  row (TSV row-count audits now routine). Files: HunterActions.h,
  HunterAiObjectContext.cpp.
- Checks: wiring gate 0, diff clean, presets 297/0, surface + host OK.

## Batch 20 — review-pass repairs + namespace-aware gate (done, uncommitted)
- Full self-review of the working diff found three dropped creator lines from
  earlier range edits (hemorrhage trigger, pummel-on-enemy-healer action,
  chain heal action) — all restored and re-verified. Also fixed an accidental
  Wybern→Wyvern identifier rename before it broke compilation.
- Upgraded the wiring gate with ActionNode inner-action resolution
  (ACTION_NODE_X primary must resolve as action) and trigger/action namespace
  buckets. It then found seven more live gaps, all fixed: lightning strike
  trigger creator (own batch omission), soul link trigger creator, "spell lock
  on enemy healer" alias (creator key lacked "on"), wrath trigger (new,
  offheal DPS filler), inferno action (new class, existing gated trigger),
  mana tap + arcane torrent actions (new classes) with spell-known trigger
  gates so non-blood-elf bots stay quiet.
- Gate now: live-missing=0, node-broken=0, exit 0.
- Files: RogueAiObjectContext.cpp, WarriorAiObjectContext.cpp,
  ShamanAiObjectContext.cpp (chain heal), RetributionPaladinStrategy.cpp
  (typo, batch 15), WorldPacketActionContext.h, GenericTriggers.h/.cpp,
  TriggerContext.h, RacialsStrategy.cpp, HunterTriggers.h (spelling revert),
  DruidTriggers.h + DruidAiObjectContext.cpp, WarlockActions.h +
  WarlockAiObjectContext.cpp, GenericSpellActions.h + ActionContext.h,
  tools/verify_action_trigger_wiring.py.
- Checks: wiring gate 0, diff clean, presets 297/0, surface + host OK.

## Batch 21 — review repairs + seven gate finds + Viper (done, uncommitted)
- Full-diff self-review repaired three dropped creator lines (hemorrhage
  trigger, pummel-on-enemy-healer action, chain heal action) and reverted an
  accidental Wybern identifier rename. Net effect of review: zero silent
  regressions left from range edits.
- Namespace-aware wiring gate (+node inner-action check) found and fixed:
  lightning strike trigger, soul link trigger, spell-lock alias, wrath
  trigger, inferno action, mana tap + arcane torrent actions with spell-known
  trigger gates. Also: master-target trigger (new), ready-check creator fix,
  repentance typo fix, naaru removal, Bloodlust action restore.
- Aspect of the Viper 45651: manual action mirroring Wolf (same oscillation
  constraint). Coverage: 88 rows, zero unresolved.
- Dead-count bookkeeping: HEAD baseline is 147 dead-tree refs; now 145
  (bisect-verified via stash; earlier "138" was a misread, corrected here).
- Checks: wiring 0/0, diff clean, presets 297/0, surface + host OK.

## Batch 22 — full-diff review pass (done, uncommitted)
- Read every C++ hunk in the working diff against HEAD. Found and fixed:
  dropped hemorrhage trigger creator, dropped pummel-on-enemy-healer action
  creator (live Fury interrupt), dropped chain heal action creator (live Resto
  node), accidental Wybern identifier rename, duplicate live stance nodes +
  stray close-comment in WarriorStrategy (now minimal: 6 creators + 7 nodes).
- Verified intact: Talentspec rewrite structure, mage/priest/hunter/shaman/
  warlock/paladin/druid diffs, preset merge (3 deletions are exactly the three
  fixed L35 links), racial trigger gates, MasterTargetActive wiring.
- Checks: wiring 0/0, diff clean, presets 297/0, surface + host OK.

## Batch 23 — new-preset integration audit (done, uncommitted)
- Verified the five new presets need no dispatch code: PlayerbotAIConfig
  loads specs 0-9 generically (any Name set) and validates with the fixed
  CheckTalents; factory selection rolls over all paths; AiFactory dispatches
  by dominant talent tab; Update*Strategies maps already enumerate
  arms/survival/subtlety/discipline/elemental alongside older specs
  (spot-checked warrior/hunter/rogue/priest/shaman maps + getName owners).
- Holy Shock caller audit: party→on-party and healer-dps→offensive pairings
  correct; self-low-health node fires offensive HS before HL — recorded as an
  open triage question (not churned without runtime evidence).
- Imbue upkeep verified coherent (windfury+rockbiter fallback, lightning
  shield maintained non-combat; combat shield upkeep added batch 11+).
- Checks: diff clean, presets 297/0, wiring 0/0, surface + host OK.

## Batch 24 — DONOR_MAP structural repair (done, committed d868e3aa on cp/shared-foundation, chain rebased)
- Column-count audit found DONOR_MAP.tsv mixed widths {8,14,7}: eleven data
  rows had merged donor_sha+donor_file fields (7 cols), one line fused two
  rows (14 cols: Heroism reject + Envenom upkeep), three rows lacked
  local_owner. Fixed minimally: split merged fields, separated fused rows,
  inserted owner "none" where the row already states no local surface, and
  swapped the Holy Shock disposition/incompatibility pair (only order yielding
  valid values; no mechanics changed). Shadow of Death needed no split (was
  already a valid merged-source row; a first-pass split to 9 cols was
  reverted). Result: 39 data rows, uniform 8 cols.
- Committed on cp/shared-foundation; rebased cp/warrior→cp/shaman (all clean,
  README stash round-tripped). Tip-vs-old-tip diff is exactly the TSV fix.
- Checks: TSV widths {8}/{16}/{8}, diff clean, wiring 0/0, presets 297/0,
  surface + host OK. No docker build (user-owned review).
