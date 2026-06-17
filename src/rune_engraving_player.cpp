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

#include "Chat.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "QuestDef.h"
#include "RuneEngravingMgr.h"
#include "ScriptMgr.h"

// Rune spells are granted as temporary, so they are not saved with the
// character and vanish on logout. This script reloads each character's engraved
// runes on login and re-grants their spells; logout drops the cached state.
class rune_engraving_player : public PlayerScript
{
public:
    rune_engraving_player() : PlayerScript("rune_engraving_player") {}

    void OnPlayerLogin(Player* player) override
    {
        if (!sRuneEngravingMgr->IsEnabled() || !player)
            return;

        sRuneEngravingMgr->LoadPlayer(player);
        sRuneEngravingMgr->ApplyAll(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;

        sRuneEngravingMgr->UnloadPlayer(player->GetGUID());
    }

    // Completing a quest unlocks any runes mapped to it (rune_quest_unlock).
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        if (!sRuneEngravingMgr->IsEnabled() || !player || !quest)
            return;

        std::vector<std::string> unlocked =
            sRuneEngravingMgr->UnlockRunesForQuest(player, quest->GetQuestId());
        for (std::string const& name : unlocked)
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cFF00FF00[Rune Engraver]|r You have discovered the |cFFFFD700{}|r "
                "rune. Visit a Rune Engraver to engrave it.", name);
    }

    // Purge a character's rune rows when it is deleted, so a later character that
    // reuses the GUID never inherits them. Appended to the deletion transaction.
    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction trans, uint32 guid) override
    {
        if (!sRuneEngravingMgr->IsEnabled())
            return;

        sRuneEngravingMgr->DeleteCharacterData(trans, guid);
    }
};

void AddSC_rune_engraving_player_script()
{
    new rune_engraving_player();
}
