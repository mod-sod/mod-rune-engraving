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

#include "Config.h"
#include "Log.h"
#include "RuneEngravingMgr.h"
#include "ScriptMgr.h"

// Server lifecycle: track the enable flag on (re)config and load the rune
// catalog from the world DB once the database is ready at startup.
class rune_engraving_world : public WorldScript
{
public:
    rune_engraving_world() : WorldScript("rune_engraving_world") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sRuneEngravingMgr->SetEnabled(
            sConfigMgr->GetOption<bool>("RuneEngraving.Enable", true));
        sRuneEngravingMgr->ApplyConfig();
    }

    void OnStartup() override
    {
        if (!sRuneEngravingMgr->IsEnabled())
        {
            LOG_INFO("module", "RuneEngraving: disabled in configuration.");
            return;
        }

        sRuneEngravingMgr->LoadCatalog();
        LOG_INFO("module", "RuneEngraving: ready ({} rune(s) in catalog).",
            sRuneEngravingMgr->CatalogSize());
    }
};

void AddSC_rune_engraving_world_script()
{
    new rune_engraving_world();
}
