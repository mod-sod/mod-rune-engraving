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

// Unit tests for the rune engine's pure eligibility logic — the slot/class
// bitmask rules that the rune_template catalog contract depends on. These run in
// the core's `unit_tests` target (registered via mod-rune-engraving.cmake) and
// touch no Player or database state.

#include "RuneEngravingMgr.h"
#include "gtest/gtest.h"

using namespace RuneRules;

// Class ids used in the class-mask tests. Local names (kClass*) avoid clashing
// with the core's CLASS_* enumerators if SharedDefines.h is transitively included.
namespace
{
    constexpr uint8 kClassWarrior = 1;
    constexpr uint8 kClassPriest  = 5;
    constexpr uint8 kClassMage    = 8;
    constexpr uint8 kClassDruid   = 11;

    constexpr uint32 kMaskMage    = 1u << (kClassMage - 1);    // 128
    constexpr uint32 kMaskWarrior = 1u << (kClassWarrior - 1); // 1
}

// --- The slot enum's numeric values are the rune_template `slot_mask` contract:
//     a content module computes slot_mask as (1 << RuneSlot). Pin them so a
//     reorder can't silently break every content module's SQL. ---
TEST(RuneSlotContract, EnumValuesAreStable)
{
    EXPECT_EQ(RUNE_SLOT_HEAD, 0);
    EXPECT_EQ(RUNE_SLOT_CHEST, 4);
    EXPECT_EQ(RUNE_SLOT_RING, 10);
    EXPECT_EQ(RUNE_SLOT_MAX, 11);
}

TEST(RuneSlotContract, SlotMaskBitsMatchDocs)
{
    EXPECT_EQ(1u << RUNE_SLOT_CHEST, 16u);  // documented Chest slot_mask
    EXPECT_EQ(1u << RUNE_SLOT_LEGS, 256u);  // documented Legs slot_mask
}

TEST(RuneSlotContract, ClassMaskMatchesAcConvention)
{
    EXPECT_EQ(kMaskMage, 128u);  // documented Mage class_mask
    EXPECT_EQ(kMaskWarrior, 1u);
}

// --- SlotIsValid ---
TEST(RuneRulesSlot, ValidRange)
{
    EXPECT_TRUE(SlotIsValid(RUNE_SLOT_HEAD));
    EXPECT_TRUE(SlotIsValid(RUNE_SLOT_RING));
    EXPECT_FALSE(SlotIsValid(RUNE_SLOT_MAX));
    EXPECT_FALSE(SlotIsValid(200));
}

// --- FitsSlot ---
TEST(RuneRulesFitsSlot, SingleSlotMask)
{
    uint32 chest = 1u << RUNE_SLOT_CHEST;
    EXPECT_TRUE(FitsSlot(chest, RUNE_SLOT_CHEST));
    EXPECT_FALSE(FitsSlot(chest, RUNE_SLOT_LEGS));
}

TEST(RuneRulesFitsSlot, MultiSlotMask)
{
    uint32 chestOrLegs = (1u << RUNE_SLOT_CHEST) | (1u << RUNE_SLOT_LEGS);
    EXPECT_TRUE(FitsSlot(chestOrLegs, RUNE_SLOT_CHEST));
    EXPECT_TRUE(FitsSlot(chestOrLegs, RUNE_SLOT_LEGS));
    EXPECT_FALSE(FitsSlot(chestOrLegs, RUNE_SLOT_HANDS));
}

TEST(RuneRulesFitsSlot, EmptyMaskFitsNothing)
{
    EXPECT_FALSE(FitsSlot(0, RUNE_SLOT_CHEST));
}

TEST(RuneRulesFitsSlot, InvalidSlotNeverFits)
{
    // Even a fully-set mask must not match an out-of-range slot.
    EXPECT_FALSE(FitsSlot(0xFFFFFFFF, RUNE_SLOT_MAX));
    EXPECT_FALSE(FitsSlot(0xFFFFFFFF, 200));
}

// --- AllowedForClass ---
TEST(RuneRulesClass, ZeroMaskAllowsAnyClass)
{
    EXPECT_TRUE(AllowedForClass(0, kClassWarrior));
    EXPECT_TRUE(AllowedForClass(0, kClassMage));
    EXPECT_TRUE(AllowedForClass(0, kClassDruid));
}

TEST(RuneRulesClass, SingleClassMask)
{
    EXPECT_TRUE(AllowedForClass(kMaskMage, kClassMage));
    EXPECT_FALSE(AllowedForClass(kMaskMage, kClassWarrior));
}

TEST(RuneRulesClass, MultiClassMask)
{
    uint32 warriorOrMage = kMaskWarrior | kMaskMage;
    EXPECT_TRUE(AllowedForClass(warriorOrMage, kClassWarrior));
    EXPECT_TRUE(AllowedForClass(warriorOrMage, kClassMage));
    EXPECT_FALSE(AllowedForClass(warriorOrMage, kClassPriest));
}

// --- SlotName (static member, links from the modules library) ---
TEST(RuneSlotName, KnownAndUnknown)
{
    EXPECT_STREQ(RuneEngravingMgr::SlotName(RUNE_SLOT_CHEST), "Chest");
    EXPECT_STREQ(RuneEngravingMgr::SlotName(RUNE_SLOT_RING), "Ring");
    EXPECT_STREQ(RuneEngravingMgr::SlotName(RUNE_SLOT_MAX), "Unknown");
}
