"""Build and query Ariane's local GTA asset intelligence index."""

from __future__ import annotations

from contextlib import closing
import json
import os
from pathlib import Path
import re
import sqlite3
from typing import Any
from urllib.request import Request, urlopen

from PIL import Image, ImageDraw


SCHEMA_VERSION = 3
DEFAULT_GTASTUFF = Path(os.environ["ARIANE_GTASTUFF"]).expanduser() if os.environ.get("ARIANE_GTASTUFF") else None
DEFAULT_DATABASE = Path(os.environ.get("ARIANE_ASSET_DB", str(Path.home() / ".cache" / "ariane-agent" / "assets-v1.sqlite3"))).expanduser()


ROLE_KEYWORDS = {
	"seating": ("bench", "chair", "seat", "stool", "sofa", "couch", "park table", "parktable", "picnic", "dinning"),
	"table": ("table", "desk", "counter"),
	"cooking_fire": ("barbecue", "barbeque", "bbq", "grill", "stove", "oven", "campfire", "fire pit", "brazier"),
	"waste": ("trash", "rubbish", "garbage", "dumpster", "binnt", " bin", "cj_bin", "dump"),
	"container": ("barrel", "drum", "crate", "box", "pallet", "sack", "basket", "container"),
	"lighting": ("lamp", "light", "lantern", "torch", "neon", "streetlit", "lamppost"),
	"signage": ("sign", "billboard", "banner", "poster", "advert"),
	"vegetation": ("tree", "bush", "grass", "plant", "cactus", "cacti", "agave", "palm", "flower"),
	"rock": ("rock", "boulder", "stone"),
	"fence_barrier": ("fence", "wall", "gate", "barrier", "rail", "bollard"),
	"vehicle_prop": ("cart", "wagon", "trailer", "wheel", "trolley"),
	"wood_fuel": ("woodpile", "firewood", "cut logs", "log pile", "timber pile"),
	"utility": ("phone", "vending", "vendmach", "hydrant", "meter", "generator", "tank", "pump"),
	"decoration": ("statue", "ornament", "decor", "flag", "sculpt", "fountain"),
	"shelter": ("canopy", "awning", "gazebo", "parasol", "umbrella", "pergola", "archway"),
	"building": ("house", "home", "shop", "store", "motel", "hotel", "saloon", "office", "warehouse", "shed", "garage"),
}

STYLE_KEYWORDS = {
	"desert": ("desert", "des_", "desn", "countn", "countryn", "vegas"),
	"rural": ("rural", "country", "cunt", "farm", "barn", "wood", "western", "saloon", "shed", "trailer"),
	"industrial": ("industrial", "factory", "warehouse", "quarry", "dock", "cargo", "barrel", "drum"),
	"urban": ("urban", "street", "city", "downtown", "lae", "las", "sf", "shop"),
	"residential": ("residential", "house", "home", "apartment", "garden"),
	"commercial": ("commercial", "shop", "store", "motel", "hotel", "restaurant", "office"),
	"weathered": ("old", "rust", "ruin", "broken", "wreck", "skuz", "crack", "dirty"),
}

SEARCH_STOPWORDS = {
	"a", "an", "and", "as", "at", "beside", "by", "for", "from", "in", "into", "near",
	"of", "on", "or", "the", "to", "used", "use", "with", "without", "thing", "object",
	"objects", "prop", "props", "placed", "place", "area", "small", "medium", "large",
}


def _matches(haystack: str, needles: tuple[str, ...]) -> bool:
	for needle in needles:
		normalized = needle.lower().replace("_", " ").strip()
		if not normalized:
			continue
		if " " in normalized:
			if normalized in haystack:
				return True
		elif re.search(rf"\b{re.escape(normalized)}\b", haystack):
			return True
	return False


def _size_class(max_dim: float | None) -> str:
	if max_dim is None:
		return "unknown"
	value = float(max_dim)
	if value <= 1.5:
		return "tiny"
	if value <= 4.0:
		return "small"
	if value <= 12.0:
		return "medium"
	if value <= 40.0:
		return "large"
	return "huge"


def _role_hits(text: str) -> set[str]:
	haystack = text.lower().replace("_", " ")
	# Legacy category labels are usually plural while role keywords are singular.
	haystack += " " + " ".join(
		token[:-1] for token in re.findall(r"[a-z0-9]+", haystack)
		if len(token) > 3 and token.endswith("s")
	)
	return {role for role, keywords in ROLE_KEYWORDS.items() if _matches(haystack, keywords)}


def _decorate_asset(asset: dict) -> dict:
	"""Attach provenance and cheap contradictions without rewriting source data."""
	asset = dict(asset)
	warnings: list[str] = []
	max_dim = float(asset.get("max_dim") or 0.0)
	semantic = str(asset.get("semantic_description") or "").lower()
	if max_dim >= 12.0 and any(re.search(rf"\b{word}\b", semantic) for word in (
		"tiny", "small", "handheld", "tabletop", "scatter", "bottle", "can",
	)):
		warnings.append("semantic_scale_conflict")
	if 0.0 < max_dim <= 1.5 and any(re.search(rf"\b{word}\b", semantic) for word in (
		"building", "warehouse", "structure", "tower", "block",
	)):
		warnings.append("semantic_scale_conflict")
	catalog_roles = _role_hits(" ".join(str(asset.get(field) or "") for field in (
		"category", "prineside_category", "semantic_role",
	)))
	semantic_roles = _role_hits(semantic)
	if catalog_roles and semantic_roles and catalog_roles.isdisjoint(semantic_roles):
		warnings.append("semantic_role_conflict")
	asset["metadata_warnings"] = sorted(set(warnings))
	asset["metadata_provenance"] = {
		"geometry": "gtastuff_model_sizes" if asset.get("width") is not None else "unavailable",
		"legacy_description": "prineside",
		"semantic_description": "optional_external_dataset",
	}
	return asset


def classify_asset(model: dict, pine: dict, size: dict, description: str) -> tuple[str, str, str, str, str]:
	"""Derive weak labels without pretending legacy names are ground truth.

	The labels are retrieval hints only. Discovery treats them as scoring channels,
	not hard exclusions, so a bad legacy name cannot make a model disappear.
	"""
	parts = [
		model.get("name", ""), model.get("dff", ""), model.get("txd", ""),
		model.get("category", ""), model.get("source", ""),
		pine.get("category", ""), description, " ".join(pine.get("tags") or model.get("tags") or []),
	]
	haystack = " ".join(str(part).lower().replace("_", " ") for part in parts if part)
	roles = [role for role, words in ROLE_KEYWORDS.items() if _matches(haystack, words)]
	styles = [style for style, words in STYLE_KEYWORDS.items() if _matches(haystack, words)]
	placements = []
	source = str(model.get("source", "")).lower()
	category = str(model.get("category", "")).lower()
	if "interior" in source or "int_" in source:
		placements.append("interior")
	else:
		placements.append("outdoor_possible")
	if any(role in roles for role in ("building", "vegetation", "rock", "fence_barrier", "vehicle_prop", "container", "seating", "table", "cooking_fire", "waste")):
		placements.append("ground")
	if any(word in haystack for word in ("door", "window", "poster", "wall sign", "walllight")):
		placements.append("wall_possible")
	if category == "lod":
		placements.append("lod")
	size_class = _size_class(size.get("maxDim"))
	primary_role = roles[0] if roles else (category or "unknown")
	pine_category = str(pine.get("category", ""))
	leaf = pine_category.rsplit(">", 1)[-1].strip().lower() if pine_category else category or "unknown"
	leaf = re.sub(r"[^a-z0-9]+", "_", leaf).strip("_") or "unknown"
	family_key = f"{primary_role}:{leaf}:{size_class}"
	return " ".join(roles), " ".join(styles), " ".join(placements), family_key, size_class


def _load(path: Path, default: Any) -> Any:
	return json.loads(path.read_text(encoding="utf-8")) if path.exists() else default


def _load_semantic_assets(path: Path | None) -> dict[int, dict]:
	if path is None or not Path(path).exists():
		return {}
	data = _load(Path(path), [])
	return {int(item.get("Id", item.get("id"))): item for item in data
	        if item.get("Id", item.get("id")) is not None}


def _gta_models(gta_dir: Path) -> list[dict]:
	"""Read object definitions locally; no game assets or external catalogue needed.

	Scan data IDE files in deterministic order. Runtime --defined-only and inspect
	remain authoritative for active definitions, collision and dimensions.
	"""
	data = next((p for p in gta_dir.iterdir() if p.is_dir() and p.name.lower() == "data"), None)
	if data is None:
		raise FileNotFoundError(f"GTA data directory not found in {gta_dir}")
	models = {}
	for path in sorted(data.rglob("*")):
		if path.suffix.lower() != ".ide":
			continue
		section = ""
		for line_number, line in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
			line = line.split("#", 1)[0].strip()
			if not line:
				continue
			if "," not in line:
				section = line.lower()
				continue
			if section not in {"objs", "tobj", "anim"}:
				continue
			fields = [f.strip() for f in line.split(",")]
			try:
				asset_id = int(fields[0])
				# III/VC: mesh count followed by 1-3 distances; SA: one distance.
				start = 4 if section == "anim" else 3
				count = int(fields[3]) if section != "anim" and len(fields) >= (8 if section == "tobj" else 6) else 0
				if count in (1, 2, 3):
					start = 4
				else:
					count = 1
				models[asset_id] = {"id": asset_id, "name": fields[1], "dff": fields[1],
					"txd": fields[2], "category": "lod" if fields[1].lower().startswith("lod") else "unknown",
					"source": path.relative_to(gta_dir).as_posix(),
					"drawDist": max(float(v) for v in fields[start:start + count]),
					"flags": int(fields[start + count], 0)}
			except (ValueError, IndexError) as exc:
				raise ValueError(f"Invalid object definition at {path}:{line_number}: {exc}") from exc
	if not models:
		raise ValueError(f"No object definitions found beneath {data}")
	return list(models.values())


def build_index(gtastuff: Path | None = DEFAULT_GTASTUFF, database: Path = DEFAULT_DATABASE,
				semantic_assets: Path | None = None, *, gta_dir: Path | None = None) -> dict:
	database = Path(database).expanduser().resolve()
	if gta_dir is not None:
		gta_dir = Path(gta_dir).expanduser().resolve()
		models = _gta_models(gta_dir)
		models_path = gta_dir / "data"
		sizes, locations, collisions, prineside = {}, {}, {}, {}
	elif gtastuff is not None:
		gtastuff = Path(gtastuff).expanduser().resolve()
		models_path = gtastuff / "models" / "data" / "models.json"
		if not models_path.exists():
			raise FileNotFoundError(f"gtastuff model database not found: {models_path}")
		models = _load(models_path, {}).get("models", [])
		sizes = _load(gtastuff / "models" / "data" / "model-sizes.json", {})
		locations = _load(gtastuff / "models" / "data" / "locations.json", {}).get("locations", {})
		collisions = _load(gtastuff / "models" / "data" / "collisions.min.json", {})
		prineside = _load(gtastuff / "scripts" / "prineside-data.json", {}).get("models", {})
	else:
		raise ValueError("Specify --gta-dir /path/to/GTA for a local catalogue, or --gtastuff /path/to/gtastuff for enriched metadata")
	semantic_by_id = _load_semantic_assets(semantic_assets)

	database.parent.mkdir(parents=True, exist_ok=True)
	temporary = database.with_suffix(database.suffix + ".tmp")
	temporary.unlink(missing_ok=True)
	connection = sqlite3.connect(temporary)
	try:
		connection.executescript("""
			PRAGMA journal_mode = OFF;
			PRAGMA synchronous = OFF;
			CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
			CREATE TABLE assets (
				id INTEGER PRIMARY KEY, name TEXT NOT NULL, dff TEXT, txd TEXT,
				category TEXT, prineside_category TEXT, description TEXT, tags TEXT,
				source TEXT, draw_distance REAL, flags INTEGER, lod_id INTEGER,
				parent_id INTEGER, is_road INTEGER NOT NULL, is_vegetation INTEGER NOT NULL,
				has_collision INTEGER NOT NULL, width REAL, height REAL, depth REAL,
				max_dim REAL, volume REAL, radius REAL, native_count INTEGER NOT NULL,
				native_ipls TEXT, thumbnail_url TEXT, role_tags TEXT NOT NULL,
				style_tags TEXT NOT NULL, placement_tags TEXT NOT NULL,
				family_key TEXT NOT NULL, size_class TEXT NOT NULL,
				semantic_description TEXT, semantic_category TEXT, semantic_biomes TEXT
			);
			CREATE VIRTUAL TABLE asset_search USING fts5(
				name, description, tags, category, prineside_category, txd, source,
				role_tags, style_tags, placement_tags, semantic_description,
				content='assets', content_rowid='id'
			);
			CREATE TABLE asset_ipls (
				asset_id INTEGER NOT NULL, ipl TEXT NOT NULL,
				PRIMARY KEY(asset_id, ipl)
			);
			CREATE INDEX asset_ipls_ipl ON asset_ipls(ipl, asset_id);
			CREATE TABLE asset_locations (
				asset_id INTEGER NOT NULL, x REAL NOT NULL, y REAL NOT NULL,
				z REAL NOT NULL, interior INTEGER NOT NULL DEFAULT 0
			);
			CREATE INDEX asset_locations_asset ON asset_locations(asset_id);
			CREATE INDEX assets_family ON assets(family_key);
			CREATE INDEX assets_txd ON assets(txd);
		""")
		rows, ipl_rows, location_rows = [], [], []
		for model in models:
			asset_id = int(model["id"])
			size = sizes.get(str(asset_id), {})
			location = locations.get(str(asset_id), {})
			pine = prineside.get(str(asset_id), {})
			semantic = semantic_by_id.get(asset_id, {})
			pine_name = pine.get("name", "") if pine.get("exists") else ""
			description = pine_name if pine_name and pine_name.lower() != model["name"].lower() else ""
			semantic_description = semantic.get("AiDescription", "")
			lod = model.get("associatedLod") or {}
			parent = model.get("parentModel") or {}
			# External captions and tags are valuable recall hints but are not
			# reliable enough to define functional families. Keep them in FTS while
			# deriving role/style labels only from stable GTAStuff/Prineside metadata.
			role_tags, style_tags, placement_tags, family_key, size_class = classify_asset(
				model, pine, size, description)
			asset_ipls = location.get("ipls", [])
			rows.append((
				asset_id, model["name"], model.get("dff"), model.get("txd"),
				model.get("category"), pine.get("category", ""), description,
				" ".join(pine.get("tags") or model.get("tags") or []), model.get("source"),
				model.get("drawDist"), model.get("flags"), lod.get("id"), parent.get("id"),
				int(bool(model.get("isRoad"))), int(bool(model.get("isVegetation"))),
				int(str(asset_id) in collisions), size.get("width"), size.get("height"),
				size.get("depth"), size.get("maxDim"), size.get("volume"), size.get("radius"),
				len(location.get("locs", [])), json.dumps(asset_ipls, separators=(",", ":")),
				"" if gta_dir is not None else f"https://gtastuff.namecdsl.xyz/thumbnails/{asset_id}.png",
				role_tags, style_tags, placement_tags, family_key, size_class,
				semantic_description, semantic.get("Category", ""),
				" ".join(semantic.get("BiomeAffinity") or []),
			))
			ipl_rows.extend((asset_id, str(ipl)) for ipl in asset_ipls)
			for item in location.get("locs", []):
				location_rows.append((asset_id, item["x"], item["y"], item["z"], item.get("i", 0)))
			for item in location.get("interiorLocs", []):
				location_rows.append((asset_id, item["x"], item["y"], item["z"], item.get("int", item.get("i", 0))))
		connection.executemany("""
			INSERT INTO assets VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
		""", rows)
		connection.executemany("INSERT INTO asset_ipls VALUES (?,?)", ipl_rows)
		connection.executemany("INSERT INTO asset_locations VALUES (?,?,?,?,?)", location_rows)
		connection.execute("INSERT INTO asset_search(asset_search) VALUES('rebuild')")
		connection.executemany("INSERT INTO metadata VALUES (?,?)", [
			("schema_version", str(SCHEMA_VERSION)),
			("source_kind", "gta_ide" if gta_dir is not None else "gtastuff"),
			("source_path", str(models_path)),
			("asset_count", str(len(rows))),
			("semantic_asset_count", str(len(semantic_by_id))),
			("semantic_assets_path", str(Path(semantic_assets).resolve()) if semantic_assets else ""),
		])
		connection.commit()
	finally:
		connection.close()
	os.replace(temporary, database)
	return {"database": str(database), "asset_count": len(models),
	        "semantic_asset_count": len(semantic_by_id), "schema_version": SCHEMA_VERSION}


class AssetIndex:
	def __init__(self, database: Path = DEFAULT_DATABASE):
		self.database = Path(database)
		if not self.database.exists():
			raise FileNotFoundError(f"asset index not found: {self.database}; run `arianectl assets index`")

	def _connect(self) -> sqlite3.Connection:
		connection = sqlite3.connect(self.database)
		connection.row_factory = sqlite3.Row
		return connection

	def inspect(self, asset_id: int) -> dict | None:
		with closing(self._connect()) as connection:
			row = connection.execute("SELECT * FROM assets WHERE id = ?", (asset_id,)).fetchone()
		return _decorate_asset(dict(row)) if row else None

	def search(self, query: str = "", *, category: str | None = None,
	           max_width: float | None = None, max_depth: float | None = None,
	           has_collision: bool | None = None, exclude_lod: bool = True,
	           limit: int = 30) -> list[dict]:
		clauses, values = [], []
		join = ""
		order = "a.native_count DESC, a.id"
		if query.strip():
			tokens = [token for token in re.findall(r"[a-z0-9]+", query.lower())
			          if len(token) > 1 and token not in SEARCH_STOPWORDS]
			if not tokens:
				tokens = ["unknown"]
			join = "JOIN asset_search s ON s.rowid = a.id"
			clauses.append("asset_search MATCH ?")
			# Agent prompts are descriptive while GTA names are abbreviated and often
			# misspelled. OR keeps useful visual candidates in the shortlist even when
			# only one prompt term appears in the legacy metadata.
			values.append(" OR ".join(f'"{token}"*' for token in tokens))
			order = "bm25(asset_search), a.native_count DESC"
		if category:
			clauses.append("(a.category = ? OR lower(a.prineside_category) LIKE ?)")
			values.extend([category, f"%{category.lower()}%"])
		if max_width is not None:
			clauses.append("a.width <= ?")
			values.append(max_width)
		if max_depth is not None:
			clauses.append("a.depth <= ?")
			values.append(max_depth)
		if has_collision is not None:
			clauses.append("a.has_collision = ?")
			values.append(int(has_collision))
		if exclude_lod:
			clauses.append("a.category != 'lod'")
		where = " WHERE " + " AND ".join(clauses) if clauses else ""
		query_sql = f"SELECT a.* FROM assets a {join}{where} ORDER BY {order} LIMIT ?"
		values.append(max(1, min(limit, 3000)))
		with closing(self._connect()) as connection:
			return [_decorate_asset(dict(row)) for row in connection.execute(query_sql, values)]

	def similar(self, asset_id: int, limit: int = 20) -> list[dict]:
		asset = self.inspect(asset_id)
		if not asset:
			return []
		with closing(self._connect()) as connection:
			rows = connection.execute("""
				SELECT *,
					(CASE WHEN category = ? THEN 3 ELSE 0 END) +
					(CASE WHEN txd = ? THEN 2 ELSE 0 END) +
					(CASE WHEN prineside_category = ? THEN 2 ELSE 0 END) -
					(abs(coalesce(max_dim, 0) - coalesce(?, 0)) / max(coalesce(?, 1), 1)) AS score
				FROM assets WHERE id != ? AND category != 'lod'
				ORDER BY score DESC, native_count DESC LIMIT ?
			""", (asset["category"], asset["txd"], asset["prineside_category"],
			      asset["max_dim"], asset["max_dim"], asset_id, limit)).fetchall()
		return [_decorate_asset(dict(row)) for row in rows]

	def catalog(self, *, exclude_lod: bool = True) -> list[dict]:
		where = "WHERE category != 'lod'" if exclude_lod else ""
		with closing(self._connect()) as connection:
			return [_decorate_asset(dict(row)) for row in connection.execute(f"SELECT * FROM assets {where} ORDER BY id")]

	def native_neighbors(self, asset_ids: list[int], limit: int = 500) -> list[dict]:
		"""Return models sharing native IPLs with context assets."""
		ids = sorted({int(asset_id) for asset_id in asset_ids})
		if not ids:
			return []
		placeholders = ",".join("?" for _ in ids)
		with closing(self._connect()) as connection:
			rows = connection.execute(f"""
				SELECT a.*, count(DISTINCT source.ipl) AS shared_ipl_count
				FROM asset_ipls source
				JOIN asset_ipls candidate ON candidate.ipl = source.ipl
				JOIN assets a ON a.id = candidate.asset_id
				WHERE source.asset_id IN ({placeholders})
				  AND candidate.asset_id NOT IN ({placeholders})
				  AND a.category != 'lod'
				GROUP BY a.id
				ORDER BY shared_ipl_count DESC, a.native_count DESC, a.id
				LIMIT ?
			""", (*ids, *ids, max(1, min(limit, 3000)))).fetchall()
		return [_decorate_asset(dict(row)) for row in rows]

	def family_assets(self, family: str) -> list[dict]:
		with closing(self._connect()) as connection:
			rows = connection.execute("""
				SELECT * FROM assets WHERE family_key = ? AND category != 'lod'
				ORDER BY native_count DESC, id
			""", (family,)).fetchall()
		return [_decorate_asset(dict(row)) for row in rows]

	def contact_sheet(self, asset_ids: list[int], output: Path, *,
	                  columns: int = 5, tile_size: int = 220,
	                  thumbnail_dir: Path | None = None) -> dict:
		"""Render labeled gtastuff thumbnails for an agent's visual shortlist."""
		assets = [asset for asset_id in asset_ids if (asset := self.inspect(asset_id))]
		if not assets:
			raise ValueError("contact sheet has no valid asset ids")
		columns = max(1, min(columns, 10))
		tile_size = max(96, min(tile_size, 512))
		label_height = 44
		rows = (len(assets) + columns - 1) // columns
		sheet = Image.new("RGB", (columns * tile_size, rows * (tile_size + label_height)), "#202226")
		draw = ImageDraw.Draw(sheet)
		loaded, missing = 0, []
		for index, asset in enumerate(assets):
			x = index % columns * tile_size
			y = index // columns * (tile_size + label_height)
			thumbnail = None
			if thumbnail_dir:
				candidate = Path(thumbnail_dir) / f"{asset['id']}.png"
				if candidate.exists():
					thumbnail = Image.open(candidate)
			if thumbnail is None:
				try:
					request = Request(asset["thumbnail_url"], headers={"User-Agent": "ariane-agent/1"})
					with urlopen(request, timeout=10) as response:
						from io import BytesIO
						thumbnail = Image.open(BytesIO(response.read()))
				except (OSError, ValueError):
					missing.append(asset["id"])
			if thumbnail is not None:
				thumbnail = thumbnail.convert("RGBA")
				thumbnail.thumbnail((tile_size - 8, tile_size - 8), Image.Resampling.LANCZOS)
				left = x + (tile_size - thumbnail.width) // 2
				top = y + (tile_size - thumbnail.height) // 2
				sheet.paste(thumbnail, (left, top), thumbnail)
				loaded += 1
			draw.rectangle((x, y + tile_size, x + tile_size, y + tile_size + label_height), fill="#111215")
			draw.text((x + 7, y + tile_size + 5), f"{asset['id']}  {asset['name']}", fill="white")
			draw.text((x + 7, y + tile_size + 23), asset.get("prineside_category") or asset.get("category") or "", fill="#aeb6c2")
		output = Path(output).resolve()
		output.parent.mkdir(parents=True, exist_ok=True)
		sheet.save(output)
		return {"path": str(output), "asset_ids": [asset["id"] for asset in assets],
		        "loaded": loaded, "missing": missing}
