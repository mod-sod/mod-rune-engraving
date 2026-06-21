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
#include "ChatCommand.h"
#include "Player.h"
#include "RuneEngravingMgr.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

namespace
{
    // The dedicated Rune Engraver NPC (rune_engraving_schema.sql). `.rune summon`
    // spawns a TEMPORARY one at the caller: not saved to the DB, auto-despawns
    // after RUNE_ENGRAVER_SUMMON_MS, and gone on a server restart.
    constexpr uint32 RUNE_ENGRAVER_NPC       = 700000;
    constexpr uint32 RUNE_ENGRAVER_SUMMON_MS = 5 * 60 * 1000; // 5 minutes
}

// .rune commands — a headless path to drive the engine without the gossip NPC,
// useful for testing the engrave -> grant -> login-reapply loop. `.rune summon`
// conjures a temporary engraver gossip NPC for the caller.
class cs_rune : public CommandScript
{
public:
    cs_rune() : CommandScript("cs_rune") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable runeTable =
        {
            { "summon",  HandleSummon,  SEC_PLAYER,        Console::No  },
            { "list",    HandleList,    SEC_GAMEMASTER,    Console::No  },
            { "slots",   HandleSlots,   SEC_GAMEMASTER,    Console::No  },
            { "engrave", HandleEngrave, SEC_GAMEMASTER,    Console::No  },
            { "clear",   HandleClear,   SEC_GAMEMASTER,    Console::No  },
            { "unlock",  HandleUnlock,  SEC_GAMEMASTER,    Console::No  },
            { "lock",    HandleLock,    SEC_GAMEMASTER,    Console::No  },
            { "unlocks", HandleUnlocks, SEC_GAMEMASTER,    Console::No  },
            { "reload",  HandleReload,  SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable root = { { "rune", runeTable } };
        return root;
    }

    // Summons a temporary Rune Engraver at the caller. Ephemeral: not saved to the
    // DB, auto-despawns after a few minutes, and gone on a server restart. Any
    // player may summon their own; the gossip still enforces the engraving rules.
    static bool HandleSummon(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return false;
        }

        player->SummonCreature(RUNE_ENGRAVER_NPC, *player, TEMPSUMMON_TIMED_DESPAWN,
            RUNE_ENGRAVER_SUMMON_MS);
        handler->SendSysMessage("A Rune Engraver appears for a short while.");
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        handler->PSendSysMessage("Engraved runes for {}:", player->GetName());
        bool any = false;
        for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
        {
            uint32 runeId = sRuneEngravingMgr->GetEngraved(player->GetGUID(), slot);
            if (!runeId)
                continue;
            any = true;
            RuneTemplate const* rune = sRuneEngravingMgr->GetRune(runeId);
            handler->PSendSysMessage("  {} : {} (rune {}, spell {})",
                RuneEngravingMgr::SlotName(slot),
                rune ? rune->Name.c_str() : "?",
                runeId, rune ? rune->SpellId : 0);
        }
        if (!any)
            handler->SendSysMessage("  (no runes engraved)");
        return true;
    }

    static bool HandleSlots(ChatHandler* handler)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        uint8 level = player->GetLevel();
        handler->PSendSysMessage("Engraving slots for {} (level {}):",
            player->GetName(), uint32(level));
        for (uint8 slot = 0; slot < RUNE_SLOT_MAX; ++slot)
        {
            uint32 minLevel = sRuneEngravingMgr->SlotMinLevel(slot);
            handler->PSendSysMessage("  {} : unlocks at {} [{}]",
                RuneEngravingMgr::SlotName(slot), minLevel,
                level >= minLevel ? "open" : "locked");
        }
        return true;
    }

    static bool HandleEngrave(ChatHandler* handler, uint8 slot, uint32 runeId)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        EngraveResult result = sRuneEngravingMgr->Engrave(player, slot, runeId);
        if (result == EngraveResult::Success)
        {
            RuneTemplate const* rune = sRuneEngravingMgr->GetRune(runeId);
            handler->PSendSysMessage("Engraved {} in slot {} ({}).",
                rune ? rune->Name.c_str() : "rune", uint32(slot),
                RuneEngravingMgr::SlotName(slot));
            return true;
        }

        char const* reason = "rune doesn't exist, fit the slot, or match the class";
        switch (result)
        {
            case EngraveResult::PrereqMissing:   reason = "the character hasn't learned Engraving"; break;
            case EngraveResult::SlotLevelTooLow: reason = "the slot isn't unlocked at this level"; break;
            case EngraveResult::DuplicateRune:   reason = "that rune is already engraved in another slot"; break;
            case EngraveResult::Locked:          reason = "the character hasn't unlocked that rune"; break;
            case EngraveResult::WrongClass:      reason = "the rune isn't for that class"; break;
            case EngraveResult::WrongSlot:       reason = "the rune doesn't fit that slot"; break;
            case EngraveResult::UnknownRune:     reason = "no such rune (or it's disabled)"; break;
            default: break;
        }
        handler->PSendSysMessage("Could not engrave rune {} in slot {}: {}.",
            runeId, uint32(slot), reason);
        return false;
    }

    static bool HandleClear(ChatHandler* handler, uint8 slot)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        if (sRuneEngravingMgr->RemoveRune(player, slot))
        {
            handler->PSendSysMessage("Cleared slot {} ({}).",
                uint32(slot), RuneEngravingMgr::SlotName(slot));
            return true;
        }

        handler->PSendSysMessage("Nothing engraved in slot {}.", uint32(slot));
        return false;
    }

    static bool HandleUnlock(ChatHandler* handler, uint32 runeId)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        if (sRuneEngravingMgr->UnlockRune(player, runeId))
            handler->PSendSysMessage("Unlocked rune {} for {}.", runeId, player->GetName());
        else
            handler->PSendSysMessage("Rune {} was already unlocked for {}.", runeId, player->GetName());
        return true;
    }

    static bool HandleLock(ChatHandler* handler, uint32 runeId)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        if (sRuneEngravingMgr->LockRune(player, runeId))
            handler->PSendSysMessage("Locked rune {} for {}.", runeId, player->GetName());
        else
            handler->PSendSysMessage("Rune {} was not unlocked for {}.", runeId, player->GetName());
        return true;
    }

    static bool HandleUnlocks(ChatHandler* handler)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage("No target player.");
            return false;
        }

        std::vector<uint32> ids = sRuneEngravingMgr->GetUnlockedRunes(player->GetGUID());
        if (ids.empty())
        {
            handler->PSendSysMessage("{} has no unlocked runes.", player->GetName());
            return true;
        }

        handler->PSendSysMessage("Unlocked runes for {}:", player->GetName());
        for (uint32 id : ids)
        {
            RuneTemplate const* rune = sRuneEngravingMgr->GetRune(id);
            handler->PSendSysMessage("  {} : {}", id, rune ? rune->Name.c_str() : "?");
        }
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        sRuneEngravingMgr->LoadCatalog();
        handler->PSendSysMessage("RuneEngraving: catalog reloaded ({} rune(s)).",
            sRuneEngravingMgr->CatalogSize());
        return true;
    }
};

void AddSC_cs_rune()
{
    new cs_rune();
}
