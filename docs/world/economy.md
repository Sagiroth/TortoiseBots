---
id: world-economy
title: Economy & auction house
category: world
summary: How bots earn gold, sell loot, repair gear, and trade on the living auction house.
tags: [world, economy, auction-house, vendors, gold, repair]
relates_to:
  - world-living-world
  - server-settings-economy
  - server-admin-commands
---

# Economy & auction house

This page explains how bots generate gold, repair their equipment, sell surplus items to vendors, and participate in the server economy. Auction house and trading settings live in `aiplayerbot.conf`; see [Economy & auction house settings](../server/settings/economy.md).

## Economic philosophy

TortoiseBots is built around a realistic player economy rather than handouts:

- **Looted and crafted goods:** Items listed on the Auction House come from creature drops, quest rewards, and bot crafting.
- **Earned gold:** Bots receive a level-scaled coin grant on first seed only (the seeder skips any bot already holding coin, so re-seeds never refill), then earn the rest from quests, kills, and vendor sales; bots short for trainer ranks sell loot first (see [vendor trip batching](#vendor-liquidation--repairs)).
- **Equipment progression:** Bots receive starter kit gear scaled to level on creation, then earn later equipment through world drops, quest rewards, professions, or vendor purchases. Seed-time gear generation is gated by [`AiPlayerbot.RandomGearMaxSourceTier`](../server/settings/world-population.md#aiplayerbotrandomgearmaxsourcetier).
- **Practical exceptions:** To keep gameplay working, class reagents and ammunition are topped up by the item cheat within bounded stacks (repeat vendor purchases stop past one held stack; the per-tick refill tops the equipped ammo stack), and profession implements are seeded with the starter kit ([Field kits](living-world.md#field-kits--equipment-upkeep)). Server admins may optionally enable uncommon and rare drop rate multipliers to compensate for lower bot kill volume.

## Vendor liquidation & repairs

Bots manage their personal finances by balancing earnings against repairs, training, and travel expenses:

| Activity | Behavior | Control |
| :--- | :--- | :--- |
| **Vendor sales** | Bots sell grey junk and surplus gear in batches to minimize unnecessary travel. | [`AiPlayerbot.VendorBatchMinCount`](../server/settings/economy.md#aiplayerbotvendorbatchmincount--vendorbatchminbagspace) |
| **Low-level vendor trips** | Fresh bots (below level 5) sell early items to nearby camp vendors within safe range. | [`AiPlayerbot.LowLevelVendorMaxDistance`](../server/settings/economy.md#aiplayerbotlowlevelvendormaxdistance) |
| **Spell money prioritization** | Bots short on gold for trainable spell ranks liquidate bags to fund training. | Automatic AI logic |
| **Equipment repairs** | Bots visit blacksmiths to repair when overall durability drops below 30% (or any item below 10%). | Automatic AI logic |
| **Inventory cleanup** | When bags fill and vendors are unreachable, bots destroy low-value items in strict order. | Automatic AI logic |

<details>
<summary>Vendor trip batching & low-level sales in detail</summary>

To prevent bots from walking to town after every kill, vendor trips are batched based on item volume and bag capacity:
- **Standard batching (level 5+):** Governed by [`AiPlayerbot.VendorBatchMinCount`](../server/settings/economy.md#aiplayerbotvendorbatchmincount--vendorbatchminbagspace) and `VendorBatchMinBagSpace`. A bot starts a vendor journey when its vendor-usable stock (at least one item a vendor actually buys) meets these thresholds. A bot short on gold for a trainable spell rank also heads to a vendor when its stock would cover the price, and bags at 85%+ fullness also trigger the errand.
- **Beginner batching (levels 1–4):** Governed by [`AiPlayerbot.LowLevelVendorBatchMinCount`](../server/settings/economy.md#aiplayerbotlowlevelvendorbatchmincount--lowlevelvendorbatchminbagspace) and `LowLevelVendorBatchMinBagSpace`. This lets new characters liquidate early pelts and broken weapons without waiting for full bags.
- **Bag-pressure line:** With stock a vendor buys, bags at 85%+ full trigger the vendor errand; empty food or drink bags trigger it only when the bot can pay to restock.
- **Camp distance limits:** Masterless bots below level 5 are restricted to nearby camp vendors within [`AiPlayerbot.LowLevelVendorMaxDistance`](../server/settings/economy.md#aiplayerbotlowlevelvendormaxdistance). If a vendor visit yields no valid sales, the errand is parked for ten minutes.
- **One stock list:** the vendor-trip need, the batch and spell-funding rule, the nearby-service sell verb and the errand sale all read the same list (`SellAction::VendorSaleStock`), and a completed sale clears the cached need values, so a bot does not run a second empty sale and park the errand.
- **Flip buys:** owned and hired bots still buy limited-supply stock to relist on the Auction House; bots on the item cheat seed their own stock, so they skip the flip. Recipes and gear are bought once per item per visit; stackable stock still buys up to 10. Hunters and gun users buy ammo only when they do not carry any (unless restocking), and item-cheat bots wait five minutes between ammo buys.

</details>

<details>
<summary><a id="protected-inventory-items--cleanup-order-in-detail"></a>Protected inventory items & cleanup order in detail</summary>

Autonomous sell routines evaluate inventory items carefully:
- **Protected goods:** The usage classifier, not a sell filter, protects rations: food and drink the bot eats, bandages, potions, class spell reagents, and ammunition matching the equipped ranged weapon are never classified as vendor trash, so autonomous sell routines never sell them. Usable food, drink, potions and bandages are kept at any item level (a lower tier becomes surplus only once the bot holds a full stack of a better tier), and class poisons, stones and weapon oils are kept until a higher tier of the same line is in the bags. Quest-class items are still destroyed in an emergency when the bot needs money for a spell rank and has nothing to sell; abandoned quest items fall through to normal handling by their new usage.
- **Liquidated stock:** Only grey trash items, unneeded profession materials, and surplus equipment scoring lower than equipped gear are sold (soulbound Bind-on-Equip gear already worn cannot be listed; repair cost above the auction profit keeps an item at the vendor).
- **Smart destruction order:** When bags reach 90% capacity and no vendor can be reached, bots destroy items in strict priority order: genuinely useless items first, then quest-class items only when coin is required for a spell rank and sales are impossible (bots with a real-player master or in a real guild destroy grey trash only), then cheapest grey trash, then profession materials and consumables last.

</details>

<details>
<summary>Mount savings & trainer reserves in detail</summary>

Bots budget gold carefully before spending on optional upgrades:
- **Trainer reserve:** When evaluating travel fares or vendor gear purchases, bots protect a reserve fund dedicated to upcoming class ability ranks (only free money above the spell purse is spendable). A trainer visit charges only ranks the bot can actually learn now: rows above its level are refused before any gold is taken, and the trip and the visit read the same spendable-money figure.
- **Crafting:** a bot crafts only when it holds every reagent in its bags (the item cheat does not conjure reagents), and each craft writes a `Craft` row to `bot_events.csv`.
- **Mount travel threshold:** Mount usage is gated by [`AiPlayerbot.MountBreakEvenFactor`](../server/settings/economy.md#aiplayerbotmountbreakevenfactor): only trips longer than the cast-time break-even distance trigger mounting (about 350 yd on the 9%-speed turtle, 50 yd at 60%, 30 yd at 100% with the default `1.5`); `0` removes the threshold.

</details>

## Living Auction House

Pool bots sell their own loot on the Auction House and buy upgrades there with their own gold, so the auction house fills up the way it would on a busy server. Selling is enabled by [`AiPlayerbot.AhMarketEnabled`](../server/settings/economy.md#aiplayerbotahmarketenabled) and needs logged-in pool bots ([`AiPlayerbot.RandomBotAutologin`](../server/settings/world-population.md#aiplayerbotrandombotautologin)):

```mermaid
flowchart TD
    Loot["Wandering Bots Loot Mobs & Craft Gear"] --> Appraise["Appraise Item Quality (Vendor Junk vs AH Surplus)"]
    Appraise --> Post["Post Native Auction House Listings (Deposit + Duration)"]
    Post --> Trade["Players & Other Bots Browse, Bid, and Buyout"]
    Trade --> Mail["Native Settlement: Gold Delivered to In-Game Mailbox"]
```

- **Bot sellers:** Bots list surplus items the usage classifier marks for auction (gear where the auction profit beats the vendor price after deposit and repair costs, profession materials, and crafted goods) at market-model prices with a native deposit.
- **Bot buyers:** Bots holding spendable gold scan the Auction House for equipment upgrades suited to their class and talent specialization, placing bids or buying out listings that beat equipped gear by the configured value threshold.
- **Native mail settlement:** All bot auction transactions use core mail systems. When a bot purchases an auction listed by a human player, gold is delivered directly to the player's in-game mailbox.
- **Getting to the auction house:** Bots walk to the auction house on their own continent (same map; cross-map listings never open a trip). A bot already on its way there, or within 300 yards of it, is moved the last stretch onto the auctioneer; nobody is teleported across the world to sell.
- **Settings:** Bots selling is [`AiPlayerbot.AhMarketEnabled`](../server/settings/economy.md#aiplayerbotahmarketenabled) and bots buying with their own gold is [`AiPlayerbot.AhMarketBuyer`](../server/settings/economy.md#aiplayerbotahmarketbuyer); both are on.
- Made-up listings for an empty auction house are [`AiPlayerbot.AhMarketSyntheticSupply`](../server/settings/economy.md#aiplayerbotahmarketsyntheticsupply), off by default.
- Chance, value, spend, quality, level, variance, bid, and duration tuning live in [Economy settings](../server/settings/economy.md).

<details>
<summary>Organic buyer mechanics in detail</summary>

The organic Auction House buyer runs without teleportation:
- Only masterless pool bots (never hires or alts) of level 10 or higher with spendable purse above the trainer reserve qualify: free money for general spending must cover at least 5 gold.
- Eligible bots walk or fly to the nearest auction house on the bot's own map (same-map house required; cross-map listings are unroutable, so no trip opens). One buyer trip per bot per 10 minutes, no teleporting.
- Upon arrival, the bot evaluates current listings matching its class and spec through the `rpg ah buy` action, placing bids using native funds.
- Rotating scans probe eight bots per listing pass to spread load across the pool. Grouped bots, LFT-queued/in-offer bots, and battleground/instance/dungeon bots are excluded from market errands.

</details>

## Market administration & drop tuning

Admins can monitor and tune economic activity:

- **Drop rate multipliers:** Admins can scale uncommon and rare item drop rates for pool bots using [`AiPlayerbot.BotLootRateUncommon`](../server/settings/economy.md#aiplayerbotbotlootrateuncommon--botlootraterare) and `BotLootRateRare` (default `1.0` = off).
- The extra roll runs once per kill on the creature's own loot table at `(multiplier - 1) x` each entry's chance.
- Quest-class or quest-starting items and unique items already held at `max_count` are never boosted.
- Player characters, hired/alt bots, and group loot (non-personal or unowned corpses) keep server rates.
