-- Warlock tier-2 trinket: 19812 Rune of the Dawn only boosts spell damage
-- against the Undead, and the tier's content (Zul'Gurub and the level 60
-- dungeons) is not. The mage tier-2 kit puts 13968 Eye of the Beast in its
-- second trinket slot; the warlock keeps 19957 in the first. flags bit1
-- marks the hand-picked row, as the generator's HAND_OVERRIDES writes it.
-- Idempotent.
UPDATE `tortoise_bots_hire_gear` SET `item_id` = 13968, `flags` = 1
WHERE `class` = 9 AND `tier` = 2 AND `slot` = 13 AND `item_id` = 19812;
