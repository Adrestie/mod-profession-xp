-- mod-profession-xp — the messages shown to the player.
--
-- English by default; every other language is served from module_string_locale,
-- falling back on English. {} is the name of the item, enchantment or research.
--
-- Regenerable: the module's rows are deleted before being written again, so
-- the file may be replayed at will.

DELETE FROM `module_string` WHERE `module` = 'mod-profession-xp';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-profession-xp', 1, 'First time crafting "{}": profession bonus earned.'),
('mod-profession-xp', 2, 'First time gathering "{}": profession bonus earned.');

DELETE FROM `module_string_locale` WHERE `module` = 'mod-profession-xp';
INSERT INTO `module_string_locale` (`module`, `id`, `locale`, `string`) VALUES
('mod-profession-xp', 1, 'frFR', 'Première fabrication de « {} » : prime de métier obtenue.'),
('mod-profession-xp', 2, 'frFR', 'Première récolte de « {} » : prime de métier obtenue.'),
('mod-profession-xp', 1, 'deDE', 'Zum ersten Mal hergestellt: „{}“. Berufsbonus erhalten.'),
('mod-profession-xp', 2, 'deDE', 'Zum ersten Mal gesammelt: „{}“. Berufsbonus erhalten.'),
('mod-profession-xp', 1, 'esES', 'Primera fabricación de «{}»: bonificación de profesión obtenida.'),
('mod-profession-xp', 2, 'esES', 'Primera recolección de «{}»: bonificación de profesión obtenida.'),
('mod-profession-xp', 1, 'esMX', 'Primera fabricación de «{}»: bonificación de profesión obtenida.'),
('mod-profession-xp', 2, 'esMX', 'Primera recolección de «{}»: bonificación de profesión obtenida.'),
('mod-profession-xp', 1, 'ruRU', 'Впервые изготовлено: «{}». Получен бонус профессии.'),
('mod-profession-xp', 2, 'ruRU', 'Впервые добыто: «{}». Получен бонус профессии.');
