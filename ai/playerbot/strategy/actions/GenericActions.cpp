
#include "playerbot/playerbot.h"
#include "GenericActions.h"
#include "AttackAction.h"
#include "playerbot/PlayerbotFactory.h"
#include <algorithm>
#include <vector>
#include "../../../runtime/PetUpkeepPolicy.h"
#include "../../../runtime/HunterPetPolicy.h"
#include "../../../runtime/PetSpellRankPolicy.h"
#include "../../../runtime/WarlockPetPolicy.h"

using namespace ai;

bool MeleeAction::isUseful()
{
    return true;
}

bool UpdateStrategyDependenciesAction::Execute(Event& event)
{
    if (!strategiesToAdd.empty() || !strategiesToRemove.empty())
    {
        // One list per bot state instead of one call per strategy. Every call
        // ends in a full rebuild of that engine's triggers, and the rebuild
        // only has to happen once the whole set is in place. Additions stay
        // ahead of removals, as before.
        std::map<BotState, std::string> changesPerState;

        for (const StrategyToUpdate* strategy : strategiesToAdd)
        {
            std::string& line = changesPerState[strategy->state];
            if (!line.empty())
                line += ",";

            line += "+" + strategy->name;
        }

        for (const StrategyToUpdate* strategy : strategiesToRemove)
        {
            std::string& line = changesPerState[strategy->state];
            if (!line.empty())
                line += ",";

            line += "-" + strategy->name;
        }

        for (std::map<BotState, std::string>::const_iterator i = changesPerState.begin(); i != changesPerState.end(); ++i)
            ai->ChangeStrategy(i->second, i->first);

        return true;
    }

    return false;
}

bool UpdateStrategyDependenciesAction::isUseful()
{
    if (!strategiesToUpdate.empty())
    {
        strategiesToAdd.clear();
        strategiesToRemove.clear();
        for (const StrategyToUpdate& strategy : strategiesToUpdate)
        {
            // Ignore if the strategies required are not found
            bool requiredStrategyMissing = false;
            for (const std::string& strategyRequired : strategy.strategiesRequired)
            {
                // Check if the strategy required has any aliases
                std::vector<std::string> strategyRequiredAliases = { strategyRequired };
                if (strategyRequired.find("/") != std::string::npos)
                {
                    strategyRequiredAliases.clear();
                    std::string alias;
                    std::stringstream ss(strategyRequired);
                    while (std::getline(ss, alias, '/'))
                    {
                        strategyRequiredAliases.push_back(alias);
                    }
                }

                bool synonymFound = false;
                for (const std::string& strategyRequiredAlias : strategyRequiredAliases)
                {
                    if (ai->HasStrategy(strategyRequiredAlias, strategy.state))
                    {
                        synonymFound = true;
                        break;
                    }
                }

                if (!synonymFound)
                {
                    requiredStrategyMissing = true;
                    break;
                }
            }

            if (requiredStrategyMissing)
            {
                // Check if we need to remove the strategy
                if (ai->HasStrategy(strategy.name, strategy.state))
                {
                    strategiesToRemove.emplace_back(&strategy);
                }
            }
            else
            {
                // Check if we need to add the strategy
                if (!ai->HasStrategy(strategy.name, strategy.state))
                {
                    strategiesToAdd.emplace_back(&strategy);
                }
            }
        }
    }

    return !strategiesToAdd.empty() || !strategiesToRemove.empty();
}

bool InitializePetAction::Execute(Event& event)
{
    (void)event;
    Pet* pet = bot->GetPet();
    // A live pet is NEVER re-created here: factory.InitPet() ends in
    // SetDeathState(JUST_DIED) ("force dismiss to fix missing flags"), so the
    // InitPet path below runs only when no pet is out (a dismissed row still
    // in character_pet is re-called by InitPet without harming anything live).
    bool upkeepAllowed =
        (ai->HasCheat(BotCheatMask::item) && sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId())) ||
        (!sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()) && sPlayerbotAIConfig.autoLearnTrainerSpells);
    if (pet && upkeepAllowed && !ai->HasRealPlayerMaster() &&
        (bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_WARLOCK))
    {
        // Rank upkeep on a LIVE pet: teach only. The same shared-table walks
        // drive the missing-rank check, so this teaches exactly what
        // isUseful named: top rank per line + singles, skipping ids with no
        // SpellEntry (unteachable) and pinning autocast.
        if (bot->GetClass() == CLASS_HUNTER)
        {
            CreatureInfo const* petInfo = sObjectMgr.GetCreatureTemplate(pet->GetEntry());
            uint32 beastFamily = petInfo ? petInfo->beast_family : 0;
            TortoiseBots::ForEachHunterWantedSpell(beastFamily, pet->GetLevel(),
                [pet](TortoiseBots::PetWantedSpell wanted)
            {
                if (!sSpellMgr.GetSpellEntry(wanted.spellId))
                    return;
                if (!pet->HasSpell(wanted.spellId))
                    pet->LearnSpell(wanted.spellId);
                if (!wanted.passive && pet->HasSpell(wanted.spellId) && !IsPassiveSpell(wanted.spellId))
                    pet->ToggleAutocast(wanted.spellId, TortoiseBots::ShouldPetSpellAutocastDefault(wanted.spellId));
            });
        }
        else
        {
            TortoiseBots::ForEachWarlockWantedSpell(pet->GetEntry(), pet->GetLevel(),
                [pet](TortoiseBots::PetWantedSpell wanted)
            {
                if (!sSpellMgr.GetSpellEntry(wanted.spellId))
                    return;
                if (!pet->HasSpell(wanted.spellId))
                    pet->LearnSpell(wanted.spellId);
                if (pet->HasSpell(wanted.spellId) && !IsPassiveSpell(wanted.spellId))
                    pet->ToggleAutocast(wanted.spellId, TortoiseBots::ShouldPetSpellAutocastDefault(wanted.spellId));
            });
        }
        return true;
    }
    if (pet)
        return true;
    // Creation path: no live pet. This is the only path that may call InitPet.
    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
    factory.InitPet();
    factory.InitPetSpells();
    return true;
}

bool InitializePetAction::isUseful()
{
    // A hunter has no pet before the Tame Beast trainer tier (spell 1515,
    // baseLevel 10 in tw_world.spell_template). Gate before the character_pet
    // lookup below so a level-1 hunter neither creates a pet nor re-queries
    // the character DB every time the strategy's "often" trigger fires.
    if (bot->GetClass() == CLASS_HUNTER && bot->GetLevel() < TortoiseBots::HUNTER_PET_MIN_LEVEL)
        return false;

    // Only for random bots with item cheats enabled
    if ((ai->HasCheat(BotCheatMask::item) && sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId())) ||
        // Or if alt bot and autoLearnTrainerSpells is true
        (!sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()) && sPlayerbotAIConfig.autoLearnTrainerSpells))
    {
        if (bot->GetClass() == CLASS_HUNTER)
        {
            bool hasTamedPet = bot->GetPet();
            if (!hasTamedPet)
            {
                std::unique_ptr<QueryResult> queryResult(CharacterDatabase.PQuery("SELECT id, entry, owner "
                                                                                    "FROM character_pet WHERE owner = '%u' AND (slot = '%u' OR slot > '%u') ",
                                                                                    bot->GetGUIDLow(), PET_SAVE_AS_CURRENT, PET_SAVE_LAST_STABLE_SLOT));

                if (queryResult)
                {
                    Field* fields = queryResult->Fetch();
                    const uint32 entry = fields[1].GetUInt32();
                    hasTamedPet = sObjectMgr.GetCreatureTemplate(entry);
                }
            }
            if (hasTamedPet)
            {
                // Rank-upkeep check, not creation: true only when the live pet
                // misses a spell the teach path can actually add. Same shared
                // table as Execute/factory (top rank per ladder + singles),
                // and ids with no SpellEntry are skipped, so an unteachable
                // row (custom spell missing from this core's DBC) can never
                // pin this true - the old per-rank scan stayed true forever
                // because AddSpell drops lower ranks. Owned/hired pets never
                // upkeep. Cheap ("often" = every 5 ticks): HasSpell map hits
                // only while everything is known; the SpellEntry lookup runs
                // only for the few wanted ids, not per known spell.
                Pet* pet = bot->GetPet();
                if (!pet || ai->HasRealPlayerMaster())
                    return false;
                CreatureInfo const* petInfo = sObjectMgr.GetCreatureTemplate(pet->GetEntry());
                uint32 beastFamily = petInfo ? petInfo->beast_family : 0;
                bool missing = false;
                TortoiseBots::ForEachHunterWantedSpell(beastFamily, pet->GetLevel(),
                    [pet, &missing](TortoiseBots::PetWantedSpell wanted)
                {
                    if (missing)
                        return;
                    if (!sSpellMgr.GetSpellEntry(wanted.spellId))
                        return;
                    if (!pet->HasSpell(wanted.spellId))
                        missing = true;
                });
                return missing;
            }
            return true;
        }
        else if (bot->GetClass() == CLASS_WARLOCK)
        {
            // Same shared-table rule as the hunter path: top rank per demon
            // line + singles, SpellEntry-less ids skipped. Ding upgrades
            // ranks without resummoning; steady state is map hits only.
            Pet* pet = bot->GetPet();
            if (!pet || ai->HasRealPlayerMaster())
                return false;
            bool missing = false;
            TortoiseBots::ForEachWarlockWantedSpell(pet->GetEntry(), pet->GetLevel(),
                [pet, &missing](TortoiseBots::PetWantedSpell wanted)
            {
                if (missing)
                    return;
                if (!sSpellMgr.GetSpellEntry(wanted.spellId))
                    return;
                if (!pet->HasSpell(wanted.spellId))
                    missing = true;
            });
            return missing;
        }
    }

    return false;
}

bool SetPetAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string command = event.GetParam();

    // Extract the command and the parameters
    std::string parameter;
    size_t spacePos = command.find_first_of(" ");
    if (spacePos != std::string::npos)
    {
        parameter = command.substr(spacePos + 1);
        command = command.substr(0, spacePos);
    }

    Pet* pet = bot->GetPet();
    if (pet)
    {
        if (command == "autocast")
        {
            const std::string& spellName = parameter;
            if (!spellName.empty())
            {
                const uint32 spellId = AI_VALUE2(uint32, "spell id", spellName);
                if (pet->HasSpell(spellId) && IsAutocastable(spellId))
                {
                    auto IsAutocastActive = [&pet, &spellId]() -> bool
                    {
                        for (AutoSpellList::iterator i = pet->m_autospells.begin(); i != pet->m_autospells.end(); ++i)
                        {
                            if (*i == spellId)
                            {
                                return true;
                            }
                        }

                        return false;
                    };

                    const bool autocastActive = IsAutocastActive();
                    pet->ToggleAutocast(spellId, !autocastActive);

                    std::ostringstream out;
                    out << (autocastActive ? "Disabling" : "Enabling") << " pet autocast for ";
                    out << ChatHelper::formatSpell(sServerFacade.LookupSpellInfo(spellId));
                    ai->TellPlayer(GetMaster(), out);

                    return true;
                }
                else
                {
                    ai->TellPlayer(requester, "I can't set to autocast that spell.");
                }
            }
            else
            {
                ai->TellPlayer(requester, "Please specify a pet spell to set the autocast.");
            }
        }
        else if (command == "aggressive")
        {
            // Send pet action packet
            const ObjectGuid& petGuid = pet->getObjectGuid();
            const uint8 flag = ACT_REACTION;
            const uint32 spellId = REACT_AGGRESSIVE;
            const uint32 data = (flag << 24) | spellId;

            WorldPacket packet(CMSG_PET_ACTION);
            packet << petGuid;
            packet << data;
            packet << uint64(0);
            bot->GetSession()->HandlePetAction(packet);
            bot->PetSpellInitialize();

            ai->TellPlayer(requester, "Setting pet to aggressive mode");
            return true;
        }
        else if (command == "defensive")
        {
            // Send pet action packet
            const ObjectGuid& petGuid = pet->getObjectGuid();
            const uint8 flag = ACT_REACTION;
            const uint32 spellId = REACT_DEFENSIVE;
            const uint32 data = (flag << 24) | spellId;

            WorldPacket packet(CMSG_PET_ACTION);
            packet << petGuid;
            packet << data;
            packet << uint64(0);
            bot->GetSession()->HandlePetAction(packet);
            bot->PetSpellInitialize();

            ai->TellPlayer(requester, "Setting pet to defensive mode");
            return true;
        }
        else if (command == "passive")
        {
            // Send pet action packet
            const ObjectGuid& petGuid = pet->getObjectGuid();
            const uint8 flag = ACT_REACTION;
            const uint32 spellId = REACT_PASSIVE;
            const uint32 data = (flag << 24) | spellId;

            WorldPacket packet(CMSG_PET_ACTION);
            packet << petGuid;
            packet << data;
            packet << uint64(0);
            bot->GetSession()->HandlePetAction(packet);
            bot->PetSpellInitialize();

            ai->TellPlayer(requester, "Setting pet to passive mode");
            return true;
        }
        else if (command == "follow")
        {
            // Send pet action packet
            const ObjectGuid& petGuid = pet->getObjectGuid();
            const ObjectGuid& targetGuid = ObjectGuid();
            const uint8 flag = ACT_COMMAND;
            const uint32 spellId = COMMAND_FOLLOW;
            const uint32 command = (flag << 24) | spellId;

            WorldPacket data(CMSG_PET_ACTION);
            data << petGuid;
            data << command;
            data << targetGuid;
            bot->GetSession()->HandlePetAction(data);

            ai->TellPlayer(requester, "Setting pet to follow me");
            return true;
        }
        else if (command == "stay")
        {
            // Send pet action packet
            const ObjectGuid& petGuid = pet->getObjectGuid();
            const ObjectGuid& targetGuid = ObjectGuid();
            const uint8 flag = ACT_COMMAND;
            const uint32 spellId = COMMAND_STAY;
            const uint32 command = (flag << 24) | spellId;

            WorldPacket data(CMSG_PET_ACTION);
            data << petGuid;
            data << command;
            data << targetGuid;
            bot->GetSession()->HandlePetAction(data);

            ai->TellPlayer(requester, "Setting pet to stay in place");
            return true;
        }
        else if (command == "attack")
        {
            if (requester->GetSelectionGuid())
            {
                constexpr uint32 PET_IMP = 416;
                constexpr uint32 PHASE_SHIFT = 4511;
                if (bot->GetClass() == CLASS_WARLOCK &&
                    pet->HasReactState(REACT_PASSIVE) &&
                    pet->GetEntry() == PET_IMP && pet->HasAura(PHASE_SHIFT))
                {
                    ai->TellPlayer(requester, "Pet has Phase Shift active, cannot attack");
                    return false;
                }

                // Send pet action packet
                const ObjectGuid& petGuid = pet->getObjectGuid();
                const ObjectGuid targetGuid = requester->GetSelectionGuid();
                const uint8 flag = ACT_COMMAND;
                const uint32 spellId = COMMAND_ATTACK;
                const uint32 command = (flag << 24) | spellId;

                WorldPacket data(CMSG_PET_ACTION);
                data << petGuid;
                data << command;
                data << targetGuid;
                bot->GetSession()->HandlePetAction(data);

                ai->TellPlayer(requester, "Sending pet to attack");
                return true;
            }
            else
            {
                ai->TellPlayer(requester, "Please select a target to attack");
            }
        }
        else if (command == "dismiss")
        {
            if (pet->getPetType() == HUNTER_PET)
            {
                if (ai->DoSpecificAction("dismiss pet", event, true))
                {
                    ai->ChangeStrategy("-pet", BotState::BOT_STATE_COMBAT);
                    ai->ChangeStrategy("-pet", BotState::BOT_STATE_NON_COMBAT);
                    ai->TellPlayer(requester, "Dismissing pet");
                    return true;
                }
                else
                {
                    ai->TellPlayer(requester, "I can't dismiss my pet");
                }
            }
            else
            {
                // Send pet action packet
                const ObjectGuid& petGuid = pet->getObjectGuid();
                const ObjectGuid& targetGuid = ObjectGuid();
                const uint8 flag = ACT_COMMAND;
                const uint32 spellId = COMMAND_DISMISS;
                const uint32 command = (flag << 24) | spellId;

                WorldPacket data(CMSG_PET_ACTION);
                data << petGuid;
                data << command;
                data << targetGuid;
                bot->GetSession()->HandlePetAction(data);

                ai->ChangeStrategy("+pet", BotState::BOT_STATE_COMBAT);
                ai->ChangeStrategy("+pet", BotState::BOT_STATE_NON_COMBAT);
                ai->ChangeStrategy("-pet", BotState::BOT_STATE_COMBAT);
                ai->ChangeStrategy("-pet", BotState::BOT_STATE_NON_COMBAT);
                ai->TellPlayer(requester, "Dismissing pet");

                return true;
            }
        }
        else if (command == "abandon")
        {
            if (bot->GetClass() == CLASS_HUNTER)
            {
                //std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_PET_ABANDON, 8));
                //*packet << pet->getObjectGuid();
                //bot->GetSession()->QueuePacket(packet.release());

                // Send pet action packet
                const ObjectGuid& petGuid = pet->getObjectGuid();

                WorldPacket packet(CMSG_PET_ABANDON);
                packet << petGuid;

                bot->GetSession()->HandlePetAbandon(packet);

                return true;
            }
            else
            {
                ai->TellPlayer(requester, "Please specify a pet command (Like autocast).");
            }
        }
        else
        {
            ai->TellPlayer(requester, "Please specify a pet command (Like autocast).");
        }
    }
    else if (command == "call")
    {
        if (bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_WARLOCK)
        {
            ai->ChangeStrategy("+pet", BotState::BOT_STATE_COMBAT);
            ai->ChangeStrategy("+pet", BotState::BOT_STATE_NON_COMBAT);
            ai->TellPlayer(requester, "Calling my pet");
            return true;
        }
        else
        {
            ai->TellPlayer(requester, "I can't call any pets");
        }
    }
    else
    {
        ai->TellPlayer(requester, "I don't have any pets");
    }

    return false;
}

bool TogglePetSpellAutoCastAction::isPossible()
{
    // Pool bots only: a player who owns or hired the bot sets its pet's
    // autocast and stance themselves, and this must not override them.
    return bot->GetPet() != nullptr && !ai->HasRealPlayerMaster();
}

// Autonomous autocast sweep (E01): ported from mod-playerbots
// TogglePetSpellAutoCastAction, adapted to the 1.12 APIs. The donor reads
// SpellInfo::IsAutocastable plus a rank-prune of m_autospells; here passive
// spells are skipped via IsPassiveSpell (the cmangos-compat-shim documents
// that 1.12 has no separate NO_AUTOCAST_AI bit, and Pet::ToggleAutocast
// refuses passives the same way) and stale autocast entries are pruned via
// Pet::HasSpell, which already excludes PETSPELL_REMOVED. Silence (no
// toggle, pet fresh from InitPet) returns true so the per-tick "has pet"
// node stays cheap instead of FAILED-logged.
bool TogglePetSpellAutoCastAction::Execute(Event& /*event*/)
{
    Pet* pet = bot->GetPet();
    if (!pet)
        return false;

    std::vector<uint32> stale;
    for (uint32 autocast : pet->m_autospells)
        if (!pet->HasSpell(autocast))
            stale.push_back(autocast);
    for (uint32 spellId : stale)
    {
        auto it = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spellId);
        if (it != pet->m_autospells.end())
            pet->m_autospells.erase(it);
    }

    for (PetSpellMap::const_iterator itr = pet->m_petSpells.begin(); itr != pet->m_petSpells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
            continue;

        uint32 spellId = itr->first;
        if (IsPassiveSpell(spellId))
            continue;

        bool active = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spellId) != pet->m_autospells.end();
        TortoiseBots::PetAutocastDecision decision =
            TortoiseBots::DecidePetAutocast(active, TortoiseBots::IsDisabledPetAutocast(spellId));
        if (decision == TortoiseBots::PetAutocastDecision::LeaveAlone)
            continue;

        pet->ToggleAutocast(spellId, decision == TortoiseBots::PetAutocastDecision::Enable);
    }

    return true;
}

bool SetPetStanceAction::isPossible()
{
    // Pool bots only: a player who owns or hired the bot sets its pet's
    // autocast and stance themselves, and this must not override them.
    return bot->GetPet() != nullptr && !ai->HasRealPlayerMaster();
}

// Autonomous stance pin (E01): ported from mod-playerbots SetPetStanceAction
// with the donor's DefaultPetStance config collapsed to our invariant —
// PlayerbotFactory::InitPet seeds REACT_DEFENSIVE and
// AttackAction::CanPetAttack refuses REACT_PASSIVE, so defensive is the only
// stance the autonomous path may set; explicit player orders still go
// through SetPetAction (pet aggressive/defensive/passive packets). Covers
// the main pet plus every guardian (totems skipped); returns false with no
// pet so the "new pet" node stays silent instead of no-op OK.
bool SetPetStanceAction::Execute(Event& /*event*/)
{
    Pet* pet = bot->GetPet();
    if (!pet)
        return false;

    pet->SetReactState(REACT_DEFENSIVE);

    struct ApplyStance
    {
        void operator()(Unit* unit) const
        {
            Creature* creature = dynamic_cast<Creature*>(unit);
            if (!creature || creature->IsTotem())
                return;
            creature->SetReactState(REACT_DEFENSIVE);
        }
    };
    bot->CallForAllControlledUnits(ApplyStance(), CONTROLLED_GUARDIANS);

    return true;
}


bool PetAttackAction::isUseful()
{
    Pet* pet = bot->GetPet();
    Unit* target = GetTarget();
    return AttackAction::CanPetAttack(ai, pet, target);
}

bool PetAttackAction::Execute(Event& event)
{
    Pet* pet = bot->GetPet();
    Unit* target = GetTarget();
    if (!pet || !target)
        return false;

    if (pet->GetReactState() == REACT_PASSIVE && !ai->GetMaster())
    {
        pet->SetReactState(REACT_DEFENSIVE);
    }

    if (!AttackAction::CanPetAttack(ai, pet, target))
        return false;

    constexpr uint32 PET_IMP = 416;
    constexpr uint32 PHASE_SHIFT = 4511;
    if (bot->GetClass() == CLASS_WARLOCK &&
        pet->GetEntry() == PET_IMP && pet->HasAura(PHASE_SHIFT))
    {
        pet->RemoveAurasDueToSpell(PHASE_SHIFT);
    }

    const ObjectGuid& petGuid = pet->getObjectGuid();
    const ObjectGuid targetGuid = target->GetObjectGuid();
    const uint8 flag = ACT_COMMAND;
    const uint32 spellId = COMMAND_ATTACK;
    const uint32 command = (flag << 24) | spellId;

    WorldPacket data(CMSG_PET_ACTION);
    data << petGuid;
    data << command;
    data << targetGuid;
    bot->GetSession()->HandlePetAction(data);
    return true;
}
