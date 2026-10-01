
#include "playerbot/playerbot.h"
#include "AutoLearnSpellAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotFactory.h"
#include "Objects/Item.h"
#include <Mail/Mail.h>
#include <map>
#include <vector>

using namespace ai;

bool AutoLearnSpellAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string param = event.GetParam();

    std::ostringstream out;

    LearnSpells(&out);

    if (!out.str().empty())
    {
        const std::string& temp = out.str();
        out.seekp(0);
        out << temp;
        out.seekp(-2, out.cur);
        out << ".";

        std::map<std::string, std::string> args;
        args["%spells"] = out.str();
        ai->TellPlayer(requester, BOT_TEXT2("auto_learn_spell", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }

    return true;
}

// Issue #189: free spell learning is random-pool only. Pool bots include both
// roaming pool bots and hired companions (Hire() keeps record->random true);
// the player's own alts (same-account characters via .bot add) and free-alts
// (non-pool accounts) are not pool bots and are excluded. Owned bots keep
// the paid trainer-with-gold path untouched (TrainerAction).
static bool IsFreeLearnBot(Player* bot)
{
    return bot && sRandomBotFacade.IsRandomBot(bot);
}

void AutoLearnSpellAction::LearnSpells(std::ostringstream* out)
{
    BroadcastHelper::BroadcastLevelup(ai, bot);

    if (!ai->HasActivePlayerMaster())
    {
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
        if (travelTarget)
        {
            sTravelMgr.SetNullTravelTarget(travelTarget);
            travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            travelTarget->SetExpireIn(1000);
        }
    }

    // Free learning is random-pool only; the paid trainer path is untouched.
    bool const freeLearn = IsFreeLearnBot(bot);

    // Turtle mount (quest 40302 equivalent): pool bots that reach the gate
    // earn the Swift Riding Turtle as if they did the quest. The free-learn
    // gate is pool identity only (no master check): a hire's AI has a live
    // master, so the old HasRealPlayerMaster()/IsOwnedBot() exclusion denied
    // companions hired below the gate when they later dinged 18.
    if (freeLearn)
    {
        PlayerbotFactory mounts(bot, bot->GetLevel());
        mounts.InitTurtleMount();

        // Organic level-up mounts (AiPlayerbot.LevelUpMounts): a bot that
        // dings 40 or 60 earns the same mount set the seed/hire path grants
        // (slow mount at 40, epic at 60); nothing else teaches them while a
        // bot levels from 1. Riding skill follows from InitSkills below and
        // InitMounts is idempotent per tier, so seeding and a repeated
        // level-up packet are no-ops.
        if (sPlayerbotAIConfig.levelUpMounts && (bot->GetLevel() == 40 || bot->GetLevel() == 60))
            mounts.InitMounts();
    }

    if (freeLearn && sPlayerbotAIConfig.autoLearnQuestSpells)
        LearnQuestSpells(out);

    if (freeLearn && sPlayerbotAIConfig.autoLearnTrainerSpells)
        LearnTrainerSpells(out);

    if (freeLearn && sPlayerbotAIConfig.autoLearnDroppedSpells)
        LearnDroppedSpells(out);

    if (freeLearn && !ai->HasActivePlayerMaster()) //Hunter spells for pets.
    {
        if (bot->GetClass() == CLASS_HUNTER && bot->GetLevel() >= 10)
        {
            bot->LearnSpell(5149, false); // Beast training
            bot->LearnSpell(883, false); // Call pet
            bot->LearnSpell(982, false); // Revive pet
            bot->LearnSpell(6991, false); // Feed pet
            bot->LearnSpell(1515, false); // Tame beast
        }
    }

    if (freeLearn)
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitSkills();
    }
}

void AutoLearnSpellAction::LearnTrainerSpells(std::ostringstream* out)
{
    bot->LearnDefaultSpells();

    // Per-class trainer cache, built once per server run. The previous
    // per-bot full-creature scan is not shippable for 500 logins.
    struct CachedTrainer
    {
        uint8 trainerType;
        uint8 trainerClass;
        TrainerSpellData const* spells;
    };
    static std::map<uint8, std::vector<CachedTrainer>> s_classTrainers;
    static std::vector<CachedTrainer> s_tradeskillTrainers;
    static bool s_trainersCached = false;
    if (!s_trainersCached)
    {
        for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
        {
            CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
            if (!co)
                continue;
            if (co->trainer_type != TRAINER_TYPE_CLASS &&
                co->trainer_type != TRAINER_TYPE_TRADESKILLS &&
                co->trainer_type != TRAINER_TYPE_PETS)
                continue;
            uint32 trainerId = co->trainer_id;
            if (!trainerId)
                trainerId = co->entry;
            TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(trainerId);
            if (!trainer_spells)
                trainer_spells = sObjectMgr.GetNpcTrainerSpells(trainerId);
            if (!trainer_spells)
                continue;
            CachedTrainer entry{ co->trainer_type, co->trainer_class, trainer_spells };
            if (co->trainer_type == TRAINER_TYPE_TRADESKILLS)
                s_tradeskillTrainers.push_back(entry);
            else
                s_classTrainers[co->trainer_class].push_back(entry);
        }
        s_trainersCached = true;
    }

    std::vector<CachedTrainer const*> trainers;
    auto classIt = s_classTrainers.find(bot->GetClass());
    if (classIt != s_classTrainers.end())
        for (auto const& e : classIt->second)
            trainers.push_back(&e);
    for (auto const& e : s_tradeskillTrainers)
        trainers.push_back(&e);

    for (CachedTrainer const* entry : trainers)
    {
        if (entry->trainerType == TRAINER_TYPE_PETS && bot->GetClass() == CLASS_HUNTER)
            continue;

        if ((entry->trainerType == TRAINER_TYPE_CLASS || entry->trainerType == TRAINER_TYPE_PETS) && entry->trainerClass != bot->GetClass())
            continue;

        uint8 trainerType = entry->trainerType;
        TrainerSpellData const* trainer_spells = entry->spells;

        for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin(); itr != trainer_spells->spellList.end(); ++itr)
        {
            TrainerSpell const* tSpell = &itr->second;

            if (!tSpell)
                continue;

            uint32 reqLevel = 0;

            reqLevel = tSpell->reqLevel;
            TrainerSpellState state = bot->GetTrainerSpellState(tSpell);
            if (state != TRAINER_SPELL_GREEN)
                continue;

            SpellEntry const* spell = sServerFacade.LookupSpellInfo(tSpell->spell);
            if (trainerType == TRAINER_TYPE_TRADESKILLS && bot->GetClass() != CLASS_HUNTER && spell)
            {
                bool teachesPetSpell = spell->Id == 6666 || spell->Id == 6667;
                for (int effect = 0; effect < 3 && !teachesPetSpell; ++effect)
                {
                    if (spell->Effect[effect] == SPELL_EFFECT_LEARN_PET_SPELL ||
                        (spell->Effect[effect] == SPELL_EFFECT_LEARN_SPELL &&
                         (spell->EffectTriggerSpell[effect] == 6666 || spell->EffectTriggerSpell[effect] == 6667)))
                    {
                        teachesPetSpell = true;
                    }
                }

                if (teachesPetSpell)
                    continue;
            }

            if (trainerType == TRAINER_TYPE_TRADESKILLS && spell)
            {
                std::string SpellName = spell->SpellName[0];
                if (spell->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_SKILL_STEP)
                {
                    uint32 skill = spell->EffectMiscValue[EFFECT_INDEX_1];

                    if (skill)
                    {
                        SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
                        if (pSkill)
                        {
                            if (SpellName.find("Apprentice") != std::string::npos && pSkill->categoryId == SKILL_CATEGORY_PROFESSION || pSkill->categoryId == SKILL_CATEGORY_SECONDARY)
                                continue;
                        }
                    }
                }
            }
            LearnSpellFromSpell(tSpell->spell, out);
        }
    }
}

void AutoLearnSpellAction::LearnQuestSpells(std::ostringstream* out)
{
    ObjectMgr::QuestMap const& questTemplates = sObjectMgr.GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        uint32 questId = i->first;
        Quest const* quest = i->second.get();

        if (!quest->GetRequiredClasses() || quest->IsRepeatable())
            continue;

        if (!bot->SatisfyQuestClass(quest, false) ||
            quest->GetMinLevel() > bot->GetLevel() ||
            !bot->SatisfyQuestRace(quest, false))
            continue;

        if (quest->GetRewSpellCast() > 0 &&
            quest->GetRewSpellCast() != 12510) // Prevents mages from learning the Teleport to Azushara Tower spell.
        {
            if (LearnSpellFromSpell(quest->GetRewSpellCast(), out))
            {
                GetClassQuestItem(quest, out);
            }
            // Shaman Call of Air Quest casts Swift Wind on player and rewards Air Totem, Swift Wind is not to be learned however it is a one time cast.
            else if (quest->GetRewSpellCast() == 8385)
            {
                bool hasAirTotem = false;
                hasAirTotem = bot->HasItemCount(5178, 1, true);
                if (!hasAirTotem)
                {
                    GetClassQuestItem(quest, out);
                }
            }
        }
        else if (quest->GetRewSpell() > 0)
        {
            if (IsTeachingSpellListedAsSpell(quest->GetRewSpell()))
            {
                if (LearnSpellFromSpell(quest->GetRewSpell(), out))
                {
                    GetClassQuestItem(quest, out);
                }
            }
            else
            {
                if (LearnSpell(quest->GetRewSpell(), out))
                {
                    GetClassQuestItem(quest, out);
                }
            }
        }
    }
}

void AutoLearnSpellAction::LearnDroppedSpells(std::ostringstream* out)
{   //       Class       Level      // Spells
    std::map<uint8, std::map<uint8, std::vector<uint32>>> spellList;
    spellList[CLASS_WARRIOR][60] = { 25289, 25288, 25286 };
    spellList[CLASS_PALADIN][60] = { 25291, 25290, 25292 };
    spellList[CLASS_HUNTER][60] = { 19801, 25296, 25294, 25295 };
    spellList[CLASS_ROGUE][60] = { 25300, 25347, 25302, 31016 };
    spellList[CLASS_PRIEST][48] = { 21562 };
    // Spell 27683 Prayer of Shadow Protection https://www.wowhead.com/classic/spell=27683/prayer-of-shadow-protection book requires level 60, spell requires 56.
    // Went with the book requirement.
    spellList[CLASS_PRIEST][60] = { 25314, 25315, 25316,21564,27683 };
    spellList[CLASS_SHAMAN][60] = { 29228, 25359, 25357, 25361 };
    spellList[CLASS_MAGE][56] = { 23028 };
    // Spell 25345 Arcane Missiles https://www.wowhead.com/classic/spell=25345/arcane-missiles book requires level 60, spell requires 56.
    // Went with the book requirement.
    spellList[CLASS_MAGE][60] = { 28612, 28609, 25345, 25306, 25304, 28271 };
    spellList[CLASS_WARLOCK][50] = { 1122 };
    spellList[CLASS_WARLOCK][60] = { 18540, 25311, 25309, 25307, 28610 };
    spellList[CLASS_DRUID][50] = { 21849 };
    spellList[CLASS_DRUID][60] = { 31018, 25297, 25299, 25298, 21850 };

    for (const auto& levelSpells : spellList[bot->GetClass()])
    {
        if (bot->GetLevel() >= levelSpells.first)
        {
            for (uint32 spellId : levelSpells.second)
            {
                if (!bot->HasSpell(spellId))
                {
                    bot->LearnSpell(spellId, false);
                }
            }
        }
    }
}

/**
* Attempts to add the quest item
* If the bot's bag is full report to player.
*/
void AutoLearnSpellAction::GetClassQuestItem(Quest const* quest, std::ostringstream* out)
{
    if (quest->GetRewItemsCount() > 0)
    {
        for (uint32 i = 0; i < quest->GetRewItemsCount(); i++)
        {
            ItemPosCountVec itemVec;
            ItemPrototype const* itemP = sObjectMgr.GetItemPrototype(quest->RewItemId[i]);
            InventoryResult result = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, itemVec, itemP->ItemId, quest->RewItemCount[i]);
            if (result == EQUIP_ERR_OK)
            {
                if (quest->RewItemId[i] != 8432 && // Stops Rogues from getting a quest reward item that is a quest item itself.
                    quest->RewItemId[i] != 8095)   // Stops Rogues from getting a quest reward item that is a quest item itself.
                {
                    ItemPosCountVec itemVec;
                    ItemPrototype const* itemP = sObjectMgr.GetItemPrototype(quest->RewItemId[i]);
                    InventoryResult result = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, itemVec, itemP->ItemId, quest->RewItemCount[i]);
                    if (result == EQUIP_ERR_OK)
                    {
                        bot->StoreNewItemInInventorySlot(itemP->ItemId, quest->RewItemCount[i]);
                        *out << "Got " << chat->formatItem(itemP, 1, 1) << " from " << quest->GetTitle();
                    }
                    else if (result == EQUIP_ERR_INVENTORY_FULL)
                    {
                        MailDraft draft("Item(s) from quest reward", quest->GetTitle());
                        Item* item = Item::CreateItem(itemP->ItemId, quest->RewItemCount[i]);
                        draft.AddItem(item);
                        draft.SendMailTo(MailReceiver(bot), MailSender(bot));
                        *out << "Could not add item " << chat->formatItem(itemP) << " from " << quest->GetTitle() << ". " << bot->GetName() << "'s inventory is full.";
                    }
                }
            }
        }
    }
}

std::string formatSpell(SpellEntry const* sInfo)
{
    std::ostringstream out;
    std::string rank = sInfo->Rank[0];

    if (rank.empty())
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName[LOCALE_enUS] << "]|h|r" << " (" << sInfo->Id << ")";
    else
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName[LOCALE_enUS] << " " << rank << "]|h|r" << " (" << sInfo->Id << ")";
    return out.str();
}

bool AutoLearnSpellAction::LearnSpell(uint32 spellId, std::ostringstream* out)
{
    bool learned = false;
    if (IsValidSpell(spellId))
    {
        SpellEntry const* proto = sServerFacade.LookupSpellInfo(spellId);

        if (!proto)
            return false;
        if (!learned && !bot->HasSpell(spellId)) {
            bot->LearnSpell(spellId, false);
            *out << formatSpell(proto) << ", ";

            learned = bot->HasSpell(spellId);
        }
    }
    return learned;
}

bool AutoLearnSpellAction::LearnSpellFromSpell(uint32 spellId, std::ostringstream* out)
{
    SpellEntry const* proto = sServerFacade.LookupSpellInfo(spellId);

    if (!proto)
        return false;

    bool learned = false;
    for (int j = 0; j < 3; ++j)
    {
        if (proto->Effect[j] == SPELL_EFFECT_LEARN_SPELL)
        {
            uint32 learnedSpell = proto->EffectTriggerSpell[j];

            if (IsValidSpell(learnedSpell))
            {
                // Racial-only spells (Touch of Weakness, Devouring Plague,
                // Fear Ward, Shadowguard...) carry a skill_line_ability race
                // mask. The quest-reward path is driven only by
                // SatisfyQuestRace on the QUEST, and quest 5659 "Touch of
                // Weakness" lists Undead|Troll, so a Troll priest was taught
                // the Undead racial (2652). Honour the spell's own mask here
                // like the trainer path (GetTrainerSpellState) already does.
                if (!bot->IsSpellFitByClassAndRace(learnedSpell))
                    continue;

                if (!bot->HasSpell(learnedSpell))
                {
                    bot->LearnSpell(learnedSpell, false);
                    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(learnedSpell);
                    *out << formatSpell(spellInfo) << ", ";
                    learned = true;
                }
            }
        }
    }
    return learned;
}
/**
* Some spells are being learned when they shouldn't be, either they are left over spells from previous patches or from mobs, or just unused blizz spells or in some cases the spell passed is also the spell that is used to teach the actual spell.
*/
bool AutoLearnSpellAction::IsValidSpell(uint32 spellId)
{
    bool isSpellValid = true;
    isSpellValid =
        // All Classes (Classic)
        spellId != 6463 && // Incorrect lock pick skill that was taught to all classes (Rogues still learn correct lock pick skill) (DB Error)
        spellId != 6461 &&  // Incorrect lock pick skill that was taught to all classes (Rogues still learn correct lock pick skill) (DB Error)
        // Warrior (Classic)
        spellId != 877 && // Prevent Warriors from learning Elemental Fury (DB Error)
        // Shaman (Classic)
        spellId != 8385 && // Prevents Shaman from learning Swift Wind spell which is cast onto player as a reward, the spell is not supposed to be learned.
        // Hunter
        spellId != 542  && // Prevents hunter from learning pet skill zzOLDLearn Nature Resistance.
        spellId != 6284 && // Prevents hunter from learning pet skill Pet Hardiness rank 1
        spellId != 6287 && // Prevents hunter from learning pet skill Pet Hardiness rank 2
        spellId != 6288 && // Prevents hunter from learning pet skill Pet Hardiness rank 3
        spellId != 6289 && // Prevents hunter from learning pet skill Pet Hardiness rank 4
        spellId != 6290 && // Prevents hunter from learning pet skill Pet Hardiness rank 5
        spellId != 6312 && // Prevents hunter from learning pet skill Pet Aggression rank 1
        spellId != 6318 && // Prevents hunter from learning pet skill Pet Aggression rank 2
        spellId != 6319 && // Prevents hunter from learning pet skill Pet Aggression rank 3
        spellId != 6320 && // Prevents hunter from learning pet skill Pet Aggression rank 4
        spellId != 6321 && // Prevents hunter from learning pet skill Pet Aggression rank 5
        spellId != 6329 && // Prevents hunter from learning pet skill Pet Recovery rank 1
        spellId != 6335 && // Prevents hunter from learning pet skill Pet Recovery rank 2
        spellId != 6336 && // Prevents hunter from learning pet skill Pet Recovery rank 3
        spellId != 6337 && // Prevents hunter from learning pet skill Pet Recovery rank 4
        spellId != 6338 && // Prevents hunter from learning pet skill Pet Recovery rank 5
        spellId != 6448 && // Prevents hunter from learning pet skill Pet Resistance rank 1
        spellId != 6450 && // Prevents hunter from learning pet skill Pet Resistance rank 2
        spellId != 6451 && // Prevents hunter from learning pet skill Pet Resistance rank 3
        spellId != 6452 && // Prevents hunter from learning pet skill Pet Resistance rank 4
        spellId != 6453 && // Prevents hunter from learning pet skill Pet Resistance rank 5
        // Paladin
        spellId != 1973; // Prevents Paladins from learning zzOldHip Shot III.
    return isSpellValid;
}

bool AutoLearnSpellAction::IsTeachingSpellListedAsSpell(uint32 spellId)
{
    bool isTeachingSpellListedAsSpell = false;
    isTeachingSpellListedAsSpell =
        spellId == 19318 ||    // Touch of weakness Teaching Spell listed as actual spell
        spellId == 2946  ||    // Devouring Plague Teaching Spell listed as actual spell
        spellId == 19325 ||    // Hex Of Weakness Teaching Spell listed as actual spell
        spellId == 19331 ||    // Shadowguard Teaching Spell listed as actual spell
        spellId == 19338 ||    // Desperate Prayer Teaching Spell listed as actual spell
        spellId == 19345 ||    // Feed Back Teaching Spell listed as actual spell
        spellId == 19337 ||    // Fear Ward Teaching Spell listed as actual spell
        spellId == 19357 ||    // Elune's Grace Teaching Spell listed as actual spell
        spellId == 19350;      // Starshards Teaching Spell listed as actual spell
        return isTeachingSpellListedAsSpell;
}
