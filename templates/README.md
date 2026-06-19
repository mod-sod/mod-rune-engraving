# Contribution templates — mod-rune-engraving

This engine ships **no runes** — content modules supply them by writing into its
catalog tables. These are the copy-paste, **class-agnostic** versions of that
data-only contract. The full reference is
[docs/integrating-content.md](../docs/integrating-content.md).

These files are **never applied** from `templates/`. The database updater only applies
`.sql` under `data/sql/db-*/`, so nothing here is in its scan set.

## What's here

| File | For | Copy into |
|---|---|---|
| `rune_template.sql` | registering a rune in the catalog | your content module's `data/sql/db-world/base/` |
| `rune_item_unlock.sql` | gating a rune behind *using an item* | same |
| `rune_quest_unlock.sql` | gating a rune behind *completing a quest* | same |
| `conf_snippet.conf` | a new engine config tunable | `conf/mod_rune_engraving.conf.dist` |

The unlock files are **optional**: a rune with no unlock mapping is available by
class. Add one to make the rune *earned*.

## The contract, briefly

All inserts are **guarded**: the engine's tables only exist when it's installed, so a
plain INSERT would error. Each template builds the INSERT as conditional dynamic SQL
and only runs it when the table is present — a clean no-op otherwise. This is what
lets a content module ship rune rows yet still install fine without the engine.

- **`class_mask`** — AzerothCore classmask, `1 << (classId - 1)`. `0` = any class.
  Warrior 1, Paladin 2, Hunter 4, Rogue 8, Priest 16, DK 32, Shaman 64, Mage 128,
  Warlock 256, Druid 1024.
- **`slot_mask`** — bitmask of `1 << RuneSlot`. Head 1, Neck 2, Shoulder 4, Cloak 8,
  Chest 16, Wrist 32, Hands 64, Waist 128, Legs 256, Feet 512, Ring 1024.
- **`rune_id`** — pick from your module's documented band (e.g. mod-sod-mage owns
  `7000000–7000999`). The unlock **item** uses the real SoD id where one exists.

## Sources of truth

- This engine's [docs/integrating-content.md](../docs/integrating-content.md) — the
  authoritative contract (columns, masks, guarded-SQL, id bands).
- **[AzerothCore wiki](https://www.azerothcore.org/wiki)** — table schemas and the
  [hooks list](https://www.azerothcore.org/wiki/hooks-script).
- A worked example: `mod-sod-mage`'s `sod_mage_runes.sql` and `*_unlock.sql`.
