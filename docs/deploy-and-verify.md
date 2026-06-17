# Deploy & verify

A change touches up to two things — the **server binary** (C++) and the
**database** (schema, catalog, the engraver NPC). Know which your change involves.

| Change type | Rebuild worldserver? | Apply SQL + restart? |
|-------------|:--:|:--:|
| C++ logic (`RuneEngravingMgr`, NPC, hooks) | ✅ | — |
| Schema / catalog rows / NPC spawn | — | ✅ |

(There is no client-side artifact in this module. Runes show in their spellbook
tab purely from the *content* module's client patch — not the engine.)

## 1. Build (only if C++ changed)

Build the whole server from the parent repo with `-DMODULES=static` — modules
compile into `worldserver`, not separately (e.g. `docker compose up -d --build`).

## 2. Apply the SQL (idempotent)

```bash
mysql -u <user> -p acore_world       < modules/mod-rune-engraving/data/sql/db-world/base/rune_engraving_schema.sql
mysql -u <user> -p acore_characters  < modules/mod-rune-engraving/data/sql/db-characters/base/rune_engraving_characters.sql
```

For a Dockerized server, run through the DB container (adjust names/creds):

```bash
docker exec -i ac-database mysql -uroot -p<pw> acore_world      < .../rune_engraving_schema.sql
docker exec -i ac-database mysql -uroot -p<pw> acore_characters < .../rune_engraving_characters.sql
```

The schema uses `CREATE TABLE IF NOT EXISTS` and `DELETE … WHERE` + `INSERT` for
the contract row and NPC, so re-running is safe. If you run content modules, apply
the engine SQL **before** their rune SQL (see [Integrating content](integrating-content.md)).

> Note: `docker compose restart` does **not** re-run the `db-import` service — that
> only runs on `up`. A plain restart never applies changed SQL; apply it yourself.

## 3. Restart the worldserver

The rune catalog loads at **startup** (`WorldScript::OnStartup`) and creature
spawns load at startup too — both require a restart, not a hot reload. (Moving an
existing spawn's position likewise needs a restart, since there's no clean reload
for a live spawn's coordinates.)

## 4. The engraver NPC

The schema spawns a **Rune Engraver** (entry `700000`) in Stormwind. To place more,
target a spot and `.npc add 700000`. Picking a good coordinate matters — see the
underground-spawn note in [Gotchas](gotchas.md); use `.gps` (its `FloorZ` is the
true surface) at the desired spot.

## Verifying it actually loaded

- **Module compiled in / catalog loaded?** The worldserver startup log shows:
  ```
  RuneEngraving: loaded N rune(s) into the catalog.
  RuneEngraving: ready (N rune(s) in catalog).
  ```
  `N = 0` is normal with no content module installed.
- **NPC present?** `SELECT entry, ScriptName FROM creature_template WHERE entry = 700000;`
  and `SELECT guid, position_x, position_y, position_z FROM creature WHERE id1 = 700000;`
- **Catalog rows?** `SELECT rune_id, spell_id, class_mask, slot_mask, name FROM rune_template;`
- **In-game (with a content module):** talk to the engraver → pick a slot → the
  class-legal runes list → engrave → the spell appears and casts; un-engrave removes
  it; relog re-grants it.

### Headless testing — the `.rune` command

GM command for driving the engine without the NPC:

```
.rune list                 # show this character's engraved runes
.rune engrave <slot> <id>  # engrave rune <id> in <slot> (e.g. 4 = Chest)
.rune clear <slot>         # clear a slot
.rune unlock <id>          # force-unlock a gated rune (testing)
.rune lock <id>            # remove an unlock (testing)
.rune unlocks              # list this character's unlocked runes
.rune reload               # reload the catalog from the DB (admin)
```

To verify **quest gating**: map a rune in `rune_quest_unlock`, `.rune reload`,
confirm it's hidden at the engraver, complete the quest (you'll get a whisper),
then confirm it now lists. `.rune unlock`/`lock` shortcut the quest for testing.

See [Gotchas](gotchas.md) for what each failure mode looks like.
