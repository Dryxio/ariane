"""SA-MP/open.mp asset setup and Pawn mapping importer tests."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))

from asset_index import AssetIndex, build_index  # noqa: E402
from samp import install_samp_assets, parse_pawn_mapping, parse_pawn_text, pawn_operations  # noqa: E402
from service import ArianeService  # noqa: E402


class PawnParserTests(unittest.TestCase):
	def test_literal_mapping_variants_comments_and_full_rotation(self):
		mapping = parse_pawn_text("""
			// CreateObject(999, 0, 0, 0, 0, 0, 0);
			new object = CreateObject(1000, 1, 2, 3, 40, 5, 270);
			CreateDynamicObject(1001, 4, 5, 6, 0, 0, -1.5, -1, -1);
			CreatePlayerObject(playerid, 1002, 7, 8, 9, 0, 0, 90);
			RemoveBuildingForPlayer(playerid, 708, -1, -2, 34.5, 0.25);
			SetObjectMaterial(object, 0, 1000, "txd", "texture", 0);
		""", "mapping.pwn")
		self.assertEqual([1000, 1001, 1002], [item.model for item in mapping.objects])
		self.assertEqual(708, mapping.removals[0].model)
		self.assertEqual(0.25, mapping.removals[0].radius)
		self.assertEqual("material_not_supported", mapping.warnings[0]["code"])
		placements, rotations = pawn_operations(mapping)
		self.assertEqual(3, len(placements))
		self.assertEqual(1, len(rotations))
		self.assertEqual([40.0, 5.0, 270.0], [
			rotations[0]["pitch"], rotations[0]["roll"], rotations[0]["heading"]])

	def test_non_literal_mapping_call_is_reported_not_guessed(self):
		mapping = parse_pawn_text("CreateObject(MODEL_ID, x, 2.0, 3.0, 0.0, 0.0, 0.0);", "variables.pwn")
		self.assertEqual([], mapping.objects)
		self.assertEqual("non_literal_call", mapping.warnings[0]["code"])

	def test_file_extension_is_required(self):
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "mapping.txt"
			path.write_text("CreateObject(1,0,0,0,0,0,0);")
			with self.assertRaisesRegex(ValueError, r"\.pwn"):
				parse_pawn_mapping(path)


class SampAssetTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		self.root = Path(self.temporary.name)
		self.gta = self.root / "gta"
		(self.gta / "DATA").mkdir(parents=True)
		(self.gta / "DATA" / "base.ide").write_text(
			"objs\n1000, base_model, base_txd, 100.0, 0\nend\n", encoding="latin-1")
		self.source = self.root / "openmp" / "SAMP"
		self.source.mkdir(parents=True)
		(self.source / "SAMP.ide").write_text(
			"objs\n19552, Plane125x125Conc2, cs_ebridge, 599.0, 2097152\nend\n",
			encoding="latin-1")
		(self.source / "SAMP.img").write_bytes(b"VER2")
		(self.source / "SAMPCOL.img").write_bytes(b"VER2")

	def tearDown(self):
		self.temporary.cleanup()

	def test_installs_user_assets_without_samp_ipl_and_indexes_declared_ide(self):
		result = install_samp_assets(self.gta, self.source)
		target = Path(result["target"])
		self.assertTrue((target / "models" / "SAMP.img").exists())
		self.assertTrue((target / "data" / "maps" / "samp" / "SAMP.ide").exists())
		self.assertFalse((target / "SAMP.ipl").exists())
		database = self.root / "assets.sqlite3"
		index = build_index(database=database, gta_dir=self.gta)
		self.assertEqual(2, index["asset_count"])
		self.assertEqual("Plane125x125Conc2", AssetIndex(database).inspect(19552)["name"])

	def test_existing_generated_package_requires_force(self):
		install_samp_assets(self.gta, self.source)
		with self.assertRaisesRegex(FileExistsError, "--force"):
			install_samp_assets(self.gta, self.source)


class PawnImportServiceTests(unittest.TestCase):
	def test_import_places_then_restores_xyz_rotation_and_suppresses_native(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root / "mapping.pwn"
			source.write_text("""
				CreateObject(1000, 1, 2, 3, 40, 0, 270);
				CreateObject(1001, 4, 5, 6, 0, 0, 90);
				RemoveBuildingForPlayer(playerid, 708, 10, 20, 30, 0.25);
			""")
			service = ArianeService.__new__(ArianeService)
			state = {"active": False, "revision": 7}
			engine_calls = []

			def engine(command, fields=None):
				engine_calls.append((command, fields or []))
				if command == "session_status":
					return {"active": state["active"], "scene_revision": state["revision"]}
				if command == "session_begin": state["active"] = True
				if command in {"session_commit", "session_rollback"}: state["active"] = False
				return {"ok": True}

			service.engine = Mock(side_effect=engine)
			service.apply_scene_patch = Mock(side_effect=lambda operations, **kwargs: {"objects": operations})
			service.suppress_native_models = Mock(return_value={"suppressed_count": 1})
			result = service.import_pawn_mapping(
				str(source), "ariane/mapping.ipl", str(root / "mapping.ipl"),
				commit=True, save=True)
			self.assertEqual(2, result["object_count"])
			self.assertEqual(1, result["full_rotation_count"])
			self.assertEqual("transform3d", service.apply_scene_patch.call_args_list[1].args[0][0]["action"])
			service.suppress_native_models.assert_called_once_with(10.0, 20.0, 0.25, [708])
			self.assertIn(("save", []), engine_calls)

	def test_import_rolls_back_when_a_patch_fails(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root / "mapping.pwn"
			source.write_text("CreateObject(1000, 1, 2, 3, 0, 0, 90);", encoding="utf-8")
			service = ArianeService.__new__(ArianeService)
			state = {"active": False, "revision": 7}
			engine_calls = []

			def engine(command, fields=None):
				engine_calls.append(command)
				if command == "session_status":
					return {"active": state["active"], "scene_revision": state["revision"]}
				if command == "session_begin": state["active"] = True
				if command == "session_rollback": state["active"] = False
				return {"ok": True}

			service.engine = Mock(side_effect=engine)
			service.apply_scene_patch = Mock(side_effect=RuntimeError("patch failed"))
			with self.assertRaisesRegex(RuntimeError, "patch failed"):
				service.import_pawn_mapping(
					str(source), "ariane/mapping.ipl", str(root / "mapping.ipl"))
			self.assertEqual("session_rollback", engine_calls[-1])
			self.assertFalse(state["active"])


if __name__ == "__main__":
	unittest.main()
