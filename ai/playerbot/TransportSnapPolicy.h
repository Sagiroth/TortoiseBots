#pragma once

// Pure decision for the follow-time transport snap in PlayerbotAI::UpdateAI.
//
// While the master is close and the bot runs the core follow, a transport
// pointer mismatch between the two means someone boarded or alighted: the
// branch boards the bot onto the master's transport, or drops the bot off
// its own. A transport pointer can outlive the ride (it is only cleared by
// the next movement packet, teleport or relog), so a raw pointer comparison
// fires every tick long after both stand on solid ground: the bot is stopped
// and snapped in place and never walks again. Only a player standing
// geometrically on the transport its pointer names counts as riding it.
//
// No core includes; tested in tools/test_transport_snap_policy.cpp.

namespace ai
{
    enum class TransportSnapStep
    {
        None,        // nothing to do: same side aboard, or a stale pointer with nobody aboard
        Board,       // master aboard, bot on solid ground standing on the master's transport
        Alight,      // bot aboard, master on solid ground: drop the bot off at its own position
        DetachStale  // bot holds a pointer to a transport it is not standing on: drop the pointer, no teleport
    };

    // masterAboard / botAboard: the player geometrically stands on the
    // transport its pointer names. botOnMasterTransport: the bot stands on
    // the master's transport (the boarding precondition).
    inline TransportSnapStep DecideTransportSnapStep(bool masterAboard, bool botHasTransport,
        bool botAboard, bool botOnMasterTransport)
    {
        if (masterAboard && !botAboard)
            return botOnMasterTransport ? TransportSnapStep::Board : TransportSnapStep::None;
        if (botAboard && !masterAboard)
            return TransportSnapStep::Alight;
        if (botHasTransport && !botAboard && !masterAboard)
            return TransportSnapStep::DetachStale;
        return TransportSnapStep::None;
    }
}
