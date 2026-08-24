// Forward-ported from mod-playerbots HasTotemValue.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "HasTotemValue.h"

// Implemented inline by the Tortoise value header.

// bool HasTotemValue::Calculate()
// {
//     for (uint8 i = 0; i < MAX_SUMMON_SLOT; ++i)
//     {
//         if (!bot->m_SummonSlot[i])
//         {
//             continue;
//         }

//         if (Creature* OldTotem = bot->GetMap()->GetCreature(bot->m_SummonSlot[i]))
//         {
//             if (OldTotem->IsSummon())
//             {
//                 if (strstri(creature->GetName().c_str(), qualifier.c_str()))
//                     return true;
//             }
//         }
//     }

//     GuidVector units = *context->GetValue<GuidVector>("nearest totems");
//     for (ObjectGuid const guid : units)
//     {
//         Unit* unit = botAI->GetUnit(guid);
//         if (!unit)
//             continue;
//         Creature* creature = dynamic_cast<Creature*>(unit);

//         if (creature->GetOwner() != bot)
//         {
//             continue;
//         }

//         if (strstri(creature->GetName().c_str(), qualifier.c_str()))
//             return true;
//     }

//     return false;
// }
