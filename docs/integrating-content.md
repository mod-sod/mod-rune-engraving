# Integrating content

How another module adds runes to the engine. The contract is **pure data**: you
insert rows into the engine's `rune_template` table. No C++ linkage, no headers —
your module never references this one at build time.

## 1. Insert into `rune_template`

One row per rune. Columns:

| Column | Type | Meaning |
|--------|------|---------|
| `rune_id` | INT | unique id (use your module's documented band — see below) |
| `spell_id` | INT | the spell granted while this rune is engraved |
| `class_mask` | INT | AC classmask: `1 << (class - 1)`; `0` = any class |
| `slot_mask` | INT | bitmask of `1 << RuneSlot` for the slots this rune is legal in |
| `name` | VARCHAR | shown in the engraver NPC menu |
| `icon` | VARCHAR | optional (reserved for a future addon UI) |
| `description` | VARCHAR | optional flavor/help text |
| `source` | VARCHAR | your module tag, e.g. `'mod-sod-mage'` (greppable) |
| `enabled` | TINYINT | `1` to make it engravable; `0` hides it from the catalog |

### Slots

`slot_mask` is a bitmask over the `RuneSlot` enum (defined in
`src/RuneEngravingMgr.h`). **The numeric values are the contract** — do not assume
they'll be renumbered:

| Slot | Value | `1 << value` |
|------|------:|-------------:|
| Head | 0 | 1 |
| Neck | 1 | 2 |
| Shoulder | 2 | 4 |
| Cloak | 3 | 8 |
| Chest | 4 | 16 |
| Wrist | 5 | 32 |
| Hands | 6 | 64 |
| Waist | 7 | 128 |
| Legs | 8 | 256 |
| Feet | 9 | 512 |
| Ring | 10 | 1024 |

A rune legal only in Chest → `slot_mask = 16`. Legal in Chest **or** Legs →
`16 | 256 = 272`.

### Classes

`class_mask` uses the standard AzerothCore classmask, `1 << (classId - 1)`:

| Class | Id | Mask |
|-------|---:|-----:|
| Warrior | 1 | 1 |
| Paladin | 2 | 2 |
| Hunter | 3 | 4 |
| Rogue | 4 | 8 |
| Priest | 5 | 16 |
| Death Knight | 6 | 32 |
| Shaman | 7 | 64 |
| Mage | 8 | 128 |
| Warlock | 9 | 256 |
| Druid | 11 | 1024 |

`0` means no class restriction. In v1 a class-legal rune is immediately engravable
(no unlock gating).

## 2. Guard the SQL so it's a no-op without the engine

Your INSERT must not error when the engine isn't installed (its `rune_template`
won't exist). A plain `INSERT INTO rune_template ... WHERE EXISTS(...)` does **not**
work — MySQL errors on the missing target table *before* the `WHERE` runs. Build
the INSERT as conditional dynamic SQL instead:

```sql
SET @rune_tbl := (SELECT COUNT(*) FROM information_schema.tables
                  WHERE table_schema = DATABASE() AND table_name = 'rune_template');

SET @sql := IF(@rune_tbl > 0,
'INSERT INTO `rune_template`
    (`rune_id`, `spell_id`, `class_mask`, `slot_mask`, `name`, `icon`, `description`, `source`, `enabled`)
 VALUES
    (7000001, 401417, 128, 16, ''Regeneration'', ''spell_arcane_studentofmagic'',
     ''A channeled heal-over-time that applies Temporal Beacon.'', ''mod-sod-mage'', 1)
 ON DUPLICATE KEY UPDATE
    `spell_id` = VALUES(`spell_id`), `class_mask` = VALUES(`class_mask`),
    `slot_mask` = VALUES(`slot_mask`), `name` = VALUES(`name`),
    `icon` = VALUES(`icon`), `description` = VALUES(`description`),
    `source` = VALUES(`source`), `enabled` = VALUES(`enabled`)',
'DO 0');

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
```

Notes:
- Use a plain single-quoted multi-line string literal (escape inner quotes as `''`)
  — it parses under any `sql_mode`, including `NO_BACKSLASH_ESCAPES`.
- `ON DUPLICATE KEY UPDATE` makes it idempotent; re-running is safe.
- When the engine is absent, `@rune_tbl = 0` and the statement runs `DO 0` (no-op).

## 3. Reserve a `rune_id` band

Pick a high, documented band so modules never collide. This table is the **single
source of truth** for the SoD class modules — each owns one 1000-wide band:

| Module | Band |
|--------|------|
| `mod-sod-mage` | `7000000–7000999` |
| `mod-sod-warrior` | `7001000–7001999` |
| `mod-sod-paladin` | `7002000–7002999` |
| `mod-sod-hunter` | `7003000–7003999` |
| `mod-sod-rogue` | `7004000–7004999` |
| `mod-sod-priest` | `7005000–7005999` |
| `mod-sod-deathknight` | `7006000–7006999` |
| `mod-sod-shaman` | `7007000–7007999` |
| `mod-sod-warlock` | `7008000–7008999` |
| `mod-sod-druid` | `7009000–7009999` |

`mod-sod-mage` ships several **item-gated** examples: `sod_mage_regeneration_unlock.sql`
and `sod_mage_living_flame_unlock.sql` (multi-step SoD combine chains) and
`sod_mage_mass_regeneration.sql` (notes dropped by the `mod-sod-world` Awakened
Lich). **Quest-gating** (`rune_quest_unlock`, below) is fully supported by the
engine, though no shipped content currently uses it.

## Order of application

The engine's schema must exist before your rune rows land. With manual SQL apply,
apply the engine's base SQL first, then your module's rune SQL. If both are
auto-imported and your rune SQL happens to run first, it harmlessly no-ops once
(the guard) and lands on the next apply — it's idempotent, so just re-run it.

## Engraving rules are server-side

The SoD engraving rules — per-slot **level unlocks**, the **learn-Engraving
prerequisite**, and the **no-duplicate-rune** guard — are server config + engine
logic. They need **no schema change** and aren't configured per rune: your job is
just to supply runes (and optionally quest-gate them). Two tips for SoD content:

- Keep each rune **single-slot** (one bit in `slot_mask`), as SoD runes are. A
  multi-slot rune still can't be engraved twice (the guard), but single-slot keeps
  the model clean.
- Slot unlock levels (`RuneEngraving.SlotMinLevel.*`, defaulting to the SoD
  phase-band starts — P1=1, P2=26, P3=41, P4=51) and the required ability
  (`RuneEngraving.RequiredSpell`) are admin-tuned in the engine's `.conf` — they're
  server policy, not something your runes set.

## Gating runes behind quests

By default a rune is **available by class** the moment its catalog row exists.
To make a rune *earned* instead, map it to one or more quests in
`rune_quest_unlock` (world DB):

| Column | Meaning |
|--------|---------|
| `rune_id` | the rune to gate |
| `quest_id` | a quest whose completion unlocks it |

Behavior:

- A rune referenced by **any** `rune_quest_unlock` row becomes **gated**: it won't
  appear at the engraver (and can't be engraved) until the character has unlocked
  it. A rune with **no** mapping stays available by class — so gating is per-rune,
  opt-in, and you don't touch runes you want freely available.
- When a character completes a mapped quest, the engine (via `OnPlayerCompleteQuest`)
  inserts the rune into `character_rune_unlock` and whispers the player. Multiple
  quests can unlock the same rune (any one suffices); multiple runes can hang off
  one quest.
- `character_rune_unlock` (characters DB: `guid`, `rune_id`) is engine-managed —
  content modules don't write it. It's cleared when a character is deleted.

Guard your `rune_quest_unlock` inserts exactly like the catalog inserts (the table
won't exist without the engine — use the conditional dynamic-SQL pattern above,
targeting `rune_quest_unlock`).

For testing without authoring a quest, a GM can force unlock state:
`.rune unlock <runeId>` / `.rune lock <runeId>` / `.rune unlocks`.

## Gating runes behind an item (contract v2)

The item analogue of `rune_quest_unlock`: a rune is unlocked by **using an item**
instead of completing a quest. Map the item to the rune in `rune_item_unlock`
(world DB):

| Column | Meaning |
|--------|---------|
| `item_id` | the item whose use unlocks the rune |
| `rune_id` | the rune to gate / unlock |

Then set that item's `item_template.ScriptName = 'item_rune_unlock'`. The engine
ships this generic `ItemScript`: on use it unlocks every rune mapped to the item
(into `character_rune_unlock`), whispers the player, and **consumes one** of the
item. Like the quest path, a rune referenced by **any** `rune_item_unlock` row
becomes **gated** until unlocked; an unmapped rune stays available by class.

Two practical notes:

- The item needs at least one **ON_USE spell** (`spelltrigger = 0`) so the client
  offers "Use" — reuse a harmless existing spell (the engine script suppresses it,
  so it never actually casts). Restrict the item to the relevant class with
  `AllowableClass` if the rune is class-specific.
- Guard the `rune_item_unlock` INSERT exactly like the catalog inserts (the table
  won't exist without the engine — conditional dynamic SQL targeting
  `rune_item_unlock`). `character_rune_unlock` is engine-managed; don't write it.

`mod-sod-mage`'s `sod_mage_regeneration_unlock.sql` is the worked example
(Comprehension Charm + scrambled notes → deciphered notes → unlock).

`rune_contract.version` (currently `2`: `rune_template`, `rune_quest_unlock`,
`rune_item_unlock`) lets your SQL sanity-check compatibility.

See also: [Architecture](architecture.md) · [Deploy & verify](deploy-and-verify.md)
