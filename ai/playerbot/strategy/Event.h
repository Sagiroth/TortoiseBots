#pragma once

#include "ObjectGuid.h"
#include "WorldPacket.h"

class Player;

namespace ai
{
    class Event
	{
	public:
        // Penqle's WorldPacket has a deleted copy operator= (it has a user-defined
        // move ctor, which implicitly deletes copy=). Use the member init list for
        // the copy ctor (WorldPacket's copy CTOR IS available) and an explicit
        // operator= that copy-constructs + move-assigns.
        Event(Event const& other)
            : source(other.source)
            , param(other.param)
            , packet(other.packet)
            , owner(other.owner)
        {}
        Event& operator=(Event const& other)
        {
            if (this == &other) return *this;
            source = other.source;
            param = other.param;
            packet = WorldPacket(other.packet);  // copy-construct + move-assign
            owner = other.owner;
            return *this;
        }
        Event() {}
        Event(std::string source) : source(source) {}
        Event(std::string source, std::string param, Player* owner = NULL) : source(source), param(param), owner(owner) {}
        Event(std::string source, WorldPacket &packet, Player* owner = NULL) : source(source), packet(packet), owner(owner) {}
        Event(std::string source, ObjectGuid object, Player* owner = NULL) : source(source), owner(owner) { packet << object; }
        virtual ~Event() {}

	public:
        std::string getSource() const { return source; }
        std::string getParam() { return param; }
        WorldPacket& getPacket() { return packet; }
        ObjectGuid getObject();
        Player* getOwner() { return owner; }
        bool operator! () const { return source.empty(); }

    protected:
        std::string source;
        std::string param;
        WorldPacket packet;
        Player* owner = nullptr;
	};
}
