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
- **Unlocks** — runes are available by class by default, or can be **gated** so a
  character must *earn* them (per-rune, opt-in): map a rune to a quest in
  `rune_quest_unlock` (unlocked on quest completion), or to an item in
  `rune_item_unlock` (unlocked by *using* an item bound to the engine's generic
  `item_rune_unlock` script, which also consumes it). A gated rune is hidden at the
  engraver until unlocked.
- **SoD engraving rules** — slots **unlock by level** (per-slot config, defaulting
  to the SoD phase-band starts), an optional **learn-Engraving prerequisite** can
  gate the system, and the **same rune can't be engraved twice**.
- **Debug reset** — an optional (`RuneEngraving.DebugMenu`) engraver gossip option
  that reverts a character's gated-rune progress — relocks quest- and item-gated
  runes, resets their quests, and hands back consumed unlock items — for re-testing.
- **Robustness** — class-gated rune lists, self-healing on GUID reuse, and cleanup
  on character deletion.

- **Client UI** — an optional in-game panel (the separate **RuneEngraver** addon)
  opens from a Character Sheet button to engrave/remove runes, talking to the engine
  over a small `RUNE` addon-message protocol (pure C++, no core edits). The gossip
  NPC stays as a debug/no-addon fallback. See [docs/addon-ui.md](docs/addon-ui.md).

`mod-sod-mage` wires two proof runes — **Regeneration** (item-gated via a SoD item
chain) and **Mass Regeneration** (drop-gated: notes from the shared `mod-sod-world`
Awakened Lich).

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
.rune slots                # each slot's unlock level + open/locked state
.rune engrave <slot> <id>  # e.g. .rune engrave 4 7000001  (slot 4 = Chest)
.rune clear <slot>
.rune unlock <id>          # force-unlock a gated rune (testing)
.rune lock <id>            # remove an unlock
.rune unlocks              # list this character's unlocked runes
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

Developer docs (architecture, the content contract, the addon-UI protocol,
deploy/verify, testing, gotchas) live in [`docs/`](docs/Home.md) and are mirrored
to the project wiki. Start at [docs/Home.md](docs/Home.md).

## Conventions

No core edits — everything stays under `modules/mod-rune-engraving/`.

## License

GPL v2 (inherited from AzerothCore). See [LICENSE](LICENSE).
