"""中文：验证题库工具的唯一解与逻辑分级边界。
English: verify uniqueness checks and logical grading boundaries.
"""
import unittest
import json
import sudoku_bank as bank
from verify_delivery import exact_cover_solutions

CLASSIC = '530070000600195000098000060800060003400803001700020006060000280000419005000080079'
SOLUTION = '534678912672195348198342567859761423426853791713924856961537284287419635345286179'


class BankTests(unittest.TestCase):
    def test_unique_and_exact_solution(self):
        count, solution = bank.count_solutions(CLASSIC)
        self.assertEqual((count, solution), (1, SOLUTION))

    def test_independent_solver_rejects_nonunique_and_invalid(self):
        self.assertEqual(exact_cover_solutions(CLASSIC), [SOLUTION])
        self.assertEqual(exact_cover_solutions('5'*81), [])
        self.assertEqual(len(exact_cover_solutions('0'*81)), 2)

    def test_rejects_conflicts_and_multiple_solutions(self):
        self.assertEqual(bank.count_solutions('55' + CLASSIC[2:])[0], 0)
        self.assertEqual(bank.count_solutions('0'*81)[0], 2)
        with self.assertRaises(ValueError):
            bank.count_solutions('0'*80)

    def test_classic_is_singles_only(self):
        result = bank.logical_solve(CLASSIC)
        self.assertEqual(result['solution'], SOLUTION)
        self.assertEqual(result['difficulty'], 'easy')
        self.assertTrue(bank.verify_trace(CLASSIC, result['trace'], SOLUTION))
        # 单数可解不再等于入门；经典题线索仍少于中等下限。/ Singles-solvable no longer means beginner; this classic has too few clues even for medium.
        self.assertIsNone(bank.product_tier(CLASSIC, result))

    def test_delivered_difficulty_curve(self):
        records = json.loads((bank.ROOT/'bank/puzzles.json').read_text(encoding='utf-8'))['records']
        for record in records:
            grid = record['grid']
            result = bank.logical_solve(grid)
            self.assertEqual(bank.product_tier(grid, result), record['difficulty'], record['id'])
            if record['difficulty'] == 'easy':
                self.assertTrue(44 <= record['clues'] <= 50)
                self.assertTrue(all(s['technique'] == 'naked_single' for s in result['trace']))
                metrics = bank.entry_metrics(grid)
                self.assertGreaterEqual(metrics['initial_singles'], 8)
                self.assertGreaterEqual(metrics['min_singles_until_last_seven'], 3)
                self.assertGreaterEqual(metrics['min_unit_clues'], 3)
            elif record['difficulty'] == 'medium':
                self.assertTrue(34 <= record['clues'] <= 40)
                self.assertTrue(all(bank.RANK[s['technique']] == 0 for s in result['trace']))

    def test_full_grid_is_not_a_beginner_puzzle(self):
        self.assertIsNone(bank.product_tier(SOLUTION, bank.logical_solve(SOLUTION)))

    def test_trace_tampering_is_rejected(self):
        result = bank.logical_solve(CLASSIC)
        trace = [dict(t) for t in result['trace']]
        trace[0]['digit'] = 1 if trace[0]['digit'] != 1 else 2
        with self.assertRaises(AssertionError):
            bank.verify_trace(CLASSIC, trace, SOLUTION)

    def test_pair_removes_only_other_unit_candidates(self):
        candidates = {0: {1, 2}, 1: {1, 2}, 2: {1, 2, 3}, 9: {1, 3}}
        step = bank.find_naked_pair(candidates)
        self.assertEqual(step['technique'], 'naked_pair')
        self.assertIn({'cell': 2, 'digits': [1, 2]}, step['removals'])
        self.assertNotIn(9, [x['cell'] for x in step['removals']])

    def test_no_guessing_fallback(self):
        self.assertIsNone(bank.logical_solve('0'*81)['difficulty'])

    def test_conflicting_complete_grid_is_not_solved(self):
        self.assertIsNone(bank.logical_solve('5'*81)['difficulty'])

    def test_locked_candidate_witness(self):
        step = bank.find_locked({0: {1, 2}, 1: {1, 3}, 9: {1, 4}, 10: {2, 4}})
        self.assertEqual(step['technique'], 'locked_candidate')
        self.assertIn({'cell': 9, 'digits': [1]}, step['removals'])

    def test_hidden_pair_and_triple_witnesses(self):
        step = bank.find_hidden_pair({0: {1, 2, 3}, 1: {1, 2, 4}, 2: {3, 4}})
        self.assertEqual(step['pattern'], [0, 1])
        self.assertEqual(step['removals'], [{'cell': 0, 'digits': [3]}, {'cell': 1, 'digits': [4]}])
        step = bank.find_subset({0: {1, 2}, 1: {2, 3}, 2: {1, 3}, 3: {1, 2, 3, 4}}, 3)
        self.assertEqual(step['removals'], [{'cell': 3, 'digits': [1, 2, 3]}])

    def test_xwing_requires_exact_two_positions(self):
        candidates = {1: {5, 6}, 7: {5, 7}, 28: {5, 8}, 34: {5, 9}, 46: {2, 5}}
        step = bank.find_xwing(candidates)
        self.assertEqual(step['technique'], 'x_wing')
        self.assertIn({'cell': 46, 'digits': [5]}, step['removals'])
        candidates[2] = {3, 5}
        self.assertIsNone(bank.find_xwing(candidates))


if __name__ == '__main__':
    unittest.main()
