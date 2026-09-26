# Stat priorities per class × talent tree (twow-repo#308, "checkup system")

**Status: v2, approved by the owner on 2026-09-26 (via OB-00, #308/#319). No code reads this table yet.**
After approval, it becomes the shared scoring basis for equipping (#308), quest reward selection (#284) and group loot rolls (#341).

## Files
- `stat-priorities-v2.tsv`: 28 specs. These are the 27 premade specs (`AiPlayerbot.PremadeSpecName.<class>.<specno>`) plus the new **bear** (druid 11.3).
- `proficiency-v1.tsv`: preferred and wearable armour types, weapons, off hand and ranged per class and level band.

## Model (owner decision 2026-09-26)
Each spec scores an item **only by its 4–5 best stats in a fixed order** (`prio1` … `prio5`). Other stats count 0.

Stamina is included only where it matters for that spec. It is not weighted higher across the board, because almost every item carries it anyway. The primary stats decide.

| Rank | prio1 | prio2 | prio3 | prio4 | prio5 |
|---|---|---|---|---|---|
| Weight per unit | 100 | 80 | 60 | 40 | 20 |

**Units** (so that one "unit" is comparable across stats):

| Stat | 1 unit = |
|---|---|
| `str agi sta int spi defense` | 1 point |
| `ap rap` | 2 points |
| `spell_power healing` (school in `spell_school` or all schools) | 1 point |
| `mp5` | 0.5 mana per 5 s (i.e. 1 mp5 = 2 units) |
| `hit_pct crit_pct spell_hit_pct spell_crit_pct dodge_pct block_pct` | 0.1 % (i.e. 1 % = 10 units) |
| `armor` | 10 points of armour above the item-type baseline |

**Example** (paladin, owner direction): 10 str / 3 sta vs 3 str / 15 sta.
- Retribution (str rank 1, sta rank 4): 10×100 + 3×40 = **1120** vs 3×100 + 15×40 = 900 → takes **strength**.
- Protection (sta rank 4, str rank 5): 10×20 + 3×40 = 320 vs 3×20 + 15×40 = **660** → takes **stamina**.

**Weapons** (`weapon_dps` = melee/ranged): for this slot, weapon DPS comes first, and the stat score only decides between weapons of similar DPS (threshold is set during implementation). Casters (`none`) and feral forms ignore weapon DPS.

## Consequences of the bear/cat split (owner decision 2026-09-26)
1. **Talent path:** the new premade path `11.3 = bear` must be created in `tools/build_premade_specs.py`:
   - `BUILDS[11]` gets a 4th entry `('bear', …)`;
   - `NEW_PATHS` gets `(11, 'bear')`;
   - `EXPECTED_TREE_PAGE[11]['bear'] = 1` and `EXPECTED_NEW_TREE_TAB`.

   It is generated for `Rate.Talent = 2` with 102 legal points and validated against the Turtle DBCs like core#70, then entered in `aiplayerbot.conf.dist.in` (`PremadeSpecName/Prob/Link.11.3.*`).
2. **Existing path:** `11.1 feral` becomes the cat path (name stays `feral`, stats as cat).
3. **Roster distribution (owner decision 2026-09-26: cat and bear 50/50, because there are too few tanks):** `SelectPremadeSpecNo` rolls weighted over `PremadeSpecProb`; `11.1 = 50` and `11.3 = 50` keep the old feral share. The 102-point feral path already fills the whole feral tree (47/47), so 11.3 can use the same link.
4. **Existing roster bots** keep their stored `specNo` (`KeepStoredSpecNo`). Today's ferals stay cats; bears only appear at the next roster reset or a new roll. **For the next roster/reset topic:** decide the druid bear share and generate the roster accordingly.
5. **Bot strategies:** `AiFactory.cpp` does not decide bear vs cat by the premade name. For the feral tree (tab 1) it plays **bear (`tank feral`)** when the role is forced to tank, or when the bot knows **Primal Fury (16958/16961)**; otherwise it plays cat.

   **Checked (2026-09-26, Turtle DBCs):** in Turtle, Primal Fury is the talent ranks **45719/45720**. The `feral` path takes it 2/2 from level 25. The classic IDs 16958/16961 are not taught by any talent, so free-roaming ferals always play cat today.

   Therefore bear vs cat must be decided **by the premade spec** (11.3 → `tank feral`), not by spell IDs. The cat path also takes Primal Fury.

## Sources and open checks
- `source`:
  - `owner-2026-09-25/26`: priority set directly by the owner.
  - `classic-1.12-consensus`: generally known classic stat priority. `guide_link = tbd` is filled in per spec after review.
- **Turtle WoW 1.18:** changed talents and new items are **not yet verified**. Affected rows are marked in `notes`; the owner clarifies them with us.
- **Old table:** `sql/world/classic/ai_playerbot_weightscales.sql` (WotLK ratings, death knight rows, mage/warlock int = 1) is **removed** (owner decision 2026-09-26). The removal and the migration come with the scoring engine, not in this data PR.
