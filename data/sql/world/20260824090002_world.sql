-- Additive compatibility for installations that applied the original native
-- migration before the help/zone schema was reconciled.

ALTER TABLE `ai_playerbot_help_texts`
  ADD COLUMN IF NOT EXISTS `template_changed` tinyint unsigned NOT NULL DEFAULT 0 AFTER `template_text`;

CREATE TABLE IF NOT EXISTS `ai_playerbot_zone_level` (
  `id` bigint unsigned NOT NULL,
  `level` bigint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
