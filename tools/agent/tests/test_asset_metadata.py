"""Cheap safeguards for contradictory semantic asset metadata."""

from pathlib import Path
import sys
import unittest


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))

from asset_index import _decorate_asset  # noqa: E402


class AssetMetadataTests(unittest.TestCase):
	def test_large_structure_described_as_scatter_is_flagged(self):
		asset = _decorate_asset({
			"id": 1,
			"max_dim": 22.5,
			"semantic_description": "informal party scatter with beer cans",
		})
		self.assertIn("semantic_scale_conflict", asset["metadata_warnings"])

	def test_consistent_metadata_is_not_flagged(self):
		asset = _decorate_asset({
			"id": 2,
			"max_dim": 22.5,
			"semantic_description": "large brewery structure",
		})
		self.assertEqual([], asset["metadata_warnings"])

	def test_table_category_described_as_shed_is_flagged(self):
		asset = _decorate_asset({
			"id": 2762,
			"max_dim": 2.0921,
			"prineside_category": "Interior objects > Tables and chairs",
			"semantic_description": "Simple wooden shack or garden shed with one door",
		})
		self.assertIn("semantic_role_conflict", asset["metadata_warnings"])


if __name__ == "__main__":
	unittest.main()
