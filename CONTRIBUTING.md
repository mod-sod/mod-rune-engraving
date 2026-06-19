# Contributing to mod-rune-engraving

This module is the **engine** — the mechanism, not the content. Most contributions
fall into one of two paths:

**Adding runes (from a content module).** You don't edit this repo — you write
guarded rows into the engine's catalog tables from your own module. Start with the
copy-paste templates in **[`templates/`](templates/README.md)** (rune catalog +
item/quest unlocks), and read the full contract in
**[docs/integrating-content.md](docs/integrating-content.md)**.

**Improving the engine itself.** Behavior lives in `src/` (the manager, the engraver
NPC, the addon protocol). See **[`docs/`](docs/Home.md)** for the architecture and the
testing setup. Pure eligibility logic is unit-tested — keep it that way.

The `templates/` files live outside `src/` and `data/sql/`, so they're never compiled
or applied — they're reference only.

No core edits — everything stays under `modules/mod-rune-engraving/`.
