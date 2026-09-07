"""The public checkout can build its catalogue without private data."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from asset_index import AssetIndex, build_index


class PortableIndexTests(unittest.TestCase):
    def test_offline_catalogue_paths_with_spaces_and_ide_formats(self):
        with tempfile.TemporaryDirectory(prefix="ariane game ") as directory:
            root = Path(directory)
            data = root / "DATA" / "maps"
            data.mkdir(parents=True)
            (data / "objects.IDE").write_text(
                "objs\n100, test_bench, wood, 120, 0\n"
                "101, test_table, wood, 2, 80, 150, 4\nend\n"
                "tobj\n102, night_light, tx, 200, 0, 20, 6\nend\n"
                "anim\n103, windmill, tx, animlib, 300, 0\nend\n")
            database = root / "catalogue output" / "assets.sqlite3"
            report = build_index(database=database, gta_dir=root)
            self.assertEqual(report["asset_count"], 4)
            index = AssetIndex(database)
            self.assertEqual(index.inspect(101)["draw_distance"], 150)
            self.assertEqual(index.inspect(102)["flags"], 0)
            self.assertEqual(index.inspect(103)["draw_distance"], 300)
            self.assertEqual(index.inspect(100)["thumbnail_url"], "")
            self.assertIsNone(index.inspect(100)["width"])
            self.assertEqual(index.search("bench")[0]["id"], 100)

    def test_missing_source_is_actionable(self):
        with self.assertRaisesRegex(ValueError, "--gta-dir"):
            build_index(gtastuff=None)

    def test_malformed_definitions_fail_before_replacing_database(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "data").mkdir()
            (root / "data" / "bad.ide").write_text("objs\n100, bad\nend\n")
            database = root / "existing.db"
            database.write_bytes(b"preserve")
            with self.assertRaisesRegex(ValueError, "bad.ide:2"):
                build_index(database=database, gta_dir=root)
            self.assertEqual(database.read_bytes(), b"preserve")
