// Standalone unit tests for PlayerUseObjectPolicy.
// Build & run:
//   g++ -std=c++17 -Wall -Wextra tools/test_player_use_object_policy.cpp -o /tmp/test_player_use_object_policy && /tmp/test_player_use_object_policy

#include "../runtime/PlayerUseObjectPolicy.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::UseObjectPolicy;

int main()
{
    int checks = 0;

    // 1. Empty list -> None.
    {
        SelectionResult r = SelectObject({});
        assert(r.outcome == SelectionOutcome::None);
        assert(r.guid == 0);
        ++checks;
    }

    // 2. Skip non-interactable, despawned, already-open, in-use, other kind, out-of-range.
    {
        std::vector<Candidate> list;
        Candidate despawned; despawned.guid = 1; despawned.kind = ObjectKind::Chest; despawned.distToRequester = 2.0f; despawned.spawned = false;
        Candidate alreadyOpen; alreadyOpen.guid = 2; alreadyOpen.kind = ObjectKind::Door; alreadyOpen.distToRequester = 2.0f; alreadyOpen.ready = false;
        Candidate noInteract; noInteract.guid = 3; noInteract.kind = ObjectKind::Button; noInteract.distToRequester = 2.0f; noInteract.noInteract = true;
        Candidate inUse; inUse.guid = 4; inUse.kind = ObjectKind::Goober; inUse.distToRequester = 2.0f; inUse.inUse = true;
        Candidate other; other.guid = 5; other.kind = ObjectKind::Other; other.distToRequester = 2.0f;
        Candidate tooFar; tooFar.guid = 6; tooFar.kind = ObjectKind::Chest; tooFar.distToRequester = 15.1f;
        list = { despawned, alreadyOpen, noInteract, inUse, other, tooFar };

        SelectionResult r = SelectObject(list);
        assert(r.outcome == SelectionOutcome::None);
        ++checks;
    }

    // 3. Distance order: picks closest usable object to requester.
    {
        Candidate farChest; farChest.guid = 10; farChest.kind = ObjectKind::Chest; farChest.distToRequester = 10.0f;
        Candidate nearButton; nearButton.guid = 11; nearButton.kind = ObjectKind::Button; nearButton.distToRequester = 4.0f;
        Candidate midQuest; midQuest.guid = 12; midQuest.kind = ObjectKind::QuestGiver; midQuest.distToRequester = 7.0f;

        SelectionResult r = SelectObject({ farChest, nearButton, midQuest });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 11);
        ++checks;
    }

    // 4. Deadmines Heavy Door case: closest is a Door without a usable lock,
    //    and a Button is within 15 yd of that door -> picks the Button.
    {
        Candidate door; door.guid = 20; door.kind = ObjectKind::Door; door.distToRequester = 2.0f;
        door.x = 0.0f; door.y = 0.0f; door.z = 0.0f; door.hasUsableLock = false;

        Candidate lever; lever.guid = 21; lever.kind = ObjectKind::Button; lever.distToRequester = 8.0f;
        lever.x = 6.0f; lever.y = 0.0f; lever.z = 0.0f;

        SelectionResult r = SelectObject({ door, lever });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 21);
        ++checks;
    }

    // 5. Door with a GOOBER within 15 yd of the door (even if slightly >15 yd from requester) -> picks the Goober.
    {
        Candidate door; door.guid = 30; door.kind = ObjectKind::Door; door.distToRequester = 3.0f;
        door.x = 3.0f; door.y = 0.0f; door.z = 0.0f; door.hasUsableLock = false;

        Candidate goober; goober.guid = 31; goober.kind = ObjectKind::Goober; goober.distToRequester = 16.5f;
        goober.x = 16.5f; goober.y = 0.0f; goober.z = 0.0f; // 13.5 yd from door

        SelectionResult r = SelectObject({ door, goober });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 31);
        ++checks;
    }

    // 6. Door with a Button > 15 yd away from the door -> picks the Door itself.
    {
        Candidate door; door.guid = 40; door.kind = ObjectKind::Door; door.distToRequester = 2.0f;
        door.x = 0.0f; door.y = 0.0f; door.z = 0.0f; door.hasUsableLock = false;

        Candidate farLever; farLever.guid = 41; farLever.kind = ObjectKind::Button; farLever.distToRequester = 16.0f;
        farLever.x = 16.0f; farLever.y = 0.0f; farLever.z = 0.0f; // 16 yd from door

        SelectionResult r = SelectObject({ door, farLever });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 40);
        ++checks;
    }

    // 7. Door WITH a usable lock (key or skill) -> picks the Door, NOT the nearby button.
    {
        Candidate door; door.guid = 50; door.kind = ObjectKind::Door; door.distToRequester = 2.0f;
        door.x = 0.0f; door.y = 0.0f; door.z = 0.0f; door.hasUsableLock = true;

        Candidate lever; lever.guid = 51; lever.kind = ObjectKind::Button; lever.distToRequester = 6.0f;
        lever.x = 4.0f; lever.y = 0.0f; lever.z = 0.0f;

        SelectionResult r = SelectObject({ door, lever });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 50);
        ++checks;
    }

    // 8. Locked door (bot cannot open) + usable button -> picks the usable button.
    {
        Candidate lockedDoor; lockedDoor.guid = 60; lockedDoor.kind = ObjectKind::Door; lockedDoor.distToRequester = 2.0f;
        lockedDoor.lockedBlocked = true;

        Candidate lever; lever.guid = 61; lever.kind = ObjectKind::Button; lever.distToRequester = 8.0f;

        SelectionResult r = SelectObject({ lockedDoor, lever });
        assert(r.outcome == SelectionOutcome::Use);
        assert(r.guid == 61);
        ++checks;
    }

    // 9. Only locked door/chest within radius -> LockBlocked.
    {
        Candidate lockedDoor; lockedDoor.guid = 70; lockedDoor.kind = ObjectKind::Door; lockedDoor.distToRequester = 3.0f;
        lockedDoor.lockedBlocked = true;

        SelectionResult r = SelectObject({ lockedDoor });
        assert(r.outcome == SelectionOutcome::LockBlocked);
        assert(r.guid == 70);
        ++checks;
    }

    // 10. VerbForKind mapping.
    {
        assert(std::string(VerbForKind(ObjectKind::Button)) == "Pulling");
        assert(std::string(VerbForKind(ObjectKind::Door)) == "Opening");
        assert(std::string(VerbForKind(ObjectKind::Chest)) == "Looting");
        assert(std::string(VerbForKind(ObjectKind::Goober)) == "Using");
        assert(std::string(VerbForKind(ObjectKind::QuestGiver)) == "Using");
        ++checks;
    }

    std::printf("test_player_use_object_policy: %d checks passed\n", checks);
    return 0;
}
