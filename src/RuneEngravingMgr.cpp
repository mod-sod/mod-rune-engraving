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

#include "RuneEngravingMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include <string>

RuneEngravingMgr* RuneEngravingMgr::instance()
{
    static RuneEngravingMgr instance;
    return &instance;
}

char const* RuneEngravingMgr::SlotName(uint8 slot)
{
    switch (slot)
    {
        case RUNE_SLOT_HEAD:     return "Head";
        case RUNE_SLOT_NECK:     return "Neck";
        case RUNE_SLOT_SHOULDER: return "Shoulder";
        case RUNE_SLOT_CLOAK:    return "Cloak";
        case RUNE_SLOT_CHEST:    return "Chest";
        case RUNE_SLOT_WRIST:    return "Wrist";
        case RUNE_SLOT_HANDS:    return "Hands";
        case RUNE_SLOT_WAIST:    return "Waist";
        case RUNE_SLOT_LEGS:     return "Legs";
        case RUNE_SLOT_FEET:     return "Feet";
        case RUNE_SLOT_RING:     return "Ring";
        default:                 return "Unknown";
    }
}

void RuneEngravingMgr::ApplyConfig()
{
    _requiredSpell = sConfigMgr->GetOption<uint32>("RuneEngraving.RequiredSpell", 0);

    // Per-slot unlock levels — SoD-phase defaults (Chest/Legs/Hands 25,
    // Waist/Wrist/Feet 40, Head/Cloak 50, the rest 60), each tunable via
    // "RuneEngraving.SlotMinLevel.<SlotName>".
    static uint32 const defaults[RUNE_SLOT_MAX] =
    {
        50, // Head
        60, // Neck
        60, // Shoulder
        50, // Cloak
        25, // Chest
        40, // Wrist
        25, // Hands
        40, // Waist
        25, // Legs
        40, // Feet
        60, // Ring
    };

    for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
        _slotMinLevel[slot] = sConfigMgr->GetOption<uint32>(
            std::string("RuneEngraving.SlotMinLevel.") + SlotName(slot), defaults[slot]);
}

uint32 RuneEngravingMgr::SlotMinLevel(uint8 slot) const
{
    return IsValidSlot(slot) ? _slotMinLevel[slot] : 0;
}

bool RuneEngravingMgr::MeetsPrereq(Player* player) const
{
    if (!player)
        return false;
    return _requiredSpell == 0 || player->HasSpell(_requiredSpell);
}

void RuneEngravingMgr::LoadCatalog()
{
    std::lock_guard<std::mutex> guard(_catalogMutex);
    _catalog.clear();
    _questUnlocks.clear();
    _gatedRunes.clear();

    if (QueryResult result = WorldDatabase.Query(
            "SELECT `rune_id`, `spell_id`, `class_mask`, `slot_mask`, `name`, `description` "
            "FROM `rune_template` WHERE `enabled` = 1"))
    {
        do
        {
            Field* f = result->Fetch();
            RuneTemplate rune;
            rune.RuneId      = f[0].Get<uint32>();
            rune.SpellId     = f[1].Get<uint32>();
            rune.ClassMask   = f[2].Get<uint32>();
            rune.SlotMask    = f[3].Get<uint32>();
            rune.Name        = f[4].Get<std::string>();
            rune.Description = f[5].Get<std::string>();
            _catalog[rune.RuneId] = std::move(rune);
        } while (result->NextRow());
    }

    // Quest-unlock mappings: a rune referenced here is "gated" and only
    // engravable once the character has unlocked it (see UnlockRunesForQuest).
    if (QueryResult qr = WorldDatabase.Query(
            "SELECT `quest_id`, `rune_id` FROM `rune_quest_unlock`"))
    {
        do
        {
            Field* f = qr->Fetch();
            uint32 questId = f[0].Get<uint32>();
            uint32 runeId = f[1].Get<uint32>();
            _questUnlocks[questId].push_back(runeId);
            _gatedRunes.insert(runeId);
        } while (qr->NextRow());
    }

    LOG_INFO("module", "RuneEngraving: loaded {} rune(s) ({} gated behind quests).",
        _catalog.size(), _gatedRunes.size());
}

uint32 RuneEngravingMgr::CatalogSize() const
{
    std::lock_guard<std::mutex> guard(_catalogMutex);
    return uint32(_catalog.size());
}

// The returned pointer is valid until the next LoadCatalog(): the catalog is
// only rewritten by startup load and the admin-only `.rune reload`.
RuneTemplate const* RuneEngravingMgr::GetRune(uint32 runeId) const
{
    std::lock_guard<std::mutex> guard(_catalogMutex);
    auto it = _catalog.find(runeId);
    return it != _catalog.end() ? &it->second : nullptr;
}

bool RuneEngravingMgr::RuneFitsSlot(RuneTemplate const& rune, uint8 slot) const
{
    return RuneRules::FitsSlot(rune.SlotMask, slot);
}

bool RuneEngravingMgr::RuneAllowedForClass(Player* player, RuneTemplate const& rune) const
{
    if (!player)
        return false;
    return RuneRules::AllowedForClass(rune.ClassMask, player->getClass());
}

bool RuneEngravingMgr::IsGated(uint32 runeId) const
{
    std::lock_guard<std::mutex> guard(_catalogMutex);
    return _gatedRunes.find(runeId) != _gatedRunes.end();
}

std::vector<RuneTemplate const*> RuneEngravingMgr::GetRunesForSlot(Player* player, uint8 slot) const
{
    std::vector<RuneTemplate const*> out;
    if (!player || !IsValidSlot(slot))
        return out;

    // Phase 1 (catalog lock): class- and slot-legal candidates, each tagged with
    // whether it's gated behind a quest unlock.
    std::vector<std::pair<RuneTemplate const*, bool>> candidates;
    {
        std::lock_guard<std::mutex> guard(_catalogMutex);
        for (auto const& [runeId, rune] : _catalog)
            if (RuneFitsSlot(rune, slot) && RuneAllowedForClass(player, rune))
                candidates.emplace_back(&rune,
                    _gatedRunes.find(runeId) != _gatedRunes.end());
    }

    // Phase 2 (state lock): drop gated runes this character hasn't unlocked.
    // Done as a separate lock so we never hold both at once (lock ordering).
    std::lock_guard<std::mutex> guard(_stateMutex);
    auto it = _unlocked.find(player->GetGUID());
    for (auto const& [rune, gated] : candidates)
    {
        if (gated && (it == _unlocked.end()
                || it->second.find(rune->RuneId) == it->second.end()))
            continue;
        out.push_back(rune);
    }
    return out;
}

void RuneEngravingMgr::LoadPlayer(Player* player)
{
    if (!player)
        return;

    ObjectGuid guid = player->GetGUID();
    std::array<uint32, RUNE_SLOT_MAX> slots{};

    QueryResult result = CharacterDatabase.Query(
        "SELECT `slot`, `rune_id` FROM `character_rune` WHERE `guid` = {}",
        guid.GetCounter());
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            uint8 slot = f[0].Get<uint8>();
            uint32 runeId = f[1].Get<uint32>();
            if (IsValidSlot(slot))
                slots[slot] = runeId;
        } while (result->NextRow());
    }

    std::unordered_set<uint32> unlocked;
    if (QueryResult ur = CharacterDatabase.Query(
            "SELECT `rune_id` FROM `character_rune_unlock` WHERE `guid` = {}",
            guid.GetCounter()))
    {
        do
        {
            unlocked.insert(ur->Fetch()[0].Get<uint32>());
        } while (ur->NextRow());
    }

    std::lock_guard<std::mutex> guard(_stateMutex);
    _engraved[guid] = slots;
    _unlocked[guid] = std::move(unlocked);
}

void RuneEngravingMgr::UnloadPlayer(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(_stateMutex);
    _engraved.erase(guid);
    _unlocked.erase(guid);
}

void RuneEngravingMgr::ApplyAll(Player* player)
{
    if (!player)
        return;

    std::lock_guard<std::mutex> guard(_stateMutex);
    auto it = _engraved.find(player->GetGUID());
    if (it == _engraved.end())
        return;

    for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
    {
        uint32 runeId = it->second[slot];
        if (!runeId)
            continue;

        RuneTemplate const* rune = GetRune(runeId);
        if (!rune)
            continue; // unknown right now (e.g. rune disabled) — keep the row, don't grant

        // A class mismatch can only arise if this character GUID was reused by a
        // character of a different class (real characters can't change class).
        // The stored rune isn't this character's — purge it instead of granting.
        if (!RuneAllowedForClass(player, *rune))
        {
            it->second[slot] = 0;
            CharacterDatabase.Execute(
                "DELETE FROM `character_rune` WHERE `guid` = {} AND `slot` = {}",
                player->GetGUID().GetCounter(), uint32(slot));
            continue;
        }

        if (rune->SpellId)
            player->learnSpell(rune->SpellId, /*temporary*/ true);
    }
}

bool RuneEngravingMgr::SpellGrantedByOtherSlot(ObjectGuid guid, uint32 spellId, uint8 exceptSlot) const
{
    auto it = _engraved.find(guid);
    if (it == _engraved.end())
        return false;

    for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
    {
        if (slot == exceptSlot)
            continue;
        uint32 runeId = it->second[slot];
        if (!runeId)
            continue;
        if (RuneTemplate const* rune = GetRune(runeId))
            if (rune->SpellId == spellId)
                return true;
    }
    return false;
}

EngraveResult RuneEngravingMgr::Engrave(Player* player, uint8 slot, uint32 runeId)
{
    if (!player || !IsValidSlot(slot))
        return EngraveResult::WrongSlot;

    std::lock_guard<std::mutex> guard(_stateMutex);

    RuneTemplate const* rune = GetRune(runeId);
    if (!rune)
        return EngraveResult::UnknownRune;
    if (!RuneFitsSlot(*rune, slot))
        return EngraveResult::WrongSlot;
    if (!RuneAllowedForClass(player, *rune))
        return EngraveResult::WrongClass;

    ObjectGuid guid = player->GetGUID();

    // Gated runes require this character to have unlocked them (e.g. via a quest).
    // IsGated briefly takes _catalogMutex — allowed here (state -> catalog order).
    if (IsGated(runeId))
    {
        auto u = _unlocked.find(guid);
        if (u == _unlocked.end() || u->second.find(runeId) == u->second.end())
            return EngraveResult::Locked;
    }

    // SoD rules: must have learned Engraving, the slot must be unlocked at this
    // level, and the same rune can't be engraved in two slots.
    if (!MeetsPrereq(player))
        return EngraveResult::PrereqMissing;
    if (!RuneRules::SlotUnlocked(player->GetLevel(), _slotMinLevel[slot]))
        return EngraveResult::SlotLevelTooLow;

    std::array<uint32, RUNE_SLOT_MAX>& slots = _engraved[guid];
    if (RuneRules::IsDuplicateRune(slots, runeId, slot))
        return EngraveResult::DuplicateRune;

    // Strip the spell from the rune currently in this slot, unless another slot
    // still grants the same spell.
    if (uint32 oldRuneId = slots[slot])
        if (RuneTemplate const* oldRune = GetRune(oldRuneId))
            if (oldRune->SpellId && oldRune->SpellId != rune->SpellId
                && !SpellGrantedByOtherSlot(guid, oldRune->SpellId, slot))
                player->removeSpell(oldRune->SpellId, SPEC_MASK_ALL, /*onlyTemporary*/ true);

    slots[slot] = runeId;
    CharacterDatabase.Execute(
        "REPLACE INTO `character_rune` (`guid`, `slot`, `rune_id`) VALUES ({}, {}, {})",
        guid.GetCounter(), uint32(slot), runeId);

    if (rune->SpellId)
        player->learnSpell(rune->SpellId, /*temporary*/ true);

    return EngraveResult::Success;
}

bool RuneEngravingMgr::RemoveRune(Player* player, uint8 slot)
{
    if (!player || !IsValidSlot(slot))
        return false;

    std::lock_guard<std::mutex> guard(_stateMutex);

    ObjectGuid guid = player->GetGUID();
    auto it = _engraved.find(guid);
    if (it == _engraved.end())
        return false;

    uint32 runeId = it->second[slot];
    if (!runeId)
        return false;

    if (RuneTemplate const* rune = GetRune(runeId))
        if (rune->SpellId && !SpellGrantedByOtherSlot(guid, rune->SpellId, slot))
            player->removeSpell(rune->SpellId, SPEC_MASK_ALL, /*onlyTemporary*/ true);

    it->second[slot] = 0;
    CharacterDatabase.Execute(
        "DELETE FROM `character_rune` WHERE `guid` = {} AND `slot` = {}",
        guid.GetCounter(), uint32(slot));

    return true;
}

uint32 RuneEngravingMgr::GetEngraved(ObjectGuid guid, uint8 slot) const
{
    if (!IsValidSlot(slot))
        return 0;

    std::lock_guard<std::mutex> guard(_stateMutex);
    auto it = _engraved.find(guid);
    return it != _engraved.end() ? it->second[slot] : 0;
}

void RuneEngravingMgr::DeleteCharacterData(CharacterDatabaseTransaction trans, uint32 guidLow)
{
    trans->Append("DELETE FROM `character_rune` WHERE `guid` = {}", guidLow);
    trans->Append("DELETE FROM `character_rune_unlock` WHERE `guid` = {}", guidLow);

    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
    std::lock_guard<std::mutex> guard(_stateMutex);
    _engraved.erase(guid);
    _unlocked.erase(guid);
}

bool RuneEngravingMgr::UnlockRune(Player* player, uint32 runeId)
{
    if (!player)
        return false;

    ObjectGuid guid = player->GetGUID();
    std::lock_guard<std::mutex> guard(_stateMutex);
    if (!_unlocked[guid].insert(runeId).second)
        return false; // already unlocked

    CharacterDatabase.Execute(
        "REPLACE INTO `character_rune_unlock` (`guid`, `rune_id`) VALUES ({}, {})",
        guid.GetCounter(), runeId);
    return true;
}

bool RuneEngravingMgr::LockRune(Player* player, uint32 runeId)
{
    if (!player)
        return false;

    ObjectGuid guid = player->GetGUID();
    std::lock_guard<std::mutex> guard(_stateMutex);
    auto it = _unlocked.find(guid);
    if (it == _unlocked.end() || it->second.erase(runeId) == 0)
        return false;

    CharacterDatabase.Execute(
        "DELETE FROM `character_rune_unlock` WHERE `guid` = {} AND `rune_id` = {}",
        guid.GetCounter(), runeId);
    return true;
}

std::vector<std::string> RuneEngravingMgr::UnlockRunesForQuest(Player* player, uint32 questId)
{
    std::vector<std::string> unlockedNames;
    if (!player)
        return unlockedNames;

    // Phase 1 (catalog lock): which runes this quest unlocks.
    std::vector<uint32> runeIds;
    {
        std::lock_guard<std::mutex> guard(_catalogMutex);
        auto it = _questUnlocks.find(questId);
        if (it == _questUnlocks.end())
            return unlockedNames;
        runeIds = it->second;
    }

    // Phase 2: unlock each (UnlockRune / GetRune take their own locks).
    for (uint32 runeId : runeIds)
        if (UnlockRune(player, runeId))
            if (RuneTemplate const* rune = GetRune(runeId))
                unlockedNames.push_back(rune->Name);

    return unlockedNames;
}

std::vector<uint32> RuneEngravingMgr::GetUnlockedRunes(ObjectGuid guid) const
{
    std::vector<uint32> out;
    std::lock_guard<std::mutex> guard(_stateMutex);
    auto it = _unlocked.find(guid);
    if (it != _unlocked.end())
        out.assign(it->second.begin(), it->second.end());
    return out;
}
