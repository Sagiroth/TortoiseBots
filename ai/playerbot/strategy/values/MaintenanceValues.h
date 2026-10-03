#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "ItemUsageValue.h"
#include "BudgetValues.h"


namespace ai
{
    class CanMoveAroundValue : public BoolCalculatedValue
    {
    public:
        CanMoveAroundValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can move around", 2) {}
        virtual bool Calculate() override
        {
            if (bot->GetTradeData())
                return false;

            if (!AI_VALUE(bool, "group ready"))
                return false;

            if (AI_VALUE2(bool, "trigger active", "castnc"))
                return false;

            if (ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
            {
                float dist = AI_VALUE2(float, "distance", "master target");
                bool wanderTooFar = dist > ai->GetRange("wandermax");

                if (wanderTooFar)
                    return false;
            }

            return true;
        }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "can move around"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "movement"; }
        virtual std::string GetHelpDescription() { return "This value indicates whether the bot should wait for a trade to complete, a crafting cast or for the group to have enough health/mana before moving to rpg, grind or travel targets."; }
        virtual std::vector<std::string> GetUsedValues() { return {"group ready", "trigger active"}; }
#endif
    };

    class ShouldHomeBindValue : public BoolCalculatedValue
    {
    public:
        ShouldHomeBindValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "should home bind", 2) {}
        virtual bool Calculate() override { return AI_VALUE2(float, "distance", "home bind") > 1000.0f; };
    };


    class ShouldRepairValue : public BoolCalculatedValue
	{
	public:
        ShouldRepairValue(PlayerbotAI* ai) : BoolCalculatedValue(ai,"should repair",2) {}
        virtual bool Calculate() override { return AI_VALUE(uint8, "durability") < 30 || AI_VALUE(uint8, "lowest durability") < 10; };
    };

    class CanRepairValue : public BoolCalculatedValue
    {
    public:
        CanRepairValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can repair",2) {}
        virtual bool Calculate() override { return  ai->HasStrategy("rpg maintenance", BotState::BOT_STATE_NON_COMBAT) && AI_VALUE(uint8, "durability inventory") < 100 && AI_VALUE(uint32, "min repair cost") < AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::repair); };
    };

    //True when the vendor-usable stock a broke bot carries is worth the walk to
    //a vendor: it either covers the money missing for the cheapest trainable
    //class rank, or a real batch has piled up while the bags are filling.
    //Without that threshold a bot that cannot afford one rank ping-pongs
    //between the field and town, one grey pelt per trip, and never grinds.
    bool SellableStockWorthAVendorTrip(PlayerbotAI* ai);

    class ShouldSellValue : public BoolCalculatedValue
    {
    public:
        ShouldSellValue(PlayerbotAI* ai, std::string name = "should sell", int checkInterval = 2) : BoolCalculatedValue(ai, name , checkInterval) {}
        virtual bool Calculate() override
        {
            if (AI_VALUE(uint8, "bag space") > 80)
                return true;

            //A broke bot converts loot to coin instead of waiting for a full
            //bag, as long as the trip actually funds the missing spell rank.
            return SellableStockWorthAVendorTrip(ai);
        }

        //True when the bot has trainable class spells it cannot pay for.
        static bool CantAffordNextSpell(PlayerbotAI* ai);

        //True when a group member is about to leave the party for a vendor and
        //must therefore not be pulled into an elite or boss fight.
        static bool GroupMemberLeavingForVendor(PlayerbotAI* ai);
    };

    // True when the bot holds stock a vendor actually buys (issue #408): the
    // usage classifier can label an item VENDOR while its SellPrice is 0 (a
    // nonzero expected-AH-price manual value opens the VENDOR/AH branch in
    // ItemUsageValue), and the sell errand then finds nothing to sell - 1,443
    // "no vendor-usable stock" rows on 413 bots. The implementation lives in
    // MaintenanceValues.cpp so the bag scan stays next to the batch scan
    // (SellableStockWorthAVendorTrip) that already does exactly this walk.
    class CanSellValue : public BoolCalculatedValue
    {
    public:
        CanSellValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can sell",2) {}
        virtual bool Calculate() override;
    };

    //The nearest quest giver, class trainer or vendor a waiting bot has a real
    //reason to use, or an empty GuidPosition when there is nothing to do or
    //nobody close enough to do it with. Empty unless the bot is idle (not
    //preparing or walking a journey, no fight, no player master) and the NPC is
    //within a short walk: a bot standing next to the NPC that would unblock it
    //must use it instead of starting a journey it may never finish (issue #379).
    GuidPosition NearbyServiceTarget(PlayerbotAI* ai);

    //True when the bot holds a finished quest it can actually be paid for. A
    //quest that is complete but cannot be rewarded (full bags, a money
    //requirement) would park the bot at the taker failing every tick, so the
    //nearby hand-in waits until the reward would go through - the same
    //CanRewardQuest test the travel layer builds its taker fetch from.
    bool HasRewardableFinishedQuest(PlayerbotAI* ai);

    //Bag pressure in the field: bags at the pressure line, stock a vendor
    //actually wants, and no vendor within the near-service radius. True means
    //the bot must request the existing Vendor travel target instead of waiting
    //for one to walk past. False once nothing sellable is left, so a bot cannot
    //loop on trips that cannot empty its bags.
    bool BagPressureVendorTrip(PlayerbotAI* ai);

    //Is there a real reason to walk to a vendor? Stock a vendor pays for and
    //that is worth the walk, a durability below the repair threshold, or an
    //empty food/drink bag the bot can pay to refill - and never while the Vendor
    //purpose is parked after a fruitless errand (ParkVendorErrand). The rpg
    //vendor travel request reads this instead of the loose `should sell` &&
    //`can sell` pair, which a starting bot satisfied with its own food.
    bool VendorTripNeeded(PlayerbotAI* ai);

    //May the bag-pressure vendor errand start while a travel target is set?
    //Yes while that target is merely parked at its destination (arrived,
    //working it, or in cooldown) and no while a journey is in flight; and only
    //for the Vendor purpose, so other request actions keep the old churn guard.
    //The travel request action and the travel multiplier both ask this, so they
    //cannot disagree.
    bool VendorErrandWhileParked(PlayerbotAI* ai, const std::string& qualifier);

    //A vendor errand that found nothing vendor-usable cannot be finished by
    //standing there. Park the Vendor travel purpose the way TrainerAction parks
    //a fruitless trainer visit: the same key ChooseTravelTargetAction sets when
    //a destination search comes up empty, cleared early by any successful pick.
    void ParkVendorErrand(PlayerbotAI* ai, uint32 minutes);

    class NearbyServiceTargetValue : public GuidPositionCalculatedValue
    {
    public:
        NearbyServiceTargetValue(PlayerbotAI* ai, std::string name = "nearby service target", int checkInterval = 5) : GuidPositionCalculatedValue(ai, name, checkInterval) {}
        virtual GuidPosition Calculate() override { return NearbyServiceTarget(ai); }
    };
    // Snapshot of VendorTripNeeded for the travel destination search, which runs
    // async off-tick and cannot call the live predicate there: true only for a
    // real vendor errand (repair, a spell the stock pays for, affordable
    // rations), never for a parked purpose. Lets grind stay a destination while
    // the vendor purpose is parked (issue #393).
    class VendorTripNeededValue : public BoolCalculatedValue
    {
    public:
        VendorTripNeededValue(PlayerbotAI* ai, std::string name = "vendor trip needed", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {}
        virtual bool Calculate() override { return VendorTripNeeded(ai); }
    };

    class ShouldServiceNearbyNpcValue : public BoolCalculatedValue
    {
    public:
        ShouldServiceNearbyNpcValue(PlayerbotAI* ai, std::string name = "should service nearby npc", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {}
        virtual bool Calculate() override { return (bool)AI_VALUE(GuidPosition, "nearby service target"); }
    };

    class CanBuyValue : public BoolCalculatedValue
    {
    public:
        CanBuyValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can buy", 2) {}
        virtual bool Calculate() override;
        // True when a masterless pool bot (never a hire) could afford a real
        // vendor weapon upgrade above the trainer reserve. Lets the rpg buy
        // trigger fire for the weapon path without loosening any other buy:
        // consumable/ammo/tradeskill budgets are untouched.
        static bool CanAffordVendorWeaponUpgrade(PlayerbotAI* ai);
    };

    class ShouldAHSellValue : public ShouldSellValue
    {
    public:
        ShouldAHSellValue(PlayerbotAI* ai) : ShouldSellValue(ai, "should ah sell", 2) {}
        virtual bool Calculate() override;
    };

    class CanAHSellValue : public BoolCalculatedValue
    {
    public:
        CanAHSellValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can ah sell", 2) {}
        virtual bool Calculate() override { return ai->HasStrategy("rpg vendor", BotState::BOT_STATE_NON_COMBAT) && AI_VALUE2(uint32, "item count", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_AH)) > 1 && AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah) > GetAuctionDeposit(); };

        uint32 GetAuctionDeposit()
        {
            float minDeposit = 0;
            for (auto item : AI_VALUE2(std::list<Item*>, "inventory items", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_AH)))
            {
                uint32 deposit = ItemUsageValue::GetAhDepositCost(item->GetProto(), item->GetCount());

                if (minDeposit == 0 || deposit < minDeposit)
                    minDeposit = deposit;
            }

            return minDeposit;
        }
    };

    class CanAHBuyValue : public BoolCalculatedValue
    {
    public:
        CanAHBuyValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can ah buy", 2) {}
        virtual bool Calculate() override { return ai->HasStrategy("rpg vendor", BotState::BOT_STATE_NON_COMBAT) && !AI_VALUE(bool, "should repair") && !AI_VALUE(bool, "should sell") && !AI_VALUE(bool, "can get mail") && AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah) > 0; };
    };


    class CanGetMailValue : public BoolCalculatedValue
    {
    public:
        CanGetMailValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can get mail", 2) {}
        virtual bool Calculate() override;
    };

    class ShouldGetMailValue : public BoolCalculatedValue
    {
    public:
        ShouldGetMailValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "should get mail", 60) {}
        virtual bool Calculate() override;
    };

    class CanFightEqualValue: public BoolCalculatedValue
    {
    public:
        CanFightEqualValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can fight equal",2) {}
        virtual bool Calculate() override { return AI_VALUE(uint8, "durability") > 20 && !ai->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS,bot); };
    };

    class CanFightEliteValue : public BoolCalculatedValue
    {
    public:
        CanFightEliteValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can fight elite", 2) {}
        virtual bool Calculate() override { return bot->GetGroup() && AI_VALUE2(bool, "group and", "can fight equal") && AI_VALUE2(bool, "group and", "following party") && !ShouldSellValue::GroupMemberLeavingForVendor(ai); };
    };

    class CanFightBossValue : public BoolCalculatedValue
    {
    public:
        CanFightBossValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "can fight boss", 2) {}
        virtual bool Calculate() override { return bot->GetGroup() && bot->GetGroup()->GetMembersCount() > 3 && AI_VALUE2(bool, "group and", "can fight equal") && AI_VALUE2(bool, "group and", "following party") && !ShouldSellValue::GroupMemberLeavingForVendor(ai); };
    };

    class ShouldDrinkValue : public BoolCalculatedValue
    {
    public:
        ShouldDrinkValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "should drink", 2) {}
        virtual bool Calculate() override
        {
            if (!bot->GetPower(POWER_MANA) > 0)
                return false;

            // Stop at almost-full for cheat bots, 85 otherwise: the drink
            // trigger ("high mana") still opens below its line, but a
            // cheat-bot caster that stops at 85 re-pulls half-oom and
            // chain-pulls OOM the same way a wounded bot chain-pulls dead.
            if (AI_VALUE2(uint8, "mana", "self target") >= DrinkStopManaPct(
                ai->HasCheat(BotCheatMask::item), sPlayerbotAIConfig.almostFullHealth))
                return false;

            Player* master = ai->GetMaster();
            if (!master)
                return true;

            if (!bot->GetGroup())
                return true;

            if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                return true;

            if (!bot->IsWithinDist(master, sPlayerbotAIConfig.EatDrinkMaxDistance))
                return true;

            if (!master->IsMoving())
                return true;

            float minDistance = sPlayerbotAIConfig.EatDrinkMinDistance;
            if (!bot->GetGroup()->isRaidGroup())
                minDistance += sPlayerbotAIConfig.followDistance;
            else
                minDistance += sPlayerbotAIConfig.raidFollowDistance;

            if (bot->IsWithinDist(master, minDistance))
                return true;

            return false;
        }
    };

    class ShouldEatValue : public BoolCalculatedValue
    {
    public:
        ShouldEatValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "should eat", 2) {}
        virtual bool Calculate() override
        {
            // Matches the trigger band UseFoodStrategy installs: a bot with free
            // conjured rations (the item cheat) tops up to AlmostFullHealth
            // before it takes another fight, everyone else still stops at
            // LowHealth. Without this the action would refuse to run for the
            // [MediumHealth, AlmostFullHealth) band the strategy just made it
            // responsible for. Start threshold unchanged: the band still opens
            // at critical/low/medium, only the stop rises.
            uint32 eatBelow = RestStopHealthPct(ai->HasCheat(BotCheatMask::item),
                sPlayerbotAIConfig.mediumHealth, sPlayerbotAIConfig.lowHealth,
                sPlayerbotAIConfig.almostFullHealth);
            if (AI_VALUE2(uint8, "health", "self target") >= eatBelow)
                return false;

            Player* master = ai->GetMaster();
            if (!master)
                return true;

            if (!bot->GetGroup())
                return true;

            if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                return true;

            if (!bot->IsWithinDist(master, sPlayerbotAIConfig.EatDrinkMaxDistance))
                return true;

            if (!master->IsMoving())
                return true;

            float minDistance = sPlayerbotAIConfig.EatDrinkMinDistance;
            if (!bot->GetGroup()->isRaidGroup())
                minDistance += sPlayerbotAIConfig.followDistance;
            else
                minDistance += sPlayerbotAIConfig.raidFollowDistance;

            if (bot->IsWithinDist(master, minDistance))
                return true;

            return false;
        }
    };

    class DrinkDurationValue : public FloatCalculatedValue
    {
    public:
        DrinkDurationValue(PlayerbotAI* ai) : FloatCalculatedValue(ai, "drink duration") {}
        virtual float Calculate() override
        {
            Player* master = ai->GetMaster();

            float mpMissingPct = 100.0f - bot->GetPowerPercent(POWER_MANA);
            float multiplier = bot->InBattleGround() ? 20000.0f : 27000.0f;
            float drinkDuration = multiplier * (mpMissingPct / 100.0f);

            if (!master)
                return drinkDuration;

            if (!bot->GetGroup())
                return drinkDuration;

            if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                return drinkDuration;

            float minDistance = sPlayerbotAIConfig.followDistance;

            if (bot->GetGroup()->isRaidGroup())
                minDistance = sPlayerbotAIConfig.raidFollowDistance;

            if (!master->IsMoving())
                minDistance += sPlayerbotAIConfig.EatDrinkMinDistance;

            if (bot->IsWithinDist(master, minDistance))
                return drinkDuration;

            float masterOrientation = master->GetOrientation();
            float angleToBot = master->GetAngle(bot);
            float angleDiff = fabs(masterOrientation - angleToBot);

            if (angleDiff > M_PI / 2 && angleDiff < 3 * M_PI / 2)
            {
                drinkDuration *= 0.25f;
            }
            return drinkDuration;
        }
    };

    class EatDurationValue : public FloatCalculatedValue
    {
    public:
        EatDurationValue(PlayerbotAI* ai) : FloatCalculatedValue(ai, "eat duration") {}
        virtual float Calculate() override
        {
            Player* master = ai->GetMaster();

            float hpMissingPct = 100.0f - bot->GetHealthPercent();
            float multiplier = bot->InBattleGround() ? 20000.0f : 27000.0f;
            float eatDuration = multiplier * (hpMissingPct / 100.0f);

            if (!master)
                return eatDuration;

            if (!bot->GetGroup())
                return eatDuration;

            if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                return eatDuration;

            float minDistance = sPlayerbotAIConfig.followDistance;

            if (bot->GetGroup()->isRaidGroup())
                minDistance = sPlayerbotAIConfig.raidFollowDistance;

            if (!master->IsMoving())
                minDistance += sPlayerbotAIConfig.EatDrinkMinDistance;

            if (bot->IsWithinDist(master, minDistance))
                return eatDuration;

            float masterOrientation = master->GetOrientation();
            float angleToBot = master->GetAngle(bot);
            float angleDiff = fabs(masterOrientation - angleToBot);

            if (angleDiff > M_PI / 2 && angleDiff < 3 * M_PI / 2)
            {
                eatDuration *= 0.25f;
            }
            return eatDuration;
        }
    };
}
