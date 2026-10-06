#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class MountValue
    {
    public:
        MountValue(uint32 spellId) : spellId(spellId) {};
        MountValue(const ItemPrototype* proto) : proto(proto) { spellId = GetMountSpell(proto->ItemId); };
        MountValue(Item* item) { spellId = GetMountSpell(item->GetProto()->ItemId); proto = item->GetProto(); };
        MountValue(const MountValue& mount) : spellId(mount.spellId), proto(mount.proto) {};

        bool IsItem() const { return proto; }
        const ItemPrototype* GetItemProto() const { return proto; }
        uint32 GetSpellId() const { return spellId; }
        uint32 GetSpeed() const { return GetSpeed(spellId); }
        // Dynamic speed mirrors the core rider rule
        // (HandleAuraModIncreaseMountedSpeed): SPELL_CUSTOM_MOUNT_SPEED_100
        // -> 100, SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED -> static DBC
        // speed, else riding 0 -> level/2 (integer division on the uint
        // level; ceil is a no-op on the truncated value), 75 -> 60,
        // 150 -> 100. Any other riding value unmounts in the core, so the
        // bot reports 0 (unusable) to avoid a cast->unmount loop. Runtime
        // selection and speed reporting must use GetSpeedFor; static
        // GetSpeed stays for bot-less contexts (factory pool filter) and the
        // hardcoded non-mount forms (travel form/ghost wolf/AQ).
        uint32 GetSpeedFor(Player* bot) const;
        // Precomputed-rider overload: same rule but reuses the caller's
        // riderSpeed (no SKILL_RIDING lookup). Use it when scoring several
        // mounts in one pass (sort) so the skill map is read once.
        uint32 GetSpeedFor(Player* bot, uint32 riderSpeed) const;
        static uint32 GetDynamicMountSpeed(uint32 spellId, Player* bot);
        static uint32 GetDynamicMountSpeed(uint32 spellId, uint32 riderSpeed);
        // Rider speed once per call: one SKILL_RIDING lookup. 0/75/150 map to
        // level/2, 60, 100; anything else is 0 (core unmounts).
        static uint32 GetRiderMountSpeed(Player* bot);
        // Core mount definition (SpellAuras.cpp ~3895:
        // EffectApplyAuraName[0] == SPELL_AURA_MOUNTED). Static speed is never
        // consulted, so 0-speed mounts (30174) still count.
        static bool IsMountSpell(uint32 spellId);
        static uint32 GetSpeed(uint32 spellId);
        static uint32 GetMountSpell(uint32 itemId);
        bool IsValidLocation(Player* bot);

    private:
        const ItemPrototype* proto = nullptr;
        uint32 spellId = 0;
    };

    class CurrentMountSpeedValue : public Uint32CalculatedValue, public Qualified
    {
    public:
        CurrentMountSpeedValue(PlayerbotAI* ai) : Uint32CalculatedValue(ai, "current mount speed", 1), Qualified() {}
        virtual uint32 Calculate() override;
    };

    class FullMountListValue : public SingleCalculatedValue<std::vector<MountValue>>
    {
    public:
        FullMountListValue(PlayerbotAI* ai) : SingleCalculatedValue<std::vector<MountValue>>(ai, "full mount list") {}
        virtual std::vector<MountValue> Calculate() override;
    };

    class MountListValue : public CalculatedValue<std::vector<MountValue>>
    {
    public:
        MountListValue(PlayerbotAI* ai) : CalculatedValue<std::vector<MountValue>>(ai, "mount list", 10) {}
        virtual std::vector<MountValue> Calculate() override;
        virtual std::string Format() override;
    };

    class MaxMountSpeedValue : public Uint32CalculatedValue, public Qualified
    {
    public:
        MaxMountSpeedValue(PlayerbotAI* ai) : Uint32CalculatedValue(ai, "max mount speed", 1), Qualified() {}
        virtual uint32 Calculate() override;
    };

    class MountSkillTypeValue : public CalculatedValue<uint32>
    {
    public:
        MountSkillTypeValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "mount skilltype", 10) {}

        virtual uint32 Calculate() override;
    };

    class AvailableMountVendors : public CalculatedValue<std::vector<int32>>
    {
    public:
        AvailableMountVendors(PlayerbotAI* ai) : CalculatedValue<std::vector<int32>>(ai, "available mount vendors", 10) {}

        virtual std::vector<int32> Calculate() override;
    };

    class CanTrainMountValue : public BoolCalculatedValue
    {
    public:
        CanTrainMountValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can train mount", 10) {}

        virtual bool Calculate() override;
    };

    class CanBuyMountValue : public BoolCalculatedValue
    {
    public:
        CanBuyMountValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can buy mount", 10) {}

        virtual bool Calculate() override;
    };
}
