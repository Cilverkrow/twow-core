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



class ShamanStageTwoTalentTests(unittest.TestCase):
    """#357 stage 2: the patched Talent.dbc adds 9001-9010 to the Enhancement tab."""

    def entries(self, with_new):
        # Two old Enhancement talents (page 1) around a new one in tree order.
        old = [dict(talent(251, 1, 5), row=0, col=1), dict(talent(254, 1, 5), row=1, col=1)]
        new = [dict(talent(talent_id, 1, 5), row=0, col=0)
               for talent_id in sorted(GENERATOR_MODULE.STAGE2_TALENTS[7])]
        entries = old + (new if with_new else [])
        entries.sort(key=lambda entry: (entry['page'], entry['row'], entry['col'], entry['id']))
        return entries

    def test_links_are_read_against_the_old_tree_and_new_talents_added_by_id(self):
        target = GENERATOR_MODULE.read_target(7, 'enhancement', '-53', self.entries(True), {7})
        expected = {251: 5, 254: 3}
        expected.update(GENERATOR_MODULE.STAGE2_TARGETS[(7, 'enhancement')])
        self.assertEqual(expected, target)

    def test_unpatched_tree_is_unchanged(self):
        self.assertEqual({251: 5, 254: 3},
                         GENERATOR_MODULE.read_target(7, 'enhancement', '-53', self.entries(False)))

    def test_patch_and_flag_must_agree(self):
        with self.assertRaises(ValueError):
            GENERATOR_MODULE.read_target(7, 'enhancement', '-53', self.entries(True))
        with self.assertRaises(ValueError):
            GENERATOR_MODULE.read_target(7, 'enhancement', '-53', self.entries(False), {7})

    def test_talent_classes_pay_nothing_for_auras(self):
        self.assertEqual(0, GENERATOR_MODULE.reserved_points(7, 'shaman tank', 60, {7}))
        self.assertEqual(20, GENERATOR_MODULE.reserved_points(4, 'rogue tank', 60, {7}))

    def test_new_talents_carry_the_phase_one_points_plus_w(self):
        # O-12 variant A: bots keep what they paid for the auras (7.1 = 14, 7.3 = 21)
        # and add the one point of the weapon talent W (9010).
        for name in ('enhancement', 'shaman tank'):
            target = GENERATOR_MODULE.STAGE2_TARGETS[(7, name)]
            self.assertEqual(GENERATOR_MODULE.reserved_points(7, name, 60) + 1,
                             sum(target.values()), name)
            self.assertEqual(1, target[9010], name)
            self.assertTrue(set(target) <= GENERATOR_MODULE.STAGE2_TALENTS[7], name)

if __name__ == '__main__':
    unittest.main()
