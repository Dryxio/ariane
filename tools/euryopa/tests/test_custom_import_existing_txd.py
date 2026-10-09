"""Headless source-contract tests for reusing an existing TXD in Import Custom Object."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


def function_body(source: str, signature: str) -> str:
	match = re.search(re.escape(signature) + r"\s*\{", source)
	if match is None:
		raise AssertionError(f"function not found: {signature}")
	brace = match.end() - 1
	depth = 0
	for index in range(brace, len(source)):
		if source[index] == "{":
			depth += 1
		elif source[index] == "}":
			depth -= 1
			if depth == 0:
				return source[brace + 1:index]
	raise AssertionError(f"unterminated function: {signature}")


GUI = (ROOT / "tools/euryopa/gui.cpp").read_text()


class CustomImportExistingTxdTests(unittest.TestCase):
	def test_txd_slots_are_enumerable(self):
		store = (ROOT / "tools/euryopa/txdstore.cpp").read_text()
		header = (ROOT / "tools/euryopa/euryopa.h").read_text()

		self.assertIn("GetNumTxdSlots(void)", store)
		self.assertIn("GetNumTxdSlots(void)", header)

	def test_import_button_accepts_existing_txd_without_file(self):
		body = function_body(GUI, "uiCustomImportPopup(void)")
		can_import = body.split("bool canImport", 1)[1].split(";", 1)[0]

		self.assertIn("customImportTxdIsExisting()", can_import)

	def test_ui_no_longer_requires_txd_file(self):
		body = function_body(GUI, "uiCustomImportPopup(void)")

		self.assertNotIn("DFF and TXD are required.", body)
		self.assertIn('"Find TXD"', body)

	def test_finalize_skips_content_compare_without_txd_file(self):
		body = function_body(GUI, "finalizeCustomImport(void)")
		flat = re.sub(r"\s+", " ", body)
		reuse_block = re.split(r"if ?\( ?reuseTxd && haveTxdFile ?\) ?\{", flat, 1)[1].split("bool importCol", 1)[0]

		self.assertIn("ModloaderFindOverride", reuse_block)
		self.assertIn("haveTxdFile = gCustomImport.txdSource[0] != '\\0'", flat)

	def test_finalize_rejects_missing_txd_without_file(self):
		body = function_body(GUI, "finalizeCustomImport(void)")

		self.assertIn("does not exist", body)

	def test_drag_drop_does_not_require_txd_file(self):
		body = function_body(GUI, "beginCustomImportFromPath(const char *path)")

		self.assertNotIn("!doesFileExist(txdPath)", body)


if __name__ == "__main__":
	unittest.main()
