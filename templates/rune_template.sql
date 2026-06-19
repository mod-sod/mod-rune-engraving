-- TEMPLATE — copy into your CONTENT module's data/sql/db-world/base/<name>_runes.sql,
-- fill the placeholders. Not applied from templates/. Idempotent.
-- Reference: docs/integrating-content.md.
--
-- GUARDED: rune_template only exists when this engine is installed. A plain INSERT
-- would error on the missing table, so we build it as dynamic SQL and only PREPARE it
-- when the table is present; otherwise it runs `DO 0` (a clean no-op). This is what
-- lets your module ship rune rows and still install without the engine.

SET @rune_tbl := (SELECT COUNT(*) FROM information_schema.tables
                  WHERE table_schema = DATABASE() AND table_name = 'rune_template');

-- The INSERT is a single-quoted multi-line literal (escape inner quotes as '')
-- so it parses under any sql_mode, including NO_BACKSLASH_ESCAPES.
SET @sql := IF(@rune_tbl > 0,
'INSERT INTO `rune_template`
    (`rune_id`, `spell_id`, `class_mask`, `slot_mask`, `name`, `icon`, `description`, `source`, `enabled`)
 VALUES
    (<RUNE_ID>, <SPELL_ID>, <CLASS_MASK>, <SLOT_MASK>, ''<Rune Name>'', ''<icon_texture_name>'',
     ''<Short description shown in the rune panel.>'', ''<your-module>'', 1)
 ON DUPLICATE KEY UPDATE
    `spell_id`    = VALUES(`spell_id`),
    `class_mask`  = VALUES(`class_mask`),
    `slot_mask`   = VALUES(`slot_mask`),
    `name`        = VALUES(`name`),
    `icon`        = VALUES(`icon`),
    `description` = VALUES(`description`),
    `source`      = VALUES(`source`),
    `enabled`     = VALUES(`enabled`)',
'DO 0');

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- <RUNE_ID>    : from your module's documented id band.
-- <SPELL_ID>   : the spell the rune grants (must already exist server-side).
-- <CLASS_MASK> : 1 << (classId - 1); 0 = any class. Mage = 128.
-- <SLOT_MASK>  : 1 << RuneSlot. Chest = 16, Legs = 256, ... (Chest|Legs = 272).
-- icon must equal the spell's displayed icon texture so the panel matches.
-- source : your module tag (greppable), e.g. 'mod-sod-mage'.
