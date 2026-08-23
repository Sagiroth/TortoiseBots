#pragma once

#include "playerbot/strategy/NamedObjectContext.h"

namespace ai
{
class Strategy;
class Action;
class Trigger;
class UntypedValue;

// Focused contexts for the first playable Warrior slice. They contain only
// behavior required for owned-bot follow, assist, melee combat, death state,
// and Vanilla Warrior rotations; deferred systems stay out of the build.
class VerticalStrategyContext : public NamedObjectContext<Strategy>
{
public:
    VerticalStrategyContext();
};

class VerticalActionContext : public NamedObjectContext<Action>
{
public:
    VerticalActionContext();
};

class VerticalTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VerticalTriggerContext();
};

class VerticalValueContext : public NamedObjectContext<UntypedValue>
{
public:
    VerticalValueContext();
};

class WarriorVerticalStrategyContext : public NamedObjectContext<Strategy>
{
public:
    WarriorVerticalStrategyContext();
};

class WarriorVerticalActionContext : public NamedObjectContext<Action>
{
public:
    WarriorVerticalActionContext();
};

class WarriorVerticalTriggerContext : public NamedObjectContext<Trigger>
{
public:
    WarriorVerticalTriggerContext();
};
}
