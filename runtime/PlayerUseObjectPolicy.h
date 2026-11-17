#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace TortoiseBots::UseObjectPolicy
{

constexpr float kSearchRadiusYd = 15.0f;
constexpr float kDoorButtonRadiusYd = 15.0f;

enum class ObjectKind : uint8_t
{
    Other = 0,
    Button,
    Goober,
    Door,
    Chest,
    QuestGiver,
};

struct Candidate
{
    uint64_t guid = 0;
    ObjectKind kind = ObjectKind::Other;
    float distToRequester = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool spawned = true;
    bool ready = true;
    bool noInteract = false;
    bool inUse = false;
    bool lockedBlocked = false;
    bool hasUsableLock = false;
};

enum class SelectionOutcome : uint8_t
{
    None = 0,
    Use,
    LockBlocked,
};

struct SelectionResult
{
    SelectionOutcome outcome = SelectionOutcome::None;
    uint64_t guid = 0;
};

inline float Distance3D(Candidate const& a, Candidate const& b)
{
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    float const dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool IsEligibleKind(ObjectKind kind)
{
    return kind != ObjectKind::Other;
}

inline bool IsInteractable(Candidate const& c)
{
    return c.spawned && c.ready && !c.noInteract && !c.inUse && IsEligibleKind(c.kind);
}

inline char const* VerbForKind(ObjectKind kind)
{
    switch (kind)
    {
        case ObjectKind::Button: return "Pulling";
        case ObjectKind::Door:   return "Opening";
        case ObjectKind::Chest:  return "Looting";
        default:                 return "Using";
    }
}

inline bool CloserToRequester(Candidate const& a, Candidate const& b)
{
    if (a.distToRequester != b.distToRequester)
        return a.distToRequester < b.distToRequester;
    return a.guid < b.guid;
}

inline SelectionResult SelectObject(std::vector<Candidate> const& candidates)
{
    Candidate const* bestUsable = nullptr;
    Candidate const* bestLocked = nullptr;

    for (Candidate const& c : candidates)
    {
        if (!IsInteractable(c) || c.distToRequester > kSearchRadiusYd)
            continue;
        if (c.lockedBlocked)
        {
            if (!bestLocked || CloserToRequester(c, *bestLocked))
                bestLocked = &c;
        }
        else
        {
            if (!bestUsable || CloserToRequester(c, *bestUsable))
                bestUsable = &c;
        }
    }

    if (bestUsable)
    {
        if (bestUsable->kind == ObjectKind::Door && !bestUsable->hasUsableLock)
        {
            Candidate const* bestButton = nullptr;
            float bestButtonDist = 0.0f;
            for (Candidate const& c : candidates)
            {
                if (!IsInteractable(c) || c.lockedBlocked)
                    continue;
                if (c.kind != ObjectKind::Button && c.kind != ObjectKind::Goober)
                    continue;
                float const distToDoor = Distance3D(c, *bestUsable);
                if (distToDoor > kDoorButtonRadiusYd)
                    continue;
                if (!bestButton || distToDoor < bestButtonDist ||
                    (distToDoor == bestButtonDist && CloserToRequester(c, *bestButton)))
                {
                    bestButton = &c;
                    bestButtonDist = distToDoor;
                }
            }
            if (bestButton)
                return { SelectionOutcome::Use, bestButton->guid };
        }
        return { SelectionOutcome::Use, bestUsable->guid };
    }

    if (bestLocked)
        return { SelectionOutcome::LockBlocked, bestLocked->guid };

    return { SelectionOutcome::None, 0 };
}

} // namespace TortoiseBots::UseObjectPolicy
