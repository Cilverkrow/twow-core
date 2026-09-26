# Stat weights per class × talent tree (twow-repo#308, "checkup system")

**Status: DRAFT v1 for owner review. Not read by any code yet.**
After approval, it becomes the shared scoring basis for equipping (#308), quest reward selection (#284) and group loot rolls (#341).

## Files
- `stat-weights-v1.tsv`: 27 rows (9 classes × 3 premade specs, numbering as `AiPlayerbot.PremadeSpecName.<class>.<specno>`).
- `proficiency-v1.tsv`: preferred and wearable armour types, weapons, off hand and ranged per class and level band.

## Units (important for reading the table)
Weights apply **within one spec** (not between specs). Main stat = 100.
- `str agi sta int spi ap rap spell_power healing mp5 defense block_value armor`: **per point** on the item.
- `hit_pct crit_pct spell_hit_pct spell_crit_pct dodge_pct parry_pct block_pct`: **per 1 %** (classic items state "+1 % chance to …").
- `melee_dps ranged_dps`: **per 1 weapon DPS**. Only for the slot the weapon belongs to. Feral druids get 0 (in classic, forms ignore weapon DPS).
- `spell_school`: which school-specific spell power counts in full (other schools 0).

Example (paladin, owner direction): 10 str / 3 sta.
- Retribution: 10×100 + 3×50 = 1150
- Protection: 10×50 + 3×100 = 800

And 3 str / 15 sta:
- Retribution: 3×100 + 15×50 = 1050
- Protection: 3×50 + 15×100 = 1650

So tanks take stamina and Retribution takes strength.

## Principles
- **Solo levelling:** stamina is weighted higher than in end-game guides for all DPS and healer specs (bots level alone; deaths are the biggest brake, #307).
- **Order:** the order per spec follows the usual classic 1.12 stat priority. The values are a proposal, not a simulation result.
- **Armour type:** from the level band's `preferred_armor` onwards, only items of that type count as upgrades. Exception: the lower type wins clearly on score (threshold is set during implementation).

## Sources and open checks
- `source=classic-1.12-consensus`: generally known classic stat priorities, adapted for levelling. **Links to a concrete guide per spec are added during review.**
- **Turtle WoW 1.18:** changed talents and new items (e.g. shaman, druid) are **not yet verified**. The affected rows are marked in `notes`.
- **Existing table:** `sql/world/classic/ai_playerbot_weightscales.sql` has WotLK ratings, death knight rows, and no stamina for most DPS specs (mage/warlock int = 1). It is replaced, not extended.
