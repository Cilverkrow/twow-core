#!/usr/bin/env python3
"""Focused deterministic unit tests for the higher-rate premade path filler."""
import importlib.util
import pathlib
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


if __name__ == '__main__':
    unittest.main()
