#pragma once
//
// LiveGroupMembers — safe iteration over a group's live members.
//
// Why this exists: GroupReference (the list behind Group::GetFirstMember())
// carries a Player* that can outlive the Player object during a headless bot's
// relogin window. Dereferencing such a stale pointer reads freed memory; that
// is the documented cause of issue #225 (SIGSEGV inside GetAI) and of the
// heap corruption seen when a real player commands owned bots in a party.
//
// Group::GetMemberSlots() carries only ObjectGuids, so resolving each slot
// through the ObjectAccessor yields either a live Player* or nothing (the
// member is offline / mid-relogin). Members that cannot be resolved are
// skipped, exactly like the old null checks on GetSource() did.
//
// Usage keeps the loop body identical to the old GroupReference loops:
//
//     for (Player* member : LiveGroupMembers(group))
//     {
//         if (member == bot) continue;   // unchanged body
//     }
//
// No allocation: the iterator walks the slot list and resolves at most one
// guid per step.

#include <list>

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"

class LiveGroupMembers
{
public:
    typedef Group::MemberSlotList SlotList;
    typedef SlotList::const_iterator SlotIterator;

    explicit LiveGroupMembers(Group* group)
        : m_slots(group ? &group->GetMemberSlots() : &EmptySlots())
    {
    }

    class const_iterator
    {
    public:
        const_iterator(SlotList const& slots, SlotIterator it) : m_slots(slots), m_it(it), m_current(nullptr)
        {
            Resolve();
        }

        Player* operator*() const { return m_current; }
        Player* operator->() const { return m_current; }

        const_iterator& operator++()
        {
            ++m_it;
            Resolve();
            return *this;
        }

        bool operator!=(const_iterator const& other) const { return m_it != other.m_it; }
        bool operator==(const_iterator const& other) const { return m_it == other.m_it; }

    private:
        void Resolve()
        {
            while (m_it != m_slots.end())
            {
                m_current = ObjectAccessor::FindPlayer(m_it->guid);
                if (m_current)
                    return;
                ++m_it;
            }
            m_current = nullptr;
        }

        SlotList const& m_slots;
        SlotIterator m_it;
        Player* m_current;
    };

    const_iterator begin() const { return const_iterator(*m_slots, m_slots->begin()); }
    const_iterator end() const { return const_iterator(*m_slots, m_slots->end()); }

private:
    static SlotList const& EmptySlots()
    {
        static SlotList const empty;
        return empty;
    }

    SlotList const* m_slots;
};
