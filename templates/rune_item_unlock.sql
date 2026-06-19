-- TEMPLATE — copy into your CONTENT module's unlock SQL, fill the placeholders. Not
-- applied from templates/. Idempotent. Reference: docs/integrating-content.md.
--
-- Maps <ITEM_ID> -> <RUNE_ID>: USING the item discovers (unlocks) the rune. A rune
-- referenced by any rune_item_unlock row becomes GATED -- hidden at the engraver until
-- unlocked. Bind the item with item_template.ScriptName = 'item_rune_unlock' (this
-- engine's generic script); on use it unlocks the mapped rune(s) and consumes one.
-- The item also needs an ON_USE spell so the client offers "Use" (the script
-- suppresses it). GUARDED: no-op without this engine.

SET @item_unlock_tbl := (SELECT COUNT(*) FROM information_schema.tables
                         WHERE table_schema = DATABASE() AND table_name = 'rune_item_unlock');

SET @sql := IF(@item_unlock_tbl > 0,
'INSERT INTO `rune_item_unlock` (`item_id`, `rune_id`)
 VALUES (<ITEM_ID>, <RUNE_ID>)
 ON DUPLICATE KEY UPDATE `rune_id` = VALUES(`rune_id`)',
'DO 0');

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- <ITEM_ID> : the unlock item (real SoD id where one exists).
-- <RUNE_ID> : the rune from your rune_template row.
-- character_rune_unlock is engine-managed -- never write it from content SQL.
