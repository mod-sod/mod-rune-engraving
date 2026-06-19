-- TEMPLATE — copy into your CONTENT module's unlock SQL, fill the placeholders. Not
-- applied from templates/. Idempotent. Reference: docs/integrating-content.md.
--
-- Maps <RUNE_ID> -> <QUEST_ID>: completing the quest discovers (unlocks) the rune. A
-- rune referenced by any rune_quest_unlock row becomes GATED -- hidden at the engraver
-- until unlocked. Multiple quests can unlock one rune (any one suffices); one quest
-- can unlock several runes. GUARDED: no-op without this engine.

SET @quest_unlock_tbl := (SELECT COUNT(*) FROM information_schema.tables
                          WHERE table_schema = DATABASE() AND table_name = 'rune_quest_unlock');

SET @sql := IF(@quest_unlock_tbl > 0,
'INSERT INTO `rune_quest_unlock` (`rune_id`, `quest_id`)
 VALUES (<RUNE_ID>, <QUEST_ID>)
 ON DUPLICATE KEY UPDATE `quest_id` = VALUES(`quest_id`)',
'DO 0');

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- <RUNE_ID>  : the rune from your rune_template row.
-- <QUEST_ID> : a quest whose completion unlocks it.
-- character_rune_unlock is engine-managed -- never write it from content SQL.
-- Test without authoring a quest: `.rune unlock <RUNE_ID>` / `.rune lock <RUNE_ID>`.
