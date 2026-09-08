"""Closed-loop acceptance tests for agent-first scene composition primitives."""

from pathlib import Path
import json
import sys
import tempfile
import unittest


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))

from catalogue_fixture import make_catalogue
from ariane_ipc import ArianeError  # noqa: E402
from service import ArianeService, ScenePatchError  # noqa: E402
from simulation_harness import SimulatedArianeEngine  # noqa: E402


class CompositionV2Tests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		root = Path(self.temporary.name)
		self.socket_path = root / "engine.sock"
		self.engine_state = root / "engine"
		self.agent_state = root / "agent"
		self.discovery_dir = root / "discovery"
		self.database = make_catalogue(root, extra_models=[
			{"id": ident, "name": name, "dff": name, "txd": "fixture",
			 "category": "Objects", "source": "fixture.ide"}
			for ident, name in [(642, "test_canopy"), (2111, "folding_table"),
			                   (1810, "folding_chair"), (1811, "folding_chair_b"),
			                   (2000, "cooler_box"), (2001, "boombox_radio"),
			                   (2002, "glass_bottles"), (2003, "wooden_crates")]
		])
		self.engine = SimulatedArianeEngine(self.socket_path, self.engine_state)
		self.engine.__enter__()
		self.engine.dispatch("layer_open", ["composition"])
		self.service = ArianeService(engine_socket=self.socket_path,
		                             state_dir=self.agent_state, database=self.database,
		                             discovery_dir=self.discovery_dir, timeout=1.0)

	def tearDown(self):
		self.engine.__exit__(None, None, None)
		self.temporary.cleanup()

	@staticmethod
	def placements(count: int, *, prefix: str = "prop", group: str | None = None) -> list[dict]:
		return [{"action": "place", "key": f"{prefix}.{index:03d}", "model": 1000 + index,
		         "x": float(index % 20), "y": float(index // 20), "z": 0.0,
		         "heading": float(index % 7) * 11.0, "snap": False, "group": group}
		        for index in range(count)]

	def test_large_patch_returns_every_receipt_over_framed_stream(self):
		result = self.service.apply_scene_patch(
			self.placements(150, group="festival"), patch_id="mass-pass")
		self.assertEqual(150, result["applied"])
		self.assertEqual(150, len(result["objects"]))
		self.assertEqual(150, len(self.service.enumerate_scene()))
		self.assertGreater(len(str(result)), 1_800)

	def test_patch_id_is_idempotent_and_stale_revision_fails_closed(self):
		revision = self.service.engine("session_status")["scene_revision"]
		first = self.service.apply_scene_patch(
			self.placements(2), patch_id="replay-safe", expected_revision=revision)
		second = self.service.apply_scene_patch(
			self.placements(2), patch_id="replay-safe")
		self.assertFalse(first["replayed"])
		self.assertTrue(second["replayed"])
		self.assertEqual(2, len(self.service.enumerate_scene()))
		with self.assertRaisesRegex(RuntimeError, "scene revision changed"):
			self.service.apply_scene_patch(self.placements(1, prefix="late"),
			                               patch_id="stale", expected_revision=revision)

	def test_reused_session_reconciles_rolled_back_ledger(self):
		self.service.apply_scene_patch(self.placements(2), patch_id="same-name")
		self.service.engine("clear")
		second = self.service.apply_scene_patch(self.placements(2), patch_id="same-name")
		self.assertFalse(second["replayed"])
		self.assertEqual(2, len(self.service.enumerate_scene()))

	def test_keys_groups_and_rigid_transform_survive_service_restart(self):
		self.service.apply_scene_patch(
			self.placements(3, group="lounge"), patch_id="group-seed")
		self.service = ArianeService(engine_socket=self.socket_path,
		                             state_dir=self.agent_state, database=self.database,
		                             discovery_dir=self.discovery_dir, timeout=1.0)
		self.assertEqual(3, self.service.list_groups()["groups"][0]["member_count"])
		before = {item["object_key"]: item for item in self.service.enumerate_scene()}
		self.service.transform_group("lounge", dx=5.0, dy=-2.0, dheading=90.0,
		                             patch_id="move-lounge")
		after = {item["object_key"]: item for item in self.service.enumerate_scene()}
		self.assertEqual(set(before), set(after))
		for key in before:
			self.assertAlmostEqual(before[key]["rotation"][2] + 90.0,
			                       after[key]["rotation"][2])

	def test_clone_and_delete_group_manage_whole_compositions(self):
		self.service.apply_scene_patch(
			self.placements(4, group="cluster"), patch_id="cluster-seed")
		clone = self.service.clone_group("cluster", "cluster-b", "clone", dx=20.0,
		                                 patch_id="clone-cluster")
		self.assertEqual(4, clone["applied"])
		self.assertEqual(8, len(self.service.enumerate_scene()))
		self.service.delete_group("cluster-b", patch_id="delete-clone")
		self.assertEqual(4, len(self.service.enumerate_scene()))

	def test_mid_patch_failure_compensates_places_and_transforms(self):
		self.service.apply_scene_patch(self.placements(1), patch_id="original")
		original = self.service.enumerate_scene()[0]
		self.engine.fail_next["transform"] = 1
		with self.assertRaisesRegex(ArianeError, "injected transform failure"):
			self.service.apply_scene_patch([
				{"action": "place", "key": "temporary", "model": 2000,
				 "x": 9, "y": 9, "z": 0, "heading": 0, "snap": False},
				{"action": "transform", "key": "prop.000",
				 "x": 4, "y": 4, "z": 0, "heading": 90, "snap": False},
			], patch_id="must-rollback")
		objects = self.service.enumerate_scene()
		self.assertEqual(1, len(objects))
		self.assertEqual(original["position"], objects[0]["position"])

	def test_explicit_support_converts_known_false_positive_to_acknowledged(self):
		operations = self.placements(2)
		operations[0].update({"key": "table", "z": 0.0})
		operations[1].update({"key": "bottle", "x": 0.0, "y": 0.0, "z": 0.0,
		                      "supported_by": "table"})
		self.service.apply_scene_patch(operations, patch_id="support-seed")
		result = self.service.snap_to_support("bottle", "table", clearance=0.02)
		self.assertAlmostEqual(1.02, result["z"])
		validation = self.service.validate_composition()
		self.assertTrue(validation["valid"])
		self.assertEqual("table", validation["acknowledged_supports"][0]["supported_by"])
		self.assertEqual([], validation["support_issues"])

	def test_support_annotation_cannot_hide_bad_geometry(self):
		operations = self.placements(2)
		operations[0].update({"key": "table", "z": 0.0})
		operations[1].update({"key": "bottle", "x": 12.0, "z": 1.0,
		                      "supported_by": "table"})
		self.service.apply_scene_patch(operations, patch_id="bad-support")
		validation = self.service.validate_composition()
		self.assertFalse(validation["valid"])
		self.assertEqual("invalid_support_geometry", validation["support_issues"][0]["code"])

	def test_optional_z_and_same_patch_support_snap_are_resolved_before_mutation(self):
		result = self.service.apply_scene_patch([
			{"action": "place", "key": "table", "model": 1000,
			 "x": 2.0, "y": 3.0, "heading": 0.0, "snap": True},
			{"action": "place", "key": "bottle", "model": 1001,
			 "x": 2.0, "y": 3.0, "heading": 0.0, "snap": True,
			 "supported_by": "table", "support_mode": "snap", "clearance": 0.02},
		], patch_id="support-in-one")
		self.assertEqual(2, result["applied"])
		objects = {item["object_key"]: item for item in self.service.enumerate_scene()}
		self.assertAlmostEqual(0.0, objects["table"]["position"][2])
		self.assertAlmostEqual(1.02, objects["bottle"]["position"][2])

	def test_bbox_relative_front_placement_uses_parent_heading(self):
		plan = self.service.resolve_scene_patch([
			{"action": "place", "key": "stall", "model": 100,
			 "x": 10, "y": 20, "heading": 90, "snap": True},
			{"action": "place_relative", "key": "sign", "model": 101,
			 "relative_to": "stall", "relation": "in_front", "gap": 0.25,
			 "snap": False},
		])
		sign = plan["operations"][1]
		# Harness bounds are [-.5, .5], so front separation is 1.25 local Y.
		self.assertAlmostEqual(8.75, sign["x"])
		self.assertAlmostEqual(20.0, sign["y"])
		self.assertEqual(90.0, sign["heading"])

	def test_unavailable_model_fails_preflight_without_mutation(self):
		revision = self.service.engine("session_status")["scene_revision"]
		with self.assertRaises(ScenePatchError) as raised:
			self.service.apply_scene_patch([
				{"action": "place", "key": "missing", "model": 99999,
				 "x": 0, "y": 0, "heading": 0, "snap": True},
			], patch_id="missing-model")
		self.assertEqual("asset_unavailable", raised.exception.payload["code"])
		self.assertEqual("missing", raised.exception.payload["key"])
		self.assertEqual(revision, self.service.engine("session_status")["scene_revision"])
		self.assertEqual([], self.service.enumerate_scene())

	def test_resolved_plan_is_deterministic_and_refuses_revision_drift(self):
		macro = [{"action": "array_line", "key": "chairs", "model": 1000,
		          "count": 3, "x": 0, "y": 0, "x2": 4, "y2": 0,
		          "heading": 90, "snap": True, "seed": "fixed"}]
		first = self.service.resolve_scene_patch(macro)
		second = self.service.resolve_scene_patch(macro)
		self.assertEqual(first["plan_id"], second["plan_id"])
		self.assertEqual(3, first["operation_count"])
		self.assertTrue(first["preflight"]["valid"])
		self.assertEqual(3, first["preflight"]["evaluated_operations"])
		self.assertTrue(all(operation["support_evaluation"]["valid"]
		                    for operation in first["operations"]))
		self.service.apply_scene_patch(self.placements(1, prefix="drift"), patch_id="drift")
		with self.assertRaisesRegex(ScenePatchError, "scene revision changed"):
			self.service.apply_resolved_plan(first["plan_id"])

	def test_array_and_direct_headings_are_normalized_in_resolved_plan(self):
		plan = self.service.resolve_scene_patch([
			{"action": "array_ring", "key": "chairs", "model": 1000,
			 "count": 4, "x": 0, "y": 0, "radius": 2,
			 "heading": 180, "heading_noise": 20, "snap": True, "seed": "angles"},
			{"action": "place", "key": "single", "model": 1001,
			 "x": 8, "y": 8, "heading": 450.0, "snap": True},
		])
		self.assertTrue(all(0.0 <= operation["heading"] < 360.0
		                    for operation in plan["operations"]))

	def test_preflight_blocks_hypothetical_prop_overlap_before_mutation(self):
		plan = self.service.resolve_scene_patch([
			{"action": "place", "key": "chair_a", "model": 1810,
			 "x": 0, "y": 0, "z": 0, "heading": 0, "snap": False},
			{"action": "place", "key": "chair_b", "model": 1811,
			 "x": 0, "y": 0, "z": 0, "heading": 0, "snap": False},
		])
		self.assertFalse(plan["preflight"]["valid"])
		self.assertEqual("chair_a", plan["preflight"]["prop_overlaps"][0]["a_key"])
		with self.assertRaises(ScenePatchError) as raised:
			self.service.apply_resolved_plan(plan["plan_id"])
		self.assertEqual("plan_invalid", raised.exception.payload["code"])
		self.assertEqual([], self.service.enumerate_scene())

	def test_shelter_containment_is_informational_in_preflight(self):
		plan = self.service.resolve_scene_patch([
			{"action": "place", "key": "canopy", "model": 642,
			 "x": 0, "y": 0, "z": 0, "heading": 0, "snap": False},
			{"action": "place", "key": "table", "model": 2111,
			 "x": 0, "y": 0, "z": 0, "heading": 0, "snap": False,
			 "supported_by": "canopy"},
		])
		self.assertTrue(plan["preflight"]["valid"])
		self.assertEqual([], plan["preflight"]["prop_overlaps"])
		self.assertEqual("contained_under_shelter",
		                 plan["preflight"]["intentional_containments"][0]["code"])

	def test_quick_discovery_is_compact_and_diverse_across_brief_concepts(self):
		result = self.service.discover_assets(
			"folding table, folding chairs, drum grill, cooler box, crates, boombox radio, bottles",
			limit=28)
		names = {str(asset["name"]).lower() for asset in result["assets"]}
		self.assertEqual("compact", result["detail"])
		self.assertEqual(7, len(result["concepts"]))
		self.assertLess(len(json.dumps(result).encode()), 20_000)
		self.assertTrue(any("table" in name for name in names))
		self.assertTrue(any("grill" in name or "barbeque" in name for name in names))
		self.assertTrue(any("cool" in name for name in names))
		self.assertTrue(any("crate" in name for name in names))

	def test_discovery_splits_scene_prefix_from_first_colon_concept(self):
		self.assertEqual(
			["garden terrace nook", "patio table", "folding chairs"],
			self.service._discovery_concepts(
				"garden terrace nook: patio table, folding chairs"),
		)

	def test_small_exact_duplicates_are_invalid_and_keyed(self):
		operations = self.placements(2)
		operations[1].update({"model": operations[0]["model"], "x": operations[0]["x"],
		                      "y": operations[0]["y"], "heading": operations[0]["heading"]})
		self.service.apply_scene_patch(operations, patch_id="duplicates")
		result = self.service.validate_composition()
		self.assertFalse(result["valid"])
		self.assertEqual("prop.000", result["exact_duplicates"][0]["a_key"])
		self.assertEqual("prop.001", result["exact_duplicates"][0]["b_key"])


if __name__ == "__main__":
	unittest.main()
