"""Integration invariants for loss-aware asset discovery."""

from pathlib import Path
import sys
import tempfile
import unittest


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))

from asset_discovery import AssetDiscovery  # noqa: E402
from asset_index import AssetIndex  # noqa: E402


from catalogue_fixture import make_catalogue


class AssetDiscoveryTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		self.index = AssetIndex(make_catalogue(Path(self.temporary.name)))
		self.discovery = AssetDiscovery(self.index, session_dir=Path(self.temporary.name))
		self.brief = {
			"scene": "outdoor guest area at a weathered desert motel",
			"roles": ["seating", "table", "cooking_fire", "waste", "lighting"],
			"styles": ["desert", "rural", "weathered"],
			"avoid": ["interior", "weapon", "building"],
		}
		self.coverage = self.discovery.start(self.brief, pool_limit=1500)

	def tearDown(self):
		self.temporary.cleanup()

	def test_entire_eligible_catalogue_is_considered(self):
		self.assertEqual(self.coverage["eligible_total"], self.coverage["considered_total"])

	def test_ranking_is_deterministic(self):
		second = self.discovery.start(self.brief, pool_limit=1500)
		first_ids = [item["id"] for item in self.discovery.results(
			self.coverage["session_id"], limit=100)["assets"]]
		second_ids = [item["id"] for item in self.discovery.results(
			second["session_id"], limit=100)["assets"]]
		self.assertEqual(first_ids, second_ids)

	def test_residual_audit_does_not_repeat_candidate_pool(self):
		session_id = self.coverage["session_id"]
		candidate_ids = {item["id"] for item in self.discovery.results(
			session_id, limit=1000)["assets"]}
		candidate_ids.update(item["id"] for item in self.discovery.results(
			session_id, offset=1000, limit=1000)["assets"])
		residual_ids = {item["id"] for item in self.discovery.residuals(
			session_id, limit=200)["assets"]}
		self.assertTrue(candidate_ids.isdisjoint(residual_ids))

	def test_open_family_pages_the_full_catalogue_family(self):
		session_id = self.coverage["session_id"]
		family = self.discovery.families(session_id, limit=1)["families"][0]["family"]
		page = self.discovery.open_family(session_id, family, limit=200)
		self.assertEqual(page["total"], len(self.index.family_assets(family)))

	def test_session_ids_cannot_escape_discovery_directory(self):
		with self.assertRaises(ValueError):
			self.discovery.coverage("../outside")


if __name__ == "__main__":
	unittest.main()
