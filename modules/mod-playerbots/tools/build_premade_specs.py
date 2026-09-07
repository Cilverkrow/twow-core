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
  row and column, then the rest. Every intermediate level from 10 through 60 is
  validated for newly added paths; historical paths retain their emitted
  five-level snapshots byte-for-byte.

  Talent rate. The points a level grants are (level - 9) * Rate.Talent, so the
  links depend on the rate the server runs. Generated for the wrong one, every
  entry demands a higher level than it is filed under and the server rejects the
  lot. Pass --rate to match your mangosd.conf; the default matches the shipped
  Rate.Talent = 1. At a higher rate the level 60 entries simply carry more
  points, up to the full build.

Usage:
    build_premade_specs.py --dbc /path/to/dbc [--rate 1.0] [--out file]
"""
import argparse
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
         ('subtlety',      '325323101--52330230112102102021')],
    5:  [('holy',          '2052022013300301-32050203023521351'),
         ('shadow',        '20504020133003115--05225000102501251'),
         ('discipline',    '235050000323231101-325532')],
    7:  [('restoration',   '50203105-500002-5503032105321251'),
         ('enhancement',   '550331300202012-5005303105023151'),
         ('elemental',     '55023105002001151--55235')],
    8:  [('arcane',        '2352551312233311251-55000001'),
         ('fire',          '230205111200301-50523201030303251-203'),
         ('frost',         '230225100210301--0533020510235110521')],
    9:  [('affliction',    '550022300223210151-053030101-5005301'),
         ('demonology',    '50002-250033150223313531-5025'),
         ('destruction',   '-053030101-5505221522525151')],
    11: [('balance',       '50000230312533113321--5030513300012'),
         ('feral',         '0140003201-553030213232021521-55'),
         ('restoration',   '5002013031203--5050013355213521')],
}

# Expected Turtle TalentTab page for every explicitly named path. Keeping this
# independent of the link catches a build accidentally filed under the wrong
# class tree even when its ranks happen to be structurally valid.
EXPECTED_TREE_PAGE = {
    1: {'arms': 0, 'fury': 1, 'protection': 2},
    2: {'holy': 0, 'protection': 1, 'retribution': 2},
    3: {'beastmastery': 0, 'marksmanship': 1, 'survival': 2},
    4: {'assassination': 0, 'combat': 1, 'subtlety': 2},
    5: {'discipline': 0, 'holy': 1, 'shadow': 2},
    7: {'elemental': 0, 'enhancement': 1, 'restoration': 2},
    8: {'arcane': 0, 'fire': 1, 'frost': 2},
    9: {'affliction': 0, 'demonology': 1, 'destruction': 2},
    11: {'balance': 0, 'feral': 1, 'restoration': 2},
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


def prefix(entries, target, main_tree, budget):
    order = ([entry for entry in entries if entry['page'] == main_tree]
             + [entry for entry in entries if entry['page'] != main_tree])
    ranks, spent = {}, 0
    for entry in order:
        if spent >= budget:
            break
        add = min(target.get(entry['id'], 0), budget - spent)
        if add > 0:
            ranks[entry['id']] = add
            spent += add
    return ranks


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dbc', required=True,
                    help='directory holding Talent.dbc, TalentTab.dbc, and Spell.dbc')
    ap.add_argument('--rate', type=float, default=1.0,
                    help='Rate.Talent from mangosd.conf')
    ap.add_argument('--out', help='write here instead of stdout')
    args = ap.parse_args()
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
                target = parse_link(link, entries)
            except ValueError as error:
                print('  BROKEN  class %d %s: %s' % (cls, name, error), file=sys.stderr)
                failures += 1
                continue

            strict_prerequisites = (cls, name) in NEW_PATHS
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

            previous = {}
            prefixes = {}
            validation_levels = (VALIDATION_LEVELS if (cls, name) in NEW_PATHS
                                 else LEVELS)
            for level in validation_levels:
                budget = int((level - 9) * args.rate)
                ranks = prefix(entries, target, main_tree, budget)
                problem = check(entries, ranks, budget, spell_ids,
                                strict_prerequisites)
                if problem:
                    print('  BROKEN  class %d %s at level %d: %s' %
                          (cls, name, level, problem), file=sys.stderr)
                    failures += 1
                    break
                expected_points = min(budget, sum(target.values()))
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
                out.append('AiPlayerbot.PremadeSpecName.%d.%d = %s' %
                           (cls, index, name))
                out.append('AiPlayerbot.PremadeSpecProb.%d.%d = 100' % (cls, index))
                for level in LEVELS:
                    out.append('AiPlayerbot.PremadeSpecLink.%d.%d.%d = %s' %
                               (cls, index, level,
                                build_link(entries, prefixes[level])))

                full = sum(target.values())
                at_sixty = sum(prefix(entries, target, main_tree,
                                      int((60 - 9) * args.rate)).values())
                coverage = ('levels 10-60' if (cls, name) in NEW_PATHS
                            else 'emitted snapshots')
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
