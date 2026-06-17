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
#include "Item.h"
#include "ItemScript.h"
#include "Player.h"
#include "RuneEngravingMgr.h"
#include "ScriptMgr.h"
#include "Spell.h"

// Generic "use this item to unlock its rune(s)" script. A content module binds
// it to an item via item_template.ScriptName = 'item_rune_unlock' and maps the
// item to rune(s) in rune_item_unlock. On use we unlock those runes (engine-
// owned state) and consume one of the item, mirroring the quest-unlock path but
// driven by an item instead of a quest. The contract is data-only: content never
// links this engine in C++.
class item_rune_unlock : public ItemScript
{
public:
    item_rune_unlock() : ItemScript("item_rune_unlock") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return false;

        uint32 itemId = item->GetEntry();
        ChatHandler handler(player->GetSession());

        if (!sRuneEngravingMgr->IsEnabled())
        {
            handler.SendSysMessage(
                "|cFFFF0000[Rune Engraver]|r Rune engraving is currently unavailable.");
            return true;
        }

        std::vector<std::string> unlocked =
            sRuneEngravingMgr->UnlockRunesForItem(player, itemId);

        if (!unlocked.empty())
        {
            for (std::string const& name : unlocked)
                handler.PSendSysMessage(
                    "|cFF00FF00[Rune Engraver]|r You have discovered the |cFFFFD700{}|r "
                    "rune. Visit a Rune Engraver to engrave it.", name);

            // The notes are spent on the discovery.
            player->DestroyItemCount(itemId, 1, true);
        }
        else
        {
            // Either already discovered, or the item maps to no rune (it shouldn't
            // carry this ScriptName then). Don't consume it.
            handler.SendSysMessage(
                "|cFFFFFF00[Rune Engraver]|r You have already discovered everything "
                "these notes contain.");
        }

        // Handled — suppress the item's benign use-spell (present only so the
        // client offers "Use").
        return true;
    }
};

void AddSC_item_rune_unlock()
{
    new item_rune_unlock();
}
