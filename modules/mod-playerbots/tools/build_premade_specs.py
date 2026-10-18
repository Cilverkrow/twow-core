#!/usr/bin/env python3
"""Generate the AiPlayerbot.PremadeSpec* block for aiplayerbot.conf.

The stock links that ship with the playerbot module are vanilla ones, and every
single one of them is rejected against Turtle's reworked talent trees - the log
says "Error with premade spec link" once per entry and "No premade specs found!!"
at the end, and the bots then run around with no talents at all.

The builds below were put together by hand in game. This script turns each of
them into a full set of config entries and validates them against Turtle's
Talent, TalentTab, and Spell DBCs.

Two things it takes care of that are easy to get wrong:

  Levelling path. ChangeTalentsAction::GetBestPremadeSpec hands out the first
  entry whose point total reaches the bot's, so with only a level 60 entry even
  a level 20 bot gets that one - and since TalentSpec applies points in tree
  order, it would spend its first points in the secondary tree. A feral druid
  built to tank would start out as a balance caster. Each build therefore gets
  an entry every five levels, filled along a learning order: main tree first by
  row and column, then the rest. Every intermediate level from 10 through 60
  is validated when using a higher rate; the Rate 1 output remains byte-for-
  byte compatible with the shipped configuration.

  Talent rate. The points a level grants are (level - 9) * Rate.Talent, so the
  links depend on the rate the server runs. Generated for the wrong one, every
  entry demands a higher level than it is filed under and the server rejects the
  lot. Pass --rate to match your mangosd.conf; the default matches the shipped
  Rate.Talent = 1. At a higher rate the level 60 entries simply carry more
  points, up to the full build.

  Spec identity (twow-repo#602 R06a, owner 2026-10-18). Every path carries
      AiPlayerbot.PremadeSpecTab.<class>.<specno> = <tab>
  directly after its PremadeSpecProb line: the Turtle TalentTab page (0, 1, 2)
  of the path's main tree, taken from EXPECTED_TREE_PAGE. The server reads the
  bot's spec from this key and never from the point majority of its current
  talents: at a higher rate the side trees can catch up with the main tree
  (see --check-levels), and a majority read would then flip the spec.

  Majority check (--check-levels). simulate_server() replays, for every level
  10..60, what the server does with the emitted links: the budget is
  int((level - 9) * Rate.Talent) minus the path's aura reserve,
  ChangeTalentsAction::GetBestPremadeSpec takes the first link (in level order)
  whose point total reaches the budget, else the last one, and
  TalentSpec::CropTalents cuts it down to the budget, largest tree first (equal
  trees in random order - every order is evaluated). Each level at which the
  expected tree is not strictly the largest is reported, with the verdict of
  the majority read TalentSpec::highestTree: sure, coin-toss, or tie (saved
  only by its lower-page rule).

  Side-tree cap (--side-tree-cap, off by default so a default run reproduces
  the shipped links byte for byte). The side trees are filled in the role order
  of SIDE_TREE_ORDER instead of TalentTab page order, and no side tree may
  reach the main tree's points at any level; the majority check then has to be
  clean, or the path is BROKEN.

Usage:
    build_premade_specs.py --dbc /path/to/dbc [--rate 1.0] [--out file] [--talent-classes 4,7]
                           [--side-tree-cap] [--check-levels]
"""
import argparse
import itertools
import os
import struct
import sys

LEVELS = [10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60]
VALIDATION_LEVELS = range(10, 61)
NEW_PATHS = {
    (1, 'arms'),
    (3, 'survival'),
    (4, 'subtlety'),
    (5, 'discipline'),
    (7, 'elemental'),
}

# Hand-built final talent distributions. At Rate.Talent 1 the generator emits
# at most the first 51 points at level 60.
BUILDS = {
    1:  [('fury',          '352200123025101-05555000003122501'),
         ('protection',    '3500531--2530510232055122231'),
         ('arms',          '052250123320131001-05555')],
    2:  [('holy',          '05320302035251351-50025-252'),
         ('protection',    '5-0531231332031551-05205001203'),
         ('retribution',   '50005-50325000003-5023005123005151')],
    3:  [('beastmastery',  '5500320152253112251-052521001'),
         ('marksmanship',  '502300015201-0555213250123251'),
         ('survival',      '550332002--35232000500020030051')],
    4:  [('combat',        '0053231052330012-500350200050122231'),
         ('assassination', '005323105233311251-500350020050001'),
         ('subtlety',      '325323101--52330230112102102021'),
         # #367: rogue tank shares the combat build (phase 1, bots only); the
         # difference is role, strategy and stats. Weight 0 until the owner decides.
         ('rogue tank',    '0053231052330012-500350200050122231')],
    5:  [('holy',          '2052022013300301-32050203023521351'),
         ('shadow',        '20504020133003115--05225000102501251'),
         ('discipline',    '235050000323231101-325532')],
    7:  [('restoration',   '50203105-500002-5503032105321251'),
         ('enhancement',   '550331300202012-5005303105023151'),
         ('elemental',     '55023105002001151--55235'),
         # #357: shaman tank shares the enhancement build (phase 1, bots only);
         # the difference is role, strategy and stats. Weight 0 until the owner decides.
         ('shaman tank',   '550331300202012-5005303105023151')],
    8:  [('arcane',        '2352551312233311251-55000001'),
         ('fire',          '230205111200301-50523201030303251-203'),
         ('frost',         '230225100210301--0533020510235110521')],
    9:  [('affliction',    '550022300223210151-053030101-5005301'),
         ('demonology',    '50002-250033150223313531-5025'),
         ('destruction',   '-053030101-5505221522525151')],
    11: [('balance',       '50000230312533113321--5030513300012'),
         ('feral',         '0140003201-553030213232021521-55'),
         ('restoration',   '5002013031203--5050013355213521'),
         # #308: bear (tank) shares the feral build - the 102-point path fills the
         # whole feral tree anyway; bear vs cat is a role and stat decision.
         ('bear',          '0140003201-553030213232021521-55')],
}

# Expected Turtle TalentTab page for every explicitly named path. Keeping this
# independent of the link catches a build accidentally filed under the wrong
# class tree even when its ranks happen to be structurally valid.
EXPECTED_TREE_PAGE = {
    1: {'arms': 0, 'fury': 1, 'protection': 2},
    2: {'holy': 0, 'protection': 1, 'retribution': 2},
    3: {'beastmastery': 0, 'marksmanship': 1, 'survival': 2},
    4: {'assassination': 0, 'combat': 1, 'subtlety': 2, 'rogue tank': 1},
    5: {'discipline': 0, 'holy': 1, 'shadow': 2},
    7: {'elemental': 0, 'enhancement': 1, 'restoration': 2, 'shaman tank': 1},
    8: {'arcane': 0, 'fire': 1, 'frost': 2},
    9: {'affliction': 0, 'demonology': 1, 'destruction': 2},
    11: {'balance': 0, 'feral': 1, 'restoration': 2, 'bear': 1},
}

# --side-tree-cap only (twow-repo#602 R06a, OB-10 2026-10-18): the order in which
# a path fills its two side trees (TalentTab pages) once the main tree is full.
# Role rule: healer -> the mana/regen tree first, tank -> survivability/threat,
# damage -> crit/hit. Talent names are those of the Turtle Talent.dbc (client
# patch v8). Without the option the side trees keep TalentTab page order.
SIDE_TREE_ORDER = {
    # Warrior: 0 arms, 1 fury, 2 protection.
    (1, 'fury'):          [0, 2],  # arms: Impale, Deep Wounds, Precision Cut (crit)
    (1, 'protection'):    [0, 1],  # arms: Deflection (parry), Tactical Mastery,
                                   # Improved Heroic Strike (threat)
    (1, 'arms'):          [1, 2],  # fury: Cruelty (crit), Unbridled Wrath
    # Paladin: 0 holy, 1 protection, 2 retribution.
    (2, 'holy'):          [2, 1],  # retribution: Benediction (seal/judgement cost),
                                   # Improved Blessings (Wisdom) - mana
    (2, 'protection'):    [0, 2],  # holy: Sanctity Aura, Holy Power (holy damage =
                                   # threat), Improved Lay on Hands, Ironclad
    (2, 'retribution'):   [0, 1],  # holy: Holy Power, Blessed Strikes (crit); the
                                   # hit of protection (Precision 3/3) is in the build
    # Hunter: 0 beast mastery, 1 marksmanship, 2 survival.
    (3, 'beastmastery'):  [1, 2],  # marksmanship: Lethal Shots, Mortal Shots (crit)
    (3, 'marksmanship'):  [2, 0],  # survival: Surefooted (hit), Killer Instinct,
                                   # Lightning Reflexes (crit)
    (3, 'survival'):      [1, 0],  # marksmanship: Lethal Shots, Mortal Shots (crit)
    # Rogue: 0 assassination, 1 combat, 2 subtlety.
    (4, 'combat'):        [0, 2],  # assassination: Malice (crit), Lethality
    (4, 'assassination'): [1, 2],  # combat: Precision (hit), Dual Wield Specialization
    (4, 'subtlety'):      [0, 1],  # assassination: Malice (crit), Lethality
    (4, 'rogue tank'):    [0, 2],  # assassination: Malice, Murder (damage = threat)
    # Priest: 0 discipline, 1 holy, 2 shadow.
    (5, 'holy'):          [0, 2],  # discipline: Mental Agility, Meditation,
                                   # Mental Strength (mana/regen)
    (5, 'shadow'):        [0, 1],  # discipline: Force of Will, Piercing Light (crit)
    (5, 'discipline'):    [1, 2],  # holy: Improved Healing, Holy Focus (mana cost),
                                   # Inspiration
    # Shaman: 0 elemental, 1 enhancement, 2 restoration.
    (7, 'restoration'):   [0, 1],  # elemental: Convection, Elemental Focus (mana)
    (7, 'enhancement'):   [0, 2],  # elemental: Elemental Devastation, Call of Thunder (crit)
    (7, 'elemental'):     [2, 1],  # restoration: Tidal Mastery, Nature's Grace (crit)
    (7, 'shaman tank'):   [0, 2],  # elemental: Concussion, Call of Flame (spell
                                   # damage = threat), Elemental Warding
    # Mage: 0 arcane, 1 fire, 2 frost.
    (8, 'arcane'):        [1, 2],  # fire: Critical Mass, Incinerate (crit)
    (8, 'fire'):          [0, 2],  # arcane: Arcane Instability (crit), Arcane Focus
    (8, 'frost'):         [0, 1],  # arcane: Arcane Instability (crit), Arcane Focus
    # Warlock: 0 affliction, 1 demonology, 2 destruction.
    (9, 'affliction'):    [2, 1],  # destruction: Devastation, Ruin, Bane (crit)
    (9, 'demonology'):    [2, 0],  # destruction: Devastation, Ruin (crit); the hit of
                                   # affliction (Suppression 5/5) is in the build
    (9, 'destruction'):   [0, 1],  # affliction: Suppression (hit)
    # Druid: 0 balance, 1 feral, 2 restoration.
    (11, 'balance'):      [2, 1],  # restoration: Genesis, Reflection; the feral crit
                                   # talents only work in forms
    (11, 'feral'):        [0, 2],  # balance: Natural Weapons, Omen of Clarity, Vengeance
    (11, 'restoration'):  [0, 1],  # balance: Moonglow, Omen of Clarity, Nature's Grace (mana)
    (11, 'bear'):         [0, 2],  # balance: Natural Weapons, Omen of Clarity (threat);
                                   # keeps the build shared with feral
}

# Roll weights of the premade paths (default 100). Owner 2026-09-26 (#308):
# cat and bear 50/50, keeping the old feral share of a druid.
PROBABILITY = {
    (11, 'feral'): 50,
    (11, 'bear'): 50,
    # #357 / #367 (owner 2026-09-27): tank paths off until the design numbers
    # are signed off; the server profile sets their weight.
    (4, 'rogue tank'): 0,
    (7, 'shaman tank'): 0,
}

# #357 O-12 (owner 2026-09-27, variant A): shamans on 7.1 / 7.3 get the reworked
# Enhancement talents as bot auras (SpecAuraPolicy.h, keep both tables equal) and
# pay for them with talent points: at every level the link leaves exactly the
# points of the aura ranks granted by then unspent (7.1: 14, 7.3: 21 at 60;
# Ghost Wolf rank 3 is Improved Ghost Wolf 2/2 for everyone, owner 2026-09-27).
# (class, name, first grant level, ranks, paths)
SPEC_AURAS = [
    (7, 'attack speed',        10, 5, {'enhancement', 'shaman tank'}),
    (7, 'defense',             10, 5, {'shaman tank'}),
    (7, 'imbue mastery',       25, 3, {'enhancement', 'shaman tank'}),
    (7, 'retaliation',         25, 3, {'shaman tank'}),
    (7, 'stormstrike charges', 30, 4, {'shaman tank'}),  # 4 ranks since train 9 (#484)
    (7, 'storm wisdom',        35, 5, {'enhancement'}),
    (7, 'chain storm',         40, 1, {'enhancement'}),
    (7, 'shield constitution', 35, 3, {'shaman tank'}),
    (7, 'shield ward',         40, 1, {'shaman tank'}),
    # #367 rogue (OB-20 IDs 61151-61191): 4.0 = 9, 4.1 = 9, 4.2 = 8, 4.3 = 20 points at 60.
    (4, 'damage from behind',  10, 4, {'assassination'}),
    (4, 'snd cooldown crits',  20, 2, {'assassination'}),
    (4, 'cold blood damage',   30, 1, {'assassination'}),
    (4, 'vigor damage',        40, 1, {'assassination'}),
    (4, 'seal fate extra',     40, 1, {'assassination'}),
    (4, 'agility',             10, 5, {'combat', 'rogue tank'}),
    (4, 'defense',             10, 5, {'rogue tank'}),
    (4, 'riposte strikes',     15, 3, {'rogue tank'}),
    (4, 'evasive resistance',  25, 3, {'rogue tank'}),
    (4, 'execute strikes',     35, 3, {'combat'}),
    (4, 'frontal backstab',    40, 1, {'combat'}),
    (4, 'tank toughness',      35, 3, {'rogue tank'}),
    (4, 'ghostly magic dodge', 40, 1, {'rogue tank'}),
    (4, 'stealth damage',      10, 4, {'subtlety'}),
    (4, 'hemorrhage stacks',   35, 1, {'subtlety'}),
    (4, 'shadow damage',       40, 3, {'subtlety'}),
]


# First rank spell of every SPEC_AURAS talent (SpecAuraPolicy.h, keep equal). With
# --talent-classes these spells are the rank 1 of a real Talent.dbc talent
# (twow-repo#409 stage 2), which is how the generator finds the new talents.
AURA_FIRST_SPELL = {
    (7, 'attack speed'): 61101, (7, 'defense'): 61106, (7, 'imbue mastery'): 61112,
    (7, 'retaliation'): 61115, (7, 'stormstrike charges'): 61118,
    (7, 'storm wisdom'): 61119, (7, 'chain storm'): 61125,
    (7, 'shield constitution'): 61127, (7, 'shield ward'): 61130,
    (4, 'damage from behind'): 61151, (4, 'snd cooldown crits'): 61155,
    (4, 'cold blood damage'): 61157, (4, 'vigor damage'): 61158,
    (4, 'seal fate extra'): 61159, (4, 'agility'): 61160, (4, 'defense'): 61165,
    (4, 'riposte strikes'): 61170, (4, 'evasive resistance'): 61173,
    (4, 'execute strikes'): 61176, (4, 'frontal backstab'): 61179,
    (4, 'tank toughness'): 61180, (4, 'ghostly magic dodge'): 61183,
    (4, 'stealth damage'): 61184, (4, 'hemorrhage stacks'): 61188,
    (4, 'shadow damage'): 61189,
}


def reserved_points(cls, name, level, talent_classes=frozenset()):
    """Talent points a path pays at this level for its #357 / #367 auras.
    Nothing once the class's auras are real talents (--talent-classes)."""
    if cls in talent_classes:
        return 0
    return sum(min(ranks, max(0, level - first + 1))
               for aura_cls, _, first, ranks, paths in SPEC_AURAS
               if aura_cls == cls and name in paths)


def aura_talents(cls, entries):
    """twow-repo#409 stage 2: {aura name: talent entry} of the class's aura
    talents in a patched Talent.dbc, found by their first rank spell. A missing
    one is an error: the DBC is not the one the talent classes were built for."""
    by_first = {entry['rankIDs'][0]: entry for entry in entries if entry['rankIDs']}
    found, missing = {}, []
    for aura_cls, name, _, ranks, _ in SPEC_AURAS:
        if aura_cls != cls:
            continue
        entry = by_first.get(AURA_FIRST_SPELL[(cls, name)])
        if entry is None or entry['maxRank'] != ranks:
            missing.append(name)
        else:
            found[name] = entry
    if missing:
        raise ValueError('class %d: no talent with the aura ranks of %s' % (cls, ', '.join(missing)))
    return found


# #357 stage 2 (core#217): new talents of the patched tree that are no SpecAura
# aura but still belong to a path - the shaman weapon talent W "Ancestral Arms"
# (rank spell 61131, 1 point) for both shaman paths (7.1 = 14 + W, 7.3 = 21 + W).
# {class: {first rank spell: {path: rank}}}
EXTRA_REAL_TALENTS = {
    7: {61131: {'enhancement': 1, 'shaman tank': 1}},
}


def extra_real_talents(cls, entries):
    """{first rank spell: talent entry} of the class's EXTRA_REAL_TALENTS in a
    patched Talent.dbc; a missing one is an error, as for the aura talents."""
    by_first = {entry['rankIDs'][0]: entry for entry in entries if entry['rankIDs']}
    found, missing = {}, []
    for spell in EXTRA_REAL_TALENTS.get(cls, {}):
        entry = by_first.get(spell)
        if entry is None:
            missing.append(str(spell))
        else:
            found[spell] = entry
    if missing:
        raise ValueError('class %d: no talent with first rank spell %s' % (cls, ', '.join(missing)))
    return found


def real_talent_target(cls, name, link, entries):
    """The hand-built link was written against the tree without the new talents,
    so it is read against that tree; the path's aura talents are then added at
    full rank (they are what the path paid its reserve for), and the extra real
    talents of the path (the shaman weapon talent) with their rank."""
    new = aura_talents(cls, entries)
    extra = extra_real_talents(cls, entries)
    new_ids = {entry['id'] for entry in new.values()} | {entry['id'] for entry in extra.values()}
    target = parse_link(link, [entry for entry in entries if entry['id'] not in new_ids])
    for aura_cls, aura, _, ranks, paths in SPEC_AURAS:
        if aura_cls == cls and name in paths:
            target[new[aura]['id']] = ranks
    for spell, per_path in EXTRA_REAL_TALENTS.get(cls, {}).items():
        if name in per_path:
            target[extra[spell]['id']] = per_path[name]
    return target


# Talents a path never takes (talent id). #357: Calming Winds (-25 % threat)
# is harmful for the shaman tank (owner 2026-09-27).
EXCLUDED_TALENTS = {
    (7, 'shaman tank'): {257},
}

# Ranks a path sets on top of its shared build (talent id -> rank). The shaman
# tank moves the 3 Calming Winds points into Ancestral Guardian (259, row 3:
# +armour, +dodge), which also keeps the 15 points before Flurry (row 4).
TARGET_OVERRIDES = {
    (7, 'shaman tank'): {259: 3},
    # #367 stage 2: in the patched tree the rogue tank talent "evasive resistance"
    # (9172, row 3) requires talent 117 (combat, row 2) at rank 3 - the arrow
    # 117 -> 9172 of the owner tree. The shared combat build does not take it fully.
    (4, 'rogue tank'): {117: 3},
}

EXPECTED_NEW_TREE_TAB = {
    (1, 'arms'): 161,
    (3, 'survival'): 362,
    (4, 'subtlety'): 183,
    (5, 'discipline'): 201,
    (7, 'elemental'): 261,
}


def read_dbc(directory, name):
    path = os.path.join(directory, name)
    with open(path, 'rb') as source:
        raw = source.read()
    if len(raw) < 20:
        raise ValueError('%s is shorter than a WDBC header' % path)
    magic, records, fields, size, string_size = struct.unpack('<4sIIII', raw[:20])
    if magic != b'WDBC':
        raise ValueError('%s has magic %r, expected WDBC' % (path, magic))
    if fields * 4 != size:
        raise ValueError('%s has %d fields but record size %d' % (path, fields, size))
    expected_size = 20 + records * size + string_size
    if len(raw) != expected_size:
        raise ValueError('%s has %d bytes, expected %d from its header' %
                         (path, len(raw), expected_size))
    for i in range(records):
        start = 20 + i * size
        yield struct.unpack('<%dI' % fields, raw[start:start + size])


def load_spells(dbc):
    return {row[0] for row in read_dbc(dbc, 'Spell.dbc')}


def load_talents(dbc):
    tabs = {v[0]: {'classMask': v[12], 'page': v[13]}
            for v in read_dbc(dbc, 'TalentTab.dbc')}

    talents = []
    for v in read_dbc(dbc, 'Talent.dbc'):
        tab = tabs.get(v[1])
        if not tab:
            continue
        rank_slots = [v[4 + k] for k in range(5)]
        rank_ids = [spell_id for spell_id in rank_slots if spell_id]
        if rank_slots[:len(rank_ids)] != rank_ids:
            raise ValueError('Talent.dbc talent %d has a gap in its RankID chain' % v[0])
        talents.append({
            'id': v[0], 'tab': v[1], 'row': v[2], 'col': v[3],
            'maxRank': len(rank_ids), 'rankIDs': rank_ids,
            'dependsOn': v[13], 'dependsOnRank': v[16],
            'dependsOnSpell': v[20],
            'classMask': tab['classMask'],
            # Talent tab 41 is filed under the wrong page in Turtle's DBC.
            'page': 1 if v[1] == 41 else tab['page'],
        })
    return talents


def talents_of(talents, cls):
    """Exactly the order TalentSpec::GetTalents produces, including the
    SortTalents(SORT_BY_DEFAULT) at the end. Turtle's Talent.dbc is not stored
    in tree order; without this sort every rank lands in the wrong tree."""
    mask = 1 << (cls - 1)
    hits = [t for t in talents if t['classMask'] & mask]
    hits.sort(key=lambda t: (t['page'], t['row'], t['col']))
    return hits


def parse_link(link, entries):
    """Map the digits of a talent link onto talents, as ReadTalents does."""
    wanted = {}
    page = pos = 0
    while pos < len(link) and link[pos] == '-':
        pos += 1
        page += 1
    for entry in entries:
        if pos >= len(link):
            break
        if entry['page'] != page:
            continue
        if not link[pos].isdigit():
            raise ValueError('unexpected character %r at offset %d' % (link[pos], pos))
        wanted[entry['id']] = int(link[pos])
        pos += 1
        while pos < len(link) and link[pos] == '-':
            pos += 1
            page += 1
    if pos != len(link):
        raise ValueError('link has unconsumed data at offset %d: %s' % (pos, link[pos:]))
    return wanted


def check(entries, ranks, budget, spell_ids, strict_prerequisites):
    """Mirror TalentSpec::CheckTalents and prove used rank spells exist."""
    by_id = {entry['id']: entry for entry in entries}
    for entry in entries:
        rank = ranks.get(entry['id'], 0)
        if rank > entry['maxRank']:
            return 'rank above maximum on talent %d' % entry['id']
        if rank > 0 and entry['dependsOn']:
            previous = by_id.get(entry['dependsOn'])
            prerequisite_rank = ranks.get(previous['id'], 0) if previous else 0
            # DependsOnRank is a zero-based rank index in the DBC. New paths
            # therefore require rank > DependsOnRank, matching Player::LearnTalent.
            # Existing paths retain the legacy TalentSpec::CheckTalents comparison
            # so this task can prove their generated bytes unchanged without
            # silently repairing historical definitions outside its scope.
            missing = (prerequisite_rank <= entry['dependsOnRank']
                       if strict_prerequisites
                       else prerequisite_rank < entry['dependsOnRank'])
            if (not previous or entry['dependsOnRank'] >= previous['maxRank'] or
                    missing):
                return 'prerequisite missing for talent %d' % entry['id']
        for spell_id in entry['rankIDs'][:rank]:
            if spell_id not in spell_ids:
                return 'rank spell %d missing for talent %d' % (spell_id, entry['id'])
        if rank > 0 and entry['dependsOnSpell']:
            if entry['dependsOnSpell'] not in spell_ids:
                return 'required spell %d missing for talent %d' % (
                    entry['dependsOnSpell'], entry['id'])
    for page in (0, 1, 2):
        spent = 0
        for entry in [item for item in entries if item['page'] == page]:
            if ranks.get(entry['id'], 0) > 0 and entry['row'] * 5 > spent:
                return 'row rule broken on talent %d (row %d, only %d spent)' % (
                    entry['id'], entry['row'], spent)
            spent += ranks.get(entry['id'], 0)
    total = sum(ranks.values())
    if total > budget:
        return 'too many points (%d of %d)' % (total, budget)
    return None


def build_link(entries, ranks):
    parts = []
    for page in (0, 1, 2):
        tree = [entry for entry in entries if entry['page'] == page]
        parts.append(''.join(str(ranks.get(entry['id'], 0))
                             for entry in tree).rstrip('0'))
    return '-'.join(parts).rstrip('-')


def tree_order(entries, main_tree, side_order=None):
    """Talents in fill order: the main tree, then the side trees - in
    TalentTab page order, or in side_order (--side-tree-cap)."""
    pages = [main_tree] + list(side_order if side_order is not None else
                               [page for page in (0, 1, 2) if page != main_tree])
    return [entry for page in pages for entry in entries if entry['page'] == page]


def page_points(entries, ranks, page):
    return sum(ranks.get(entry['id'], 0) for entry in entries if entry['page'] == page)


def side_tree_reaches_main(entries, ranks, main_tree):
    """True when a side tree has as many points as the main tree (or more)."""
    main = page_points(entries, ranks, main_tree)
    return any(page_points(entries, ranks, page) >= main
               for page in (0, 1, 2) if page != main_tree
               if page_points(entries, ranks, page) > 0)


def prefix(entries, target, main_tree, budget, side_order=None):
    order = tree_order(entries, main_tree, side_order)
    ranks, spent = {}, 0
    for entry in order:
        if spent >= budget:
            break
        add = min(target.get(entry['id'], 0), budget - spent)
        if add > 0:
            ranks[entry['id']] = add
            spent += add
    return ranks


def extend_target(entries, target, main_tree, desired, spell_ids, excluded=frozenset(),
                  side_order=None, side_cap=False):
    """Fill a Rate-1 target deterministically for higher talent rates.

    The accepted target remains untouched at Rate 1.  For an additive higher
    rate, finish the selected main tree first, then visit the other trees in
    ascending Turtle TalentTab page order (or in side_order).  A rank is
    retained only when the same DBC checks used for emitted prefixes accept it.
    With side_cap no side tree may reach the main tree's points.
    """
    ranks = dict(target)
    order = tree_order(entries, main_tree, side_order)
    while sum(ranks.values()) < desired:
        added = False
        for entry in order:
            if entry['id'] in excluded:
                continue
            current = ranks.get(entry['id'], 0)
            if current >= entry['maxRank']:
                continue
            candidate = dict(ranks)
            candidate[entry['id']] = current + 1
            if (side_cap and entry['page'] != main_tree and
                    page_points(entries, candidate, entry['page']) >=
                    page_points(entries, candidate, main_tree)):
                continue
            if not check(entries, candidate, desired, spell_ids, True):
                ranks = candidate
                added = True
                break
        if not added:
            break
    return ranks


def legal_prefix(entries, target, main_tree, budget, spell_ids, side_order=None):
    """Return the deterministic, DBC-legal Rate-2 prefix of a target."""
    ranks = {}
    order = tree_order(entries, main_tree, side_order)
    while sum(ranks.values()) < budget:
        added = False
        for entry in order:
            current = ranks.get(entry['id'], 0)
            if current >= target.get(entry['id'], 0):
                continue
            candidate = dict(ranks)
            candidate[entry['id']] = current + 1
            if not check(entries, candidate, budget, spell_ids, True):
                ranks = candidate
                added = True
                break
        if not added:
            break
    return ranks


def link_tree_points(link):
    """Points per TalentTab page (0, 1, 2) of a talent link, without a DBC: the
    link holds one digit per talent in tree order and '-' between the trees."""
    parts = (link.split('-') + ['', '', ''])[:3]
    if len(link.split('-')) > 3 or any(not part.isdigit() for part in parts if part):
        raise ValueError('not a talent link: %r' % link)
    return tuple(sum(int(digit) for digit in part) for part in parts)


def crop_orders(points):
    """Every tree order TalentSpec::CropTalents can use: largest tree first, and
    equal trees in either order (SORT_BY_POINTS_TREE breaks ties with irand)."""
    orders = set()
    for order in itertools.permutations((0, 1, 2)):
        if all(points[order[i]] >= points[order[i + 1]] for i in range(2)):
            orders.add(order)
    return sorted(orders)


def crop(points, budget, order):
    """Points per tree after CropTalents(budget) walks the trees in this order.
    Inside a tree the ranks are kept in row order, so only the totals matter."""
    left, result = budget, [0, 0, 0]
    for page in order:
        result[page] = min(points[page], left)
        left -= result[page]
    return tuple(result)


def simulate_server(links_by_level, rate, expected_tree, reserved=None,
                    levels=VALIDATION_LEVELS):
    """Replay the server for one path at every level and return the levels at
    which the expected tree is not strictly the largest after the crop.

    links_by_level: {level: link} as in AiPlayerbot.PremadeSpecLink.<c>.<s>.<level>.
    reserved: optional function level -> points the path pays for its talent
    auras (ChangeTalentsAction::PremadeBudget); None means nothing reserved.

    Mirrors ChangeTalentsAction::GetBestPremadeSpec (first link in level order
    whose total reaches the budget, else the last link) and
    TalentSpec::CropTalents (only when the link holds more than the budget;
    largest tree first, ties in every order). Each result is a dict with level,
    budget, the picked link level, its tree points, the cropped points per tie
    order and kind, judged by the majority read TalentSpec::highestTree (equal
    trees: the lower page wins):
      'sure'      every crop order makes highestTree name another tree,
      'coin-toss' it depends on the random crop order,
      'tie'       the expected tree only ties, and highestTree's lower-page rule
                  still names it."""
    specs = [(level, link_tree_points(link))
             for level, link in sorted(links_by_level.items()) if link]
    flips = []
    if not specs:
        return flips
    for level in levels:
        budget = int((level - 9) * rate) if level >= 10 else 0
        if reserved is not None:
            budget = max(0, budget - reserved(level))
        if budget <= 0:
            continue
        picked = next(((spec_level, points) for spec_level, points in specs
                       if sum(points) >= budget), specs[-1])
        points = picked[1]
        results = ([crop(points, budget, order) for order in crop_orders(points)]
                   if sum(points) > budget else [points])
        strict = [result[expected_tree] > max(result[page] for page in (0, 1, 2)
                                              if page != expected_tree)
                  for result in results]
        if all(strict):
            continue
        flipped = [highest_tree(result) != expected_tree for result in results]
        flips.append({
            'level': level, 'budget': budget, 'link_level': picked[0],
            'points': points, 'results': sorted(set(results)),
            'kind': ('sure' if all(flipped) else
                     'coin-toss' if any(flipped) else 'tie'),
        })
    return flips


def highest_tree(points):
    """TalentSpec::highestTree: the strictly largest tree, else the C++ fallback
    (page 0 if it beats one tree, else page 1 if it beats one, else page 0)."""
    p1, p2, p3 = points
    if p1 > p2 and p1 > p3:
        return 0
    if p2 > p1 and p2 > p3:
        return 1
    if p3 > p1 and p3 > p2:
        return 2
    if p1 > p2 or p1 > p3:
        return 0
    if p2 > p3 or p2 > p1:
        return 1
    return 0


FLIP_KINDS = ('tie', 'coin-toss', 'sure')


def summarize_flips(flips):
    """(first level not strictly won, worst kind, first 'sure' level or None)
    of a simulate_server() result, None for a clean path."""
    if not flips:
        return None
    sure = [flip['level'] for flip in flips if flip['kind'] == 'sure']
    worst = max((flip['kind'] for flip in flips), key=FLIP_KINDS.index)
    return (flips[0]['level'], worst, sure[0] if sure else None)


def format_points(points):
    return '/'.join(str(value) for value in points)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dbc', required=True,
                    help='directory holding Talent.dbc, TalentTab.dbc, and Spell.dbc')
    ap.add_argument('--rate', type=float, default=1.0,
                    help='Rate.Talent from mangosd.conf')
    ap.add_argument('--out', help='write here instead of stdout')
    ap.add_argument('--talent-classes', default='',
                    help='comma-separated classes whose SPEC_AURAS are real talents in '
                         'this Talent.dbc (twow-repo#409 stage 2), as in '
                         'AiPlayerbot.SpecAura.TalentClasses')
    ap.add_argument('--side-tree-cap', action='store_true',
                    help='fill the side trees in SIDE_TREE_ORDER and keep each of '
                         'them below the main tree at every level (twow-repo#602 '
                         'R06a; changes the links, off by default)')
    ap.add_argument('--check-levels', action='store_true',
                    help='print, per path, every level 10-60 at which the server '
                         '(GetBestPremadeSpec + CropTalents) leaves the main tree '
                         'not strictly the largest')
    args = ap.parse_args()
    talent_classes = frozenset(int(c) for c in args.talent_classes.split(',') if c.strip())
    if args.rate <= 0:
        ap.error('--rate must be greater than zero')

    try:
        talents = load_talents(args.dbc)
        spell_ids = load_spells(args.dbc)
    except (OSError, ValueError, struct.error) as error:
        print('DBC ERROR: %s' % error, file=sys.stderr)
        return 1

    out = []
    failures = 0

    for cls in sorted(BUILDS):
        entries = talents_of(talents, cls)
        class_mask = 1 << (cls - 1)
        if not entries or any(not (entry['classMask'] & class_mask) for entry in entries):
            print('  BROKEN  class %d has no consistent TalentTab entries' % cls,
                  file=sys.stderr)
            failures += 1
            continue
        out.append('')
        out.append('# --- class %d ---' % cls)

        for index, (name, link) in enumerate(BUILDS[cls]):
            try:
                target = (real_talent_target(cls, name, link, entries)
                          if cls in talent_classes else parse_link(link, entries))
            except ValueError as error:
                print('  BROKEN  class %d %s: %s' % (cls, name, error), file=sys.stderr)
                failures += 1
                continue

            excluded = EXCLUDED_TALENTS.get((cls, name), frozenset())
            target = {talent_id: rank for talent_id, rank in target.items()
                      if talent_id not in excluded}
            target.update(TARGET_OVERRIDES.get((cls, name), {}))
            strict_prerequisites = ((cls, name) in NEW_PATHS or args.rate > 1)
            problem = check(entries, target, sum(target.values()), spell_ids,
                            strict_prerequisites)
            if problem:
                print('  BROKEN  class %d %s: %s' % (cls, name, problem), file=sys.stderr)
                failures += 1
                continue

            main_tree = max((0, 1, 2), key=lambda page: sum(
                target.get(entry['id'], 0)
                for entry in entries if entry['page'] == page))
            expected_tree = EXPECTED_TREE_PAGE.get(cls, {}).get(name)
            if expected_tree is None or main_tree != expected_tree:
                print('  BROKEN  class %d %s: main tree %d, expected %r' %
                      (cls, name, main_tree, expected_tree), file=sys.stderr)
                failures += 1
                continue
            expected_tab = EXPECTED_NEW_TREE_TAB.get((cls, name))
            if expected_tab is not None:
                selected_main_tabs = {
                    entry['tab'] for entry in entries
                    if entry['page'] == main_tree and target.get(entry['id'], 0) > 0
                }
                if selected_main_tabs != {expected_tab}:
                    print('  BROKEN  class %d %s: main TalentTab %r, expected %d' %
                          (cls, name, selected_main_tabs, expected_tab),
                          file=sys.stderr)
                    failures += 1
                    continue

            side_order = None
            if args.side_tree_cap:
                side_order = SIDE_TREE_ORDER.get((cls, name))
                if sorted(side_order or []) != [page for page in (0, 1, 2)
                                                if page != main_tree]:
                    print('  BROKEN  class %d %s: SIDE_TREE_ORDER %r does not name '
                          'the two side trees of main tree %d' %
                          (cls, name, side_order, main_tree), file=sys.stderr)
                    failures += 1
                    continue

            desired = int((60 - 9) * args.rate) - reserved_points(cls, name, 60, talent_classes)
            active_target = target
            if args.rate > 1:
                active_target = extend_target(entries, target, main_tree,
                                              desired, spell_ids, excluded,
                                              side_order, args.side_tree_cap)
                if sum(active_target.values()) != desired:
                    print('  BROKEN  class %d %s: only %d of %d legal points%s' %
                          (cls, name, sum(active_target.values()), desired,
                           ' under the side-tree cap' if args.side_tree_cap else ''),
                          file=sys.stderr)
                    failures += 1
                    continue
            if args.side_tree_cap and side_tree_reaches_main(entries, active_target,
                                                             main_tree):
                print('  BROKEN  class %d %s: a side tree reaches the main tree '
                      '(%s) despite the side-tree cap' %
                      (cls, name, format_points([page_points(entries, active_target, page)
                                                 for page in (0, 1, 2)])),
                      file=sys.stderr)
                failures += 1
                continue

            previous = {}
            prefixes = {}
            validation_levels = (VALIDATION_LEVELS if args.rate > 1 or
                                 (cls, name) in NEW_PATHS else LEVELS)
            for level in validation_levels:
                budget = max(0, int((level - 9) * args.rate) -
                             reserved_points(cls, name, level, talent_classes))
                ranks = (legal_prefix(entries, active_target, main_tree,
                                      budget, spell_ids, side_order) if args.rate > 1 else
                         prefix(entries, active_target, main_tree, budget, side_order))
                problem = check(entries, ranks, budget, spell_ids,
                                strict_prerequisites)
                if (not problem and args.side_tree_cap and
                        side_tree_reaches_main(entries, ranks, main_tree)):
                    problem = 'a side tree reaches the main tree (%s)' % format_points(
                        [page_points(entries, ranks, page) for page in (0, 1, 2)])
                if problem:
                    print('  BROKEN  class %d %s at level %d: %s' %
                          (cls, name, level, problem), file=sys.stderr)
                    failures += 1
                    break
                expected_points = min(budget, sum(active_target.values()))
                if sum(ranks.values()) != expected_points:
                    print('  BROKEN  class %d %s at level %d: %d of %d points spent' %
                          (cls, name, level, sum(ranks.values()), expected_points),
                          file=sys.stderr)
                    failures += 1
                    break
                if any(ranks.get(talent_id, 0) < rank
                       for talent_id, rank in previous.items()):
                    print('  BROKEN  class %d %s at level %d: not a prefix' %
                          (cls, name, level), file=sys.stderr)
                    failures += 1
                    break
                previous = ranks
                if level in LEVELS:
                    prefixes[level] = ranks
            else:
                links = {level: build_link(entries, prefixes[level]) for level in LEVELS}
                if args.side_tree_cap or args.check_levels:
                    flips = simulate_server(
                        links, args.rate, main_tree,
                        lambda level: reserved_points(cls, name, level, talent_classes))
                    if args.check_levels:
                        for flip in flips:
                            print('  FLIP    class %d.%d %-14s level %2d budget %3d: '
                                  'link %d (%s) -> %s, %s' %
                                  (cls, index, name, flip['level'], flip['budget'],
                                   flip['link_level'], format_points(flip['points']),
                                   ' or '.join(format_points(result)
                                               for result in flip['results']),
                                   flip['kind']), file=sys.stderr)
                        summary = summarize_flips(flips)
                        print('  LEVELS  class %d.%d %-14s main tree %d: %s' %
                              (cls, index, name, main_tree,
                               'clean' if summary is None else
                               '%d flip levels, first %d, %s%s' %
                               (len(flips), summary[0], summary[1],
                                '' if summary[2] is None else
                                ', sure from %d' % summary[2])),
                              file=sys.stderr)
                    if args.side_tree_cap and flips:
                        print('  BROKEN  class %d %s: main tree %d not strictly the '
                              'largest at level %d despite the side-tree cap' %
                              (cls, name, main_tree, flips[0]['level']), file=sys.stderr)
                        failures += 1
                        continue
                out.append('AiPlayerbot.PremadeSpecName.%d.%d = %s' %
                           (cls, index, name))
                out.append('AiPlayerbot.PremadeSpecProb.%d.%d = %d' % (cls, index, PROBABILITY.get((cls, name), 100)))
                # #602 R06a: the spec identity, from the path and never from the
                # point majority (owner 2026-10-18).
                out.append('AiPlayerbot.PremadeSpecTab.%d.%d = %d' % (cls, index, expected_tree))
                for level in LEVELS:
                    link = links[level]
                    # #357: a level whose points all go to talent auras has no
                    # link (the loader skips empty ones anyway).
                    if not link:
                        continue
                    out.append('AiPlayerbot.PremadeSpecLink.%d.%d.%d = %s' %
                               (cls, index, level, link))

                full = sum(active_target.values())
                at_sixty = sum(prefix(entries, active_target, main_tree,
                                      int((60 - 9) * args.rate)).values())
                coverage = ('levels 10-60' if args.rate > 1 or
                            (cls, name) in NEW_PATHS else 'emitted snapshots')
                print('  class %-2d %-14s main tree %d, %2d of %d points at level 60; %s'
                      % (cls, name, main_tree, at_sixty, full, coverage),
                      file=sys.stderr)

    text = '\n'.join(out) + '\n'
    if args.out:
        with open(args.out, 'w', newline='\n') as destination:
            destination.write(text)
    else:
        sys.stdout.write(text)

    print('\nfailures: %d' % failures, file=sys.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
