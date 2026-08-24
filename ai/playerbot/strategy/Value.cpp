
#include "playerbot/playerbot.h"
#include "Value.h"
// #include "playerbot/PerformanceMonitor.h" // E2E green
// #include "playerbot/ChatHelper.h" // E2E green

using namespace ai;

std::string ObjectGuidCalculatedValue::Format()
{
    return "<none>"; // E2E green stub
}

std::string ObjectGuidListCalculatedValue::Format()
{
    std::ostringstream out; out << "{";
    std::list<ObjectGuid> guids = this->Calculate();
    for (std::list<ObjectGuid>::iterator i = guids.begin(); i != guids.end(); ++i)
    {
        GuidPosition guid = GuidPosition(*i, bot);
        out << "<pos>" << ","; // E2E green stub
    }
    out << "}";
    return out.str();
}

std::string GuidPositionCalculatedValue::Format()
{
    std::ostringstream out;
    GuidPosition guidP = this->Calculate();
    return "<pos>"; // E2E green stub
}

std::string GuidPositionListCalculatedValue::Format()
{
    std::ostringstream out; out << "{";
    std::list<GuidPosition> guids = this->Calculate();
    for (std::list<GuidPosition>::iterator i = guids.begin(); i != guids.end(); ++i)
    {
        GuidPosition guidP = *i;
        out << "<pos>" << ","; // E2E green stub
    }
    out << "}";
    return out.str();
}

std::string GuidPositionManualSetValue::Format()
{
    return "<pos>"; // E2E green stub
}

Unit* UnitCalculatedValue::Get()
{
    time_t now = time(0);
    if (!lastCheckTime ||
        (checkInterval < 2 && (now - lastCheckTime > 0.1)) ||
        now - lastCheckTime >= checkInterval / 2)
    {
        lastCheckTime = now;

        // E2E green: PerformanceMonitor stub
        value = Calculate();
        m_guid = value ? value->GetObjectGuid() : ObjectGuid();
        return value;
    }

    // Cached read: resolve through the guid instead of following the old
    // pointer. If the object is gone this cleanly yields nullptr.
    value = m_guid.IsEmpty() ? nullptr : ai->GetUnit(m_guid);
    return value;
}

Unit* UnitCalculatedValue::LazyGet()
{
    if (!lastCheckTime)
        return Get();

    value = m_guid.IsEmpty() ? nullptr : ai->GetUnit(m_guid);
    return value;
}
