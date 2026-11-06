#pragma once
#include "ActionBasket.h"

#include <cstddef>
#include <list>
#include <string>
#include <vector>

namespace ai
{
// One action still queued when a tick ended, as the `.bot why` trace names it.
struct PendingAction
{
    std::string name;
    float relevance = 0.0f;
};

class Queue
{
public:
    Queue(void) {}
public:
    ~Queue(void) {}
public:
	void Push(ActionBasket *action);
	ActionNode* Pop(ActionBasket* action = nullptr);
    ActionBasket* Peek();
	int Size();
	void RemoveExpired();
    // The highest-relevance baskets left in the queue, at most `limit`, for the
    // why trace: these were triggered but a more relevant action ran first.
    void Snapshot(std::vector<PendingAction>& out, size_t limit);
private:
	std::list<ActionBasket*> actions;
};
}
