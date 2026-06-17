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
#include "Creature.h"
#include "GossipDef.h"
#include "Player.h"
#include "RuneEngravingMgr.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include <mutex>
#include <unordered_map>

// The gossip `sender` field tags which menu an item belongs to, so a single
// OnGossipSelect can route slot picks, rune picks, removals, and navigation
// without colliding action-ID ranges.
enum RuneGossipSender
{
    SENDER_SLOT      = 1, // action = RuneSlot      -> open that slot's rune list
    SENDER_RUNE      = 2, // action = rune_id       -> engrave it in the selected slot
    SENDER_UNENGRAVE = 3, // action = RuneSlot      -> clear that slot
    SENDER_BACK      = 4, // action = 0             -> back to the slot list
    SENDER_RESET     = 5, // action = 0             -> debug: reset quests + unlocks
};

// The slot a player is currently browsing, so a SENDER_RUNE pick knows where to
// engrave. Confined to gossip interaction (single map thread per player), but
// guarded for parity with the rest of the module.
static std::unordered_map<ObjectGuid, uint8> sBrowsingSlot;
static std::mutex sBrowsingMutex;

class npc_rune_engraver : public CreatureScript
{
public:
    npc_rune_engraver() : CreatureScript("npc_rune_engraver") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sRuneEngravingMgr->IsEnabled())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Rune Engraver]|r Rune engraving is currently unavailable.");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }

        if (!sRuneEngravingMgr->MeetsPrereq(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Rune Engraver]|r You must learn Engraving before you can "
                "engrave runes.");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }

        ShowSlotMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();

        switch (sender)
        {
            case SENDER_SLOT:
                SetBrowsingSlot(player->GetGUID(), uint8(action));
                ShowRuneMenu(player, creature, uint8(action));
                break;
            case SENDER_RUNE:
            {
                uint8 slot = GetBrowsingSlot(player->GetGUID());
                ChatHandler handler(player->GetSession());
                switch (sRuneEngravingMgr->Engrave(player, slot, action))
                {
                    case EngraveResult::Success:
                    {
                        RuneTemplate const* rune = sRuneEngravingMgr->GetRune(action);
                        handler.PSendSysMessage(
                            "|cFF00FF00[Rune Engraver]|r Engraved |cFFFFD700{}|r in your {} slot.",
                            rune ? rune->Name.c_str() : "rune", RuneEngravingMgr::SlotName(slot));
                        break;
                    }
                    case EngraveResult::PrereqMissing:
                        handler.SendSysMessage("|cFFFF0000[Rune Engraver]|r You must learn Engraving first.");
                        break;
                    case EngraveResult::SlotLevelTooLow:
                        handler.PSendSysMessage(
                            "|cFFFF0000[Rune Engraver]|r Your {} slot unlocks at level {}.",
                            RuneEngravingMgr::SlotName(slot), sRuneEngravingMgr->SlotMinLevel(slot));
                        break;
                    case EngraveResult::DuplicateRune:
                        handler.SendSysMessage("|cFFFF0000[Rune Engraver]|r That rune is already engraved in another slot.");
                        break;
                    case EngraveResult::Locked:
                        handler.SendSysMessage("|cFFFF0000[Rune Engraver]|r You haven't discovered that rune yet.");
                        break;
                    case EngraveResult::WrongClass:
                        handler.SendSysMessage("|cFFFF0000[Rune Engraver]|r That rune isn't for your class.");
                        break;
                    default:
                        handler.SendSysMessage("|cFFFF0000[Rune Engraver]|r You cannot engrave that rune there.");
                        break;
                }
                ShowRuneMenu(player, creature, slot);
                break;
            }
            case SENDER_UNENGRAVE:
                if (sRuneEngravingMgr->RemoveRune(player, uint8(action)))
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cFFFFFF00[Rune Engraver]|r Cleared the rune from your {} slot.",
                        RuneEngravingMgr::SlotName(uint8(action)));
                ShowRuneMenu(player, creature, uint8(action));
                break;
            case SENDER_RESET:
            {
                // Guard against a stale menu if the debug flag was turned off.
                if (sRuneEngravingMgr->DebugMenu())
                {
                    RuneResetSummary summary = sRuneEngravingMgr->ResetGatedProgress(player);
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cFFFFFF00[Rune Engraver]|r Debug reset: locked {} rune(s), "
                        "reset {} quest(s), restored {} unlock item(s).",
                        summary.RunesLocked, summary.QuestsReset, summary.ItemsRestored);
                }
                ShowSlotMenu(player, creature);
                break;
            }
            case SENDER_BACK:
            default:
                ShowSlotMenu(player, creature);
                break;
        }
        return true;
    }

private:
    static void SetBrowsingSlot(ObjectGuid guid, uint8 slot)
    {
        std::lock_guard<std::mutex> guard(sBrowsingMutex);
        sBrowsingSlot[guid] = slot;
    }

    static uint8 GetBrowsingSlot(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> guard(sBrowsingMutex);
        auto it = sBrowsingSlot.find(guid);
        return it != sBrowsingSlot.end() ? it->second : RUNE_SLOT_MAX;
    }

    // Top level: one entry per engraving slot, annotated with its current rune.
    void ShowSlotMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
        {
            std::string text = "Slot: ";
            text += RuneEngravingMgr::SlotName(slot);

            uint32 minLevel = sRuneEngravingMgr->SlotMinLevel(slot);
            if (player->GetLevel() < minLevel)
            {
                text += " |cFF808080(unlocks at " + std::to_string(minLevel) + ")|r";
            }
            else
            {
                uint32 runeId = sRuneEngravingMgr->GetEngraved(player->GetGUID(), slot);
                if (runeId)
                    if (RuneTemplate const* rune = sRuneEngravingMgr->GetRune(runeId))
                        text += " |cFF00FF00[" + rune->Name + "]|r";
            }

            AddGossipItemFor(player, GOSSIP_ICON_TALK, text, SENDER_SLOT, slot);
        }

        // Debug aid (off by default): revert quest + rune-unlock progress so the
        // discovery flow can be re-tested without GM commands.
        if (sRuneEngravingMgr->DebugMenu())
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                "|cFFFF0000[Debug] Reset my runes & quests|r", SENDER_RESET, 0);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // Per-slot: the runes this player may engrave here, plus remove / back.
    void ShowRuneMenu(Player* player, Creature* creature, uint8 slot)
    {
        player->PlayerTalkClass->ClearMenus();

        if (!RuneEngravingMgr::IsValidSlot(slot))
        {
            ShowSlotMenu(player, creature);
            return;
        }

        // Level-gated slot: show the unlock notice instead of the rune list.
        uint32 minLevel = sRuneEngravingMgr->SlotMinLevel(slot);
        if (player->GetLevel() < minLevel)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|cFF808080This slot unlocks at level " + std::to_string(minLevel) + ".|r",
                SENDER_BACK, 0);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<- Back to slots", SENDER_BACK, 0);
            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        uint32 currentRuneId = sRuneEngravingMgr->GetEngraved(player->GetGUID(), slot);
        std::vector<RuneTemplate const*> runes = sRuneEngravingMgr->GetRunesForSlot(player, slot);

        for (RuneTemplate const* rune : runes)
        {
            std::string text = rune->Name;
            if (rune->RuneId == currentRuneId)
                text += " |cFF00FF00(engraved)|r";
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, text, SENDER_RUNE, rune->RuneId);
        }

        if (runes.empty())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|cFF808080No runes available for this slot.|r", SENDER_BACK, 0);

        if (currentRuneId)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                "|cFFFF0000Remove engraved rune|r", SENDER_UNENGRAVE, slot);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<- Back to slots", SENDER_BACK, 0);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }
};

void AddSC_npc_rune_engraver()
{
    new npc_rune_engraver();
}
