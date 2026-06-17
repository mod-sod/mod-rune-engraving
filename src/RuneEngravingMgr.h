/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MOD_RUNE_ENGRAVING_MGR_H
#define MOD_RUNE_ENGRAVING_MGR_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

// Abstract engraving slots. Deliberately independent of EquipmentSlots: a rune
// is engraved into one of these per character regardless of equipped gear. The
// numeric values are the contract used by `slot_mask` in `rune_template`
// (slot_mask bit N == 1 << RuneSlot N), so do not reorder without a DB migration.
enum RuneSlot : uint8
{
    RUNE_SLOT_HEAD     = 0,
    RUNE_SLOT_NECK     = 1,
    RUNE_SLOT_SHOULDER = 2,
    RUNE_SLOT_CLOAK    = 3,
    RUNE_SLOT_CHEST    = 4,
    RUNE_SLOT_WRIST    = 5,
    RUNE_SLOT_HANDS    = 6,
    RUNE_SLOT_WAIST    = 7,
    RUNE_SLOT_LEGS     = 8,
    RUNE_SLOT_FEET     = 9,
    RUNE_SLOT_RING     = 10,
    RUNE_SLOT_MAX      = 11
};

// Pure eligibility predicates — no Player, no DB. The manager delegates to these
// so the slot/class-mask contract (which content modules rely on) is unit-testable
// in isolation. `slotMask` is a bitmask of (1 << RuneSlot); `classMask` is the AC
// classmask (1 << (classId-1), classId 1..11; 0 = any class).
namespace RuneRules
{
    inline bool SlotIsValid(uint8 slot) { return slot < RUNE_SLOT_MAX; }

    inline bool FitsSlot(uint32 slotMask, uint8 slot)
    {
        return SlotIsValid(slot) && (slotMask & (1u << slot)) != 0;
    }

    inline bool AllowedForClass(uint32 classMask, uint8 classId)
    {
        return classMask == 0 || (classMask & (1u << (classId - 1))) != 0;
    }

    // A slot is unlocked once the character reaches its minimum level (SoD gates
    // engraving slots by level/phase). minLevel 0/1 == always available.
    inline bool SlotUnlocked(uint8 level, uint32 minLevel)
    {
        return level >= minLevel;
    }

    // Whether `runeId` is already engraved in some slot other than `exceptSlot`
    // (SoD runes are single-slot; this guards a rune being engraved twice).
    inline bool IsDuplicateRune(std::array<uint32, RUNE_SLOT_MAX> const& slots,
                                uint32 runeId, uint8 exceptSlot)
    {
        if (runeId == 0)
            return false; // an empty slot ("no rune") is never a duplicate
        for (uint8 s = 0; s < RUNE_SLOT_MAX; ++s)
            if (s != exceptSlot && slots[s] == runeId)
                return true;
        return false;
    }
}

// One catalog entry from `rune_template`. Content modules own these rows; the
// engine only reads them, so it never needs to know which module a rune is from.
struct RuneTemplate
{
    uint32      RuneId    = 0;
    uint32      SpellId   = 0;
    uint32      ClassMask = 0; // 0 == any class; else AC classmask (1 << (class-1))
    uint32      SlotMask  = 0; // bitmask of (1 << RuneSlot) this rune is legal in
    std::string Name;
    std::string Description;
    std::string Icon;          // inventory-icon name (for the addon UI); may be empty
};

// Outcome of an Engrave attempt, so callers (NPC, .rune command) can give the
// player a specific reason rather than a generic failure.
enum class EngraveResult
{
    Success,
    UnknownRune,     // no such rune / disabled
    WrongClass,      // rune not legal for the character's class
    WrongSlot,       // rune can't go in that slot
    Locked,          // gated rune the character hasn't unlocked (quest)
    PrereqMissing,   // character hasn't learned Engraving (RequiredSpell)
    SlotLevelTooLow, // slot not yet unlocked at the character's level
    DuplicateRune,   // same rune already engraved in another slot
};

// Result of a debug reset: how much gated progress was reverted for a character.
struct RuneResetSummary
{
    uint32 QuestsReset = 0;
    uint32 RunesLocked = 0;
    uint32 ItemsRestored = 0; // unlock items handed back (e.g. deciphered notes)
};

// Class-agnostic rune engraving engine. Loads the catalog from the world DB at
// startup and tracks each online character's engraved runes, granting the
// associated spells as temporary (so they vanish on logout and are reapplied on
// the next login). Content modules contribute purely via the DB catalog.
class RuneEngravingMgr
{
public:
    static RuneEngravingMgr* instance();

    // Lifecycle
    void LoadCatalog();
    void ApplyConfig();                 // read engraving-rule tunables from config
    bool IsEnabled() const { return _enabled; }
    void SetEnabled(bool enabled) { _enabled = enabled; }
    bool DebugMenu() const { return _debugMenu; }
    uint32 CatalogSize() const;

    // Catalog access
    RuneTemplate const* GetRune(uint32 runeId) const;
    // Runes this player may engrave into `slot` (class- and slot-legal, unlocked).
    std::vector<RuneTemplate const*> GetRunesForSlot(Player* player, uint8 slot) const;

    // Per-character state
    void LoadPlayer(Player* player);    // on login: read character_rune(+_unlock)
    void UnloadPlayer(ObjectGuid guid); // on logout: drop cached state
    void ApplyAll(Player* player);      // grant the spells for all engraved slots
    EngraveResult Engrave(Player* player, uint8 slot, uint32 runeId);
    bool RemoveRune(Player* player, uint8 slot);
    uint32 GetEngraved(ObjectGuid guid, uint8 slot) const;

    // Engraving rules (SoD): the learned-Engraving prerequisite and per-slot
    // unlock levels, both read from config by ApplyConfig.
    uint32 SlotMinLevel(uint8 slot) const;
    bool MeetsPrereq(Player* player) const;

    // Unlocks. A rune is "gated" if any rune_quest_unlock row references it; a
    // gated rune is only engravable once the character has unlocked it. Runes
    // with no quest mapping are not gated (available by class).
    bool IsGated(uint32 runeId) const;
    bool UnlockRune(Player* player, uint32 runeId);  // true if newly unlocked
    bool LockRune(Player* player, uint32 runeId);    // true if it was unlocked
    // Unlock every rune mapped to `questId`; returns the names newly unlocked.
    std::vector<std::string> UnlockRunesForQuest(Player* player, uint32 questId);
    // Unlock every rune mapped to `itemId` (rune_item_unlock); same return.
    // Driven by the `item_rune_unlock` ItemScript when such an item is used.
    std::vector<std::string> UnlockRunesForItem(Player* player, uint32 itemId);
    std::vector<uint32> GetUnlockedRunes(ObjectGuid guid) const;

    // Debug/testing: revert a character's gated-rune progress — reset every quest
    // mapped in rune_quest_unlock, lock every quest- AND item-unlocked rune, clear
    // any engraving of a now-locked rune (so no engraved-but-locked state lingers),
    // and hand back the unlock item for each relocked item-gated rune (e.g. the
    // consumed deciphered notes) so the discovery can be re-run. Lets the engraver's
    // debug menu put a tester back to the pre-discovery state.
    RuneResetSummary ResetGatedProgress(Player* player);

    // Purge a character's rune rows. Called when a character is deleted so a
    // reused GUID can never inherit a previous (possibly different-class)
    // character's runes. Appends to the deletion transaction so it's atomic.
    void DeleteCharacterData(CharacterDatabaseTransaction trans, uint32 guidLow);

    // Eligibility helpers
    bool RuneFitsSlot(RuneTemplate const& rune, uint8 slot) const;
    bool RuneAllowedForClass(Player* player, RuneTemplate const& rune) const;

    static char const* SlotName(uint8 slot);
    static bool IsValidSlot(uint8 slot) { return RuneRules::SlotIsValid(slot); }

private:
    RuneEngravingMgr() = default;

    // Whether any *other* engraved slot of this character grants `spellId`, so a
    // swap/removal doesn't strip a spell another slot still provides.
    bool SpellGrantedByOtherSlot(ObjectGuid guid, uint32 spellId, uint8 exceptSlot) const;

    // Grant a rune's spell as temporary (one client "learned" notification — see
    // the .cpp note on why this uses addSpell, not learnSpell).
    void GrantSpell(Player* player, uint32 spellId);

    // Reset one quest's status on a character (mirrors the core's `.quest remove`):
    // clear it from the log, and drop its active/rewarded state.
    void ResetQuest(Player* player, uint32 questId);

    bool _enabled = true;
    bool _debugMenu = false;                         // expose the engraver debug menu
    uint32 _requiredSpell = 0;                       // learned-Engraving gate; 0 = off
    std::array<uint32, RUNE_SLOT_MAX> _slotMinLevel{}; // per-slot unlock level (config)
    std::unordered_map<uint32, RuneTemplate> _catalog;                          // guarded by _catalogMutex
    std::unordered_map<uint32, std::vector<uint32>> _questUnlocks;              // questId -> runeIds; _catalogMutex
    std::unordered_map<uint32, std::vector<uint32>> _itemUnlocks;              // itemId -> runeIds; _catalogMutex
    std::unordered_set<uint32> _gatedRunes;                                     // runes gated behind a quest/item; _catalogMutex
    std::unordered_map<ObjectGuid, std::array<uint32, RUNE_SLOT_MAX>> _engraved; // guarded by _stateMutex
    std::unordered_map<ObjectGuid, std::unordered_set<uint32>> _unlocked;        // guid -> unlocked runeIds; _stateMutex

    // Lock ordering: only ever acquire _catalogMutex while (optionally) holding
    // _stateMutex, never the reverse. The state-side methods call GetRune (which
    // takes _catalogMutex) while holding _stateMutex; nothing locks _stateMutex
    // while holding _catalogMutex.
    mutable std::mutex _catalogMutex;
    mutable std::mutex _stateMutex;
};

#define sRuneEngravingMgr RuneEngravingMgr::instance()

#endif // MOD_RUNE_ENGRAVING_MGR_H
