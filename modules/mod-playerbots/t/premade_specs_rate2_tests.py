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
        for cls, spec, name in names:
            match = re.search(r'^AiPlayerbot\.PremadeSpecLink\.' + cls + r'\.' +
                              spec + r'\.60 = ([0-9-]+)$', config,
                              flags=re.MULTILINE)
            self.assertIsNotNone(match)
            # #357 O-12: 7.1 / 7.3 leave the points of their talent auras free.
            reserved = GENERATOR_MODULE.reserved_points(int(cls), name, 60)
            self.assertEqual(102 - reserved, sum(int(value) for value in match.group(1)
                                                 if value.isdigit()))

    def test_spec_aura_reserve_matches_the_owner_values(self):
        # O-12 (owner 2026-09-27): 7.1 pays 14, 7.3 pays 21 talent points at 60
        # (Ghost Wolf rank 3 is Improved Ghost Wolf 2/2 for everyone).
        self.assertEqual(14, GENERATOR_MODULE.reserved_points(7, 'enhancement', 60))
        self.assertEqual(21, GENERATOR_MODULE.reserved_points(7, 'shaman tank', 60))
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
        # the rogue 'agility' talent (rank 1 = 90159). The link '-32' was written
        # for the old tree, so its digits must land on 1 and 2, not on the new one.
        entries = [talent(1, 1, 5), talent(50, 1, 5), talent(2, 1, 5)]
        entries[1]['rankIDs'] = [90159, 90160, 90161, 90162, 90163]
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
            w['rankIDs'] = [90130]
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
        # O-12 variant A: 7.1 = 14 + W, 7.3 = 21 + W.
        for name, reserve in (('enhancement', 14), ('shaman tank', 21)):
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


if __name__ == '__main__':
    unittest.main()
