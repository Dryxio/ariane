"""SA-MP/open.mp asset setup and literal Pawn mapping import support."""

from __future__ import annotations

from dataclasses import dataclass, field
import math
import os
from pathlib import Path
import re
import shutil
import tempfile


SAMP_ASSET_FILES = ("SAMP.ide", "SAMP.img", "SAMPCOL.img")
SAMP_MODLOADER_README = """# SA-MP/open.mp object pack for Ariane
IDE data\\maps\\samp\\SAMP.ide
IMG models\\SAMP.img
COLFILE 0 models\\SAMPCOL.img
"""
_NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")
_CALL = re.compile(
	r"\b(CreateObject|CreateDynamicObject|CreatePlayerObject|RemoveBuildingForPlayer)\s*"
	r"\((.*?)\)\s*;",
	re.IGNORECASE | re.DOTALL,
)
_MAPPING_CALL = re.compile(
	r"\b(CreateObject|CreateDynamicObject|CreatePlayerObject|RemoveBuildingForPlayer|"
	r"SetObjectMaterial|SetDynamicObjectMaterial|SetObjectMaterialText|SetDynamicObjectMaterialText)\s*\(",
	re.IGNORECASE,
)


@dataclass(frozen=True)
class PawnObject:
	model: int
	x: float
	y: float
	z: float
	pitch: float
	roll: float
	heading: float
	line: int
	function: str


@dataclass(frozen=True)
class PawnRemoval:
	model: int
	x: float
	y: float
	z: float
	radius: float
	line: int


@dataclass
class PawnMapping:
	source: str
	objects: list[PawnObject] = field(default_factory=list)
	removals: list[PawnRemoval] = field(default_factory=list)
	warnings: list[dict] = field(default_factory=list)

	def summary(self) -> dict:
		return {
			"source": self.source,
			"object_count": len(self.objects),
			"removal_count": len(self.removals),
			"models": sorted({item.model for item in self.objects}),
			"warnings": self.warnings,
		}


def _strip_comments(text: str) -> str:
	"""Remove Pawn comments while preserving newlines and quoted strings."""
	result, index, state = [], 0, "code"
	while index < len(text):
		char = text[index]
		next_char = text[index + 1] if index + 1 < len(text) else ""
		if state == "code" and char == '"':
			state = "string"
			result.append(char)
		elif state == "string":
			result.append(char)
			if char == "\\" and next_char:
				result.append(next_char)
				index += 1
			elif char == '"':
				state = "code"
		elif state == "code" and char == "/" and next_char == "/":
			state = "line_comment"
			result.extend("  ")
			index += 1
		elif state == "code" and char == "/" and next_char == "*":
			state = "block_comment"
			result.extend("  ")
			index += 1
		elif state == "line_comment":
			result.append("\n" if char == "\n" else " ")
			if char == "\n":
				state = "code"
		elif state == "block_comment":
			if char == "*" and next_char == "/":
				result.extend("  ")
				index += 1
				state = "code"
			else:
				result.append("\n" if char == "\n" else " ")
		else:
			result.append(char)
		index += 1
	return "".join(result)


def _literal(value: str) -> float:
	value = value.strip()
	if not _NUMBER.fullmatch(value):
		raise ValueError(f"not a numeric literal: {value!r}")
	number = float(value)
	if not math.isfinite(number):
		raise ValueError(f"numeric literal is not finite: {value!r}")
	return number


def _model_id(value: float) -> int:
	if not value.is_integer() or not 0 <= value <= 2_147_483_647:
		raise ValueError(f"model id must be a non-negative integer: {value}")
	return int(value)


def parse_pawn_text(text: str, source: str = "<memory>") -> PawnMapping:
	"""Parse literal SA-MP mapping calls without executing Pawn code."""
	clean = _strip_comments(text)
	mapping = PawnMapping(source=source)
	consumed: set[int] = set()
	for match in _CALL.finditer(clean):
		function = match.group(1)
		name = function.lower()
		line = clean.count("\n", 0, match.start()) + 1
		consumed.add(match.start())
		arguments = [item.strip() for item in match.group(2).split(",")]
		try:
			if name == "removebuildingforplayer":
				if len(arguments) < 6:
					raise ValueError("expected player, model, position and radius")
				values = [_literal(item) for item in arguments[1:6]]
				mapping.removals.append(PawnRemoval(
					model=_model_id(values[0]), x=values[1], y=values[2], z=values[3],
					radius=values[4], line=line))
				continue
			if name == "createplayerobject":
				if len(arguments) < 8:
					raise ValueError("expected player, model, position and rotation")
				values = [_literal(item) for item in arguments[1:8]]
			else:
				if len(arguments) < 7:
					raise ValueError("expected model, position and rotation")
				values = [_literal(item) for item in arguments[:7]]
			mapping.objects.append(PawnObject(
				model=_model_id(values[0]), x=values[1], y=values[2], z=values[3],
				pitch=values[4], roll=values[5], heading=values[6],
				line=line, function=function))
		except (ValueError, IndexError) as error:
			mapping.warnings.append({
				"line": line, "function": function, "code": "non_literal_call",
				"message": str(error),
			})

	# Surface mapping-related calls that the literal importer did not consume.
	for match in _MAPPING_CALL.finditer(clean):
		if match.start() in consumed:
			continue
		function = match.group(1)
		line = clean.count("\n", 0, match.start()) + 1
		code = "material_not_supported" if "material" in function.lower() else "unsupported_call"
		mapping.warnings.append({
			"line": line, "function": function, "code": code,
			"message": "call was not imported; use literal mapping calls",
		})
	return mapping


def parse_pawn_mapping(path: Path) -> PawnMapping:
	path = Path(path).expanduser().resolve()
	if path.suffix.lower() != ".pwn":
		raise ValueError(f"Pawn mapping must use a .pwn extension: {path}")
	return parse_pawn_text(path.read_text(encoding="utf-8-sig", errors="replace"), str(path))


def pawn_operations(mapping: PawnMapping, *, group: str | None = None) -> tuple[list[dict], list[dict]]:
	"""Return placement operations followed by full-rotation corrections."""
	stem = re.sub(r"[^A-Za-z0-9._-]+", "-", Path(mapping.source).stem).strip("-.") or "pawn"
	group = group or f"pawn:{stem[:96]}"
	placements, rotations = [], []
	for index, item in enumerate(mapping.objects, 1):
		key = f"pawn:{stem[:80]}:{index:05d}"
		placements.append({
			"action": "place", "key": key, "group": group, "model": item.model,
			"x": item.x, "y": item.y, "z": item.z,
			"heading": item.heading, "snap": False,
		})
		if abs(item.pitch) > 1.0e-6 or abs(item.roll) > 1.0e-6:
			rotations.append({
				"action": "transform3d", "key": key,
				"x": item.x, "y": item.y, "z": item.z,
				"pitch": item.pitch, "roll": item.roll,
				"heading": item.heading, "snap": False,
			})
	return placements, rotations


def _casefold_file(directory: Path, name: str) -> Path | None:
	if not directory.is_dir():
		return None
	return next((item for item in directory.iterdir()
	             if item.is_file() and item.name.lower() == name.lower()), None)


def find_samp_assets(source: Path | None = None) -> Path:
	"""Locate a user-owned SA-MP asset directory, including open.mp's cache."""
	candidates: list[Path] = []
	if source is not None:
		root = Path(source).expanduser()
		candidates.extend([root, root / "SAMP"])
	local = os.environ.get("LOCALAPPDATA")
	if local:
		candidates.append(Path(local) / "mp.open.launcher" / "samp" / "shared" / "SAMP")
	for candidate in candidates:
		if all(_casefold_file(candidate, name) for name in SAMP_ASSET_FILES):
			return candidate.resolve()
	raise FileNotFoundError(
		"SA-MP assets not found; pass --source pointing to a directory containing "
		+ ", ".join(SAMP_ASSET_FILES))


def install_samp_assets(gta_dir: Path, source: Path | None = None, *, force: bool = False) -> dict:
	"""Install user-owned SA-MP assets as an isolated Ariane modloader package."""
	gta_dir = Path(gta_dir).expanduser().resolve()
	data_dir = next((path for path in gta_dir.iterdir()
	                 if path.is_dir() and path.name.lower() == "data"), None) if gta_dir.is_dir() else None
	if data_dir is None:
		raise FileNotFoundError(f"GTA data directory not found in {gta_dir}")
	source_dir = find_samp_assets(source)
	target = gta_dir / "modloader" / "SAMP"
	destinations = {
		"SAMP.ide": target / "data" / "maps" / "samp" / "SAMP.ide",
		"SAMP.img": target / "models" / "SAMP.img",
		"SAMPCOL.img": target / "models" / "SAMPCOL.img",
	}
	conflicts = [str(path) for path in destinations.values() if path.exists()]
	readme = target / "readme.txt"
	if readme.exists():
		conflicts.append(str(readme))
	if conflicts and not force:
		raise FileExistsError("SA-MP modloader package already exists; use --force to update it: "
		                      + ", ".join(conflicts))
	for name, destination in destinations.items():
		destination.parent.mkdir(parents=True, exist_ok=True)
		source_file = _casefold_file(source_dir, name)
		assert source_file is not None
		with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
			temporary_path = Path(temporary.name)
		try:
			shutil.copy2(source_file, temporary_path)
			os.replace(temporary_path, destination)
		finally:
			temporary_path.unlink(missing_ok=True)
	readme.parent.mkdir(parents=True, exist_ok=True)
	readme.write_text(SAMP_MODLOADER_README, encoding="ascii")
	return {
		"source": str(source_dir), "target": str(target),
		"files": [str(readme), *[str(path) for path in destinations.values()]],
		"samp_ipl_loaded": False,
	}
