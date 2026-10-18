#!/usr/bin/env python3
"""Focused deterministic unit tests for the higher-rate premade path filler."""
import importlib.util
import pathlib
import re
import unittest


GENERATOR = (pathlib.Path(__file__).resolve().parents[1] /
             'tools' / 'build_premade_specs.py')
SPEC = importlib.util.spec_from_file_location('premade_specs_generator', GENERATOR)
GENERATOR_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR_MODULE)


CONFIG = (pathlib.Path(__file__).resolve().parents[1] /
          'src' / 'playerbot' / 'aiplayerbot.conf.dist.in')

# twow-repo#602 R06a: paths of the shipped Rate-2 block whose main tree the
# server (GetBestPremadeSpec + CropTalents, simulate_server) leaves not strictly
# the largest at some level 10-60, as (first such level, worst verdict of the
# majority read TalentSpec::highestTree, first 'sure' level). The links stay as
# shipped until the owner approves a respec (OB-10 2026-10-18); a new flip, or
# a fixed one, must show up here.
KNOWN_MAJORITY_FLIPS = {
    (1, 'arms'):         (56, 'sure', 56),       # 49/53/0 at 60, fury wins
    (3, 'marksmanship'): (56, 'sure', 56),       # 52/48/2, beast mastery wins
    (3, 'survival'):     (56, 'sure', 60),       # 51/0/51, crop coin toss, then tie -> page 0
    (5, 'holy'):         (56, 'sure', 58),       # 49/49/4, tie -> discipline (page 0)
    (5, 'shadow'):       (56, 'sure', 58),       # 49/4/49, tie -> discipline (page 0)
    (5, 'discipline'):   (56, 'coin-toss', None),  # 49/49/4, the tie keeps page 0
    (8, 'fire'):         (56, 'sure', 56),       # 49/48/5, arcane wins
    (9, 'affliction'):   (51, 'coin-toss', None),  # 44/44/x from 55, the tie keeps page 0
}


def shipped_block():
    """{(class, specno): name}, {(class, specno): {level: link}},
    {(class, specno): tab}, the PremadeSpecProb/Tab line order, and the rate
    and talent classes of the shipped aiplayerbot.conf.dist.in."""
    config = CONFIG.read_text(encoding='utf-8')
    names, links, tabs = {}, {}, {}
    lines = config.splitlines()
    tab_after = {}
    for number, line in enumerate(lines):
        match = re.match(r'^AiPlayerbot\.PremadeSpecName\.(\d+)\.(\d+) = (.+?)\s*$', line)
        if match:
            names[(int(match.group(1)), int(match.group(2)))] = match.group(3)
        # The loader cuts a link at '#' or ' ', so a trailing comment still counts as the link.
        match = re.match(r'^AiPlayerbot\.PremadeSpecLink\.(\d+)\.(\d+)\.(\d+) = ([0-9-]+)\s*(?:#.*)?$', line)
        if match:
            links.setdefault((int(match.group(1)), int(match.group(2))), {})[
                int(match.group(3))] = match.group(4)
        match = re.match(r'^AiPlayerbot\.PremadeSpecTab\.(\d+)\.(\d+) = (\d+)\s*$', line)
        if match:
            key = (int(match.group(1)), int(match.group(2)))
            tabs[key] = int(match.group(3))
            tab_after[key] = lines[number - 1] if number else ''
    rate = re.search(r'^# Generated for the funserver Rate\.Talent = (\d+(?:\.\d+)?) profile',
                     config, flags=re.MULTILINE)
    classes = re.search(r'^AiPlayerbot\.SpecAura\.TalentClasses =[ \t]*([0-9, ]*?)\s*$',
                        config, flags=re.MULTILINE)
    talent_classes = frozenset(int(c) for c in classes.group(1).split(',') if c.strip())
    return (names, links, tabs, tab_after,
            float(rate.group(1)) if rate else None, talent_classes)


def talent(talent_id, page, maximum):
    return {
        'id': talent_id,
        'page': page,
        'maxRank': maximum,
        'rankIDs': [],
        'dependsOn': 0,
        'dependsOnRank': 0,
        'dependsOnSpell': 0,
        'row': 0,
    }


class RateTwoPremadePathTests(unittest.TestCase):
    def setUp(self):
        # Turtle TalentTab page order is deterministic: page 0, then 1, then 2.
        # The selected main tree is page 1, so it must be exhausted before page 0.
        self.entries = [
            talent(10, 0, 5),
            talent(20, 1, 5),
            talent(30, 2, 5),
        ]
        self.rate_one_target = {20: 2}

    def test_rate_one_target_is_not_mutated(self):
        result = GENERATOR_MODULE.extend_target(
            self.entries, self.rate_one_target, 1, 10, set())
        self.assertEqual(self.rate_one_target, {20: 2})
        self.assertEqual(sum(result.values()), 10)

    def test_main_tree_precedes_secondary_pages(self):
        result = GENERATOR_MODULE.extend_target(
            self.entries, self.rate_one_target, 1, 8, set())
        self.assertEqual(result, {20: 5, 10: 3})

    def test_prefix_is_deterministic_and_never_regresses(self):
        target = GENERATOR_MODULE.extend_target(
            self.entries, self.rate_one_target, 1, 12, set())
        previous = {}
        for budget in range(2, 13):
            current = GENERATOR_MODULE.prefix(self.entries, target, 1, budget)
            self.assertEqual(sum(current.values()), budget)
            self.assertTrue(all(current.get(key, 0) >= value
                                for key, value in previous.items()))
            previous = current

    def test_legal_prefix_obeys_the_same_order(self):
        target = GENERATOR_MODULE.extend_target(
            self.entries, self.rate_one_target, 1, 12, set())
        self.assertEqual(
            GENERATOR_MODULE.legal_prefix(self.entries, target, 1, 8, set()),
            {20: 5, 10: 3})

    def test_all_30_paths_have_a_full_102_point_level_60_template(self):
        config = (pathlib.Path(__file__).resolve().parents[1] /
                  'src' / 'playerbot' / 'aiplayerbot.conf.dist.in').read_text(
                      encoding='utf-8')
        names = re.findall(r'^AiPlayerbot\.PremadeSpecName\.(\d+)\.(\d+) = (.+?)\s*$',
                           config, flags=re.MULTILINE)
        self.assertEqual(30, len(names))  # 27 + bear (#308) + shaman tank (#357) + rogue tank (#367)
        self.assertEqual(30, len({(cls, spec) for cls, spec, _ in names}))
        # Train 8: the links match the classes whose aura talents are real talents.
        classes = re.search(r'^AiPlayerbot\.SpecAura\.TalentClasses =[ \t]*([0-9, ]*?)\s*$',
                            config, flags=re.MULTILINE)
        self.assertIsNotNone(classes)
        talent_classes = frozenset(int(c) for c in classes.group(1).split(',') if c.strip())
        for cls, spec, name in names:
            match = re.search(r'^AiPlayerbot\.PremadeSpecLink\.' + cls + r'\.' +
                              spec + r'\.60 = ([0-9-]+)$', config,
                              flags=re.MULTILINE)
            self.assertIsNotNone(match)
            # #357 O-12: 7.1 / 7.3 leave the points of their talent auras free,
            # unless the class is a talent class (then nothing is reserved).
            reserved = GENERATOR_MODULE.reserved_points(int(cls), name, 60, talent_classes)
            self.assertEqual(102 - reserved, sum(int(value) for value in match.group(1)
                                                 if value.isdigit()))

    def test_spec_aura_reserve_matches_the_owner_values(self):
        # O-12 (owner 2026-09-27): 7.1 pays 14, 7.3 pays 21 talent points at 60;
        # train 9 (#484): Charged Stormstrike has 4 ranks, so 7.3 pays 24
        # (Ghost Wolf rank 3 is Improved Ghost Wolf 2/2 for everyone).
        self.assertEqual(14, GENERATOR_MODULE.reserved_points(7, 'enhancement', 60))
        self.assertEqual(24, GENERATOR_MODULE.reserved_points(7, 'shaman tank', 60))
        self.assertEqual(0, GENERATOR_MODULE.reserved_points(7, 'elemental', 60))
        # #367 rogue (OB-20 IDs, OB-10 path assignment).
        self.assertEqual(9, GENERATOR_MODULE.reserved_points(4, 'combat', 60))
        self.assertEqual(9, GENERATOR_MODULE.reserved_points(4, 'assassination', 60))
        self.assertEqual(8, GENERATOR_MODULE.reserved_points(4, 'subtlety', 60))
        self.assertEqual(20, GENERATOR_MODULE.reserved_points(4, 'rogue tank', 60))

    def test_spec_aura_table_matches_the_core_header(self):
        # tools/build_premade_specs.py SPEC_AURAS and SpecAuraPolicy.h must agree,
        # or the links leave a different number of points free than the core grants.
        header = (pathlib.Path(__file__).resolve().parents[1] /
                  'src' / 'playerbot' / 'SpecAuraPolicy.h').read_text(encoding='utf-8')
        paths = {'Both': {'enhancement', 'shaman tank'},
                 'Enhancement': {'enhancement'}, 'ShamanTank': {'shaman tank'},
                 'RogueCombat': {'combat'}, 'RogueAssassination': {'assassination'},
                 'RogueSubtlety': {'subtlety'}, 'RogueTank': {'rogue tank'}}
        core = []
        for cls, function in ((7, 'ShamanAuras'), (4, 'RogueAuras')):
            body = header.split('inline std::vector<AuraTalent> const& %s()' % function)[1].split('return auras;')[0]
            rows = re.findall(r'\{\s*"([^"]+)",\s*([\w |]+?),\s*(\d+),\s*\{([^}]*)\}\s*\}', body)
            for name, mask, level, ids in rows:
                names = set()
                for part in mask.split('|'):
                    names |= paths[part.strip()]
                core.append((cls, name, int(level), len([i for i in ids.split(',') if i.strip()]), names))
        self.assertEqual(GENERATOR_MODULE.SPEC_AURAS, core)

    def test_aura_first_spells_match_the_core_header(self):
        # --talent-classes finds each aura talent in Talent.dbc by its first rank.
        header = (pathlib.Path(__file__).resolve().parents[1] /
                  'src' / 'playerbot' / 'SpecAuraPolicy.h').read_text(encoding='utf-8')
        core = {}
        for cls, function in ((7, 'ShamanAuras'), (4, 'RogueAuras')):
            body = header.split('inline std::vector<AuraTalent> const& %s()' % function)[1].split('return auras;')[0]
            for name, ids in re.findall(r'\{\s*"([^"]+)",\s*[\w |]+?,\s*\d+,\s*\{\s*(\d+)', body):
                core[(cls, name)] = int(ids)
        self.assertEqual(GENERATOR_MODULE.AURA_FIRST_SPELL, core)

    def test_real_talents_reserve_nothing(self):
        self.assertEqual(0, GENERATOR_MODULE.reserved_points(4, 'rogue tank', 60, frozenset({4})))
        self.assertEqual(20, GENERATOR_MODULE.reserved_points(4, 'rogue tank', 60, frozenset({7})))

    def test_real_talent_target_reads_the_link_without_the_new_talents(self):
        # Page 1 holds two old talents (ids 1, 2) and, between them in tree order,
        # the rogue 'agility' talent (rank 1 = 61160). The link '-32' was written
        # for the old tree, so its digits must land on 1 and 2, not on the new one.
        entries = [talent(1, 1, 5), talent(50, 1, 5), talent(2, 1, 5)]
        entries[1]['rankIDs'] = [61160, 61161, 61162, 61163, 61164]
        for t in entries[::2]:
            t['rankIDs'] = [t['id'] * 10]
        others = []
        for n, (cls, name, _, ranks, _) in enumerate(GENERATOR_MODULE.SPEC_AURAS):
            if cls == 4 and name != 'agility':
                extra = talent(100 + n, 2, ranks)
                first = GENERATOR_MODULE.AURA_FIRST_SPELL[(4, name)]
                extra['rankIDs'] = list(range(first, first + ranks))
                others.append(extra)
        target = GENERATOR_MODULE.real_talent_target(4, 'combat', '-32', entries + others)
        self.assertEqual(target[1], 3)
        self.assertEqual(target[2], 2)
        self.assertEqual(target[50], 5)          # agility, full rank, paid by 4.0
        self.assertNotIn(100, target)             # damage from behind is 4.1 only

    def test_real_talent_target_fails_closed_without_the_talents(self):
        with self.assertRaises(ValueError):
            GENERATOR_MODULE.real_talent_target(4, 'combat', '-32', [talent(1, 1, 5)])



class ShamanStageTwoTalentTests(unittest.TestCase):
    """#357 stage 2 (core#217): the shaman aura talents and the weapon talent W
    through the shared real-talent path of #219."""

    def entries(self, with_w=True):
        # Page 1: two old Enhancement talents with W between them in tree order.
        entries = [talent(1, 1, 5), talent(2, 1, 5)]
        entries[0]['rankIDs'] = [10]
        entries[1]['rankIDs'] = [20]
        if with_w:
            w = talent(9010, 1, 1)
            w['rankIDs'] = [61131]
            entries.insert(1, w)
        for n, (cls, name, _, ranks, _) in enumerate(GENERATOR_MODULE.SPEC_AURAS):
            if cls == 7:
                aura = talent(9001 + n, 2, ranks)
                first = GENERATOR_MODULE.AURA_FIRST_SPELL[(7, name)]
                aura['rankIDs'] = list(range(first, first + ranks))
                entries.append(aura)
        return entries

    def expected(self, name):
        by_first = {entry['rankIDs'][0]: entry['id'] for entry in self.entries()}
        target = {}
        for cls, aura, _, ranks, paths in GENERATOR_MODULE.SPEC_AURAS:
            if cls == 7 and name in paths:
                target[by_first[GENERATOR_MODULE.AURA_FIRST_SPELL[(7, aura)]]] = ranks
        return target

    def test_links_read_against_the_old_tree_plus_auras_and_w(self):
        for name in ('enhancement', 'shaman tank'):
            target = GENERATOR_MODULE.real_talent_target(7, name, '-32', self.entries())
            self.assertEqual(3, target[1], name)      # the link lands on the old talents,
            self.assertEqual(2, target[2], name)      # not on W between them
            self.assertEqual(1, target[9010], name)   # W, one point
            for talent_id, ranks in self.expected(name).items():
                self.assertEqual(ranks, target[talent_id], name)

    def test_w_points_on_top_of_the_phase_one_reserve(self):
        # O-12 variant A: 7.1 = 14 + W, 7.3 = 21 + W (24 + W since Charged Stormstrike has 4 ranks, #484).
        for name, reserve in (('enhancement', 14), ('shaman tank', 24)):
            self.assertEqual(reserve, GENERATOR_MODULE.reserved_points(7, name, 60), name)
            self.assertEqual(reserve, sum(self.expected(name).values()), name)

    def test_elemental_gets_no_w_but_reads_the_old_tree(self):
        target = GENERATOR_MODULE.real_talent_target(7, 'elemental', '-32', self.entries())
        self.assertNotIn(9010, target)
        self.assertEqual(3, target[1])
        self.assertEqual(2, target[2])

    def test_patched_tree_without_w_fails_closed(self):
        with self.assertRaises(ValueError):
            GENERATOR_MODULE.real_talent_target(7, 'enhancement', '-32', self.entries(with_w=False))


class ShippedSpecIdentityTests(unittest.TestCase):
    """twow-repo#602 R06a: the spec of a premade path comes from the path, never
    from the point majority (owner 2026-10-18)."""

    @classmethod
    def setUpClass(cls):
        (cls.names, cls.links, cls.tabs, cls.tab_after,
         cls.rate, cls.talent_classes) = shipped_block()

    def expected(self, key):
        return GENERATOR_MODULE.EXPECTED_TREE_PAGE[key[0]][self.names[key]]

    def test_header_rate_matches_the_level_60_links(self):
        self.assertEqual(2.0, self.rate)
        self.assertEqual(30, len(self.names))
        self.assertEqual(set(self.names), set(self.links))

    def test_level_10_links_start_in_the_expected_tree(self):
        # Drift guard: the first link is spent in EXPECTED_TREE_PAGE only.
        for key, name in sorted(self.names.items()):
            points = GENERATOR_MODULE.link_tree_points(self.links[key][10])
            expected = self.expected(key)
            self.assertGreater(points[expected], 0, (key, name))
            self.assertEqual(sum(points), points[expected], (key, name, points))

    def test_premade_spec_tab_matches_the_expected_tree(self):
        # twow-repo#602 R06a: every path carries its tab key; a removed key fails here (no skip).
        self.assertEqual(30, len(self.tabs))
        self.assertEqual(set(self.names), set(self.tabs))
        for key, name in sorted(self.names.items()):
            self.assertEqual(self.expected(key), self.tabs[key], (key, name))
            # The generator writes the key right after PremadeSpecProb.
            self.assertTrue(self.tab_after[key].startswith(
                'AiPlayerbot.PremadeSpecProb.%d.%d = ' % key), (key, self.tab_after[key]))

    def test_shipped_majority_flips_are_the_known_set(self):
        found = {}
        for key, name in sorted(self.names.items()):
            flips = GENERATOR_MODULE.simulate_server(
                self.links[key], self.rate, self.expected(key),
                lambda level, cls=key[0], name=name: GENERATOR_MODULE.reserved_points(
                    cls, name, level, self.talent_classes))
            summary = GENERATOR_MODULE.summarize_flips(flips)
            if summary is not None:
                found[(key[0], name)] = summary
        self.assertEqual(KNOWN_MAJORITY_FLIPS, found)

    def test_rate_one_paths_keep_the_main_tree(self):
        # The shipped block is Rate 2, so the Rate-1 case is rebuilt from BUILDS
        # with the generator's own prefix() over a synthetic tree (one entry per
        # link digit; prefix() checks no DBC rule, so the digits are all it needs).
        # The talent classes (4, 7) are left out: their Rate-1 target adds the aura
        # talents at Talent.dbc positions - the DBC run --rate 1 --check-levels
        # covers them.
        checked = 0
        for cls, builds in sorted(GENERATOR_MODULE.BUILDS.items()):
            for index, (name, link) in enumerate(builds):
                with self.subTest(cls=cls, name=name):
                    if cls in self.talent_classes:
                        self.skipTest('class %d: aura talents need Talent.dbc' % cls)
                    entries, next_id = [], 1
                    for page, part in enumerate((link.split('-') + ['', ''])[:3]):
                        for _ in part:
                            entries.append(talent(next_id, page, 5))
                            next_id += 1
                    target = GENERATOR_MODULE.parse_link(link, entries)
                    points = [GENERATOR_MODULE.page_points(entries, target, page)
                              for page in (0, 1, 2)]
                    main = GENERATOR_MODULE.EXPECTED_TREE_PAGE[cls][name]
                    self.assertEqual(main, max((0, 1, 2), key=lambda page: points[page]))
                    links = {}
                    for level in GENERATOR_MODULE.LEVELS:
                        budget = level - 9 - GENERATOR_MODULE.reserved_points(cls, name, level)
                        links[level] = GENERATOR_MODULE.build_link(
                            entries, GENERATOR_MODULE.prefix(entries, target, main, budget))
                    self.assertEqual([], GENERATOR_MODULE.simulate_server(
                        links, 1.0, main,
                        lambda level: GENERATOR_MODULE.reserved_points(cls, name, level)))
                    checked += 1
        self.assertEqual(22, checked)


class SideTreeCapTests(unittest.TestCase):
    """--side-tree-cap: side trees in SIDE_TREE_ORDER, each below the main tree."""

    def setUp(self):
        # Main tree page 1 holds 10 points; each side tree 15.
        self.entries = ([talent(10 + n, 0, 5) for n in range(3)] +
                        [talent(20 + n, 1, 5) for n in range(2)] +
                        [talent(30 + n, 2, 5) for n in range(3)])

    def points(self, ranks):
        return [GENERATOR_MODULE.page_points(self.entries, ranks, page) for page in (0, 1, 2)]

    def test_uncapped_fill_lets_a_side_tree_pass_the_main_tree(self):
        ranks = GENERATOR_MODULE.extend_target(self.entries, {}, 1, 25, set())
        self.assertEqual([15, 10, 0], self.points(ranks))
        self.assertTrue(GENERATOR_MODULE.side_tree_reaches_main(self.entries, ranks, 1))

    def test_cap_fills_the_role_tree_first_and_stays_below_the_main_tree(self):
        ranks = GENERATOR_MODULE.extend_target(self.entries, {}, 1, 25, set(),
                                               side_order=[2, 0], side_cap=True)
        self.assertEqual([6, 10, 9], self.points(ranks))
        self.assertFalse(GENERATOR_MODULE.side_tree_reaches_main(self.entries, ranks, 1))
        # Every level's prefix keeps the order: main, then page 2, then page 0.
        self.assertEqual([0, 10, 5], self.points(GENERATOR_MODULE.legal_prefix(
            self.entries, ranks, 1, 15, set(), [2, 0])))
        self.assertEqual([0, 10, 5], self.points(GENERATOR_MODULE.prefix(
            self.entries, ranks, 1, 15, [2, 0])))
        for budget in range(1, 26):
            self.assertFalse(GENERATOR_MODULE.side_tree_reaches_main(
                self.entries, GENERATOR_MODULE.legal_prefix(
                    self.entries, ranks, 1, budget, set(), [2, 0]), 1), budget)

    def test_cap_stops_short_when_the_points_do_not_fit(self):
        # 10 + 9 + 9 = 28 < 30: the generator reports the path BROKEN.
        ranks = GENERATOR_MODULE.extend_target(self.entries, {}, 1, 30, set(),
                                               side_order=[2, 0], side_cap=True)
        self.assertEqual(28, sum(ranks.values()))

    def test_default_order_is_unchanged(self):
        self.assertEqual(GENERATOR_MODULE.tree_order(self.entries, 1),
                         GENERATOR_MODULE.tree_order(self.entries, 1, [0, 2]))

    def test_side_tree_order_names_the_side_trees_of_every_path(self):
        for cls, builds in GENERATOR_MODULE.BUILDS.items():
            for name, _ in builds:
                main = GENERATOR_MODULE.EXPECTED_TREE_PAGE[cls][name]
                self.assertEqual(sorted(GENERATOR_MODULE.SIDE_TREE_ORDER[(cls, name)]),
                                 [page for page in (0, 1, 2) if page != main], (cls, name))
        self.assertEqual(30, len(GENERATOR_MODULE.SIDE_TREE_ORDER))


class ServerSimulationTests(unittest.TestCase):
    """simulate_server mirrors GetBestPremadeSpec, CropTalents and highestTree."""

    def simulate(self, links, expected, rate=1.0, levels=None, reserved=None):
        return GENERATOR_MODULE.simulate_server(
            links, rate, expected, reserved,
            levels if levels is not None else GENERATOR_MODULE.VALIDATION_LEVELS)

    def test_link_tree_points(self):
        self.assertEqual((0, 0, 2), GENERATOR_MODULE.link_tree_points('--2'))
        self.assertEqual((3, 5, 0), GENERATOR_MODULE.link_tree_points('21-0302'))
        with self.assertRaises(ValueError):
            GENERATOR_MODULE.link_tree_points('1-2-3-4')

    def test_first_link_that_reaches_the_budget_is_used(self):
        # Level 12 (budget 3): the level 10 link holds 2 points, so the server
        # takes the level 20 one - all in tree 1 - and crops it to 3.
        flips = self.simulate({10: '2', 20: '-55555555555'}, 0, levels=[11, 12])
        self.assertEqual(1, len(flips))
        self.assertEqual((12, 20, 'sure'), (flips[0]['level'], flips[0]['link_level'],
                                            flips[0]['kind']))
        self.assertEqual([(0, 3, 0)], flips[0]['results'])

    def test_the_last_link_serves_budgets_above_it(self):
        # Level 30 (budget 21) is above every link: the level 15 one is used uncropped.
        flips = self.simulate({10: '1', 15: '-111111'}, 0, levels=[30])
        self.assertEqual((30, 15), (flips[0]['level'], flips[0]['link_level']))
        self.assertEqual([(0, 6, 0)], flips[0]['results'])
        self.assertEqual([], self.simulate({10: '1', 15: '-111111'}, 0, levels=[10]))

    def test_crop_takes_the_largest_tree_first(self):
        # 3/6/0 cropped to 5: tree 1 first -> 0/5/0.
        flips = self.simulate({10: '111-111111'}, 0, levels=[14])
        self.assertEqual([(0, 5, 0)], flips[0]['results'])
        self.assertEqual('sure', flips[0]['kind'])

    def test_equal_trees_are_cropped_in_both_orders(self):
        self.assertEqual([(0, 1, 2), (1, 0, 2)], GENERATOR_MODULE.crop_orders((5, 5, 0)))
        self.assertEqual(6, len(GENERATOR_MODULE.crop_orders((2, 2, 2))))
        flips = self.simulate({10: '11111-11111'}, 0, levels=[12])
        self.assertEqual([(0, 3, 0), (3, 0, 0)], flips[0]['results'])
        self.assertEqual('coin-toss', flips[0]['kind'])

    def test_an_exact_tie_is_judged_by_the_lower_page_rule(self):
        tie = {10: '11111-11111'}
        self.assertEqual('tie', self.simulate(tie, 0, levels=[19])[0]['kind'])
        self.assertEqual('sure', self.simulate(tie, 1, levels=[19])[0]['kind'])
        self.assertEqual([(5, 5, 0)], self.simulate(tie, 1, levels=[19])[0]['results'])

    def test_highest_tree_mirrors_the_core(self):
        cases = {(5, 5, 0): 0, (0, 5, 5): 1, (5, 0, 5): 0, (1, 5, 5): 1,
                 (5, 1, 5): 0, (3, 3, 3): 0, (0, 0, 7): 2, (2, 9, 1): 1}
        for points, tree in cases.items():
            self.assertEqual(tree, GENERATOR_MODULE.highest_tree(points), points)

    def test_reserved_points_lower_the_budget(self):
        # Budget 20 - 15 = 5 stays inside the level 10 link's tree 0.
        links = {10: '11111', 20: '11111-111111'}
        self.assertEqual(1, len(self.simulate(links, 0, levels=[29])))
        self.assertEqual([], self.simulate(links, 0, levels=[29], reserved=lambda level: 15))

    def test_summary(self):
        self.assertIsNone(GENERATOR_MODULE.summarize_flips([]))
        flips = [{'level': 51, 'kind': 'coin-toss'}, {'level': 58, 'kind': 'sure'},
                 {'level': 60, 'kind': 'tie'}]
        self.assertEqual((51, 'sure', 58), GENERATOR_MODULE.summarize_flips(flips))
        self.assertEqual((60, 'tie', None), GENERATOR_MODULE.summarize_flips(flips[2:]))


if __name__ == '__main__':
    unittest.main()
