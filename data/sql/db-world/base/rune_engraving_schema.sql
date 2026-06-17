-- mod-rune-engraving: world-side schema + the Rune Engraver NPC.
-- Idempotent and safe to re-run on a fresh or existing world DB.
--
-- The engine OWNS these tables but seeds NO runes: the `rune_template` catalog
-- is populated by content modules (e.g. mod-sod-mage) writing their own rows,
-- guarded so their import is a no-op when this schema is absent.

-- =====================================================================
-- Catalog: one row per rune. `class_mask` is the AC classmask
-- (1 << (class-1); 0 = any). `slot_mask` is a bitmask of (1 << RuneSlot)
-- where RuneSlot matches the enum in src/RuneEngravingMgr.h:
--   HEAD 0, NECK 1, SHOULDER 2, CLOAK 3, CHEST 4, WRIST 5, HANDS 6,
--   WAIST 7, LEGS 8, FEET 9, RING 10.
-- =====================================================================
CREATE TABLE IF NOT EXISTS `rune_template` (
    `rune_id`     INT UNSIGNED     NOT NULL,
    `spell_id`    INT UNSIGNED     NOT NULL,
    `class_mask`  INT UNSIGNED     NOT NULL DEFAULT 0,
    `slot_mask`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `name`        VARCHAR(100)     NOT NULL DEFAULT '',
    `icon`        VARCHAR(100)     NOT NULL DEFAULT '',
    `description` VARCHAR(255)     NOT NULL DEFAULT '',
    `source`      VARCHAR(64)      NOT NULL DEFAULT '',
    `enabled`     TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`rune_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =====================================================================
-- Quest-unlock contract. Content modules map a rune to the quest(s) that
-- unlock it; the engine consumes this on quest completion
-- (OnPlayerCompleteQuest -> UnlockRunesForQuest). A rune referenced here is
-- "gated": hidden at the engraver until the character unlocks it.
-- =====================================================================
CREATE TABLE IF NOT EXISTS `rune_quest_unlock` (
    `rune_id`  INT UNSIGNED NOT NULL,
    `quest_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`rune_id`, `quest_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =====================================================================
-- Item-unlock contract (v2). The item analogue of rune_quest_unlock: map a
-- rune to the item(s) that unlock it. The engine's `item_rune_unlock`
-- ItemScript (bound via item_template.ScriptName) calls UnlockRunesForItem on
-- use. A rune referenced here is "gated" exactly like a quest-gated one.
-- =====================================================================
CREATE TABLE IF NOT EXISTS `rune_item_unlock` (
    `item_id` INT UNSIGNED NOT NULL,
    `rune_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`item_id`, `rune_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =====================================================================
-- Contract version: lets content modules sanity-check compatibility.
--   v1: rune_template, rune_quest_unlock
--   v2: + rune_item_unlock
-- =====================================================================
CREATE TABLE IF NOT EXISTS `rune_contract` (
    `version` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `rune_contract`;
INSERT INTO `rune_contract` (`version`) VALUES (2);

-- =====================================================================
-- Rune Engraver NPC (entry 700000). Gossip-only, neutral, non-attackable.
-- DisplayID 3167 (Stormwind City Guard) is a verified, faction-neutral
-- placeholder appearance — swap it freely. ScriptName binds the C++ gossip.
-- =====================================================================
DELETE FROM `creature`                WHERE `id1` = 700000;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 700000;
DELETE FROM `creature_template`       WHERE `entry` = 700000;

INSERT INTO `creature_template` (
    `entry`,
    `name`, `subname`,
    `minlevel`, `maxlevel`,
    `faction`, `npcflag`,
    `speed_walk`, `speed_run`,
    `unit_class`, `unit_flags`, `unit_flags2`,
    `type`, `flags_extra`,
    `ScriptName`
) VALUES (
    700000,
    'Rune Engraver',
    'Engraving',
    80, 80,
    190,   -- neutral (yellow nameplate)
    1,     -- UNIT_NPC_FLAG_GOSSIP
    1.0, 1.14286,
    1,     -- warrior (simplest stats)
    2,     -- NON_ATTACKABLE
    0,
    7,     -- humanoid
    2,     -- CIVILIAN (no aggro radius)
    'npc_rune_engraver'
);

INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`
) VALUES (
    700000, 0, 3167, 1.0, 1.0
);

-- One convenience spawn in Stormwind, Trade District (verified on-surface
-- coordinate from `.gps`: Z matches FloorZ). guid 8800001 sits in a high band
-- to avoid collisions. Use `.npc add 700000` to place additional engravers.
INSERT INTO `creature` (
    `guid`, `id1`, `map`, `spawnMask`, `phaseMask`,
    `position_x`, `position_y`, `position_z`, `orientation`,
    `spawntimesecs`
) VALUES
    (8800001, 700000, 0, 1, 1, -8825.063, 649.7763, 94.57176, 4.7048225, 180);
