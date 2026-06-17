# mod-rune-engraving

A reusable, **class-agnostic** [AzerothCore](https://www.azerothcore.org/) module
that adds a *Season of Discovery*-style **rune engraving** system for the WotLK
**3.3.5a** client: players engrave a rune into a slot and gain its spell.

## Goal

Provide the engraving *mechanism* as a standalone, reusable engine — not tied to
any one class or content set. The engine ships **no runes** itself; **content
modules** (e.g. [`mod-sod-mage`](../mod-sod-mage)) supply runes by writing into the
engine's catalog. The two stay **optionally** coupled through the database, with no
C++ dependency, so a content module installs and works with or without the engine.

## Current state — early / proof of concept

The engine works end-to-end:

- **Catalog** — runes are loaded at startup from the `rune_template` table.
- **Engraver NPC** — a gossip NPC (entry `700000`) lists slots and the runes legal
  for your class, and engraves/removes them. A `.rune` GM command does the same
  headlessly for testing.
- **Grant/persist** — engraving grants the rune's spell (as a temporary spell),
  saved per-character; it's re-applied on login and removed when un-engraved.
- **Unlocks** — runes are available by class by default, or can be **gated behind
  quests**: a rune mapped in `rune_quest_unlock` is hidden until the character
  completes the quest that unlocks it (per-rune, opt-in).
- **SoD engraving rules** — slots **unlock by level** (per-slot config, SoD-phase
  defaults), an optional **learn-Engraving prerequisite** can gate the system, and
  the **same rune can't be engraved twice**.
- **Robustness** — class-gated rune lists, self-healing on GUID reuse, and cleanup
  on character deletion.

Scope so far: the UI is the **gossip NPC** (no client addon yet), and
`mod-sod-mage` wires its **Regeneration** spell as the first proof rune.

## Install

Server-side only — there is **no client patch** for this module.

1. Place this module at `modules/mod-rune-engraving/` in your AzerothCore source.
2. Build the worldserver with modules enabled (`-DMODULES=static`) — modules are
   compiled into `worldserver`, not built separately.
3. Apply the base SQL:
   ```bash
   mysql -u <user> -p acore_world      < modules/mod-rune-engraving/data/sql/db-world/base/rune_engraving_schema.sql
   mysql -u <user> -p acore_characters < modules/mod-rune-engraving/data/sql/db-characters/base/rune_engraving_characters.sql
   ```
   (Idempotent — safe to re-run.)
4. Restart the worldserver (the catalog and the NPC spawn load at startup).

`RuneEngraving.Enable = 1` by default; copy `conf/mod_rune_engraving.conf.dist` →
`mod_rune_engraving.conf` to change it.

### Use it

A **Rune Engraver** spawns in Stormwind (Trade District); `.npc add 700000` places
more. Talk to it to engrave, or test headlessly:

```
.rune list                 # engraved runes for the selected character
.rune engrave <slot> <id>  # e.g. .rune engrave 4 7000001  (slot 4 = Chest)
.rune clear <slot>
.rune reload               # reload the catalog (admin)
```

With no content module installed the catalog is empty (the engine is a no-op);
install a content module to populate it.

## Adding runes (for content modules)

Insert rows into the engine's `rune_template` table, **guarded** so your SQL is a
no-op when the engine isn't installed. The full contract — columns, slot/class
masks, the guarded-SQL pattern, and id bands — is in
[docs/integrating-content.md](docs/integrating-content.md).

## Tests

Unit tests cover the pure slot/class eligibility rules (the catalog contract) and
run in AzerothCore's `unit_tests` target — no core edits. Build with
`-DBUILD_TESTING=ON` and run `./src/test/unit_tests --gtest_filter='Rune*'`. See
[docs/testing.md](docs/testing.md).

## Documentation

Developer docs (architecture, the content contract, deploy/verify, testing,
gotchas) live in [`docs/`](docs/Home.md) and are mirrored to the project wiki.
Start at [docs/Home.md](docs/Home.md).

## Conventions

No core edits — everything stays under `modules/mod-rune-engraving/`.

## License

GPL v2 (inherited from AzerothCore). See [LICENSE](LICENSE).
