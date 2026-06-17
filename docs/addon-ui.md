# Addon UI (the RUNE protocol)

Players can engrave from an in-game panel instead of the gossip NPC, via a
separate client addon (**RuneEngraver**, its own repo). The NPC stays as a
debug/no-addon fallback — the addon is just another front-end onto the same
`RuneEngravingMgr` API.

A stock 3.3.5a client can't be *sent* UI, so the panel is a Lua addon the player
installs; this engine only provides the **server side of the transport**. There's
**no framework and no core edit** — it's pure C++ in `src/rune_engraving_addon.cpp`.

## How the transport works

- **Receive**: the client self-whispers `LANG_ADDON` messages prefixed `RUNE`,
  which reach a `PlayerScript::OnPlayerBeforeSendChatMessage` hook (the core fires
  it for addon messages; `AddonChannelCommandHandler` only claims `AzerothCore\t`
  messages, so ours fall through). The handler parses the body and calls the
  manager.
- **Send**: replies are addon whispers built with
  `ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, msg)`
  and `player->GetSession()->SendPacket(&data)` — the same pattern the core's own
  addon-channel handler uses.
- `Addon.Channel` world config must be on (default). The panel reuses the exact
  manager calls the NPC makes (`MeetsPrereq`, `SlotMinLevel`, `GetEngraved`,
  `GetRunesForSlot`, `Engrave`, `RemoveRune`), plus the `RuneTemplate.Icon` field
  (loaded from `rune_template.icon`) so the panel can show rune icons.

## Protocol grammar

Addon prefix `RUNE`; bodies are `~`-separated; a push is a `BEGIN…END` sequence so
each line stays under the 254-char cap. This is the **contract**, also documented
in the addon repo (`docs/protocol.md`) — keep both in sync.

**Client → server**

| Body | Meaning |
|---|---|
| `REQ` | request the full panel state |
| `ENG~<slot>~<runeId>` | engrave |
| `DEL~<slot>` | clear the slot |

**Server → client** (client accumulates between `BEGIN` and `END`, then renders)

| Body | Fields |
|---|---|
| `BEGIN~<prereqMet 0\|1>~<level>` | learned-Engraving flag + character level |
| `SLOT~<slot>~<name>~<minLevel>~<current>` | one per slot (`current` = engraved rune id, 0 = none) |
| `RUNE~<slot>~<runeId>~<icon>~<name>` | one per engravable rune; `name` is last so it may contain `~` |
| `MSG~<text>` | optional feedback (engrave result / failure reason) |
| `END` | push complete |

After an `ENG`/`DEL` the server re-sends the whole `BEGIN…END` block (with a
`MSG`), so the client just re-renders.

## The client addon

**RuneEngraver** is a standalone repo (installed in `Interface/AddOns/`), not part
of this engine — they couple only through the protocol above. It adds a Character
Sheet button that opens the panel. See its README for install/use.

See also: [Architecture](architecture.md) · [Deploy & verify](deploy-and-verify.md)
