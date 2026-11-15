// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
#include "../ai/playerbot/CampMgr.h"
#include <iomanip>
#include <sstream>

namespace TortoiseBots {
namespace BotCommands {

namespace
{
    bool RequireCampGm(ChatHandler* handler, bool needPlayer, Player*& outPlayer)
    {
        outPlayer = nullptr;
        if (IsServerConsole(handler))
        {
            if (needPlayer)
            {
                handler->PSendSysMessage("This subcommand must be run in-game by a GM.");
                return false;
            }
            return true;
        }
        WorldSession* session = handler ? handler->GetSession() : nullptr;
        if (!session || !session->GetPlayer())
        {
            handler->PSendSysMessage("You must be in-game or at the server console.");
            return false;
        }
        if (session->GetSecurity() < SEC_GAMEMASTER)
        {
            handler->PSendSysMessage("Your account may not manage bot camp spots.");
            return false;
        }
        outPlayer = session->GetPlayer();
        return true;
    }
}

bool HandleCamp(ChatHandler* handler, char const* args)
{
    std::string raw = args ? TrimCopy(args) : "";
    std::string sub;
    std::string rest;
    size_t sp = raw.find(' ');
    if (sp == std::string::npos)
    {
        sub = ToLowerAscii(raw);
    }
    else
    {
        sub = ToLowerAscii(raw.substr(0, sp));
        rest = TrimCopy(raw.substr(sp + 1));
    }

    if (sub == "add")
    {
        Player* gm = nullptr;
        if (!RequireCampGm(handler, true, gm))
            return false;

        std::string name = rest;
        if (name.size() > 100)
            name.resize(100);

        uint32 newId = 0;
        if (!sCampMgr.AddSpot(
                gm->GetMapId(),
                gm->GetPositionX(),
                gm->GetPositionY(),
                gm->GetPositionZ(),
                gm->GetOrientation(),
                gm->GetZoneId(),
                name,
                newId))
        {
            handler->PSendSysMessage("Failed to insert camp spot into ai_playerbot_camp_spot.");
            return false;
        }

        handler->PSendSysMessage(
            "Added camp spot #%u (map %u, zone %u, %.1f %.1f %.1f, o %.2f)%s%s. Total spots: %u.",
            newId,
            gm->GetMapId(),
            gm->GetZoneId(),
            gm->GetPositionX(),
            gm->GetPositionY(),
            gm->GetPositionZ(),
            gm->GetOrientation(),
            name.empty() ? "" : " name=",
            name.empty() ? "" : name.c_str(),
            static_cast<uint32>(sCampMgr.GetSpotCount()));
        return true;
    }

    if (sub == "list")
    {
        Player* gm = nullptr;
        if (!RequireCampGm(handler, true, gm))
            return false;

        auto nearby = sCampMgr.FindNearbySpots(
            gm->GetMapId(),
            gm->GetPositionX(),
            gm->GetPositionY(),
            gm->GetPositionZ(),
            200.0f);

        handler->PSendSysMessage("Camp spots within 200 yd: %u (total loaded: %u, active bot camps: %u)",
            static_cast<uint32>(nearby.size()),
            static_cast<uint32>(sCampMgr.GetSpotCount()),
            static_cast<uint32>(sCampMgr.GetActiveCamps().size()));

        for (auto const& entry : nearby)
        {
            CampSpot const& s = entry.first;
            handler->PSendSysMessage("  #%u [%.1f yd] map=%u zone=%u (%.1f, %.1f, %.1f) src=%u %s",
                s.id, entry.second, s.map, s.zoneId, s.x, s.y, s.z, static_cast<uint32>(s.source),
                s.name.empty() ? "(unnamed)" : s.name.c_str());
        }
        return true;
    }

    if (sub.empty() || sub == "status")
    {
        Player* gm = nullptr;
        if (!RequireCampGm(handler, false, gm))
            return false;

        auto active = sCampMgr.GetActiveCamps();
        handler->PSendSysMessage("Bot camps: %u spots loaded, %u active camps right now.",
            static_cast<uint32>(sCampMgr.GetSpotCount()),
            static_cast<uint32>(active.size()));
        for (ActiveCampInfo const& c : active)
        {
            handler->PSendSysMessage("  bot=%u map=%u zone=%u (%.1f, %.1f, %.1f) %s",
                c.botGuid, c.mapId, c.zoneId, c.x, c.y, c.z,
                c.spotName.empty() ? "" : c.spotName.c_str());
        }
        return true;
    }

    handler->PSendSysMessage("Usage: .bot camp <add [name]|list|status>");
    return false;
}

} // namespace BotCommands
} // namespace TortoiseBots
