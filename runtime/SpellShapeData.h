#pragma once

namespace TortoiseBots {

// Area shapes of boss and elite spells whose hit area points somewhere (the dashboard's boss overlay, fight debugger
// phase 2). DATA, not code: runtime/SpellShape.h parses this text and the module sends no shape for a spell absent from
// it, so a raid adds rows here, never a branch in the game code. Columns:
//
//   spell,shape,angle deg,reach yd,width yd,x0,y0,x1,y1,source
//
// shape: cone (in front), back-cone (behind), line (along the facing, or between two absolute ends). angle is the full
// arc the core fills, 0 for a line; reach is the distance from the caster along its facing (a line with ends has reach 0
// and its length is the ends' distance); width is the band across a line, 0 for a cone. A line's ends are map
// coordinates, all four columns set or all four 0: a damaging lane does not start at the boss's feet, so the two ends are
// what the dashboard draws. Units are the game's yards. A trailing source says where each number was read.
//
// Core (Tortoise 1.18.1, ~/tortoise-wow): tw_world.spell_template row `entry`; effectImplicitTargetA 24 is an enemy
// cone whose arc the core picks by spellVisual (src/game/Spells/Spell.cpp:2751-2768) and fills with HasInArc
// (Spell.cpp:8258-8272): 2*pi/3 = 120 degrees in front, 75 degrees behind for spellVisual 3879. Target 54 is a narrower
// cone: Spell.cpp:2771-2840 fills a 90 degree arc unless the spell id is one of its own rotated 120 degree cases. The
// reach is effectRadiusIndex -> SpellRadius.dbc yards (9=20, 10=30, 11=45, 12=100, 13=10, 21=35, 23=40). A spell whose target is a
// single unit (effectImplicitTargetA 6, e.g. Cleave) or a circle around the caster has no row: there is no arc to draw.
//
// The Deep Breath lanes are chains of link spells fired along a straight lane, each link hitting a 30 yd radius (radius
// index 10), so the lane is 60 yd wide. A lane row is the complete damaging footprint of its chain: a line from the first
// to the last point of tw_world.spell_target_position for that head spell, which the block at the end of
// src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp walks (head to link to link). North-to-south is the long one: its
// chain runs on past the south hover point into the tunnel, 151.5 yd in all.
inline char const* const kSpellShapeCsv = R"(
# spell,shape,angle,reach,width,x0,y0,x1,y1,source
18435,cone,120,45,0,0,0,0,0,spell_template 18435 Flame Breath: target 24; radius 11 = 45 yd; front 120
23461,cone,120,45,0,0,0,0,0,spell_template 23461 Flame Breath: target 24 x2; radius 11 = 45 yd; front 120
18500,cone,120,20,0,0,0,0,0,spell_template 18500 Wing Buffet: target 24 x2; radius 9 = 20 yd; front 120
15847,back-cone,75,30,0,0,0,0,0,spell_template 15847 Tail Sweep: target 24 x2; spellVisual 3879 = back 75; radius 10 = 30 yd
19272,cone,90,35,0,0,0,0,0,spell_template 19272 Lava Breath: target 54 (90 degree arc); radius 21 = 35 yd; front
24839,cone,120,20,0,0,0,0,0,spell_template 24839 Acid Breath: target 24 x2; radius 9 = 20 yd; front 120
25814,cone,120,10,0,0,0,0,0,spell_template 25814 Wide Slash: target 24; radius 13 = 10 yd; front 120
11837,cone,120,10,0,0,0,0,0,spell_template 11837 Wide Slash: target 24; radius 13 = 10 yd; front 120
12884,cone,120,10,0,0,0,0,0,spell_template 12884 Acid Breath: target 24 x2; radius 13 = 10 yd; front 120
12882,cone,120,10,0,0,0,0,0,spell_template 12882 Wing Flap: target 24; radius 13 = 10 yd; front 120
26102,cone,90,40,0,0,0,0,0,spell_template 26102 Sand Blast: target 54 x2 (90 degree arc); radius 23 = 40 yd; front
26029,cone,15,100,0,0,0,0,0,spell_template 26029 Dark Glare: target 24; spellVisual 7441 = PUSH_IN_FRONT_15 (Spell.cpp:2760-2761, HasInArc pi/12 at Spell.cpp:8276-8277) = front 15; radius 12 = 100 yd
17086,line,0,0,60,20.730539,-215.237610,-130.788300,-213.424026,spell_target_position 17086 chain (north to south, 20 links, first point 17086 to last 21139 in the tunnel; 151.53 yd); radius 10 = 30 yd each side
18351,line,0,0,60,-68.834236,-215.036163,20.730539,-215.237610,spell_target_position 18351 chain (south to north, 11 links; 89.57 yd); radius 10 = 30 yd each side
18576,line,0,0,60,-37.743851,-243.667923,-37.728523,-188.616806,spell_target_position 18576 chain (east to west, 7 links; 55.05 yd); radius 10 = 30 yd each side
18609,line,0,0,60,-37.728523,-188.616806,-37.743851,-243.667923,spell_target_position 18609 chain (west to east, 7 links; 55.05 yd); radius 10 = 30 yd each side
18564,line,0,0,60,-56.559654,-241.223923,6.016711,-181.305771,spell_target_position 18564 chain (south-east to north-west, 12 links; 86.64 yd); radius 10 = 30 yd each side
18584,line,0,0,60,6.016711,-181.305771,-56.559654,-241.223923,spell_target_position 18584 chain (north-west to south-east, 12 links; 86.64 yd); radius 10 = 30 yd each side
18596,line,0,0,60,-58.250900,-189.020004,12.120926,-243.439407,spell_target_position 18596 chain (south-west to north-east, 12 links; 88.96 yd); radius 10 = 30 yd each side
18617,line,0,0,60,12.120926,-243.439407,-58.250900,-189.020004,spell_target_position 18617 chain (north-east to south-west, 12 links; 88.96 yd); radius 10 = 30 yd each side
)";

} // namespace TortoiseBots
