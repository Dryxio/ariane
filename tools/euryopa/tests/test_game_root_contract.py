"""Headless source-contract tests for Windows game-root stability."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


def function_body(source: str, signature: str) -> str:
	start = source.index(signature)
	brace = source.index("{", start)
	depth = 0
	for index in range(brace, len(source)):
		if source[index] == "{":
			depth += 1
		elif source[index] == "}":
			depth -= 1
			if depth == 0:
				return source[brace + 1:index]
	raise AssertionError(f"unterminated function: {signature}")


class WindowsGameRootContractTests(unittest.TestCase):
	def test_game_root_uses_executable_directory_on_windows(self):
		source = (ROOT / "tools/euryopa/main.cpp").read_text()
		body = function_body(source, "GetGameRootDirectory(char *dir, size_t size)")
		windows_branch = body.split("#ifdef _WIN32", 1)[1].split("#else", 1)[0]

		self.assertIn("GetEditorRootDirectory(dir, size)", windows_branch)
		self.assertNotIn("GetCurrentDirectory", windows_branch)

	def test_native_file_picker_restores_process_directory(self):
		source = (ROOT / "tools/euryopa/gui.cpp").read_text()
		body = function_body(source, "pickFileDialog(char *dst, size_t size, const char *expectedExt)")
		windows_branch = body.split("#ifdef _WIN32", 1)[1].split("#else", 1)[0]

		self.assertIn("OFN_NOCHANGEDIR", windows_branch)
		self.assertLess(windows_branch.index("GetCurrentDirectoryA"),
		                windows_branch.index("GetOpenFileNameA"))
		self.assertGreater(windows_branch.index("SetCurrentDirectoryA"),
		                   windows_branch.index("GetOpenFileNameA"))


if __name__ == "__main__":
	unittest.main()
