---
id: server-settings-economy
title: Economy & auction house settings
category: server
summary: Configuration settings for the simulated auction house market, vendor trips, and bot economy.
tags: [config, economy, ah, auction-house, vendor]
relates_to:
  - world-economy
  - world-social
  - server-settings-world-population
---

# Economy & Auction House Settings

These settings configure the simulated bot economy, including the native Auction House market service, vendor selling thresholds, bot loot drop adjustments, and travel mount cost-benefit rules. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

For player trading permissions with open-world bots, see [`AiPlayerbot.PoolBotTradeMode`](world-population.md#aiplayerbotpoolbottrademode). Administrative auction house commands (`.bot ah` / `.ahbot`) are documented in [Admin commands](../admin-commands.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AhMarketEnabled`](#aiplayerbotahmarketenabled) | `1` | Enables bots posting surplus items and bidding from their own bags on the Auction House. |
| [`AiPlayerbot.AhMarketInterval`](#aiplayerbotahmarketinterval) | `120` | Interval in seconds between seller ticks that attempt bot auction postings. |
| [`AiPlayerbot.AhMarketBatchSize`](#aiplayerbotahmarketbatchsize) | `1` | Maximum bots (one listing each) attempted per seller tick (effective 1..5). |
| [`AiPlayerbot.AhMarketBuyer`](#aiplayerbotahmarketbuyer) | `1` | Pool bots standing at an auctioneer bid on and buy auctions with their own gold. |
| [`AiPlayerbot.AhMarketBudgetUs`](#aiplayerbotahmarketbudgetus) | `2000` | Microsecond time budget ceiling per world tick for the organic buyer scan. |
| [`AiPlayerbot.AhMarketMaxOperations`](#aiplayerbotahmarketmaxoperations) | `32` | Maximum organic buyer scan work steps processed per tick. |
| [`AiPlayerbot.AhMarketChanceBuy`](#aiplayerbotahmarketchancebuy) | `10` | Percentage chance per cycle to scan listings for organic buyer bids. |
| [`AiPlayerbot.AhMarketBuyValue`](#aiplayerbotahmarketbuyvalue) | `80` | Required perceived value percentage before a bot purchases an auction. |
| [`AiPlayerbot.AhMarketMaxSpendPerBot`](#aiplayerbotahmarketmaxspendperbot) | `0` | Maximum copper for a single organic buyer purchase (0 = no cap). |
| [`AiPlayerbot.AhMarketVariance`](#aiplayerbotahmarketvariance) | `10` | Percentage price variance applied to organic buyer baseline valuations. |
| [`AiPlayerbot.AhBot.Enabled`](#aiplayerbotahbotenabled) | `0` | Master toggle for the offline AHBot service (stocks houses toward Min/Max targets). |
| [`AiPlayerbot.AhBot.MinutesBetweenSellCycle`](#aiplayerbotahbotminutesbetweensellcycle) | `1` | Minutes between AHBot sell cycles (`N` or `min:max`). |
| [`AiPlayerbot.AhBot.MinutesBetweenBuyCycle`](#aiplayerbotahbotminutesbetweenbuycycle) | `1` | Minutes between AHBot buy cycles (`N` or `min:max`). |
| [`AiPlayerbot.AhBot.ItemsPerCycle`](#aiplayerbotahbotitemspercycle) | `150` | Maximum new listings added per enabled auction house in one sell cycle. |
| [`AiPlayerbot.AhBot.ListingExpireTimeInSecondsMin` / `Max`](#aiplayerbotahbotlistingexpiretimeinsecondsmin--max) | `900` / `86400` | Duration range in seconds for AHBot listings (bounds `900..172800`). |
| [`AiPlayerbot.AhBot.Alliance.MinItems` / `MaxItems`](#aiplayerbotahbotallianceminitems--maxitems) | `3000` / `3000` | Minimum and maximum listing targets for the Alliance auction house (`0` disables). |
| [`AiPlayerbot.AhBot.Horde.MinItems` / `MaxItems`](#aiplayerbotahhordeminitems--maxitems) | `3000` / `3000` | Minimum and maximum listing targets for the Horde auction house (`0` disables). |
| [`AiPlayerbot.AhBot.Neutral.MinItems` / `MaxItems`](#aiplayerbotahbotneutralminitems--maxitems) | `3000` / `3000` | Minimum and maximum listing targets for the Neutral auction house (`0` disables). |
| [`AiPlayerbot.AhBot.Buyer.Enabled`](#aiplayerbotahbotbuyerenabled) | `0` | Enables the AHBot buyer to bid on and buy out non-AHBot auctions. |
| [`AiPlayerbot.AhBot.Buyer.BuyCandidatesPerBuyCycle`](#aiplayerbotahbotbuyerbuycandidatesperbuycycle) | `1` | Candidate auctions evaluated per enabled house each buy cycle (`N` or `min:max`). |
| [`AiPlayerbot.AhBot.Buyer.AlwaysBidMaxCalculatedPrice`](#aiplayerbotahbotbuyeralwaysbidmaxcalculatedprice) | `0` | Bids the full calculated fair value instead of the minimum next bid. |
| [`AiPlayerbot.AhBot.Buyer.PreventOverpayingForVendorItems`](#aiplayerbotahbotbuyerpreventoverpayingforvendoritems) | `1` | Prevents the AHBot buyer from paying above vendor sell price on vendor items. |
| [`AiPlayerbot.AhBot.Buyer.BidAgainstPlayers`](#aiplayerbotahbotbuyerbidagainstplayers) | `0` | Allows the AHBot buyer to outbid auctions that already have a bid. |
| [`AiPlayerbot.AhBot.RespectContentPhase`](#aiplayerbotahbotrespectcontentphase) | `1` | Hides items whose only sources, recipes, or reagents belong to locked raids or content phases. |
| [`AiPlayerbot.AhBot.TickBudgetUs` / `MaxListingsPerTick`](#aiplayerbotahbottickbudgetus--maxlistingspertick) | `2000` / `10` | Per-tick microsecond budget and operation cap for AHBot sell/buy/cleanup work. |
| [`AiPlayerbot.AuctionPriceRefreshInterval`](#aiplayerbotauctionpricerefreshinterval) | `60` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) | Seconds between market price evaluation sweeps. |
| [`AiPlayerbot.AhSellBlacklist`](#aiplayerbotahsellblacklist) | `18256` | Item ids bots never post to the auction house. |
| [`AiPlayerbot.VendorBatchMinCount` / `VendorBatchMinBagSpace`](#aiplayerbotvendorbatchmincount--vendorbatchminbagspace) | `8` / `60` | Inventory thresholds that trigger vendor selling trips for level 5+ bots. |
| [`AiPlayerbot.LowLevelVendorBatchMinCount` / `LowLevelVendorBatchMinBagSpace`](#aiplayerbotlowlevelvendorbatchmincount--lowlevelvendorbatchminbagspace) | `3` / `25` | Inventory thresholds that trigger vendor selling trips for sub-level 5 bots. |
| [`AiPlayerbot.LowLevelVendorMaxDistance`](#aiplayerbotlowlevelvendormaxdistance) | `600` | Maximum yard radius sub-level 5 bots travel to reach camp vendors. |
| [`AiPlayerbot.BotLootRateUncommon` / `BotLootRateRare`](#aiplayerbotbotlootrateuncommon--botlootraterare) | `1.0` / `1.0` (both built in; the keys are not in the template — add them to `aiplayerbot.conf` to change them) | Multipliers boosting green and blue item drop rates for pool bots. |
| [`AiPlayerbot.MountBreakEvenFactor`](#aiplayerbotmountbreakevenfactor) | `1.5` | Safety factor applied to distance calculations before bots mount up. |
| [`AiPlayerbot.PoolGearTopUpOnLevel`](#aiplayerbotpoolgeartopuponlevel) | `0` | Opt-in: fill a pool bot's empty or outdated gear slots when it levels up. |
| [`AiPlayerbot.PoolMoneyTopUp`](#aiplayerbotpoolmoneytopup) | `0` | Opt-in: top a broke pool bot's purse up to its next trainer visit when it levels up. |

## Settings

### `AiPlayerbot.AhMarketEnabled`
Enables the autonomous Auction House market service for pool bots.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, random pool bots with AH-profitable surplus items in their bags post them to the Auction House using native auction listings.

This system requires `AiPlayerbot.RandomBotAutologin = 1`. Failed auction attempts are rate-limited per bot to prevent spam.

</details>

### `AiPlayerbot.AhMarketInterval`
Interval in seconds between seller ticks that attempt bot auction postings.
**Default:** `120` · **Values:** seconds (5..3600) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how frequently the seller loop runs its posting pass for eligible bots.

</details>

### `AiPlayerbot.AhMarketBatchSize`
Maximum bots (one listing each) attempted per seller tick.
**Default:** `1` · **Values:** integers (effective 1..5; 0 is treated as 1) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the number of eligible seller bots attempted per seller tick (0 is treated as 1 and values above 5 are capped at 5), with each chosen bot posting one item listing, spreading auction load across server ticks.

</details>

### `AiPlayerbot.AhMarketBuyer`
Pool bots standing at an auctioneer bid on and buy auctions with their own gold.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Simulates demand by having bots buy sensibly priced player auctions. When a purchase happens, the bid or buyout is placed through native auction handling.

Purchases are organic: only bots already standing at an auctioneer bid, and only with their own gold.

</details>

### `AiPlayerbot.AhMarketBudgetUs`
Microsecond execution budget allocated per world tick for the organic buyer scan.
**Default:** `2000` · **Values:** microseconds (100..20000) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the CPU time spent on the organic buyer scan within any single server tick. The service yields work when this budget is reached, preventing auction processing from causing world tick latency.

</details>

### `AiPlayerbot.AhMarketMaxOperations`
Maximum number of organic buyer scan work steps processed during a single server tick.
**Default:** `32` · **Values:** operations (1..256) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Places a hard ceiling on organic buyer scan work steps handled per tick, even if time remains within `AhMarketBudgetUs`.

</details>

### `AiPlayerbot.AhMarketChanceBuy`
Percentage chance per engine cycle that the organic buyer enters a house scan.
**Default:** `10` · **Values:** percentage (0..100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls how likely the organic market service is to enter the buyer scan when `AiPlayerbot.AhMarketBuyer` is on.

</details>

### `AiPlayerbot.AhMarketBuyValue`
Perceived value percentage required before a bot commits to buying an auction.
**Default:** `80` · **Values:** percentage (0..200; values above 200 are clamped to 200; `0` = never buys) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A higher threshold makes the buyer more discerning, requiring listings to be priced favorably relative to fair unit value and the market median.

</details>

### `AiPlayerbot.AhMarketMaxSpendPerBot`
Maximum copper amount for a single organic buyer bid or buyout.
**Default:** `0` · **Values:** copper amount (0 = no cap) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Refuses any single bid or buyout above this price; it does not track cumulative spend.

</details>

### `AiPlayerbot.AhMarketVariance`
Percentage price variance applied to organic buyer baseline market valuations.
**Default:** `10` · **Values:** percentage (0..100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Introduces randomized price fluctuations above and below baseline item valuations when the organic buyer estimates fair value.

</details>

### `AiPlayerbot.AhBot.Enabled`
Master switch for the offline AHBot stock and buyer service.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed (or `.bot ah reload` for pool/pricing/targets)

<details>
<summary>How it works in detail</summary>

When enabled, the service maintains dedicated offline seller characters, removes any leftover legacy owner-0 listings, removes its own unbid listings just before core expiry so no expired mail accumulates, and stocks each enabled auction house toward its configured minimum/maximum item counts.

- **Seller account:** created on first enable as `AiPlayerbot.AhBot.AccountName` (default `TBAHBOT`) with `AiPlayerbot.AhBot.SellersPerFaction` characters per faction. Its password is random on every start, its characters never log in, and every bot system (pool reset, cleanup, hire, alts, guilds, LFT, battlegrounds) ignores it. If the account or a seller is deleted, it is recreated within `AiPlayerbot.AhBot.SellerRepairIntervalSeconds`. Sale money that reaches the sellers by mail is deleted (a gold sink).
- **What gets listed:** every obtainable, tradeable item (never grey, Bind on Pickup, quest-bound, conjured, unreleased or sold by a vendor without a stock limit), picked by the `AiPlayerbot.AhBot.ListProportion.*` weights. The default weights give roughly 40% trade goods, 20% consumables, 13% armour, 8% weapons and 5% recipes, with few epics.
- **Prices:** vendor value times category and quality multipliers, calibrated to what bots on a live realm ask, with random variation and never below the vendor sell price.
- **Shared auction house:** with `AllowTwoSide.Interaction.Auction = 1` every auctioneer opens the neutral house, so only the neutral target applies.
- **PvP realms:** the seller account holds characters of both factions. On a PvP realm with `AllowTwoSide.Accounts = 0` the core refuses the second faction and the service stays off (logged once).

</details>

### `AiPlayerbot.AhBot.MinutesBetweenSellCycle`
Minutes to wait between AHBot sell cycles.
**Default:** `1` · **Values:** minutes (`N` or `min:max`, clamped to `1..1440`) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.MinutesBetweenBuyCycle`
Minutes to wait between AHBot buy cycles.
**Default:** `1` · **Values:** minutes (`N` or `min:max`, clamped to `1..1440`) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.ItemsPerCycle`
Maximum number of new listings posted per enabled auction house in one sell cycle.
**Default:** `150` · **Values:** non-negative integer · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.ListingExpireTimeInSecondsMin` / `Max`
Random duration range in seconds for newly posted AHBot listings.
**Default:** `900` / `86400` · **Values:** seconds (`900..172800`) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Alliance.MinItems` / `MaxItems`
Listing count thresholds for the Alliance auction house (house `1`).
**Default:** `3000` / `3000` · **Values:** integers (`MaxItems = 0` disables stocking that house) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Horde.MinItems` / `MaxItems`
Listing count thresholds for the Horde auction house (house `6`).
**Default:** `3000` / `3000` · **Values:** integers (`MaxItems = 0` disables stocking that house) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Neutral.MinItems` / `MaxItems`
Listing count thresholds for the Neutral auction house (house `7`, also used alone when cross-faction AH is enabled).
**Default:** `3000` / `3000` · **Values:** integers (`MaxItems = 0` disables stocking that house) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Buyer.Enabled`
Enables the AHBot buyer to evaluate and bid on or buy out non-AHBot listings.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** `.bot ah reload`

<details>
<summary>How it works in detail</summary>

Requires `AiPlayerbot.AhBot.Enabled = 1`. Evaluates auctions not owned by AHBot sellers using the configured fair-value pricing and places bids or buyouts through the core auction and mail paths.

</details>

### `AiPlayerbot.AhBot.Buyer.BuyCandidatesPerBuyCycle`
Number of candidate auctions evaluated per enabled house each buy cycle.
**Default:** `1` · **Values:** count (`N` or `min:max`, clamped to `1..1000`) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Buyer.AlwaysBidMaxCalculatedPrice`
Bids the buyer's full calculated fair value instead of the minimum next bid increment.
**Default:** `0` · **Values:** `0` minimum bid, `1` full fair value · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Buyer.PreventOverpayingForVendorItems`
Prevents the AHBot buyer from bidding or buying out non-trade-good vendor items above their vendor sell price.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.Buyer.BidAgainstPlayers`
Allows the AHBot buyer to outbid auctions that already have an active bid.
**Default:** `0` · **Values:** `0` unbid only, `1` may outbid existing bids · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.RespectContentPhase`
Hides items from the AHBot listing pool until their raid, seasonal event, recipe, or crafting reagents unlock in the realm's current content phase.
**Default:** `1` · **Values:** `1` respect realm content phase, `0` list everything regardless of phase · **Restart:** `.bot ah reload`

### `AiPlayerbot.AhBot.TickBudgetUs` / `MaxListingsPerTick`
Per-tick microsecond time budget and maximum listing/buyer/cleanup operations processed on one world tick.
**Default:** `2000` / `10` · **Values:** microseconds (`100..50000`) / operations (`1..150`) · **Restart:** `.bot ah reload`

### `AiPlayerbot.AuctionPriceRefreshInterval`
Interval in seconds between background auction price and market valuation updates.
**Default:** `60` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** seconds (5..3600) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls how frequently the auction price mirror reloads active listings used for market price averages and item valuations.

</details>

### `AiPlayerbot.AhSellBlacklist`
Item ids bots never post to the auction house.
**Default:** `18256` (Imbued Vial) · **Values:** comma-separated item ids · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots also never post items any vendor sells without a stock limit, even if other vendors limit the same item: any player can buy those at the vendor buy price, so no auction listing can be fair. Vendor-buy-to-flip uses the same rule, refusing the item whenever the visited vendor's own stock is unlimited. The blacklist covers anything else odd the owner finds and also blocks synthetic market supply.

</details>

### `AiPlayerbot.VendorBatchMinCount` / `VendorBatchMinBagSpace`
Inventory thresholds that trigger vendor selling trips for bots at level 5 or higher.
**Default:** `8` / `60` · **Values:** item count (no upper bound) / bag-space percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots at level 5 or higher travel to merchant NPCs to sell unwanted items once their inventory holds at least `VendorBatchMinCount` vendor-usable items and total bag capacity is at least `VendorBatchMinBagSpace` percent full.

Bots also initiate vendor trips early if selling their inventory would cover the gold required for their next trainable class ability rank.

</details>

### `AiPlayerbot.LowLevelVendorBatchMinCount` / `LowLevelVendorBatchMinBagSpace`
Inventory thresholds that trigger vendor selling trips for beginner bots below level 5.
**Default:** `3` / `25` · **Values:** item count (no upper bound) / bag-space percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Beginner bots carry small starter bags and collect only a few grey items before running out of space.

These lowered thresholds ensure low-level characters sell their loot and afford early class spells instead of hoarding items indefinitely.

</details>

### `AiPlayerbot.LowLevelVendorMaxDistance`
Maximum yard distance sub-level 5 bots may travel to reach a vendor.
**Default:** `600` · **Values:** yards (`0` disables beginner vendor trips) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

General roleplay travel is restricted below level 5 to prevent fragile characters from wandering into higher-level hostile creatures. Setting this to `0` disables beginner vendor trips.

This exception allows low-level bots to navigate to local vendors within their immediate starting camp, capped to this yard radius and restricted to starter area level bands.

</details>

### `AiPlayerbot.BotLootRateUncommon` / `BotLootRateRare`
Drop rate multipliers for green (uncommon) and blue (rare) items dropped for pool bots.
**Default:** `1.0` / `1.0` (both built in; the keys are not in the template — add them to `aiplayerbot.conf` to change them) · **Values:** float multiplier (1.0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Provides an optional loot boost for random pool bots to stimulate their equipment progression and supply the Auction House.

When a random pool bot kills a creature, uncommon and rare loot table entries roll an extra check scaled by this factor:

- Only the bot's own personal loot from its own kill. Quest items, quest starters, and unique items the bot already holds at `max_count` are skipped. Other qualities are untouched.
- When `bot_events.csv` logging is enabled, boost events are logged there as `BotLootBonus` rows.
- Real players and characters that are not random pool bots always use standard core drop rates.

A value of `1.0` disables the boost.

**Tip for a fresh realm where everyone starts at level 1:** bots mostly grind, so a value of `5` to `10` for both gives them some gear while they level and keeps the Auction House stocked. Pair it with the default bot experience rate ([`AiPlayerbot.XPRate`](world-population.md#aiplayerbotxprate), `3`) so they keep pace with players.

</details>

### `AiPlayerbot.MountBreakEvenFactor`
Safety multiplier applied to the break-even distance before bots mount up for travel.
**Default:** `1.5` · **Values:** float multiplier · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots compare the remaining travel distance against the break-even distance for their best usable mount here, scaled by this factor.

This multiplier adds a safety buffer to the calculated break-even distance. With the default factor of `1.5`, bots mount up when travel exceeds approximately 350 yards on a turtle mount (9% speed), 50 yards on a standard mount (60% speed), and 30 yards on an epic mount (100% speed).

Setting this to `0` removes the distance threshold, causing bots to mount for any movement.

</details>

### `AiPlayerbot.PoolGearTopUpOnLevel`
Fills empty and clearly outdated equipment slots when a masterless pool bot reaches a new level.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With the key on, a pool bot at level 5 or higher gets its empty equipment slots, and grey/white items 6 or more item levels below its level, filled with level- and spec-appropriate gear from the same selection the fresh-bot seed uses. Green or better gear is never replaced; the shirt and tabard slots are left alone. The top-up runs once per level (after a restart, once at the current level) and only out of combat. Hired, guild-claimed and player-grouped bots are never touched.

</details>

### `AiPlayerbot.PoolMoneyTopUp`
Tops a broke masterless pool bot's purse up to what its next class-trainer visit costs when it levels up.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With the key on, a pool bot that reaches a new level holding less than its next trainer visit costs (every trainable class rank plus the repair, ammo, auction-house and guild reserves the trainer trip keeps aside) is topped up to that amount, capped at 1 gold per bot level. A bot that already has the money gets nothing, and the top-up runs once per level. Hired, guild-claimed and player-grouped bots are never paid. Each top-up writes a `PoolMoneyTopUp` row to `bot_events.csv`.

</details>

## Removed settings

The legacy synthetic AH supply settings and the `ahbot_items` override table have been removed. Existing `aiplayerbot.conf` files containing these keys still load cleanly (unknown keys are ignored).

| Removed setting | Replacement |
| :--- | :--- |
| `AiPlayerbot.AhMarketSyntheticSupply` | `AiPlayerbot.AhBot.Enabled` (offline AHBot stock service, default `0`) |
| `AiPlayerbot.AhMarketChanceSell` | `AiPlayerbot.AhBot.MinutesBetweenSellCycle` + `AiPlayerbot.AhBot.ItemsPerCycle` + `AiPlayerbot.AhBot.<House>.MinItems`/`MaxItems` |
| `AiPlayerbot.AhMarketMaxQuality` | `AiPlayerbot.AhBot.ListProportion.Category*.Quality*` (set quality weights to `0` to exclude) |
| `AiPlayerbot.AhMarketMaxLevel` | `AiPlayerbot.AhBot.EquipItemUseOrEquipLevelRestrict.*` |
| `AiPlayerbot.AhMarketDynamicLevel` | None (replaced by fixed level/item-level filter config) |
| `AiPlayerbot.AhMarketLevelRefresh` | None (dynamic level sampler removed) |
| `AiPlayerbot.AhMarketBidMin` | `AiPlayerbot.AhBot.BidVariationLowReducePercent` |
| `AiPlayerbot.AhMarketBidMax` | `AiPlayerbot.AhBot.BidVariationHighReducePercent` |
| `AiPlayerbot.AhMarketTimeMin` | `AiPlayerbot.AhBot.ListingExpireTimeInSecondsMin` (seconds, default `900`) |
| `AiPlayerbot.AhMarketTimeMax` | `AiPlayerbot.AhBot.ListingExpireTimeInSecondsMax` (seconds, default `86400`) |
| `ahbot_items` table & `.bot ah item` / `rebuild` | `AiPlayerbot.AhBot.CompleteItemValueOverride.*`, `Disabled*`, `.bot ah reload` / `empty` / `update` |
