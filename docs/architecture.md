# Architecture

## The goal: optional coupling

The engine and its content (runes) must stay **optionally** connected. Installing
a content module like `mod-sod-mage` on its own should still work (its spells are
real, learnable by GM command); installing this engine *as well* should make those
same spells acquirable as engravable runes — with **no hard dependency** in either
direction.

AzerothCore modules are statically linked into `worldserver`, so any C++ call from
a content module into this engine would be a compile-time dependency — exactly what
we must avoid. The coupling is therefore **through the database, not symbols**.

| Side | Owns | Role |
|------|------|------|
| **Engine** (this module) | `rune_template`, `rune_quest_unlock`, `rune_contract` (world); `character_rune`, `character_rune_unlock` (characters); all C++ | the mechanism; seeds **no** runes |
| **Content** (e.g. `mod-sod-mage`) | its own spells + rune rows | inserts into `rune_template`, guarded so it's a no-op without the engine |

Result:

- **Engine absent** — the content module's rune SQL is a clean no-op; its spells
  still exist and are GM-`.learn`-able.
- **Engine present** — the content's runes appear at the engraver NPC and grant
  their spells when engraved.

The full content-side contract is in [Integrating content](integrating-content.md).

## Abstract engraving slots

Engraving slots (Head, Neck, Shoulder, Cloak, Chest, Wrist, Hands, Waist, Legs,
Feet, Ring) are an engine concept **independent of equipped gear** — engraving is a
per-character choice, one rune per slot. They are *not* `EquipmentSlots`; a rune's
spell applies regardless of what the character is wearing. A rune's legal slots are
a `slot_mask` bitmask of `1 << RuneSlot`, and the `RuneSlot` enum's numeric values
are the contract (see `src/RuneEngravingMgr.h`).

## How a spell is granted

Rune spells are granted as **temporary** spells:

- engrave → `Player::learnSpell(spellId, /*temporary*/ true)`
- un-engrave / swap → `Player::removeSpell(spellId, SPEC_MASK_ALL, /*onlyTemporary*/ true)`

`onlyTemporary` means the engine never strips a spell the player learned by other
means. Temporary spells are **not persisted** to `character_spell`, so they vanish
on logout — which is intentional: the engine **re-applies** every engraved rune's
spell on login (`OnPlayerLogin`). The source of truth is the `character_rune` table.

## Unlocks (quest gating)

A rune is **available by class** by default. A rune mapped in `rune_quest_unlock`
becomes **gated**: it's hidden at the engraver and unengravable until the character
unlocks it. `OnPlayerCompleteQuest` looks up the completed quest in
`rune_quest_unlock` and records the unlock per-character in `character_rune_unlock`
(loaded on login alongside engraved runes). Gating is per-rune and opt-in — a rune
with no quest mapping is never gated. See [Integrating content](integrating-content.md).

The catalog-side gating data (`rune_quest_unlock`) and the per-character unlock
state live under the two different locks, so the engrave list is computed in two
phases (collect class/slot-legal candidates + their gated flag under the catalog
lock, then filter by the character's unlock set under the state lock) to keep the
one-way lock ordering.

## Engraving rules (SoD)

Beyond class/slot legality and unlocks, `Engrave` enforces three Season of
Discovery rules, then returns an **`EngraveResult`** so callers (the NPC and the
`.rune` command) can give a specific reason rather than a generic failure:

- **Learned-Engraving prerequisite** — if `RuneEngraving.RequiredSpell` is set,
  the character must have learned that ability first (`Player::HasSpell`). The
  engine only enforces it; granting the ability is content's job.
- **Level-gated slots** — each slot has a minimum level
  (`RuneEngraving.SlotMinLevel.<SlotName>`, SoD-phase defaults), so slots unlock as
  the character levels. The NPC shows locked slots as "(unlocks at N)".
- **No duplicate rune** — the same rune can't be engraved in two slots (SoD runes
  are single-slot; this guards the engine's more permissive multi-slot `slot_mask`).

These are **server config + engine logic only — no schema or contract change**, so
the catalog and content modules are untouched. The prereq spell id and per-slot
levels are read by `ApplyConfig()` on `OnAfterConfigLoad`. The pure decision bits
(`RuneRules::SlotUnlocked`, `RuneRules::IsDuplicateRune`) are unit-tested.

## Self-healing edge cases

- **GUID reuse.** AzerothCore can reseed the character-GUID counter after a restart,
  so a deleted character's GUID can be reused by a new (possibly different-class)
  character. On login, `ApplyAll` skips and purges any stored rune whose `class_mask`
  doesn't match the current character. A rune merely *missing* from the catalog
  (e.g. temporarily `enabled = 0`) is kept, not purged.
- **Character deletion.** `OnPlayerDeleteFromDB` appends `DELETE`s for the
  character's rune rows to the deletion transaction, so nothing is left to inherit.

## Concurrency

`RuneEngravingMgr` separates two concerns under two locks: the **catalog** (loaded
once at startup, rewritten only by the admin-only `.rune reload`) and the
**per-character state**. Lock ordering is one-way (state → catalog, never the
reverse) to avoid deadlock. See [Gotchas](gotchas.md) for the reload caveat.

## UI: gossip first

The 3.3.5a client has no engraving panel, and there's no clean core hook for
receiving client addon messages — so the v1 UI is a **gossip NPC** (entry `700000`,
`ScriptName npc_rune_engraver`): pick a slot → pick a rune → engrave/remove. The
engine logic lives in `RuneEngravingMgr`; the NPC and the `.rune` GM command are
thin front-ends, so a future addon UI can reuse the same API.

## No core edits

Everything lives under `modules/mod-rune-engraving/`. Behavior is driven through
script hooks and DB rows — never by patching AzerothCore itself.

See also: [Integrating content](integrating-content.md) · [Deploy & verify](deploy-and-verify.md) · [Gotchas](gotchas.md)
