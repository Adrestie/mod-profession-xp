-- mod-profession-xp — characters schema.
--
-- What every character has already crafted or gathered once: the first time
-- earns a bonus, every later one does not. One row per character and per item
-- (kind 0), or per spell for a craft that leaves no item behind, such as an
-- enchantment applied to gear (kind 1).
--
-- Idempotent: the file may be replayed, it creates nothing that already exists.

CREATE TABLE IF NOT EXISTS `mod_profession_xp_character_done` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = item entry, 1 = spell id',
  `entry` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`, `kind`, `entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ProfessionXP: what each character already crafted or gathered once';
