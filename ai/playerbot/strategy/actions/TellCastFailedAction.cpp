
#include "playerbot/playerbot.h"
#include "TellCastFailedAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/TargetValue.h"

using namespace ai;

bool TellCastFailedAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    uint8 castCount, result;
    uint32 spellId;
    p >> castCount >> spellId >> result;
    ai->SpellInterrupted(spellId);

    if (result == SPELL_CAST_OK)
        return false;

    const SpellEntry *const pSpellInfo =  sServerFacade.LookupSpellInfo(spellId);
    std::ostringstream out; out << chat->formatSpell(pSpellInfo) << ": ";
    switch (result)
    {
    case SPELL_FAILED_NOT_READY:
        out << "not ready";
        break;
    case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
        out << "requires spell focus";
        break;
    case SPELL_FAILED_REQUIRES_AREA:
        out << "cannot cast here";
        break;
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
        out << "requires item";
        break;
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND:
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND:
        out << "requires weapon";
        break;
    case SPELL_FAILED_PREVENTED_BY_MECHANIC:
    case SPELL_FAILED_INTERRUPTED:
        out << "interrupted";
        break;
    case SPELL_FAILED_OUT_OF_RANGE:
        out << "out of range";
        break;
    case SPELL_FAILED_LINE_OF_SIGHT:
        out << "not in line of sight";
        break;
    case SPELL_FAILED_MOVING:
        out << "moving";
        break;
    default:
        out << "cannot cast";
    }
    int32 castTime = GetSpellCastTime(pSpellInfo
#ifdef CMANGOS
            , bot
#endif
    );
    // Crowd control is reported at any cast time: a failed 1.5 s Polymorph
    // (or an instant Sap) was silent, and the reason was the only way to
    // see why a sheep never landed.
    if (castTime >= 2000 || FindNonCcTargetStrategy::IsCcSpell(pSpellInfo))
        ai->TellError(requester, out.str());
    return true;
}

bool TellSpellAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string spell = event.GetParam();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    SpellEntry const *spellInfo = sServerFacade.LookupSpellInfo(spellId );
    if (!spellInfo)
        return false;

    std::ostringstream out; out << chat->formatSpell(spellInfo);
    ai->TellError(requester, out.str());
    return true;
}
