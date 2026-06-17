# Gotchas & troubleshooting

The hard-won lessons, in one place. Most "it doesn't work" cases are one of these.

## A content rune INSERT can't be guarded with `WHERE EXISTS`

`INSERT INTO rune_template ... WHERE EXISTS(SELECT ... information_schema ...)`
**errors** when the engine isn't installed: MySQL resolves the missing target
table *before* evaluating the `WHERE`. Build the INSERT as conditional dynamic SQL
(`SET @sql := IF(table_exists, 'INSERT ...', 'DO 0'); PREPARE/EXECUTE`) so it's a
true no-op without the engine. Full pattern in
[Integrating content](integrating-content.md).

## The NPC is invisible or "underneath the city" — it's the Z coordinate

If the engraver doesn't appear but the DB row exists and the model is valid, the
spawn's `position_z` is almost always below the actual floor (it spawned under the
map). Diagnose in-game:

- `.go xyz <x> <y> <z>` — teleport to the spawn point; if you land under the world,
  the Z is wrong.
- `.npc near` — lists nearby creatures with entry/guid/distance; confirms it spawned.

Fix by using a **verified surface coordinate**: stand exactly where you want it,
run `.gps`, and use its reported `X/Y/Z` (its `FloorZ` is the true surface height).
A spawn's position only loads at startup, so changing it needs a worldserver
restart.

## The NPC appears but gossip does nothing

Two usual causes:

1. **Bad position** — interacting with an NPC embedded under terrain often fails
   the range/line-of-sight check, so gossip never opens. Fix the coordinate (above).
2. **Script not bound** — `creature_template.ScriptName` must be exactly
   `npc_rune_engraver`, and the module must be compiled in (`-DMODULES=static`).
   The startup log's `RuneEngraving: ready` line confirms the module loaded.

## DB changes need a worldserver restart, not a `compose restart`

The catalog and creature spawns load at startup and aren't hot-reloadable, and
`docker compose restart` does **not** re-run the `db-import` service (only `up`
does). Apply changed SQL yourself, then restart. (`.rune reload` re-reads only the
*catalog* in a running server — not spawns.)

## Removing a rune won't strip a spell the player already knew permanently

Rune spells are granted/removed as **temporary** (`learnSpell(id, true)` /
`removeSpell(id, …, onlyTemporary=true)`). If the player already learned the spell
permanently (e.g. a GM `.learn`), then:

- engraving is a silent no-op (`learnSpell` skips an already-known spell), and
- un-engraving deliberately won't remove the permanent copy.

This is correct — the engine never clobbers a spell from another source. For a
clean test, `.unlearn <spellId>` the permanent copy first (a permanently-learned
spell shows up in `character_spell`; temporary rune grants never do).

## A removed spell lingers in the spellbook until it's reopened

After un-engraving, the server sends the removal packet — the action bar drops the
spell and it's no longer castable — but the 3.3.5a spellbook frame doesn't always
repaint a removed entry live. It clears on reopening the spellbook or relogging.
Replacement UIs (e.g. DragonUI) can make this stickier. It's cosmetic; the spell is
genuinely gone.

## A gated rune doesn't appear at the engraver — that's by design

A rune mapped in `rune_quest_unlock` is hidden until the character unlocks it (by
completing the quest). If a rune you expect is missing, check whether it's gated
and whether the character has the unlock: `.rune unlocks` lists unlocks, and
`.rune unlock <id>` force-grants one for testing. A rune with no quest mapping is
never gated. Remember `.rune reload` after changing `rune_quest_unlock`.

## Which spellbook tab a rune spell shows in is the *content* module's job

Tab placement (General vs Arcane/Fire/…) is decided client-side by
`SkillLineAbility.dbc` and lives in the **content** module's client patch, not this
engine. The engine only grants/removes the server-side spell.

## A reused character GUID could inherit old runes — handled, but know why

AzerothCore reseeds the character-GUID counter after a restart, so a deleted top
character's GUID can be reused by a new character of a *different* class. The engine
guards this two ways: `OnPlayerDeleteFromDB` purges a deleted character's rune rows,
and on login `ApplyAll` skips/purges any stored rune whose `class_mask` doesn't
match the current character. (A rune merely missing from the catalog — e.g.
`enabled = 0` — is kept, not purged.)

## `.rune reload` on a busy server is a minor race

The catalog is read lock-free by the gossip NPC for speed; `.rune reload` rewrites
it under a lock. It's safe for container integrity, but a rune row pointer obtained
just before a concurrent reload could briefly dangle. Reload is admin-only and the
catalog is otherwise immutable after startup — run `.rune reload` on a quiet server.

## No `CMakeLists.txt` is needed

The parent build auto-globs `src/` and generates the `Addmod_rune_engravingScripts()`
call from the folder name. Don't add a `CMakeLists.txt`.
