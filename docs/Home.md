# mod-rune-engraving

`mod-rune-engraving` is a reusable, **class-agnostic** [AzerothCore](https://www.azerothcore.org/)
module that adds a *Season of Discovery*-style rune engraving system to a WotLK
**3.3.5a** server: players visit a **Rune Engraver** NPC, pick a slot, and engrave
a rune to gain its spell.

These are developer docs: how the engine is built, how content modules plug into
it, and how to deploy and verify it. For a high-level summary and install steps,
see the [README](https://github.com/bennybroseph/mod-rune-engraving) in the repo root.

> **Just want to play?** You don't need these docs — the
> [**SoD installer**](https://github.com/bennybroseph/sod-installer) sets everything
> up in one command. These pages are for developers building or extending the engine.

## The one thing to internalize

This module is the **mechanism**, not the content. It ships **no runes** of its
own. Runes come from **content modules** (e.g. `mod-sod-mage`) that write rows
into the engine's catalog table — and they do so without any C++ linkage, so a
content module installs and works fine whether or not this engine is present.
See [Architecture](architecture.md).

## Start here

- **[Architecture](architecture.md)** — the database-not-symbols decoupling, the
  abstract-slot model, and how rune spells are granted (temporary) and re-applied
  on login.
- **[Integrating content](integrating-content.md)** — the contract: how a content
  module adds runes (the catalog table, the guarded SQL pattern, slot/class masks,
  id bands). Read this to add runes from another module.
- **[Deploy & verify](deploy-and-verify.md)** — applying SQL, the engraver NPC,
  the `.rune` test command, and confirming it loaded.
- **[Addon UI](addon-ui.md)** — the optional in-game panel: the `RUNE`
  addon-message protocol and the pure-C++ server side (the client addon,
  *RuneEngraver*, is a separate repo).
- **[Gotchas & troubleshooting](gotchas.md)** — the hard-won lessons, in one place.

## At a glance

| Piece | Where | Role |
|-------|-------|------|
| `rune_template` | `acore_world` | the rune catalog (content modules write here) |
| `rune_quest_unlock` / `rune_item_unlock` | `acore_world` | optional gating: a rune is *earned* via a quest or by using an item (content writes here) |
| `character_rune` / `character_rune_unlock` | `acore_characters` | per-character engraved slots + which gated runes are unlocked |
| `RuneEngravingMgr` | C++ | loads the catalog, tracks state, grants/removes spells |
| Rune Engraver NPC | entry `700000` | the gossip front-end for engraving (+ an optional debug-reset option) |
| `item_rune_unlock` | C++ ItemScript | generic "use this item to unlock its rune(s)" handler |
| `.rune` | GM command | headless engrave/list/slots/clear/unlock/lock/reload for testing |
