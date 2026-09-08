#include <iostream>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <string>
#include <cmath>
#include <cassert>
#include <cstdint>

// Lightweight mock types representing the MaNGOS/Tortoise types used in AttackersValue and AoeValues
struct ObjectGuid
{
    uint64_t value;

    ObjectGuid() : value(0) {}
    explicit ObjectGuid(uint64_t val) : value(val) {}

    bool IsEmpty() const { return value == 0; }
    bool operator==(const ObjectGuid& other) const { return value == other.value; }
    bool operator!=(const ObjectGuid& other) const { return value != other.value; }
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
};

struct Unit
{
    ObjectGuid guid;
    std::string name;
    float x, y, z;
    bool isValid;

    Unit(ObjectGuid g, std::string n, float px, float py, float pz = 0.0f, bool valid = true)
        : guid(g), name(n), x(px), y(py), z(pz), isValid(valid) {}

    ObjectGuid getObjectGuid() const { return guid; }
    float getPositionX() const { return x; }
    float getPositionY() const { return y; }
    float getPositionZ() const { return z; }
};

float getDistance2d(const Unit* u1, const Unit* u2)
{
    float dx = u1->x - u2->x;
    float dy = u1->y - u2->y;
    return std::sqrt(dx * dx + dy * dy);
}

// C++ implementation of the hardened FindMaxDensity logic preserving first-seen sequence
std::list<ObjectGuid> SimulateFindMaxDensity(
    const Unit* bot,
    const std::list<ObjectGuid>& units,
    const std::map<ObjectGuid, Unit*>& unitMap,
    float range = 100.0f,
    float aoeRadius = 10.0f)
{
    size_t maxCount = 0;
    ObjectGuid maxGroup;
    std::map<ObjectGuid, std::set<ObjectGuid>> groups;
    std::vector<ObjectGuid> uniqueUnits;

    if (bot)
    {
        // Stable first-seen deduplication preserving priority order
        std::set<ObjectGuid> seen;
        for (const ObjectGuid& guid : units)
        {
            if (seen.insert(guid).second)
                uniqueUnits.push_back(guid);
        }

        for (const ObjectGuid& guid : uniqueUnits)
        {
            auto it = unitMap.find(guid);
            Unit* unit = (it != unitMap.end()) ? it->second : nullptr;
            if (unit)
            {
                float distanceToPlayer = getDistance2d(unit, bot);
                if (distanceToPlayer <= range)
                {
                    for (const ObjectGuid& otherGuid : uniqueUnits)
                    {
                        auto itOther = unitMap.find(otherGuid);
                        Unit* other = (itOther != unitMap.end()) ? itOther->second : nullptr;
                        if (other)
                        {
                            float d = getDistance2d(unit, other);
                            if (d <= aoeRadius * 2.0f)
                            {
                                groups[guid].insert(otherGuid);
                            }
                        }
                    }

                    if (maxCount < groups[guid].size())
                    {
                        maxCount = groups[guid].size();
                        maxGroup = guid;
                    }
                }
            }
        }
    }

    if (!maxCount || maxGroup.IsEmpty())
    {
        return std::list<ObjectGuid>();
    }

    // Return the cluster members in original attackers priority order
    std::list<ObjectGuid> result;
    const std::set<ObjectGuid>& maxSet = groups[maxGroup];
    for (const ObjectGuid& guid : uniqueUnits)
    {
        if (maxSet.find(guid) != maxSet.end())
        {
            result.push_back(guid);
        }
    }

    return result;
}

// C++ implementation of the fixed shareTargets deduplication logic preserving first-seen sequence
std::list<ObjectGuid> SimulateShareTargets(
    std::list<ObjectGuid> initialList,
    const std::vector<Unit*>& otherBotSpecificTargets,
    const std::vector<Unit*>& thisBotSpecificTargets,
    const std::map<ObjectGuid, Unit*>& unitMap,
    bool getOne = false)
{
    // Remove other bot specific targets
    for (Unit* target : otherBotSpecificTargets)
    {
        if (target)
            initialList.remove(target->getObjectGuid());
    }

    // Append bot specific targets of this bot in natural priority order
    for (Unit* target : thisBotSpecificTargets)
    {
        if (target)
            initialList.push_back(target->getObjectGuid());
    }

    // Validate targets and enforce distinct hostile units, preserving first-seen order
    std::list<ObjectGuid> distinctResult;
    std::set<ObjectGuid> seen;

    for (const ObjectGuid& guid : initialList)
    {
        if (!seen.insert(guid).second)
            continue;

        auto it = unitMap.find(guid);
        Unit* target = (it != unitMap.end()) ? it->second : nullptr;
        if (target && target->isValid)
        {
            distinctResult.push_back(guid);
            if (getOne)
                break;
        }
    }

    return distinctResult;
}

int main()
{
    Unit bot(ObjectGuid(1), "BotPlayer", 0.0f, 0.0f);
    Unit mob1(ObjectGuid(101), "Defias Rogue", 10.0f, 0.0f);
    Unit mob2(ObjectGuid(102), "Defias Bandit", 12.0f, 2.0f);
    Unit mob3(ObjectGuid(103), "Defias Highwayman", 11.0f, -1.0f);
    Unit mobFar(ObjectGuid(104), "Defias Overseer", 60.0f, 50.0f);

    std::map<ObjectGuid, Unit*> unitMap = {
        {bot.guid, &bot},
        {mob1.guid, &mob1},
        {mob2.guid, &mob2},
        {mob3.guid, &mob3},
        {mobFar.guid, &mobFar},
    };

    // Test 1: 1 mob with duplicated references across specific targets
    {
        std::list<ObjectGuid> copied = { mob1.guid };
        std::vector<Unit*> otherTargets = { &mob1 };
        // Bot references mob1 in current target, attack target, pull target, old target
        std::vector<Unit*> thisTargets = { &mob1, &mob1, &mob1, &mob1 };

        std::list<ObjectGuid> attackers = SimulateShareTargets(copied, otherTargets, thisTargets, unitMap);
        assert(attackers.size() == 1 && "Test 1 Failed: AttackersValue must return count 1 for 1 mob");
        assert(attackers.front() == mob1.guid && "Test 1 Failed: attacker GUID must match mob1");

        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, attackers, unitMap);
        assert(density.size() == 1 && "Test 1 Failed: AoeCountValue::FindMaxDensity must return count 1");
        bool aoeActive = density.size() >= 3;
        assert(!aoeActive && "Test 1 Failed: AoE trigger (>= 3) must NOT trigger on 1 mob");
        std::cout << "[PASS] Test 1: 1 mob = count 1, AoE inactive (density: " << density.size() << ")\n";
    }

    // Test 2: 2 mobs with duplicated references -> count 2 != 3+
    {
        std::list<ObjectGuid> copied = { mob1.guid, mob2.guid };
        std::vector<Unit*> otherTargets = { &mob1 };
        std::vector<Unit*> thisTargets = { &mob2, &mob1, &mob2, &mob2 };

        std::list<ObjectGuid> attackers = SimulateShareTargets(copied, otherTargets, thisTargets, unitMap);
        assert(attackers.size() == 2 && "Test 2 Failed: AttackersValue must contain exactly 2 distinct units");

        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, attackers, unitMap);
        assert(density.size() == 2 && "Test 2 Failed: Density for 2 mobs must be 2");
        assert(density.size() != 3 && "Test 2 Failed: 2 mobs must NOT evaluate to 3+");
        bool aoeActive = density.size() >= 3;
        assert(!aoeActive && "Test 2 Failed: AoE trigger (>= 3) must NOT trigger on 2 mobs");
        std::cout << "[PASS] Test 2: 2 mobs != 3+, AoE inactive (density: " << density.size() << ")\n";
    }

    // Test 3: Real clustered 3 mobs = AoE eligible
    {
        std::list<ObjectGuid> copied = { mob1.guid, mob2.guid, mob3.guid };
        std::vector<Unit*> otherTargets = { &mob1 };
        std::vector<Unit*> thisTargets = { &mob1, &mob2, &mob3 };

        std::list<ObjectGuid> attackers = SimulateShareTargets(copied, otherTargets, thisTargets, unitMap);
        assert(attackers.size() == 3 && "Test 3 Failed: All 3 distinct mobs must be retained");

        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, attackers, unitMap);
        assert(density.size() == 3 && "Test 3 Failed: Clustered 3 mobs must produce density 3");
        bool aoeActive = density.size() >= 3;
        assert(aoeActive && "Test 3 Failed: Real clustered 3 mobs must be AoE eligible (>= 3)");
        std::cout << "[PASS] Test 3: Real clustered 3 mobs = AoE eligible (density: " << density.size() << ")\n";
    }

    // Test 4: 3 spread mobs far apart -> not AoE eligible
    {
        Unit mobA(ObjectGuid(201), "Mob A", 10.0f, 0.0f);
        Unit mobB(ObjectGuid(202), "Mob B", 10.0f, 40.0f);
        Unit mobC(ObjectGuid(203), "Mob C", 10.0f, 80.0f);
        std::map<ObjectGuid, Unit*> spreadMap = {
            {bot.guid, &bot},
            {mobA.guid, &mobA},
            {mobB.guid, &mobB},
            {mobC.guid, &mobC},
        };
        std::list<ObjectGuid> attackers = { mobA.guid, mobB.guid, mobC.guid };

        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, attackers, spreadMap);
        assert(density.size() == 1 && "Test 4 Failed: Spread mobs must yield cluster density 1");
        bool aoeActive = density.size() >= 3;
        assert(!aoeActive && "Test 4 Failed: Spread mobs must NOT be AoE eligible");
        std::cout << "[PASS] Test 4: 3 spread mobs = AoE inactive (density: " << density.size() << ")\n";
    }

    // Test 5: Defensive FindMaxDensity with raw duplicated input
    {
        std::list<ObjectGuid> rawDuplicates = { mob1.guid, mob1.guid, mob1.guid, mob1.guid, mob1.guid };
        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, rawDuplicates, unitMap);
        assert(density.size() == 1 && "Test 5 Failed: Raw duplicate inputs must not inflate density");
        std::cout << "[PASS] Test 5: Defensive FindMaxDensity with raw duplicates (density: " << density.size() << ")\n";
    }

    // Test 6: getOne qualifier
    {
        std::list<ObjectGuid> copied = { mob1.guid, mob2.guid, mob3.guid };
        std::vector<Unit*> thisTargets = { &mob1, &mob2 };
        std::list<ObjectGuid> attackers = SimulateShareTargets(copied, {}, thisTargets, unitMap, true);
        assert(attackers.size() == 1 && "Test 6 Failed: getOne qualifier must return exactly 1 attacker");
        std::cout << "[PASS] Test 6: getOne qualifier returns exactly 1 attacker\n";
    }

    // Test 7: Target ordering preservation (inverse of GUID numerical order)
    {
        Unit highMob(ObjectGuid(99999), "HighGuid", 10.0f, 0.0f);
        Unit midMob(ObjectGuid(55555), "MidGuid", 10.5f, 0.5f);
        Unit lowMob(ObjectGuid(11111), "LowGuid", 11.0f, 1.0f);
        std::map<ObjectGuid, Unit*> orderMap = {
            {bot.guid, &bot},
            {highMob.guid, &highMob},
            {midMob.guid, &midMob},
            {lowMob.guid, &lowMob},
        };
        // Raw list in high -> mid -> low order with duplicates
        std::list<ObjectGuid> rawList = { highMob.guid, highMob.guid, midMob.guid, lowMob.guid, midMob.guid };

        std::list<ObjectGuid> attackers = SimulateShareTargets(rawList, {}, {}, orderMap);
        auto it = attackers.begin();
        assert(*it++ == highMob.guid && "Test 7 Failed: highMob must be 1st");
        assert(*it++ == midMob.guid && "Test 7 Failed: midMob must be 2nd");
        assert(*it++ == lowMob.guid && "Test 7 Failed: lowMob must be 3rd");
        assert(it == attackers.end());

        std::list<ObjectGuid> density = SimulateFindMaxDensity(&bot, attackers, orderMap);
        auto dit = density.begin();
        assert(*dit++ == highMob.guid && "Test 7 Failed: density output must preserve highMob 1st");
        assert(*dit++ == midMob.guid && "Test 7 Failed: density output must preserve midMob 2nd");
        assert(*dit++ == lowMob.guid && "Test 7 Failed: density output must preserve lowMob 3rd");
        assert(dit == density.end());
        std::cout << "[PASS] Test 7: Target sequence preserved without GUID-sorting\n";
    }

    std::cout << "\nAll 7 regression tests PASSED successfully.\n";
    return 0;
}
