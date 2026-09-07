"""Acceptance and adversarial tests for the robust Ariane agent contract."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))

from ariane_ipc import ArianeClient, ArianeError  # noqa: E402
from simulation_harness import SimulatedArianeEngine, audit_capabilities, run_simulation  # noqa: E402


class SimulatedEngineTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		root = Path(self.temporary.name)
		self.socket_path, self.state_dir = root / "engine.sock", root / "state"
		self.engine = SimulatedArianeEngine(self.socket_path, self.state_dir)
		self.engine.__enter__()
		self.client = ArianeClient(self.socket_path, timeout=1.0)

	def tearDown(self):
		self.engine.__exit__(None, None, None)
		self.temporary.cleanup()

	def test_capability_contract_is_machine_auditable(self):
		report = audit_capabilities(self.socket_path)
		self.assertTrue(report["ok"])
		self.assertEqual("simulated-engine-v1", report["build_id"])
		self.assertEqual([], report["missing_commands"])
		self.assertEqual(
			["rgb_capture", "screen_ray_depth", "screen_ray_object_id"],
			report["observation"],
		)

	def test_cli_default_socket_honors_environment(self):
		environment = dict(os.environ)
		environment["ARIANE_ENGINE_SOCKET"] = str(self.socket_path)
		result = subprocess.run([
			sys.executable, str(AGENT_DIR / "arianectl.py"), "session", "status",
		], env=environment, text=True, capture_output=True, check=True)
		self.assertIn(str(self.socket_path), environment["ARIANE_ENGINE_SOCKET"])
		self.assertIn('"active": false', result.stdout)

	def test_pagination_has_no_gaps_duplicates_or_oversized_pages(self):
		self.client.command("layer_open", "large_scene")
		expected = []
		for index in range(613):
			result = self.client.command("place", index, index % 31, index // 31, 0, "prop")
			expected.append(result["object"]["object_key"])
		actual, offset = [], 0
		while offset is not None:
			result = self.client.command("list_page", offset, 47)
			self.assertLessEqual(result["page"]["returned"], 47)
			actual.extend(item["object_key"] for item in result["items"])
			offset = result["page"]["next_offset"]
		self.assertEqual(expected, actual)
		self.assertEqual(len(actual), len(set(actual)))

	def test_zone_filter_is_applied_before_pagination(self):
		self.client.command("layer_open", "zoned")
		for index in range(20):
			self.client.command("place", index, index, 0, 0, "prop")
		page = self.client.command("inspect_zone_page", 0, 0, 2.1, 0, 3)
		self.assertEqual(3, page["page"]["total"])
		self.assertIsNone(page["page"]["next_offset"])

	def test_camera_and_screen_to_world_share_viewport_contract(self):
		self.client.command("layer_open", "camera")
		camera = self.client.command("camera_context")["camera"]
		self.assertEqual([1280, 720], camera["viewport"])
		hit = self.client.command("screen_to_world", 639.5, 359.5, 1280, 720)
		self.assertTrue(hit["hit"])
		for value in hit["world"]:
			self.assertAlmostEqual(0.0, value, places=5)
		self.assertTrue(hit["passes"]["depth"])
		self.assertTrue(hit["passes"]["object_id"])
		with self.assertRaisesRegex(ArianeError, "outside"):
			self.client.command("screen_to_world", 1280, 720, 1280, 720)

	def test_capture_pose_restores_full_camera_and_reports_provenance(self):
		before = self.client.command("camera_context")["camera"]
		path = self.state_dir / "capture.png"
		result = self.client.capture_at_pose(path, position=[10, 20, 30],
		                                     target=[1, 2, 3], up=[0, 1, 0],
		                                     fov=44, label="aerial")
		after = self.client.command("camera_context")["camera"]
		self.assertEqual(before["position"], after["position"])
		self.assertEqual(before["target"], after["target"])
		self.assertEqual(before["up"], after["up"])
		self.assertTrue(result["restored"])
		self.assertEqual("aerial", result["label"])
		self.assertEqual([10.0, 20.0, 30.0], result["actual_pose"]["position"])
		self.assertIn("forward", result["actual_pose"])
		self.assertIn("right", result["actual_pose"])
		self.assertIn("up_reference", result["actual_pose"])
		self.assertTrue(path.exists())

	def test_current_capture_reports_the_same_view_basis_as_camera_context(self):
		context = self.client.command("camera_context")["camera"]
		result = self.client.capture_current(self.state_dir / "basis.png", label="basis")
		for field in ("forward", "right", "up"):
			for actual, expected in zip(result["actual_pose"][field], context[field]):
				self.assertAlmostEqual(expected, actual)

	def test_empty_scene_survey_uses_explicit_center_and_labels_every_view(self):
		output = self.state_dir / "survey"
		manifest = self.client.capture_views(output, center=[100, 200, 12], span=80)
		self.assertEqual([100.0, 200.0, 12.0], manifest["center"])
		self.assertEqual({"aerial", "south", "west", "player"},
		                 {item["label"] for item in manifest["views"]})
		self.assertTrue(all(item["restored"] for item in manifest["views"]))
		self.assertTrue(Path(manifest["manifest_path"]).exists())
		self.assertTrue(all(Path(item["path"]).exists() for item in manifest["views"]))

	def test_survey_orbits_away_from_a_blocked_base_pose(self):
		self.engine.raycast_blocker = lambda start, _target: start[0] < -25 and start[1] < -25
		manifest = self.client.capture_views(
			self.state_dir / "visible-survey", center=[0, 0, 5], span=40)
		aerial = next(item for item in manifest["views"] if item["label"] == "aerial")
		self.assertTrue(aerial["framing"]["adjusted"])
		self.assertEqual(8, aerial["framing"]["visible_probes"])
		self.assertGreater(aerial["framing"]["candidate_count"], 1)
		self.assertLessEqual(abs(aerial["framing"]["azimuth_adjustment_degrees"]), 30)

	def test_survey_omits_unsatisfiable_below_ground_probe(self):
		manifest = self.client.capture_views(
			self.state_dir / "ground-survey", center=[0, 0, 5], span=40)
		for view in manifest["views"]:
			self.assertIn("low_below_support_plane", view["framing"]["omitted_probes"])
			self.assertEqual(8, view["framing"]["probe_count"])
			self.assertEqual(0, view["framing"]["azimuth_adjustment_degrees"])

	def test_stale_capture_pose_refuses_without_moving_user_camera(self):
		stale_revision = self.client.command("camera_context")["camera"]["camera_revision"]
		self.client.command("camera", 5, 6, 7, 0, 0, 0, 52)
		before = self.client.command("camera_context")["camera"]
		with self.assertRaisesRegex(ArianeError, "camera revision changed"):
			self.client.capture_at_pose(
				self.state_dir / "stale.png", position=[10, 20, 30], target=[0, 0, 0],
				expected_camera_revision=stale_revision)
		after = self.client.command("camera_context")["camera"]
		self.assertEqual(before, after)
		self.assertFalse((self.state_dir / "stale.png").exists())

	def test_asset_probe_documents_origin_bounds_and_identity_axes(self):
		asset = self.client.command("asset_probe", 1234, 0)["asset"]
		self.assertEqual("model_local_relative_to_origin", asset["bounds_space"])
		self.assertEqual([0, 1, 0], asset["identity_axes"]["forward"])

	def test_checkpoint_preserves_stable_keys_across_restart(self):
		self.client.command("layer_open", "persistent")
		key = self.client.command("place", 1234, 1, 2, 3, "prop")["object"]["object_key"]
		self.client.command("checkpoint_save", "accepted")
		self.engine.__exit__(None, None, None)
		self.engine = SimulatedArianeEngine(self.socket_path, self.state_dir)
		self.engine.__enter__()
		self.client = ArianeClient(self.socket_path, timeout=1.0)
		self.client.command("layer_open", "persistent")
		self.assertEqual(key, self.client.command("list_page", 0, 10)["items"][0]["object_key"])
		self.client.command("checkpoint_restore", "accepted")
		self.assertEqual(key, self.client.command("list_page", 0, 10)["items"][0]["object_key"])

	def test_minimal_semantic_validation_flags_route_clearance(self):
		self.client.command("layer_open", "semantics")
		blocked = self.client.command("place", 100, 0, 0, 0, "door_blocker")["object"]
		self.client.command("place", 101, 10, 0, 0, "decoration")
		result = self.client.command("validate_zone", 0, 0, 50, 0, 25, 1)
		self.assertFalse(result["valid"])
		self.assertEqual(1, result["summary"]["semantic_issues"])
		self.assertEqual(blocked["object_key"], result["items"][0]["object_key"])

	def test_response_budget_fails_closed(self):
		self.client.command("layer_open", "budget")
		for index in range(40):
			self.client.command("place", index, index, 0, 0, "prop")
		self.engine.response_budget = 500
		with self.assertRaisesRegex(ArianeError, "response budget exceeded"):
			self.client.command("list_page", 0, 40)

	def test_request_limit_is_enforced_before_transport(self):
		with self.assertRaisesRegex(ValueError, "1 MB"):
			self.client.command("ping", "x" * 1_048_577)


class EndToEndHarnessTests(unittest.TestCase):
	def test_default_simulation_report_passes(self):
		report = run_simulation()
		self.assertTrue(report["ok"], report)


if __name__ == "__main__":
	unittest.main()
