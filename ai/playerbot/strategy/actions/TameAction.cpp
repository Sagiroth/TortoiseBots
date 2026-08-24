// Vanilla/Turtle pet-taming command.
// The host owns the authoritative tame-beast spell effect; this action selects
// a nearby tameable creature and invokes that normal spell path instead of
// constructing Pet objects or owning pet lifecycle outside the core.

#include "playerbot/playerbot.h"
#include "TameAction.h"

#include "Creature.h"
#include "DBCStructure.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "ServerFacade.h"
#include "SpellEntry.h"
#include "SpellDefines.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>

namespace
{
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

uint32 FindTameSpell(Player* bot)
{
    if (!bot)
        return 0;

    for (PlayerSpellMap::const_iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled)
            continue;

        SpellEntry const* spell = sServerFacade.LookupSpellInfo(itr->first);
        if (spell && (spell->AttributesEx2 & SPELL_ATTR_EX2_TAME_BEAST))
            return spell->Id;
    }

    return 0;
}
}

Creature* TameAction::FindTarget(std::string const& mode, std::string const& value, Player* requester)
{
    ObjectGuid selected;
    if (requester)
        selected = requester->GetSelectionGuid();
    if (selected.IsEmpty() && bot)
        selected = bot->GetSelectionGuid();

    if (mode.empty() || mode == "target")
        return selected.IsEmpty() ? nullptr : botAI->GetCreature(selected);

    std::string needle = Lower(value);
    uint32 entry = 0;
    if (mode == "id")
    {
        try
        {
            entry = static_cast<uint32>(std::stoul(value));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    std::list<ObjectGuid> nearby = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid const& guid : nearby)
    {
        Creature* creature = botAI->GetCreature(guid);
        if (!creature || !creature->GetCreatureInfo())
            continue;

        CreatureInfo const* info = creature->GetCreatureInfo();
        if (mode == "id" && info->entry != entry)
            continue;
        if (mode == "name" && Lower(creature->GetName()) != needle)
            continue;
        if (mode == "family")
        {
            CreatureFamilyEntry const* family = sCreatureFamilyStore.LookupEntry(info->beast_family);
            if (!family || Lower(family->Name[LOCALE_enUS]) != needle)
                continue;
        }
        return creature;
    }

    return nullptr;
}

bool TameAction::RenamePet(std::string const& value, Player* requester)
{
    Pet* pet = bot->GetPet();
    std::string name = value;
    if (!pet || name.size() < 2 || name.size() > MAX_CHARTER_NAME)
    {
        ai->TellPlayer(requester, "I do not have a pet to rename, or that name is invalid.");
        return false;
    }

    for (unsigned char c : name)
    {
        if (!std::isalpha(c) && c != ' ' && c != '-')
        {
            ai->TellPlayer(requester, "Pet names may contain letters, spaces, and hyphens only.");
            return false;
        }
    }

    if (sObjectMgr.IsReservedName(name))
    {
        ai->TellPlayer(requester, "That pet name is reserved.");
        return false;
    }

    pet->SetName(name);
    pet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, static_cast<uint32>(std::time(nullptr)));
    pet->SavePetToDB(PET_SAVE_AS_CURRENT);
    bot->PetSpellInitialize();
    ai->TellPlayer(requester, "Pet renamed to " + name + ".");
    return true;
}

bool TameAction::AbandonPet(Player* requester)
{
    if (!bot->GetPet())
    {
        ai->TellPlayer(requester, "I do not have a pet to abandon.");
        return false;
    }

    bot->RemovePet(PET_SAVE_AS_DELETED);
    ai->TellPlayer(requester, "Pet abandoned.");
    return true;
}

bool TameAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!requester)
        requester = bot;

    std::istringstream input(event.GetParam());
    std::string mode;
    input >> mode;
    mode = Lower(mode);

    std::string value;
    std::getline(input, value);
    if (!value.empty() && value.front() == ' ')
        value.erase(0, value.find_first_not_of(' '));

    if (mode == "abandon")
        return AbandonPet(requester);
    if (mode == "rename")
        return RenamePet(value, requester);

    if (bot->GetClass() != CLASS_HUNTER || bot->GetLevel() < 10)
    {
        ai->TellPlayer(requester, "Only level 10+ hunters can tame pets.");
        return false;
    }
    if (bot->GetPet() || bot->GetPetGuid())
    {
        ai->TellPlayer(requester, "I already have a pet; abandon it before taming another.");
        return false;
    }

    if (mode.empty())
        mode = "target";
    if (mode != "target" && mode != "name" && mode != "id" && mode != "family")
    {
        ai->TellPlayer(requester, "Usage: tame [target|name <name>|id <entry>|family <family>|rename <name>|abandon]");
        return false;
    }

    Creature* target = FindTarget(mode, value, requester);
    if (!target || !target->GetCreatureInfo() || !target->GetCreatureInfo()->isTameable())
    {
        ai->TellPlayer(requester, "I cannot find a nearby tameable beast.");
        return false;
    }

    uint32 tameSpell = FindTameSpell(bot);
    if (!tameSpell)
    {
        ai->TellPlayer(requester, "I do not know a tame-beast spell.");
        return false;
    }

    if (!ai->CastSpell(tameSpell, target))
    {
        ai->TellPlayer(requester, "I could not start taming that beast.");
        return false;
    }

    ai->TellPlayer(requester, "Taming " + std::string(target->GetName()) + ".");
    return true;
}
