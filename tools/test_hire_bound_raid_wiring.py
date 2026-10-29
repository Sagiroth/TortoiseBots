"""C6 (Onyxia kill plan): one more hire into a bound raid, the glue.

The commander of a bound raid stands outside the raid group, so the raid holds 40 bots. The decision is
pure (runtime/HireRaidPolicy.h, tools/test_hire_raid_policy.cpp); these pins hold the wiring that calls it:
Hire/HireRaid ask the policy, the provisioner joins the hire to the lead bot's raid instead of grouping it
with the commander, the join goes through the binding (hire state, follow target, layout) and never through
an unbind, and the config clamp for players stays 39.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HIRE = (ROOT / "runtime/HireProvisionService.cpp").read_text()
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
LIFECYCLE = (ROOT / "runtime/HireLifecycle.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
POLICY = (ROOT / "runtime/HireRaidPolicy.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


HIRE_SRC = strip_comments(HIRE)
CMD_SRC = strip_comments(COMMANDS)


class HireBoundRaidWiringTest(unittest.TestCase):
    def test_hire_asks_the_policy_and_leaves_the_commander_without_a_party(self):
        hire = body(HIRE_SRC, "HireOutcome HireProvisionService::Hire(", "uint32_t HireProvisionService::QuoteHireFee(")
        facts = hire.index("BotCommands::BoundRaidOf(requester)")
        into = hire.index("HireIntoBoundRaid(boundRaid.bound, boundRaid.commanderInGroup)")
        limit = hire.index("HireMaxCompanions(sPlayerbotAIConfig.hireMaxBotsPerPlayer, intoBoundRaid)")
        seat = hire.index("DecideHireSeat(seatFacts)")
        group = hire.index("Group* group = intoBoundRaid ? nullptr : requester->GetGroup();")
        self.assertLess(facts, into)
        self.assertLess(into, limit)
        self.assertLess(limit, seat)
        self.assertLess(seat, group)
        self.assertIn("seatFacts.boundRaidSeatsTaken = boundRaid.members + MusteringOutsideBoundRaid(m_pending, requester);", hire)
        self.assertIn("HireSeatVerdict::RaidFull ? HireStatus::GroupFull : HireStatus::CapReached", hire)

    def test_hire_raid_plans_forty_for_the_bound_commander_only(self):
        raid = body(HIRE_SRC, "HireRaidOutcome HireProvisionService::HireRaid(", "void HireProvisionService::StepRaidJobs(")
        self.assertIn("HireMaxCompanions(sPlayerbotAIConfig.hireMaxBotsPerPlayer, intoBoundRaid)", raid)
        self.assertIn("Group* group = intoBoundRaid ? nullptr : requester->GetGroup();", raid)
        self.assertIn("uint32_t members = intoBoundRaid ? boundRaid.members :", raid)
        self.assertIn("intoBoundRaid ? kHireBoundRaidMaxCompanions : kHireRaidMaxCompanions", raid)
        self.assertIn("uint32_t const raidSeats = kHireRaidMaxCompanions + 1;", raid)

    def test_provisioning_joins_the_lead_raid_without_a_summon_to_the_commander(self):
        prov = body(HIRE_SRC, "bool HireProvisionService::ProvisionNow(", "void HireProvisionService::ProvisionHeavy(")
        into = prov.index("bool const intoBoundRaid = pending.seatMode == HireSeatMode::BoundRaidSeat;")
        skip = prov.index("if (intoBoundRaid)\n                pending.summonRequested = true;")
        summon = prov.index("RequestSummon(master, bot)")
        join = prov.index("intoBoundRaid ? !BotCommands::JoinBoundRaid(master, bot) : !Reunite(bot, master)")
        grouped = prov.index("MarkGrouped(pending.botGuid)")
        self.assertLess(into, skip)
        self.assertLess(skip, summon)
        self.assertLess(summon, join)
        self.assertLess(join, grouped)

    def test_a_seat_hire_is_rechecked_right_before_the_join_and_cancelled_never_reunited(self):
        prov = body(HIRE_SRC, "bool HireProvisionService::ProvisionNow(", "void HireProvisionService::ProvisionHeavy(")
        # the mode decides, the binding is not asked for the mode again
        self.assertNotIn("HireIntoBoundRaid(", prov)
        order = [
            "if (masterOnline && SeatHireCancelled(bot, master, pending))\n        return true;",
            "if (!masterOnline && HireNeedsSeatRecheck(pending.seatMode, pending.boundSeatJoined))",
            'CancelBoundSeatHire(pending, "you went offline before it arrived");\n        return true;',
            "ProvisionHeavy(bot, pending, ai,",
            "PlayerConvenience::Instance().IsBusy(pending.botGuid)",
            "if (SeatHireCancelled(bot, master, pending))\n            return true;",
            "intoBoundRaid ? !BotCommands::JoinBoundRaid(master, bot) : !Reunite(bot, master)",
            "pending.boundSeatJoined = true;",
            "MarkGrouped(pending.botGuid)",
            "provisioned while master offline",
        ]
        at = [prov.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        check = body(HIRE_SRC, "bool HireProvisionService::SeatHireCancelled(", "\n}\n")
        order = [
            "if (!HireNeedsSeatRecheck(pending.seatMode, pending.boundSeatJoined))\n        return false;",
            "BotCommands::BoundRaidOf(master)",
            "now.owned = CountHired(master);",
            "RecheckBoundSeat(pending.seatGrant, now)",
            "if (check.verdict != BoundSeatVerdict::Cancel)\n        return false;",
            "CancelBoundSeatHire(pending, check.reason);\n    return true;",
        ]
        at = [check.index(part) for part in order]
        self.assertEqual(at, sorted(at))

    def test_every_pending_drop_cancels_a_seat_hire(self):
        update = body(HIRE_SRC, "void HireProvisionService::Update(uint32_t diff)", "\n}\n")
        self.assertEqual(update.count("if (HireNeedsSeatRecheck(it->seatMode, it->boundSeatJoined))"), 2)
        attempts = update.index("never joined master after 60 reunite attempts")
        self.assertLess(attempts, update.index('CancelBoundSeatHire(*it, "it could not reach the bound raid");'))
        self.assertLess(update.index('CancelBoundSeatHire(*it, "it could not reach the bound raid");'),
                        update.index("it = m_pending.erase(it);", attempts))
        offline = update.index("if (!master || !master->IsInWorld())")
        self.assertLess(offline, update.index('CancelBoundSeatHire(*it, "you went offline before it arrived");'))
        stale = body(HIRE_SRC, "void HireProvisionService::DropStalePending()", "\n}\n")
        self.assertLess(stale.index("CancelBoundSeatHire(*it,"), stale.index("it = m_pending.erase(it);"))

    def test_hire_records_the_authorization(self):
        hire = body(HIRE_SRC, "HireOutcome HireProvisionService::Hire(", "uint32_t HireProvisionService::QuoteHireFee(")
        order = [
            "pending.seatMode = HireSeatModeFor(intoBoundRaid);",
            "pending.seatGrant.commanderGuid = masterGuid.GetRawValue();",
            "pending.seatGrant.leadGuid = boundRaid.leadGuid;",
            "pending.seatGrant.groupId = boundRaid.groupId;",
            "pending.cost = cost;",
            "m_pending.push_back(pending);",
        ]
        at = [hire.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        of = body(CMD_SRC, "BoundRaidFacts BoundRaidOf(Player* commander)", "\n}\n")
        self.assertIn("facts.leadGuid = binding->leadGuid;", of)
        self.assertIn("facts.groupId = binding->groupId;", of)

    def test_a_release_cancels_one_mustering_seat_hire(self):
        cancel = body(HIRE_SRC, "void HireProvisionService::CancelMusteringSeatHire(", "\n}\n")
        self.assertIn("pending.botGuid == botGuid && HireNeedsSeatRecheck(pending.seatMode, pending.boundSeatJoined)", cancel)
        self.assertIn("CancelBoundSeatHire(pending, why);", cancel)
        self.assertNotIn("erase", cancel)
        mustering = body(HIRE_SRC, "bool HireProvisionService::IsMusteringSeatHire(", "\n}\n")
        self.assertIn("return HireNeedsSeatRecheck(pending.seatMode, pending.boundSeatJoined);", mustering)
        # the binding is gone before EndCommand runs, so the release's cancel and trim are not skipped
        release = body(CMD_SRC, "uint32 ReleaseBinding(uint64 commanderRaw, RemoteCommander::ReleaseReason reason, bool notify)", "\n}\n")
        self.assertLess(release.index("CommanderBindings().Unbind(commanderRaw);"),
                        release.index("HireLifecycle::Instance().EndCommand(::ObjectGuid(commanderRaw));"))

    def test_cancel_is_the_failed_hire_path(self):
        cancel = body(HIRE_SRC, "void HireProvisionService::CancelBoundSeatHire(", "\n}\n")
        order = [
            "if (!HireLifecycle::Instance().IsHired(pending.botGuid))",
            "cancel.state.deletionQueued = HireLifecycle::Instance().DismissForRefund(pending.botGuid, kSeatCancelReason);",
            "if (!SettleSeatCancel(cancel))\n        m_seatCancels.push_back(cancel);",
        ]
        at = [cancel.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        # no gold moves here: only the policy's Refund step pays
        for forbidden in ("ModifyMoney", "SetMoney", "RefundSeatCancel", "DismissNow(", "Reunite", "EnsureGrouped"):
            self.assertNotIn(forbidden, cancel)
        settle = body(HIRE_SRC, "bool HireProvisionService::SettleSeatCancel(", "\n}\n")
        self.assertIn("switch (NextSeatCancelAction(cancel.state))", settle)
        self.assertLess(settle.index("RefundSeatCancel(cancel);"), settle.index("cancel.state.refunded = true;"))
        self.assertEqual(settle.count("RefundSeatCancel("), 1)
        refund = body(HIRE_SRC, "void HireProvisionService::RefundSeatCancel(", "\n}\n")
        self.assertIn("master->ModifyMoney(static_cast<int32>(cancel.cost));\n            master->SaveToDB();", refund)
        self.assertIn(".SetMoney(cancel.cost)", refund)
        sweep = body(HIRE_SRC, "void HireProvisionService::SweepSeatCancels()", "\n}\n")
        order = ["if (NextSeatCancelAction(it->state) == SeatCancelAction::RetryDeletion)", "++it->state.attempts;",
                 "HireLifecycle::Instance().EnsureDeletionIntent(it->botGuid, kSeatCancelReason)",
                 "if (SettleSeatCancel(*it))", "it = m_seatCancels.erase(it);"]
        at = [sweep.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        self.assertNotIn("RefundSeatCancel", sweep)
        update = body(HIRE_SRC, "void HireProvisionService::Update(uint32_t diff)", "\n}\n")
        self.assertLess(update.index("SweepSeatCancels();"), update.index("if (m_pending.empty())"))

    def test_the_refund_waits_for_durable_deletion_intent(self):
        refund = body(LIFECYCLE, "bool HireLifecycle::DismissForRefund(", "\n}\n")
        self.assertIn("if (it == m_hired.end())\n        return false;", refund)
        self.assertIn('return Dismiss(record, reason ? reason : "released", false);', refund)
        dismiss = body(LIFECYCLE, "bool HireLifecycle::Dismiss(HiredRecord const& record", "\n}\n")
        self.assertIn("deletionDurable = QueueDeletion(botGuidLow, characterAccountId,", dismiss)
        self.assertIn("return deletionDurable;", dismiss)
        self.assertIn("if (!m_dismissing.insert(botGuidLow).second)\n        return false;", dismiss)
        queue = body(LIFECYCLE, "bool HireLifecycle::QueueDeletion(", "\n}\n")
        self.assertIn("bool const durable = CharacterDatabase.DirectPExecute(", queue)
        self.assertIn("return durable;", queue)
        self.assertIn("if (!ledgerAccountId)", queue)
        retry = body(LIFECYCLE, "bool HireLifecycle::EnsureDeletionIntent(", "\n}\n")
        order = ["if (m_hired.find(botGuid.GetCounter()) != m_hired.end())\n        return false;",
                 "if (LedgerRow(botGuid, characterAccountId))\n        return QueueDeletion(",
                 "SELECT COUNT(*) FROM `characters` WHERE `guid` = '%u'",
                 "return count && count->Fetch()[0].GetUInt32() == 0;"]
        at = [retry.index(part) for part in order]
        self.assertEqual(at, sorted(at))

    def test_release_dismisses_the_surplus_before_hires_go_back(self):
        end = body(LIFECYCLE, "uint32_t HireLifecycle::EndCommand(ObjectGuid commanderGuid)", "\n}\n")
        order = ["if (hires.empty())", "DismissSurplusAfterBinding(commanderGuid);", "for (ObjectGuid const& guid : hires)",
                 "continue; // dismissed meanwhile", "Reunite(record, master);"]
        at = [end.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        surplus = body(LIFECYCLE, "void HireLifecycle::DismissSurplusAfterBinding(", "\n}\n")
        order = ["if (BotCommands::HasCommanderBinding(commanderGuid.GetRawValue()))\n        return;",
                 "hire.musteringSeat = provision.IsMusteringSeatHire(kv.second.botGuid);",
                 "HireMaxCompanions(sPlayerbotAIConfig.hireMaxBotsPerPlayer, false)",
                 "PlanBindingRelease(hires, cap)",
                 "for (size_t index : plan.cancel)",
                 "CancelMusteringSeatHire(owned[index],",
                 "for (size_t index : plan.dismiss)",
                 'DismissNow(guid, "bound raid ended above the companion limit");', "ChatHandler(master).PSendSysMessage("]
        at = [surplus.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        claim = body(LIFECYCLE, "bool HireLifecycle::Claim(", "\n}\n")
        self.assertLess(claim.index("record.hireOrder = ++m_nextHireOrder;"), claim.index("m_hired[botGuidLow] = record;"))
        # every binding release reaches EndCommand
        release = body(CMD_SRC, "uint32 ReleaseBinding(uint64 commanderRaw, RemoteCommander::ReleaseReason reason, bool notify)", "\n}\n")
        self.assertIn("HireLifecycle::Instance().EndCommand(::ObjectGuid(commanderRaw));", release)

    def test_the_bound_raid_limit_is_the_binding_size(self):
        self.assertIn("kHireBoundRaidMaxCompanions == RemoteCommander::kMaxRaidBindSize", HIRE_SRC)
        self.assertIn("constexpr uint32_t kHireBoundRaidMaxCompanions = 40;", POLICY)

    def test_players_keep_the_clamp_of_39(self):
        self.assertIn("if (hireMaxBotsPerPlayer > 39)\n        hireMaxBotsPerPlayer = 39;", CONFIG)

    def test_join_goes_through_the_binding_and_never_unbinds(self):
        join = body(CMD_SRC, "bool JoinBoundRaid(Player* commander, Player* bot)", "\n}\n")
        order = [
            "BeginCommand(botGuid, commander->GetObjectGuid())",
            "JoinLeadGroup(lead, bot)",
            "hires.CancelCommand(botGuid);",
            "hires.ConfirmCommand(commander->GetObjectGuid());",
            "PlaceJoinerInRaidLayout(group, lead, bot, chat.get());",
            "FormUpOnLead(commander, lead, std::vector<Player*>{ bot }, false);",
            "GiveCommanderThreatBackoff(bot);",
            "HoldCommanderAutonomy(bot,",
            "bot->TeleportTo(",
        ]
        at = [join.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        # nobody else moves: the join never runs the full plan again
        for forbidden in ("EndCommand", "ReleaseBinding", "StandingBinding", "Bind(binding)", "ClearRaidLayout",
                          "ApplyRaidLayout", "CommandedBots", "SwapMembersGroup"):
            self.assertNotIn(forbidden, join)
        # the commander inside the group, a full raid and a settled-out bot are not joins
        self.assertIn("group->IsMember(commander->GetObjectGuid())", join)
        self.assertIn("group->GetMembersCount() >= RemoteCommander::kMaxRaidBindSize", join)
        self.assertIn("!bot->IsInWorld() || bot->IsBeingTeleported()", join)

    def test_cancel_command_touches_one_hire_in_transition(self):
        cancel = body(LIFECYCLE, "void HireLifecycle::CancelCommand(", "\n}\n")
        self.assertIn("m_hired.find(botGuid.GetCounter())", cancel)
        self.assertIn("commandState == HireCommandState::Transition", cancel)
        self.assertIn("commandState = HireCommandState::None;", cancel)
        self.assertNotIn("for (", cancel)

    def test_the_joiner_alone_is_placed_by_the_policy(self):
        place = body(CMD_SRC, "static void PlaceJoinerInRaidLayout(", "\n}\n")
        order = [
            "RaidLayouts().find(group->GetId())",
            "TortoiseBots::RaidLayout::JoinFacts facts;",
            "applied.originalSubgroups.push_back({botGuid, before});",
            "TortoiseBots::RaidLayout::PlanJoinerSubgroup(facts)",
            "group->ChangeMembersGroup(bot, seat.subgroup);",
            "group->SetAssistant(botGuid, true);",
            "applied.assistants.push_back(botGuid);",
            "TortoiseBots::RaidLayout::PickJoinerHealTank(perTank, raidHealers)",
            "applied.tankHealers.emplace_back(bot->GetName(), tankName);",
            "applied.focusHealers.push_back(record);",
        ]
        at = [place.index(part) for part in order]
        self.assertEqual(at, sorted(at))
        # its role is read from the bot, as the bind's plan reads it
        self.assertIn("facts.role = PlayerbotAI::IsTank(bot) ? TortoiseBots::RaidLayout::Role::Tank", place)
        self.assertIn(": PlayerbotAI::IsHeal(bot) ? TortoiseBots::RaidLayout::Role::Healer", place)
        # only the new bot moves: one subgroup change, never a swap or a plan
        self.assertEqual(place.count("ChangeMembersGroup("), 1)
        for forbidden in ("SwapMembersGroup", "RaidLayout::Plan(", "PlanSubgroupMoves", "SetMainTank", "ApplyRaidLayout"):
            self.assertNotIn(forbidden, place)
        # a raid healer keeps the dashboard's list; a tank is an off-tank
        self.assertIn("applied.raidHealers.push_back(bot->GetName());", place)
        self.assertIn("applied.offTanks.push_back(bot->GetName());", place)

    def test_the_bind_keeps_its_full_plan(self):
        bind = body(CMD_SRC, "static bool HandleCommanderBind(", "\n}\n")
        self.assertIn("ApplyRaidLayout(group, lead, bots, handler, commanderRaw);", bind)
        layout = body(CMD_SRC, "static void ApplyRaidLayout(Group* group, Player* lead, std::vector<Player*> const& bots, ChatHandler* handler, uint64_t commanderRaw)\n{",
                      "static void RestoreRaidFocusHealer(Player* bot, RaidFocusHealer const& record)\n{")
        self.assertIn("RaidLayoutApplied applied;\n    applied.hadMainTank = ", layout)
        self.assertNotIn("replan", layout)

    def test_the_query_is_declared_for_the_hire_service(self):
        header = (ROOT / "runtime/CommandedGroupQuery.h").read_text()
        self.assertIn("BoundRaidFacts BoundRaidOf(Player* commander);", header)
        self.assertIn("bool JoinBoundRaid(Player* commander, Player* bot);", header)
        self.assertIn('#include "CommandedGroupQuery.h"', HIRE)


if __name__ == "__main__":
    unittest.main()
