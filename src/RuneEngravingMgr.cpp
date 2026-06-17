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
#include "QuestDef.h"
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
    _debugMenu = sConfigMgr->GetOption<bool>("RuneEngraving.DebugMenu", false);

    // Per-slot unlock levels — defaults map each SoD engraving slot to the START
    // of its phase's level band (P1=1 Chest/Legs/Hands, P2=26 Waist/Feet,
    // P3=41 Head/Wrist, P4=51 Cloak/Ring). SoD itself gated slots by the server
    // phase + rune discovery, not character level, so these approximate that on a
    // single, non-phased realm. Neck/Shoulder aren't SoD slots (left generic at
    // 60). Each tunable via "RuneEngraving.SlotMinLevel.<SlotName>".
    static uint32 const defaults[RUNE_SLOT_MAX] =
    {
        41, // Head      (P3)
        60, // Neck      (not a SoD slot; generic)
        60, // Shoulder  (not a SoD slot; generic)
        51, // Cloak     (P4)
        1,  // Chest     (P1)
        41, // Wrist     (P3)
        1,  // Hands     (P1)
        26, // Waist     (P2)
        1,  // Legs      (P1)
        26, // Feet      (P2)
        51, // Ring      (P4)
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
    _itemUnlocks.clear();
    _gatedRunes.clear();

    if (QueryResult result = WorldDatabase.Query(
            "SELECT `rune_id`, `spell_id`, `class_mask`, `slot_mask`, `name`, `description`, `icon` "
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
            rune.Icon        = f[6].Get<std::string>();
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

    // Item-unlock mappings: same gating, but unlocked by using an item bound to
    // the `item_rune_unlock` ItemScript (see UnlockRunesForItem).
    if (QueryResult ir = WorldDatabase.Query(
            "SELECT `item_id`, `rune_id` FROM `rune_item_unlock`"))
    {
        do
        {
            Field* f = ir->Fetch();
            uint32 itemId = f[0].Get<uint32>();
            uint32 runeId = f[1].Get<uint32>();
            _itemUnlocks[itemId].push_back(runeId);
            _gatedRunes.insert(runeId);
        } while (ir->NextRow());
    }

    LOG_INFO("module", "RuneEngraving: loaded {} rune(s) ({} gated behind quests/items).",
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
            GrantSpell(player, rune->SpellId);
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

    // Re-engraving the rune already in this slot is a no-op: the spell is already
    // granted, so skip the redundant DB write and grant (whose client "learned"
    // notification would otherwise fire again).
    if (slots[slot] == runeId)
        return EngraveResult::Success;

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
        GrantSpell(player, rune->SpellId);

    return EngraveResult::Success;
}

// Grant a rune's spell as temporary.
//
// We deliberately use addSpell instead of Player::learnSpell. learnSpell is the
// high-level path (it fires OnPlayerLearnSpell, follows rank chains, learns
// spells that require this one, and guards against re-learning an active spell),
// but for a *temporary* spell it sends the "you have learned X" packet TWICE —
// once inside addSpell and once itself — so callers see a doubled notification.
// Rune spells are standalone custom IDs (no rank chain, nothing requires them,
// nothing tracks learning them), so the only learnSpell behavior we actually
// want is its already-known guard, which we replicate here. addSpell then sends
// exactly one packet — and that single packet is required, since ApplyAll runs
// after the client's initial spell list, so the client must be told the spell.
void RuneEngravingMgr::GrantSpell(Player* player, uint32 spellId)
{
    if (!player || player->HasActiveSpell(spellId))
        return;

    player->addSpell(spellId, SPEC_MASK_ALL, /*updateActive*/ true, /*temporary*/ true);
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

std::vector<std::string> RuneEngravingMgr::UnlockRunesForItem(Player* player, uint32 itemId)
{
    std::vector<std::string> unlockedNames;
    if (!player)
        return unlockedNames;

    // Phase 1 (catalog lock): which runes this item unlocks.
    std::vector<uint32> runeIds;
    {
        std::lock_guard<std::mutex> guard(_catalogMutex);
        auto it = _itemUnlocks.find(itemId);
        if (it == _itemUnlocks.end())
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

void RuneEngravingMgr::ResetQuest(Player* player, uint32 questId)
{
    // Mirror the core's `.quest remove`: drop it from the active log, then clear
    // its active/rewarded state so it can be taken and completed again.
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        if (player->GetQuestSlotQuestId(slot) == questId)
        {
            player->SetQuestSlot(slot, 0);
            player->TakeQuestSourceItem(questId, false);
        }
    }

    player->RemoveRewardedQuest(questId);
    player->RemoveActiveQuest(questId, false);
}

RuneResetSummary RuneEngravingMgr::ResetGatedProgress(Player* player)
{
    RuneResetSummary summary;
    if (!player)
        return summary;

    // Snapshot the quest/item -> rune mappings under the catalog lock, then act
    // without it held (the state-side calls below take _stateMutex / their own
    // locks).
    std::vector<std::pair<uint32, std::vector<uint32>>> questMaps, itemMaps;
    {
        std::lock_guard<std::mutex> guard(_catalogMutex);
        questMaps.assign(_questUnlocks.begin(), _questUnlocks.end());
        itemMaps.assign(_itemUnlocks.begin(), _itemUnlocks.end());
    }

    std::unordered_set<uint32> gatedRunes;
    for (auto const& [questId, runeIds] : questMaps)
    {
        bool hadProgress = player->GetQuestRewardStatus(questId) ||
                           player->GetQuestStatus(questId) != QUEST_STATUS_NONE;
        ResetQuest(player, questId);
        if (hadProgress)
            ++summary.QuestsReset;

        for (uint32 runeId : runeIds)
            gatedRunes.insert(runeId);
    }
    for (auto const& [itemId, runeIds] : itemMaps)
        for (uint32 runeId : runeIds)
            gatedRunes.insert(runeId);

    // Lock each gated rune, clearing any engraving of it first so we never leave an
    // engraved-but-locked rune (which would still grant its spell on next login).
    std::unordered_set<uint32> lockedRunes;
    for (uint32 runeId : gatedRunes)
    {
        for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
            if (GetEngraved(player->GetGUID(), slot) == runeId)
                RemoveRune(player, slot);

        if (LockRune(player, runeId))
        {
            lockedRunes.insert(runeId);
            ++summary.RunesLocked;
        }
    }

    // For an item-unlocked rune that was actually relocked, give back one of its
    // unlock item so the discovery can be re-run (e.g. the deciphered spell notes
    // the player consumed). The engine only knows the final unlock item; earlier
    // chain items are content's own (re-obtained from their vendor/drops).
    for (auto const& [itemId, runeIds] : itemMaps)
    {
        bool relocked = false;
        for (uint32 runeId : runeIds)
            if (lockedRunes.count(runeId))
            {
                relocked = true;
                break;
            }

        if (relocked)
        {
            player->AddItem(itemId, 1);
            ++summary.ItemsRestored;
        }
    }

    return summary;
}
