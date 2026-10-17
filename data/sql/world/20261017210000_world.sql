-- Uldaman (map 70): the use_item of the Gni'kiv Medallion (7740 -> Staff of Prehistoria 7733, leg 1 seq 6) names its
-- second input, the Shaft of Tsol (7741, the loot step before it), in `param`. The run leader sends an open / use_item
-- to the bot that carries every input of the step, so a Shaft that Revelosh's quest item left in another bot's bags
-- (Uldaman 19z13, loot step 20) does not leave the use on a lead bot that lacks it. `param` of a use_item / open is the
-- step's second item; it was unused for these kinds. Idempotent. Mirrored in turtle-agent dungeons/uldaman.json as
-- `needs_item` (exported into `param`). It sorts after the whole-plan re-exports 20261017180000 and 20261017200000,
-- which DELETE and re-INSERT the plan: a re-export made before the turtle-agent change carries param 0, and this
-- UPDATE sets it again. Regenerate the re-export from turtle-agent once that change has merged.
UPDATE `ai_dungeon_step` SET `param` = 7741
WHERE `plan` = 'uldaman' AND `leg` = 1 AND `seq` = 6 AND `kind` = 'use_item' AND `entry` = 7740;
