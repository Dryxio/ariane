"""Shared service layer used by Ariane's CLI, daemon, and MCP adapter."""

from __future__ import annotations

import json
import copy
import hashlib
import math
import os
from pathlib import Path
import re
import random
import tempfile
from typing import Any
import uuid

if __package__:
	from .ariane_ipc import ArianeClient, ArianeError, DEFAULT_ENGINE_SOCKET
else:
	from ariane_ipc import ArianeClient, ArianeError, DEFAULT_ENGINE_SOCKET
if __package__:
	from .asset_index import AssetIndex, DEFAULT_DATABASE
else:
	from asset_index import AssetIndex, DEFAULT_DATABASE
if __package__:
	from .asset_discovery import AssetDiscovery, DEFAULT_SESSION_DIR
else:
	from asset_discovery import AssetDiscovery, DEFAULT_SESSION_DIR
if __package__:
	from .asset_profiles import AssetProfiles
else:
	from asset_profiles import AssetProfiles
if __package__:
	from .creative_catalog import CreativeCatalog
else:
	from creative_catalog import CreativeCatalog
if __package__:
	from .review_rig import ReviewRig
else:
	from review_rig import ReviewRig
if __package__:
	from .vibe import VibeWorkflow
else:
	from vibe import VibeWorkflow
from contextlib import contextmanager
import errno
import time
if os.name == "nt":
	import msvcrt
else:
	import fcntl
if __package__:
	from .spatial import footprint, rectangle, polygons_overlap, circle_intersects_polygon, relative_offset, facing_heading
else:
	from spatial import footprint, rectangle, polygons_overlap, circle_intersects_polygon, relative_offset, facing_heading


DEFAULT_STATE_DIR = Path(os.environ.get(
	"ARIANE_AGENT_STATE_DIR", Path.home() / ".local" / "share" / "ariane-agent"
))
CHECKPOINT_SCHEMA = 1
COMPOSITION_SCHEMA = 1
KEY_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}")


@contextmanager
def _edit_lock(path: Path):
	"""Serialize patch journals across processes; closing releases either OS lock."""
	with path.open("a+b") as lock:
		if os.name == "nt":
			# Windows byte-range locks require a byte and a stable file position.
			lock.seek(0, os.SEEK_END)
			if lock.tell() == 0:
				lock.write(b"\0")
				lock.flush()
			while True:
				lock.seek(0)
				try:
					msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
					break
				except OSError as exc:
					if exc.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
						raise
					time.sleep(0.05)
		else:
			fcntl.flock(lock, fcntl.LOCK_EX)
		try:
			yield
		finally:
			if os.name == "nt":
				lock.seek(0)
				msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
			else:
				fcntl.flock(lock, fcntl.LOCK_UN)


class ScenePatchError(ArianeError):
	"""Structured patch failure retained across CLI and MCP adapters."""

	def __init__(self, payload: dict):
		self.payload = payload
		super().__init__(str(payload.get("error", "scene patch failed")), payload)


class ArianeService:
	def __init__(self, engine_socket: Path = DEFAULT_ENGINE_SOCKET,
	             database: Path = DEFAULT_DATABASE, timeout: float = 30.0,
	             discovery_dir: Path = DEFAULT_SESSION_DIR,
	             state_dir: Path = DEFAULT_STATE_DIR):
		self.client = ArianeClient(engine_socket, timeout)
		self.database = Path(database)
		self.discovery_dir = Path(discovery_dir)
		self.state_dir = Path(state_dir)
		self._stable_keys_by_instance: dict[int, str] = {}

	@property
	def vibe(self):
		return VibeWorkflow(self)

	@property
	def profiles(self):
		return AssetProfiles(self)

	@property
	def composition_dir(self) -> Path:
		return self.state_dir / "compositions"

	@property
	def plan_dir(self) -> Path:
		return self.state_dir / "plans"

	@staticmethod
	def _safe_key(value: str, kind: str = "object key") -> str:
		if not KEY_PATTERN.fullmatch(value):
			raise ValueError(f"{kind} must be 1-128 safe identifier characters")
		return value

	def _composition_path(self, session: dict) -> Path:
		identity = "\0".join([
			str(session.get("session_id", "")), str(session.get("logical_path", "")),
			str(session.get("physical_path", "")),
		])
		return self.composition_dir / f"{hashlib.sha256(identity.encode()).hexdigest()}.json"

	def _load_composition(self) -> tuple[dict, Path, dict]:
		session = self.engine("session_status")
		if not session.get("active"):
			raise RuntimeError("begin a scratch session before editing composition metadata")
		path = self._composition_path(session)
		if path.exists():
			state = json.loads(path.read_text(encoding="utf-8"))
			if state.get("schema") != COMPOSITION_SCHEMA:
				raise ValueError("unsupported composition state schema")
			# Session names are user-facing and may be reused after rollback. Reconcile
			# the sidecar ledger against the actual scratch scene so stale receipts can
			# never suppress a new patch or target unrelated recycled runtime IDs.
			live_ids = self._live_instance_ids()
			known_ids = {int(metadata["instance_id"]) for metadata in state["objects"].values()
			             if metadata.get("instance_id") is not None}
			if known_ids != live_ids.intersection(known_ids):
				state["objects"] = {key: metadata for key, metadata in state["objects"].items()
				                    if int(metadata.get("instance_id", -1)) in live_ids}
				valid_keys = set(state["objects"])
				state["groups"] = {group: [key for key in members if key in valid_keys]
				                   for group, members in state["groups"].items()
				                   if any(key in valid_keys for key in members)}
				state["supports"] = {child: parent for child, parent in state["supports"].items()
				                     if child in valid_keys and parent in valid_keys}
				state["receipts"] = {}
				self._atomic_json(path, state)
		else:
			state = {"schema": COMPOSITION_SCHEMA, "session_id": session["session_id"],
			         "objects": {}, "groups": {}, "supports": {}, "receipts": {}}
		# Include objects created through the legacy CLI or editor in stable-key operations.
		known = {int(m["instance_id"]) for m in state["objects"].values() if m.get("instance_id") is not None}
		for instance_id in self._live_instance_ids() - known:
			state["objects"][f"runtime:{instance_id}"] = {"instance_id": instance_id}
		return state, path, session

	def _live_instance_ids(self) -> set[int]:
		result: set[int] = set()
		offset = 0
		revision: int | None = None
		while True:
			page = self.engine("list_page", [offset, 256])
			if revision is None:
				revision = int(page["scene_revision"])
			elif int(page["scene_revision"]) != revision:
				raise RuntimeError("scene changed while reconciling composition state")
			result.update(int(item["instance_id"]) for item in page["items"])
			next_offset = page["page"]["next_offset"]
			if next_offset is None:
				return result
			offset = int(next_offset)

	@staticmethod
	def _atomic_json(path: Path, payload: dict) -> None:
		path.parent.mkdir(parents=True, exist_ok=True)
		with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
		                                 prefix=f".{path.name}.", suffix=".tmp",
		                                 delete=False) as stream:
			json.dump(payload, stream, indent=2, sort_keys=True)
			stream.flush()
			os.fsync(stream.fileno())
			temporary = Path(stream.name)
		try:
			os.replace(temporary, path)
		finally:
			temporary.unlink(missing_ok=True)

	def _save_composition(self, path: Path, state: dict) -> None:
		# Bound replay metadata so long-running editor sessions cannot grow state
		# indefinitely merely because an agent uses unique patch identifiers.
		if len(state.get("receipts", {})) > 100:
			state["receipts"] = dict(list(state["receipts"].items())[-100:])
		self._atomic_json(path, state)
		self._stable_keys_by_instance = {
			int(metadata["instance_id"]): key for key, metadata in state["objects"].items()
			if metadata.get("instance_id") is not None
		}

	def _resolve_ref(self, reference: str | int, state: dict) -> tuple[str, int]:
		if isinstance(reference, int):
			for key, metadata in state["objects"].items():
				if int(metadata.get("instance_id", -1)) == reference:
					return key, reference
			return f"runtime:{reference}", reference
		key = self._safe_key(reference)
		metadata = state["objects"].get(key)
		if not metadata or metadata.get("instance_id") is None:
			raise ValueError(f"unknown object key: {key}")
		return key, int(metadata["instance_id"])

	@property
	def assets(self) -> AssetIndex:
		return AssetIndex(self.database)

	@property
	def discovery(self) -> AssetDiscovery:
		return AssetDiscovery(self.assets, session_dir=self.discovery_dir)

	def engine(self, command: str, fields: list[Any] | None = None) -> dict:
		fields = fields or []
		if command in {"transform", "transform3d", "delete", "fit_terrain"} and fields:
			state, _, _ = self._load_composition()
			self.vibe.guard([{"action": "delete" if command == "delete" else "transform", "instance_id": int(fields[0])}], state)
		if command == "clear":
			project = self.vibe.project()
			if project["locked_keys"] or project["locked_groups"]:
				raise ValueError("cannot clear a scene containing locked elements")
		return self.client.command(command, *fields)

	def search_assets(self, query: str, *, category: str | None = None,
	                  max_width: float | None = None, max_depth: float | None = None,
	                  has_collision: bool | None = None, defined_only: bool = False,
	                  limit: int = 30) -> list[dict]:
		candidate_limit = min(3000, max(limit, limit * 8 if defined_only else limit))
		candidates = self.assets.search(query, category=category, max_width=max_width,
		                                max_depth=max_depth, has_collision=has_collision,
		                                limit=candidate_limit)
		learned = []
		for annotation in self.profiles.search(query, min(limit, 50)):
			asset = self.assets.inspect(annotation["asset_id"])
			if not asset: continue
			if category is not None and asset.get("category") != category: continue
			if max_width is not None and float(asset.get("width") or math.inf) > max_width: continue
			if max_depth is not None and float(asset.get("depth") or math.inf) > max_depth: continue
			if has_collision is not None and bool(asset.get("has_collision")) != has_collision: continue
			learned.append({**asset, "visual_annotation": annotation})
		learned_ids = {a["id"] for a in learned}
		candidates = learned + [a for a in candidates if a["id"] not in learned_ids]
		results = []
		for asset in candidates:
			if defined_only:
				runtime = self.probe_asset(int(asset["id"]), ensure_renderable=False)
				if not runtime.get("defined"):
					continue
				asset = {**asset, "runtime": runtime}
			results.append(asset)
			if len(results) >= limit:
				break
		return results

	def probe_asset(self, model: int | str, *, ensure_renderable: bool = False) -> dict:
		return self.engine("asset_probe", [model, int(ensure_renderable)])["asset"]

	def inspect_asset(self, asset_id: int, *, ensure_renderable: bool = False) -> dict | None:
		asset = self.assets.inspect(asset_id)
		if asset is None:
			return None
		return {**asset, "runtime": self.probe_asset(asset_id,
		                                                ensure_renderable=ensure_renderable)}

	@staticmethod
	def _discovery_concepts(brief: str, roles: list[str] | None = None) -> list[str]:
		"""Split enumerated briefs so one frequent token cannot consume every slot."""
		parts = re.split(r"\s*(?::|,|;|\band\b|\bplus\b|\bwith\b)\s*", brief,
		                 flags=re.IGNORECASE)
		concepts: list[str] = []
		for value in [*(roles or []), *parts]:
			concept = re.sub(r"\s+", " ", str(value)).strip(" .:-")
			if concept and concept.lower() not in {item.lower() for item in concepts}:
				concepts.append(concept)
		return concepts or [brief.strip()]

	@staticmethod
	def _scale_prior(assets: list[dict], concept: str) -> list[dict]:
		"""Softly demote implausibly huge props without removing valid structures."""
		structure_words = {"building", "house", "wall", "fence", "warehouse", "tower", "shed"}
		if any(word in concept.lower().split() for word in structure_words):
			return assets
		return [item for _, item in sorted(enumerate(assets), key=lambda pair: (
			0 if float(pair[1].get("max_dim") or 0.0) <= 12.0 else
			1 if float(pair[1].get("max_dim") or 0.0) <= 40.0 else 2,
			pair[0],
		))]

	@staticmethod
	def _compact_asset(asset: dict) -> dict:
		runtime = asset.get("runtime") or {}
		compact = {
			"id": asset.get("id"), "name": asset.get("name"),
			"category": asset.get("category"),
			"prineside_category": asset.get("prineside_category"),
			"semantic_role": asset.get("semantic_role"),
			"dimensions": [asset.get("width"), asset.get("depth"), asset.get("height")],
			"max_dim": asset.get("max_dim"), "size_class": asset.get("size_class"),
			"metadata_warnings": asset.get("metadata_warnings", []),
			"availability": runtime.get("availability"),
			"collision_bounds": runtime.get("collision_bounds"),
			"ground_offset": runtime.get("ground_offset"),
		}
		return {key: value for key, value in compact.items()
		        if value is not None and value != []}

	def render_asset_views(self, asset_id: int, output_path: Path, *, size: int = 512) -> dict:
		"""Render two identity-oriented views and annotate the known local axes."""
		from PIL import Image, ImageDraw
		output_path = Path(output_path).resolve()
		output_path.parent.mkdir(parents=True, exist_ok=True)
		views = []
		for label, angle in (("front-left", math.radians(45.0)),
		                     ("rear-right", math.radians(225.0))):
			path = output_path.with_name(f"{output_path.stem}-{label}{output_path.suffix or '.png'}")
			result = self.engine("asset_preview", [asset_id, path, angle, size])
			views.append({"label": label, "angle_radians": angle, "path": result["path"]})
		images = [Image.open(item["path"]).convert("RGB") for item in views]
		sheet = Image.new("RGB", (size * 2, size + 54), "#17191d")
		draw = ImageDraw.Draw(sheet)
		for index, (item, view) in enumerate(zip(views, images)):
			view.thumbnail((size, size), Image.Resampling.LANCZOS)
			x = index * size
			sheet.paste(view, (x, 0))
			quadrant = "+X/+Y" if index == 0 else "-X/-Y"
			draw.text((x + 8, size + 6), f"view from {quadrant} · identity heading 0", fill="white")
			draw.text((x + 8, size + 28), "world +Z up; functional front requires review", fill="#aeb6c2")
		sheet.save(output_path)
		self.profiles.remember_preview(asset_id, output_path)
		return {"path": str(output_path), "asset_id": asset_id, "views": views,
		        "orientation": "model identity transform; +X right, +Y forward axis reference"}

	def discover_assets(self, brief: str, *, thoroughness: str = "quick",
	                    roles: list[str] | None = None, styles: list[str] | None = None,
	                    avoid: list[str] | None = None, limit: int = 40,
	                    detail: str = "compact") -> dict:
		"""Route the common case while preserving the auditable exhaustive workflow."""
		if thoroughness not in {"quick", "broad", "exhaustive"}:
			raise ValueError("thoroughness must be quick, broad, or exhaustive")
		if detail not in {"compact", "full"}:
			raise ValueError("detail must be compact or full")
		limit = max(1, min(int(limit), 200))
		def guided(candidates):
			def haystack(asset):
				return " ".join(str(asset.get(field) or "") for field in (
					"name", "category", "semantic_description", "role_tags", "style_tags", "placement_tags", "visual_annotation")).lower().replace("_", " ")
			def matches(term, text):
				return bool(re.search(r"(?<![a-z0-9])" + re.escape(term.lower().replace("_", " ")) + r"(?![a-z0-9])", text))
			candidates = [a for a in candidates if not any(matches(term, haystack(a)) for term in avoid or [])]
			return sorted(candidates, key=lambda a: -sum(matches(term, haystack(a)) for term in styles or []))
		if thoroughness == "quick":
			concepts = self._discovery_concepts(brief, roles)
			per_concept = max(2, math.ceil(limit / len(concepts)))
			channels: list[list[dict]] = []
			for concept in concepts:
				candidates = self.search_assets(
					concept, defined_only=True, limit=max(per_concept * 3, 8))
				channels.append(guided(self._scale_prior(candidates, concept))[:per_concept])
			assets, seen = [], set()
			for rank in range(per_concept):
				for channel in channels:
					if rank >= len(channel): continue
					asset = channel[rank]
					if asset["id"] in seen: continue
					seen.add(asset["id"])
					assets.append(asset)
					if len(assets) >= limit: break
				if len(assets) >= limit: break
			if len(assets) < limit:
				for asset in guided(self.search_assets(brief, defined_only=True, limit=limit * 4)):
					if asset["id"] in seen: continue
					seen.add(asset["id"])
					assets.append(asset)
					if len(assets) >= limit: break
			return {"mode": "quick", "detail": detail, "concepts": concepts,
			        "styles": styles or [], "avoid": avoid or [],
			        "assets": [self._compact_asset(asset) for asset in assets]
			                  if detail == "compact" else assets}
		coverage = self.discovery.start(
			{"scene": brief, "roles": roles or [], "styles": styles or [], "avoid": avoid or []},
			pool_limit=2500 if thoroughness == "exhaustive" else max(100, min(1000, limit * 10)))
		session_id = coverage["session_id"]
		result = {"mode": thoroughness, "coverage": coverage,
		          "assets": guided(self.discovery.results(session_id, limit=min(2000, limit * 10))["assets"])[:limit]}

		if thoroughness == "exhaustive":
			result["families"] = self.discovery.families(
				session_id, limit=min(200, limit * 4))["families"]
			result["residuals"] = self.discovery.residuals(
				session_id, limit=min(64, limit))["assets"]
		if detail == "compact":
			result["assets"] = [self._compact_asset(asset) for asset in result["assets"]]
		result["detail"] = detail
		return result

	@staticmethod
	def _expand_operations(operations: list[dict]) -> list[dict]:
		"""Expand deterministic line/ring authoring macros into ordinary places."""
		expanded: list[dict] = []
		for operation in operations:
			action = operation.get("action")
			if action not in {"array_line", "array_ring"}:
				expanded.append(copy.deepcopy(operation))
				continue
			count = int(operation.get("count", 0))
			if count < 1 or count > 500:
				raise ValueError("array count must be 1-500")
			prefix = str(operation.get("key") or "")
			if not prefix:
				raise ValueError("array operation requires a key prefix")
			rng = random.Random(str(operation.get("seed", prefix)))
			jitter = max(0.0, float(operation.get("jitter", 0.0)))
			heading_noise = max(0.0, float(operation.get("heading_noise", 0.0)))
			for index in range(count):
				fraction = 0.0 if count == 1 else index / (count - 1)
				if action == "array_line":
					x = float(operation["x"]) + (float(operation["x2"]) - float(operation["x"])) * fraction
					y = float(operation["y"]) + (float(operation["y2"]) - float(operation["y"])) * fraction
					z = float(operation.get("z", 0.0)) + (
						float(operation.get("z2", operation.get("z", 0.0))) -
						float(operation.get("z", 0.0))) * fraction
					heading = float(operation.get("heading", 0.0))
				else:
					angle = math.tau * index / count
					radius = float(operation["radius"])
					x = float(operation["x"]) + math.cos(angle) * radius
					y = float(operation["y"]) + math.sin(angle) * radius
					z = float(operation.get("z", 0.0))
					heading = math.degrees(angle) + float(operation.get("heading", 0.0))
					if operation.get("face") == "inward": heading += 180.0
				if jitter:
					x += rng.uniform(-jitter, jitter)
					y += rng.uniform(-jitter, jitter)
				if heading_noise:
					heading += rng.uniform(-heading_noise, heading_noise)
				heading %= 360.0
				item = {"action": "place", "key": f"{prefix}.{index:03d}",
				        "model": operation["model"], "x": x, "y": y, "z": z,
				        "heading": heading, "snap": bool(operation.get("snap", True))}
				for field in ("group", "supported_by", "support_mode", "clearance"):
					if field in operation: item[field] = operation[field]
				expanded.append(item)
		if len(expanded) > 500:
			raise ValueError("expanded scene patch exceeds 500 operations")
		return expanded

	def _raise_patch_error(self, error: str, session: dict, *, index: int | None = None,
	                       operation: dict | None = None, rolled_back: bool = False,
	                       code: str = "patch_invalid") -> None:
		payload = {"ok": False, "code": code, "error": error,
		           "current_revision": int(session.get("scene_revision", 0)),
		           "rolled_back": rolled_back}
		if index is not None: payload["operation_index"] = index
		if operation:
			for source, target in (("key", "key"), ("model", "model"), ("action", "action")):
				if operation.get(source) is not None: payload[target] = operation[source]
		raise ScenePatchError(payload)

	def _resolve_operations(self, operations: list[dict], state: dict, session: dict) -> list[dict]:
		operations = self._expand_operations(operations)
		self.vibe.guard(operations, state)
		planned_keys = set(state["objects"])
		for index, operation in enumerate(operations):
			if operation.get("action") in {"place", "place_relative"}:
				try: key = self._safe_key(operation.get("key", ""))
				except ValueError as error: self._raise_patch_error(str(error), session, index=index, operation=operation)
				if key in planned_keys:
					self._raise_patch_error(f"operation {index} duplicates object key: {key}",
					                        session, index=index, operation=operation)
				planned_keys.add(key)

		scene = {item.get("object_key", f"runtime:{item['instance_id']}"): copy.deepcopy(item)
		         for item in self.enumerate_scene()}
		resolved: list[dict] = []
		for index, source in enumerate(operations):
			operation = copy.deepcopy(source)
			action = operation.get("action")
			try:
				if action in {"place", "place_relative"}:
					key = self._safe_key(operation.get("key", ""))
					if operation.get("model") is None:
						raise ValueError("place requires model")
					runtime = self.probe_asset(operation["model"], ensure_renderable=True)
					if not runtime.get("defined"):
						raise ValueError(f"unknown model: {operation['model']}")
					if not runtime.get("renderable"):
						raise ValueError(f"model could not be loaded: {operation['model']}")
					if action == "place_relative":
						parent_key = self._safe_key(str(operation.get("relative_to", "")))
						parent = scene.get(parent_key)
						if parent is None: raise ValueError(f"unknown relative parent: {parent_key}")
						if any(abs(float(v)) > 1e-6 for v in parent["rotation"][:2]):
							raise ValueError("relative placement on tilted parents is unsupported")
						if operation.get("heading") is None:
							operation["heading"] = float(parent["rotation"][2])
						relation = operation.get("relation", "offset")
						if relation not in {"offset", "on_top", "in_front", "behind", "left", "right"}:
							raise ValueError("relative relation must be offset, on_top, in_front, behind, left, or right")
						base_x = base_y = base_z = 0.0
						if relation != "offset":
							parent_runtime = self.probe_asset(parent["model_id"], ensure_renderable=True)
							child_bounds = runtime.get("collision_bounds")
							parent_bounds = parent_runtime.get("collision_bounds")
							if not child_bounds or not parent_bounds:
								raise ValueError("bbox-relative placement requires collision bounds on both assets")
							if any(abs(float(v)) > 1e-6 for v in parent["rotation"][:2]):
								raise ValueError("relative placement on tilted parents is unsupported")
							annotation = self.profiles.get(int(parent["model_id"]))["annotation"] if operation.get("functional_front") else None
							features = (annotation or {}).get("features", {})
							if operation.get("functional_front") and "front_heading" not in features:
								raise ValueError("functional front unknown; inspect and annotate first")
							base_x, base_y, base_z = relative_offset(parent_bounds, child_bounds,
								float(parent["rotation"][2]), float(operation["heading"]), relation,
								float(operation.get("gap", 0)), float(features.get("front_heading", 0)))
							if relation == "on_top": operation.setdefault("supported_by", parent_key)
						angle = math.radians(float(parent["rotation"][2]))
						local_x = base_x + float(operation.get("local_x", 0.0))
						local_y = base_y + float(operation.get("local_y", 0.0))
						if operation.get("parent_anchor"):
							anchor = self.profiles.resolve_anchor(int(parent["model_id"]), operation["parent_anchor"])
							local_x = float(anchor[0]) + float(operation.get("local_x", 0))
							local_y = float(anchor[1]) + float(operation.get("local_y", 0))
							base_z = float(anchor[2])
						operation["x"] = float(parent["position"][0]) + local_x * math.cos(angle) - local_y * math.sin(angle)
						operation["y"] = float(parent["position"][1]) + local_x * math.sin(angle) + local_y * math.cos(angle)
						operation["z"] = float(parent["position"][2]) + base_z + float(operation.get("local_z", 0.0))
						if operation.get("child_anchor"):
							anchor = self.profiles.resolve_anchor(int(runtime["id"]), operation["child_anchor"])
							child_angle = math.radians(float(operation["heading"]))
							operation["x"] -= anchor[0]*math.cos(child_angle) - anchor[1]*math.sin(child_angle)
							operation["y"] -= anchor[0]*math.sin(child_angle) + anchor[1]*math.cos(child_angle)
							operation["z"] -= anchor[2]
						if relation == "on_top": operation["snap"] = False
						operation["action"] = "place"
					if operation.get("face_towards") is not None:
						if action == "place_relative" and (operation.get("relation", "offset") != "offset" or operation.get("child_anchor")):
							raise ValueError("face_towards with bbox-relative placement is ambiguous; use explicit heading")
						target = operation["face_towards"]
						if isinstance(target, str):
							if target not in scene: raise ValueError("unknown facing target")
							target = scene[target]["position"][:2]
						annotation = self.profiles.get(int(runtime["id"]))["annotation"]
						features = (annotation or {}).get("features", {})
						if "front_heading" not in features:
							raise ValueError("functional front unknown; inspect and annotate before face_towards")
						operation["heading"] = facing_heading(float(operation["x"]), float(operation["y"]), target, float(features["front_heading"]))
					if operation.get("heading") is None:
						raise ValueError("place requires heading")
					operation["heading"] = float(operation["heading"]) % 360.0
					if operation.get("x") is None or operation.get("y") is None:
						raise ValueError("place requires x and y")
					support_mode = operation.get("support_mode", "declare_only")
					if support_mode not in {"declare_only", "snap"}:
						raise ValueError("support_mode must be declare_only or snap")
					if support_mode == "snap":
						parent_key = self._safe_key(str(operation.get("supported_by", "")))
						parent = scene.get(parent_key)
						if parent is None: raise ValueError(f"support parent must precede child: {parent_key}")
						parent_runtime = self.probe_asset(parent["model_id"], ensure_renderable=True)
						child_bounds, parent_bounds = runtime.get("collision_bounds"), parent_runtime.get("collision_bounds")
						if not child_bounds or not parent_bounds:
							raise ValueError("support snap requires collision bounds on both assets")
						operation["z"] = (float(parent["position"][2]) + float(parent_bounds[1][2]) -
						                  float(child_bounds[0][2]) + float(operation.get("clearance", 0.0)))
						operation["snap"] = False
					if bool(operation.get("snap", True)):
						placement_result = self.engine("resolve_placement", [runtime["id"], operation["x"],
							operation["y"], operation.get("z", 0.0), operation["heading"], 1])
						placement = placement_result["placement"]
						operation["x"], operation["y"], operation["z"] = placement["position"]
						operation["snap"] = False
						if placement_result.get("support_evaluation"):
							operation["support_evaluation"] = placement_result["support_evaluation"]
					elif operation.get("z") is None:
						raise ValueError("place requires z when snap is false")
					operation["model"] = int(runtime["id"])
					if not operation.get("support_evaluation"):
						placement_result = self.engine("resolve_placement", [runtime["id"], operation["x"],
							operation["y"], operation["z"], operation["heading"], 0])
						operation["support_evaluation"] = placement_result.get("support_evaluation")
					resolved.append(operation)
					scene[key] = {"object_key": key, "model_id": int(runtime["id"]),
					              "position": [float(operation["x"]), float(operation["y"]), float(operation["z"])],
					              "rotation": [0.0, 0.0, float(operation["heading"])]}
				elif action in {"transform", "transform3d", "delete"}:
					reference = operation.get("key", operation.get("instance_id"))
					if reference is None: raise ValueError(f"{action} requires key or instance_id")
					key, instance_id = self._resolve_ref(reference, state)
					operation["key"] = key
					operation["instance_id"] = instance_id
					if action in {"transform", "transform3d"}:
						if any(operation.get(field) is None for field in ("x", "y", "heading")):
							raise ValueError("transform requires x, y and heading")
						operation["heading"] = float(operation["heading"]) % 360.0
						item = scene.get(key)
						if item is None: raise ValueError(f"object is not present in scene: {key}")
						if action == "transform3d":
							if any(operation.get(field) is None for field in ("z", "pitch", "roll")):
								raise ValueError("transform3d requires z, pitch and roll")
							operation["snap"] = False
						if bool(operation.get("snap", True)):
							placement_result = self.engine("resolve_placement", [item["model_id"], operation["x"],
								operation["y"], operation.get("z", 0.0), operation["heading"], 1])
							placement = placement_result["placement"]
							operation["x"], operation["y"], operation["z"] = placement["position"]
							operation["snap"] = False
							if placement_result.get("support_evaluation"):
								operation["support_evaluation"] = placement_result["support_evaluation"]
						elif operation.get("z") is None:
							raise ValueError("transform requires z when snap is false")
						if not operation.get("support_evaluation"):
							placement_result = self.engine("resolve_placement", [item["model_id"], operation["x"],
								operation["y"], operation["z"], operation["heading"], 0])
							operation["support_evaluation"] = placement_result.get("support_evaluation")
					resolved.append(operation)
				else:
					raise ValueError(f"unsupported action: {action!r}")
			except (ArianeError, KeyError, TypeError, ValueError) as error:
				current = self.engine("session_status")
				code = "asset_unavailable" if "model" in str(error).lower() else "patch_invalid"
				self._raise_patch_error(str(error), current, index=index, operation=source, code=code)
		return resolved

	@staticmethod
	def _world_collision_bounds(item: dict, bounds: list[list[float]]) -> dict:
		"""Return the yaw-rotated collision AABB used by authored scene patches."""
		x, y, z = map(float, item["position"])
		heading = math.radians(float(item.get("rotation", [0.0, 0.0, 0.0])[2]))
		cosine, sine = math.cos(heading), math.sin(heading)
		points = []
		for local_x in (float(bounds[0][0]), float(bounds[1][0])):
			for local_y in (float(bounds[0][1]), float(bounds[1][1])):
				points.append((x + local_x * cosine - local_y * sine,
				               y + local_x * sine + local_y * cosine))
		return {
			"left": min(point[0] for point in points),
			"right": max(point[0] for point in points),
			"bottom": min(point[1] for point in points),
			"top": max(point[1] for point in points),
			"min_z": z + float(bounds[0][2]),
			"max_z": z + float(bounds[1][2]),
		}

	def _asset_is_shelter(self, model_id: int) -> bool:
		# Unverified external prose must never authorize a collision exemption.
		try:
			passport = self.profiles.get(model_id)
		except ValueError:
			return False
		features = (passport["annotation"] or {}).get("features", {})
		if "roles" in features:
			return "shelter" in features["roles"]
		asset = passport["asset"]
		text = " ".join(str(asset.get(field) or "") for field in ("name", "role_tags")).lower().replace("_", " ")
		return any(re.search(rf"\b{re.escape(word)}\b", text) for word in (
			"canopy", "awning", "gazebo", "parasol", "umbrella", "pergola", "archway"))

	def _partition_intentional_containments(self, overlaps: list[dict],
	                                      scene: dict[str, dict]) -> tuple[list[dict], list[dict]]:
		"""Downgrade high-containment shelter pairs without hiding ordinary collisions."""
		blocking, intentional = [], []
		for overlap in overlaps:
			a_key, b_key = overlap.get("a_key"), overlap.get("b_key")
			a, b = scene.get(str(a_key)), scene.get(str(b_key))
			ratio = float(overlap.get("footprint_overlap_ratio", 0.0))
			a_shelter = bool(a and self._asset_is_shelter(int(a["model_id"])))
			b_shelter = bool(b and self._asset_is_shelter(int(b["model_id"])))
			if ratio >= 0.95 and a_shelter != b_shelter:
				intentional.append({**overlap, "severity": "info",
				                    "code": "contained_under_shelter",
				                    "shelter_key": a_key if a_shelter else b_key})
			else:
				blocking.append(overlap)
		return blocking, intentional

	def _hypothetical_scene(self, operations: list[dict]) -> dict[str, dict]:
		scene = {item.get("object_key", f"runtime:{item['instance_id']}"): copy.deepcopy(item)
		         for item in self.enumerate_scene()}
		for operation in operations:
			key = str(operation.get("key", ""))
			if operation["action"] == "delete":
				scene.pop(key, None)
			elif operation["action"] in {"transform", "transform3d"}:
				item = scene[key]
				item["position"] = [float(operation[field]) for field in ("x", "y", "z")]
				item["rotation"] = [float(operation.get("pitch", 0)), float(operation.get("roll", 0)), float(operation["heading"])]
			else:
				scene[key] = {"object_key": key, "instance_id": None,
				              "model_id": int(operation["model"]),
				              "position": [float(operation[field]) for field in ("x", "y", "z")],
				              "rotation": [0.0, 0.0, float(operation["heading"])]}
		return scene

	def _validate_hypothetical(self, operations: list[dict], state: dict) -> dict:
		"""Run support, duplicate, building, and prop checks on resolved poses."""
		scene = self._hypothetical_scene(operations)
		changed = {str(operation.get("key")) for operation in operations}
		by_instance = {int(item["instance_id"]): key for key, item in scene.items()
		               if item.get("instance_id") is not None}
		current_validation = self.engine("validate")
		support_issues = []
		for issue in current_validation.get("support_issues", []):
			key = by_instance.get(int(issue["instance_id"]))
			if key and key not in changed and key not in state.get("supports", {}):
				support_issues.append({**issue, "object_key": key})
		for index, operation in enumerate(operations):
			if operation["action"] == "delete" or operation.get("supported_by") or (
				operation.get("key") in state.get("supports", {})):
				continue
			evaluation = operation.get("support_evaluation")
			if evaluation and not evaluation.get("valid", False):
				support_issues.append({"operation_index": index, "object_key": operation.get("key"),
				                       **evaluation})

		asset_cache: dict[int, dict] = {}
		world: dict[str, dict] = {}
		for key, item in scene.items():
			model_id = int(item["model_id"])
			if model_id not in asset_cache:
				asset_cache[model_id] = self.engine("asset_detail", [model_id])["asset"]
			bounds = asset_cache[model_id].get("collision_bounds")
			if bounds:
				world[key] = self._world_collision_bounds(item, bounds)
				world[key]["footprint"] = footprint(item, bounds)

		building_overlaps, prop_overlaps, duplicates = [], [], []
		keys = list(scene)
		for index, a_key in enumerate(keys):
			a_item, a_box = scene[a_key], world.get(a_key)
			if not a_box: continue
			for b_key in keys[index + 1:]:
				b_item, b_box = scene[b_key], world.get(b_key)
				if not b_box: continue
				if (int(a_item["model_id"]) == int(b_item["model_id"]) and
				    math.dist(list(map(float, a_item["position"])),
				              list(map(float, b_item["position"]))) < 0.01 and
				    all(abs(float(a_item["rotation"][axis]) - float(b_item["rotation"][axis])) < 0.01
				        for axis in range(3))):
					duplicates.append({"a_key": a_key, "b_key": b_key,
					                   "model_id": int(a_item["model_id"])})
					continue
				if not polygons_overlap(a_box["footprint"], b_box["footprint"]): continue
				if min(a_box["max_z"], b_box["max_z"]) - max(a_box["min_z"], b_box["min_z"]) <= 0.05: continue
				overlap_x = min(a_box["right"], b_box["right"]) - max(a_box["left"], b_box["left"])
				overlap_y = min(a_box["top"], b_box["top"]) - max(a_box["bottom"], b_box["bottom"])
				a_width, a_depth = a_box["right"] - a_box["left"], a_box["top"] - a_box["bottom"]
				b_width, b_depth = b_box["right"] - b_box["left"], b_box["top"] - b_box["bottom"]
				both_buildings = min(a_width, a_depth, b_width, b_depth) >= 3.0
				if both_buildings and overlap_x > 1.0 and overlap_y > 1.0 and overlap_x * overlap_y > 4.0:
					building_overlaps.append({"a_key": a_key, "b_key": b_key,
					                          "overlap_area": round(overlap_x * overlap_y, 3)})
				overlap_z = min(a_box["max_z"], b_box["max_z"]) - max(a_box["min_z"], b_box["min_z"])
				footprint_ratio = ((overlap_x * overlap_y) / min(a_width * a_depth, b_width * b_depth)
				                   if overlap_x > 0.0 and overlap_y > 0.0 else 0.0)
				if not both_buildings and overlap_z > 0.05 and footprint_ratio >= 0.35:
					prop_overlaps.append({"a_key": a_key, "b_key": b_key,
					                      "footprint_overlap_ratio": round(footprint_ratio, 3),
					                      "vertical_overlap": round(overlap_z, 3)})
		prop_overlaps, intentional = self._partition_intentional_containments(prop_overlaps, scene)
		zones = self.vibe.project()["zones"]
		semantic = self.validate_semantic_zones(zones, list(scene.values())) if zones else {"valid": True, "issues": []}
		return {"support_contract": "terrain-support-v1", "semantic_issues": semantic["issues"],
		        "evaluated_operations": sum(1 for operation in operations
		                                    if operation.get("support_evaluation")),
		        "support_issues": support_issues,
		        "building_overlaps": building_overlaps,
		        "prop_overlaps": prop_overlaps,
		        "exact_duplicates": duplicates,
		        "intentional_containments": intentional,
		        "valid": semantic["valid"] and not (support_issues or building_overlaps or prop_overlaps or duplicates)}

	def apply_scene_patch(self, operations: list[dict], *, patch_id: str | None = None,
	                      expected_revision: int | None = None) -> dict:
		self.state_dir.mkdir(parents=True, exist_ok=True)
		with _edit_lock(self.state_dir / "edit.lock"):
			state, _, _ = self._load_composition()
			if patch_id and patch_id in state["receipts"]:
				return {**state["receipts"][patch_id], "replayed": True}
			# Validate locks before journalling or mutation.
			self.vibe.guard(operations, state)
			path, record = self.vibe.begin_record(patch_id, operations)
			try:
				receipt = self._apply_scene_patch_unlocked(operations, patch_id=patch_id,
				                                         expected_revision=expected_revision)
			except Exception as error:
				record.update(status="failed", error=str(error))
				self._atomic_json(path, record)
				raise
			self.vibe.finish_record(path, record, receipt)
			return {**receipt, "edit_id": record["id"]}

	def _apply_scene_patch_unlocked(self, operations: list[dict], *, patch_id: str | None = None,
		                  expected_revision: int | None = None) -> dict:
		"""Apply a revisioned, replay-safe patch using caller-owned object keys."""
		if not operations or len(operations) > 500:
			raise ValueError("a scene patch must contain 1-500 operations")
		state, path, session = self._load_composition()
		if expected_revision is not None and int(session["scene_revision"]) != expected_revision:
			self._raise_patch_error(
				f"scene revision changed: expected {expected_revision}, got {session['scene_revision']}",
				session, code="stale_revision")
		patch_id = self._safe_key(patch_id or f"patch:{uuid.uuid4().hex}", "patch id")
		if patch_id in state["receipts"]:
			return {**state["receipts"][patch_id], "replayed": True}

		operations = self._resolve_operations(operations, state, session)
		current = self.engine("session_status")
		if int(current["scene_revision"]) != int(session["scene_revision"]):
			self._raise_patch_error("scene changed while resolving patch", current,
			                        code="stale_revision")

		working = copy.deepcopy(state)
		objects_by_id = {int(item["instance_id"]): item for item in self.enumerate_scene()}
		applied: list[dict] = []
		created_ids: list[int] = []
		transformed: list[tuple[int, dict]] = []
		deleted: list[tuple[str, dict]] = []
		failed_index: int | None = None
		failed_operation: dict | None = None
		try:
			# Deletes run last, making compensation reliable if placement or transform fails.
			for failed_index, operation in [(index, item) for index, item in enumerate(operations)
			                                 if item["action"] != "delete"]:
				failed_operation = operation
				action = operation["action"]
				if action == "place":
					key = operation["key"]
					fields = [operation[field] for field in ("model", "x", "y", "z", "heading")]
					fields.append(int(operation.get("snap", True)))
					result = self.engine("place", fields)
					instance_id = int(result["instance_id"])
					created_ids.append(instance_id)
					working["objects"][key] = {"instance_id": instance_id,
						"group": operation.get("group")}
					if operation.get("group"):
						group = self._safe_key(operation["group"], "group key")
						working["groups"].setdefault(group, [])
						if key not in working["groups"][group]: working["groups"][group].append(key)
					if operation.get("supported_by"):
						working["supports"][key] = self._safe_key(operation["supported_by"])
					applied.append({"action": action, "key": key, "instance_id": instance_id})
				else:
					key, instance_id = self._resolve_ref(
						operation.get("key", operation.get("instance_id")), working)
					original = objects_by_id.get(instance_id)
					if original is None:
						raise ValueError(f"object is not present in scene: {key}")
					transformed.append((instance_id, original))
					fields = [instance_id] + [operation[field] for field in ("x", "y", "z", "heading")]
					fields.append(int(operation.get("snap", True)))
					if action == "transform3d":
						self.engine("transform3d", [instance_id, operation["x"], operation["y"], operation["z"],
						                          operation["pitch"], operation["roll"], operation["heading"]])
					else:
						self.engine("transform", fields)
					applied.append({"action": action, "key": key, "instance_id": instance_id})

			for failed_index, operation in [(index, item) for index, item in enumerate(operations)
			                                 if item["action"] == "delete"]:
				failed_operation = operation
				key, instance_id = self._resolve_ref(
					operation.get("key", operation.get("instance_id")), working)
				original = objects_by_id.get(instance_id)
				if original is None:
					raise ValueError(f"object is not present in scene: {key}")
				self.engine("delete", [instance_id])
				deleted.append((key, original))
				working["objects"].pop(key, None)
				working["supports"].pop(key, None)
				working["supports"] = {child: parent for child, parent in working["supports"].items()
					if parent != key}
				for members in working["groups"].values():
					if key in members: members.remove(key)
				applied.append({"action": "delete", "key": key, "instance_id": instance_id})
		except Exception as error:
			# Engine V1 has no native transaction. Recreate successfully deleted
			# objects first, then restore transforms and remove new placements. Stable
			# caller keys absorb the runtime-ID remap in the sidecar state.
			compensated = True
			for key, original in deleted:
				position, rotation = original["position"], original["rotation"]
				try:
					restored = self.engine("place", [original["model_id"], *position, rotation[2], 0])
					new_id = int(restored["instance_id"])
					if abs(rotation[0]) > 0.001 or abs(rotation[1]) > 0.001:
						self.engine("transform3d", [new_id, *position, *rotation])
					state["objects"].setdefault(key, {})["instance_id"] = new_id
				except Exception:
					compensated = False
			for instance_id, original in reversed(transformed):
				position, rotation = original["position"], original["rotation"]
				try: self.engine("transform3d", [instance_id, *position, *rotation])
				except Exception: compensated = False
			for instance_id in reversed(created_ids):
				try: self.engine("delete", [instance_id])
				except Exception: compensated = False
			if deleted:
				self._save_composition(path, state)
			current = self.engine("session_status")
			self._raise_patch_error(str(error), current, index=failed_index,
			                        operation=failed_operation, rolled_back=compensated,
			                        code="operation_failed")

		receipt = {"ok": True, "patch_id": patch_id, "applied": len(applied),
		           "objects": applied, "scene_revision": self.engine("session_status")["scene_revision"],
		           "replayed": False}
		working["receipts"][patch_id] = receipt
		self._save_composition(path, working)
		return receipt

	def resolve_scene_patch(self, operations: list[dict], *,
	                        expected_revision: int | None = None) -> dict:
		"""Resolve assets, arrays, relative placement, terrain and support without scene mutation."""
		if not operations or len(operations) > 500:
			raise ValueError("a scene patch must contain 1-500 operations")
		state, _, session = self._load_composition()
		base_revision = int(session["scene_revision"])
		if expected_revision is not None and base_revision != expected_revision:
			self._raise_patch_error(
				f"scene revision changed: expected {expected_revision}, got {base_revision}",
				session, code="stale_revision")
		resolved = self._resolve_operations(operations, state, session)
		current = self.engine("session_status")
		if int(current["scene_revision"]) != base_revision:
			self._raise_patch_error("scene changed while resolving patch", current,
			                        code="stale_revision")
		canonical = json.dumps({"session_id": session.get("session_id"),
		                        "revision": base_revision, "operations": resolved},
		                       sort_keys=True, separators=(",", ":"))
		plan_id = f"plan-{hashlib.sha256(canonical.encode()).hexdigest()[:20]}"
		preflight = self._validate_hypothetical(resolved, state)
		payload = {"schema": 1, "plan_id": plan_id,
		           "session_id": session.get("session_id"),
		           "logical_path": session.get("logical_path"),
		           "based_on_scene_revision": base_revision,
		           "operations": resolved, "preflight": preflight}
		self._atomic_json(self.plan_dir / f"{plan_id}.json", payload)
		return {**payload, "operation_count": len(resolved)}

	def apply_resolved_plan(self, plan_id: str, *, patch_id: str | None = None) -> dict:
		if not re.fullmatch(r"plan-[0-9a-f]{20}", plan_id):
			raise ValueError("invalid resolved plan id")
		path = self.plan_dir / f"{plan_id}.json"
		if not path.exists():
			raise ValueError(f"unknown resolved plan: {plan_id}")
		plan = json.loads(path.read_text(encoding="utf-8"))
		session = self.engine("session_status")
		if session.get("session_id") != plan.get("session_id") or session.get("logical_path") != plan.get("logical_path"):
			self._raise_patch_error("resolved plan belongs to a different scratch session",
			                        session, code="stale_plan")
		if not plan.get("preflight", {}).get("valid", True):
			self._raise_patch_error("resolved plan failed composition preflight",
			                        session, code="plan_invalid")
		return self.apply_scene_patch(plan["operations"], patch_id=patch_id or plan_id,
		                              expected_revision=int(plan["based_on_scene_revision"]))

	def create_group(self, group: str, members: list[str | int]) -> dict:
		group = self._safe_key(group, "group key")
		state, path, _ = self._load_composition()
		resolved: list[str] = []
		for reference in members:
			key, instance_id = self._resolve_ref(reference, state)
			state["objects"].setdefault(key, {"instance_id": instance_id, "group": group})
			state["objects"][key]["group"] = group
			if key not in resolved: resolved.append(key)
		state["groups"][group] = resolved
		self._save_composition(path, state)
		return {"ok": True, "group": group, "member_count": len(resolved), "members": resolved}

	def list_groups(self) -> dict:
		state, _, _ = self._load_composition()
		return {"groups": [{"group": key, "member_count": len(members)}
		                   for key, members in sorted(state["groups"].items())]}

	def inspect_group(self, group: str, *, offset: int = 0, limit: int = 50) -> dict:
		group = self._safe_key(group, "group key")
		if offset < 0 or limit < 1 or limit > 250:
			raise ValueError("offset must be non-negative and limit must be 1-250")
		state, _, _ = self._load_composition()
		members = state["groups"].get(group)
		if members is None:
			raise ValueError(f"unknown group: {group}")
		scene = {item["object_key"]: item for item in self.enumerate_scene()}
		items = [{**scene.get(key, {}), "object_key": key,
		          "supported_by": state["supports"].get(key)} for key in members]
		page = items[offset:offset + limit]
		next_offset = offset + len(page)
		return {"group": group, "items": page, "page": {"offset": offset,
		        "returned": len(page), "total": len(items),
		        "next_offset": next_offset if next_offset < len(items) else None}}

	def transform_group(self, group: str, *, dx: float = 0.0, dy: float = 0.0,
		                dz: float = 0.0, dheading: float = 0.0,
		                patch_id: str | None = None,
		                expected_revision: int | None = None) -> dict:
		group = self._safe_key(group, "group key")
		state, _, _ = self._load_composition()
		members = state["groups"].get(group)
		if not members:
			raise ValueError(f"unknown or empty group: {group}")
		scene = {item["object_key"]: item for item in self.enumerate_scene()}
		items = [scene[key] for key in members if key in scene]
		if len(items) != len(members):
			raise RuntimeError("group membership is stale; inspect and repair the group")
		if len(items) != len(members):
			raise RuntimeError("group membership is stale; inspect and repair the group")
		cx = sum(float(item["position"][0]) for item in items) / len(items)
		cy = sum(float(item["position"][1]) for item in items) / len(items)
		angle = math.radians(dheading)
		cosine, sine = math.cos(angle), math.sin(angle)
		operations = []
		for key, item in zip(members, items):
			x, y, z = map(float, item["position"])
			rx, ry = x - cx, y - cy
			operations.append({"action": "transform", "key": key,
				"x": cx + rx * cosine - ry * sine + dx,
				"y": cy + rx * sine + ry * cosine + dy, "z": z + dz,
				"heading": float(item["rotation"][2]) + dheading, "snap": False})
		return self.apply_scene_patch(operations, patch_id=patch_id,
		                              expected_revision=expected_revision)

	def clone_group(self, group: str, new_group: str, key_prefix: str, *,
		            dx: float = 0.0, dy: float = 0.0, dz: float = 0.0,
		            dheading: float = 0.0, patch_id: str | None = None) -> dict:
		group, new_group = self._safe_key(group, "group key"), self._safe_key(new_group, "group key")
		key_prefix = self._safe_key(key_prefix, "key prefix")
		state, _, _ = self._load_composition()
		members = state["groups"].get(group)
		if not members or new_group in state["groups"]:
			raise ValueError("source group must exist and destination group must be new")
		scene = {item["object_key"]: item for item in self.enumerate_scene()}
		items = [scene[key] for key in members if key in scene]
		cx = sum(float(item["position"][0]) for item in items) / len(items)
		cy = sum(float(item["position"][1]) for item in items) / len(items)
		angle = math.radians(dheading)
		cosine, sine = math.cos(angle), math.sin(angle)
		operations = []
		for index, item in enumerate(items):
			x, y, z = map(float, item["position"])
			rx, ry = x - cx, y - cy
			operations.append({"action": "place", "key": f"{key_prefix}.{index:03d}",
				"model": int(item["model_id"]),
				"x": cx + rx * cosine - ry * sine + dx,
				"y": cy + rx * sine + ry * cosine + dy, "z": z + dz,
				"heading": float(item["rotation"][2]) + dheading, "snap": False,
				"group": new_group})
		return self.apply_scene_patch(operations, patch_id=patch_id)

	def delete_group(self, group: str, *, patch_id: str | None = None) -> dict:
		group = self._safe_key(group, "group key")
		state, _, _ = self._load_composition()
		members = state["groups"].get(group)
		if members is None:
			raise ValueError(f"unknown group: {group}")
		result = self.apply_scene_patch(
			[{"action": "delete", "key": key} for key in list(members)], patch_id=patch_id)
		state, path, _ = self._load_composition()
		state["groups"].pop(group, None)
		self._save_composition(path, state)
		return result

	def set_support(self, child: str | int, parent: str | int) -> dict:
		state, path, _ = self._load_composition()
		child_key, _ = self._resolve_ref(child, state)
		parent_key, _ = self._resolve_ref(parent, state)
		if child_key == parent_key:
			raise ValueError("an object cannot support itself")
		state["supports"][child_key] = parent_key
		self._save_composition(path, state)
		return {"ok": True, "child": child_key, "supported_by": parent_key}

	def snap_to_support(self, child: str | int, parent: str | int,
		                clearance: float = 0.0) -> dict:
		state, _, _ = self._load_composition()
		child_key, child_id = self._resolve_ref(child, state)
		parent_key, parent_id = self._resolve_ref(parent, state)
		scene = {int(item["instance_id"]): item for item in self.enumerate_scene()}
		child_item, parent_item = scene[child_id], scene[parent_id]
		child_asset = self.engine("asset_detail", [child_item["model_id"]])["asset"]
		parent_asset = self.engine("asset_detail", [parent_item["model_id"]])["asset"]
		if not child_asset.get("collision_bounds") or not parent_asset.get("collision_bounds"):
			raise ValueError("snap_to_support requires collision bounds on both assets")
		parent_top = float(parent_item["position"][2]) + float(parent_asset["collision_bounds"][1][2])
		child_bottom = float(child_asset["collision_bounds"][0][2])
		position = list(map(float, child_item["position"]))
		position[2] = parent_top - child_bottom + float(clearance)
		result = self.apply_scene_patch([{"action": "transform", "key": child_key,
			"x": position[0], "y": position[1], "z": position[2],
			"heading": float(child_item["rotation"][2]), "snap": False}],
			patch_id=f"support:{uuid.uuid4().hex}")
		self.set_support(child_key, parent_key)
		return {**result, "supported_by": parent_key, "z": position[2]}

	def validate_composition(self) -> dict:
		state, _, _ = self._load_composition()
		result = self.engine("validate")
		by_instance = {int(metadata["instance_id"]): key
		               for key, metadata in state["objects"].items()}
		scene = {key: item for item in self.enumerate_scene()
		         if (key := item.get("object_key")) is not None}
		asset_cache: dict[int, dict] = {}

		def asset(item: dict) -> dict:
			model_id = int(item["model_id"])
			if model_id not in asset_cache:
				asset_cache[model_id] = self.engine("asset_detail", [model_id])["asset"]
			return asset_cache[model_id]

		def support_geometry(child_key: str, parent_key: str) -> tuple[bool, dict]:
			child, parent = scene.get(child_key), scene.get(parent_key)
			if not child or not parent:
				return False, {"code": "support_object_missing"}
			child_bounds = asset(child).get("collision_bounds")
			parent_bounds = asset(parent).get("collision_bounds")
			if not child_bounds or not parent_bounds:
				return False, {"code": "support_bounds_missing"}
			child_x, child_y, child_z = map(float, child["position"])
			parent_x, parent_y, parent_z = map(float, parent["position"])
			parent_half_x = max(abs(float(parent_bounds[0][0])), abs(float(parent_bounds[1][0])))
			parent_half_y = max(abs(float(parent_bounds[0][1])), abs(float(parent_bounds[1][1])))
			child_half_x = max(abs(float(child_bounds[0][0])), abs(float(child_bounds[1][0])))
			child_half_y = max(abs(float(child_bounds[0][1])), abs(float(child_bounds[1][1])))
			margin_x = parent_half_x + child_half_x * 0.35
			margin_y = parent_half_y + child_half_y * 0.35
			vertical_gap = (child_z + float(child_bounds[0][2])) - (
				parent_z + float(parent_bounds[1][2]))
			valid = abs(child_x - parent_x) <= margin_x and abs(child_y - parent_y) <= margin_y
			valid = valid and -0.1 <= vertical_gap <= 0.25
			return valid, {"code": "invalid_support_geometry", "vertical_gap": round(vertical_gap, 3),
			               "horizontal_offset": [round(child_x - parent_x, 3),
			                                     round(child_y - parent_y, 3)]}

		acknowledged, unresolved = [], []
		for issue in result.get("support_issues", []):
			key = by_instance.get(int(issue["instance_id"]), f"runtime:{issue['instance_id']}")
			decorated = {**issue, "object_key": key}
			if key in state["supports"]:
				decorated["supported_by"] = state["supports"][key]
				valid, geometry = support_geometry(key, state["supports"][key])
				if valid:
					acknowledged.append(decorated)
				else:
					unresolved.append({**decorated, **geometry})
			else:
				inferred_parent = None
				for parent_key in scene:
					if parent_key == key: continue
					valid, _ = support_geometry(key, parent_key)
					if valid:
						inferred_parent = parent_key
						break
				if inferred_parent:
					acknowledged.append({**decorated, "supported_by": inferred_parent,
					                     "support_inferred": True})
				else:
					unresolved.append(decorated)
		for field in ("building_overlaps", "prop_overlaps", "exact_duplicates"):
			for overlap in result.get(field, []):
				for endpoint in ("a", "b"):
					instance_id = int(overlap[endpoint])
					overlap[f"{endpoint}_key"] = by_instance.get(instance_id, f"runtime:{instance_id}")
		blocking_props, intentional = self._partition_intentional_containments(
			result.get("prop_overlaps", []), scene)
		result["prop_overlaps"] = blocking_props
		result["intentional_containments"] = intentional
		blocking_overlaps = any(result.get(field) for field in (
			"building_overlaps", "prop_overlaps", "exact_duplicates"))
		return {**result, "valid": not unresolved and not blocking_overlaps,
		        "support_issues": unresolved, "acknowledged_supports": acknowledged}

	def capture_views(self, output_dir: Path, fov: float = 58.0, *,
	                  center: list[float] | None = None,
	                  span: float | None = None) -> dict:
		return self.client.capture_views(output_dir, fov, center=center, span=span)

	def enumerate_scene(self, *, x: float | None = None, y: float | None = None,
	                    radius: float | None = None) -> list[dict]:
		"""Read one immutable revision and decorate runtime IDs with stable keys."""
		items: list[dict] = []
		stable_keys = dict(self._stable_keys_by_instance)
		try:
			state, _, _ = self._load_composition()
			stable_keys.update({int(metadata["instance_id"]): key
			                    for key, metadata in state["objects"].items()
			                    if metadata.get("instance_id") is not None})
		except RuntimeError:
			pass
		offset = 0
		revision: int | None = None
		while True:
			fields: list[object] = [offset, 256]
			if x is not None and y is not None and radius is not None:
				fields.extend([x, y, radius])
			page = self.engine("list_page", fields)
			if revision is None:
				revision = page["scene_revision"]
			elif page["scene_revision"] != revision:
				raise RuntimeError("scene changed during pagination; retry enumeration")
			for item in page["items"]:
				stable_key = stable_keys.get(int(item["instance_id"]))
				if stable_key:
					item["object_key"] = stable_key
			items.extend(page["items"])
			next_offset = page["page"]["next_offset"]
			if next_offset is None:
				return items
			offset = int(next_offset)

	@staticmethod
	def _safe_checkpoint_name(name: str) -> str:
		if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", name):
			raise ValueError("checkpoint name must be 1-64 safe filename characters")
		return name

	@property
	def checkpoint_dir(self) -> Path:
		return self.state_dir / "checkpoints"

	def checkpoint_list(self) -> dict:
		if not self.checkpoint_dir.exists():
			return {"checkpoints": []}
		return {"checkpoints": sorted(path.stem for path in self.checkpoint_dir.glob("*.json"))}

	def checkpoint_save(self, name: str) -> dict:
		name = self._safe_checkpoint_name(name)
		objects = self.enumerate_scene()
		layer_uuid = str(uuid.uuid4())
		for index, item in enumerate(objects):
			# Caller-owned keys survive; legacy runtime keys are upgraded in the manifest.
			if item["object_key"].startswith("runtime:"):
				item["object_key"] = f"{layer_uuid}:{index:06d}"
			self._stable_keys_by_instance[int(item["instance_id"])] = item["object_key"]
		session = self.engine("session_status")
		manifest = {
			"schema": CHECKPOINT_SCHEMA,
			"name": name,
			"layer_uuid": layer_uuid,
			"scene_revision": session["scene_revision"],
			"source_scene": {
				"logical_path": session.get("logical_path", ""),
				"physical_path": session.get("physical_path", ""),
			},
			"camera": self.engine("camera_context")["camera"],
			"objects": objects,
		}
		manifest["project"] = self.vibe.project()
		if session.get("active"):
			state, _, _ = self._load_composition()
			manifest["composition"] = {"groups": state["groups"], "supports": state["supports"]}
		self.checkpoint_dir.mkdir(parents=True, exist_ok=True)
		target = self.checkpoint_dir / f"{name}.json"
		with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=self.checkpoint_dir,
		                                 prefix=f".{name}.", suffix=".tmp", delete=False) as stream:
			json.dump(manifest, stream, indent=2)
			stream.flush()
			os.fsync(stream.fileno())
			temporary = Path(stream.name)
		try:
			# Re-read before publication so a partial/corrupt manifest never becomes current.
			candidate = json.loads(temporary.read_text(encoding="utf-8"))
			if candidate.get("schema") != CHECKPOINT_SCHEMA or candidate.get("layer_uuid") != layer_uuid:
				raise ValueError("checkpoint verification failed")
			os.replace(temporary, target)
			directory_fd = os.open(self.checkpoint_dir, os.O_RDONLY)
			try:
				os.fsync(directory_fd)
			finally:
				os.close(directory_fd)
		finally:
			temporary.unlink(missing_ok=True)
		return {"checkpoint": name, "path": str(target), "object_count": len(objects)}

	def checkpoint_restore(self, name: str) -> dict:
		name = self._safe_checkpoint_name(name)
		path = self.checkpoint_dir / f"{name}.json"
		manifest = json.loads(path.read_text(encoding="utf-8"))
		if manifest.get("schema") != CHECKPOINT_SCHEMA or manifest.get("name") != name:
			raise ValueError("unsupported or mismatched checkpoint manifest")
		if not self.engine("session_status")["active"]:
			raise RuntimeError("begin a scratch session before restoring a checkpoint")
		# A full restore is a replacement, so protected elements must be unlocked explicitly.
		project = self.vibe.project()
		if project["locked_keys"] or project["locked_groups"]:
			raise ValueError("checkpoint restore would replace locked elements; use selective edits")
		for item in manifest["objects"]:
			if not self.probe_asset(int(item["model_id"]), ensure_renderable=True).get("renderable"):
				raise ValueError(f"checkpoint model unavailable: {item['model_id']}")
		self.engine("clear")
		mapping = []
		for item in manifest["objects"]:
			position = item["position"]
			rotation = item["rotation"]
			placed = self.engine("place", [item["model_id"], *position, rotation[2], 0])
			if abs(rotation[0]) > 0.001 or abs(rotation[1]) > 0.001:
				self.engine("transform3d", [placed["instance_id"], *position, *rotation])
			mapping.append({"object_key": item["object_key"],
			                "instance_id": placed["instance_id"]})
			self._stable_keys_by_instance[int(placed["instance_id"])] = item["object_key"]
		session = self.engine("session_status")
		composition = manifest.get("composition", {})
		state = {"schema": COMPOSITION_SCHEMA, "session_id": session["session_id"],
		         "objects": {item["object_key"]: {"instance_id": item["instance_id"]} for item in mapping},
		         "groups": composition.get("groups", {}), "supports": composition.get("supports", {}), "receipts": {}}
		self._save_composition(self._composition_path(session), state)
		if "project" in manifest:
			self.vibe.update_project({k: v for k, v in manifest["project"].items() if k not in {"revision", "schema"}})
		return {"checkpoint": name, "restored_count": len(mapping), "objects": mapping}

	def validate_semantic_zones(self, zones: list[dict], objects: list[dict] | None = None) -> dict:
		"""Oriented collision-footprint checks for explicit routes; not a navmesh."""
		for zone in zones:
			if zone.get("shape") not in {"circle", "rect"}:
				raise ValueError("semantic zone shape must be circle or rect")
			fields = ["x", "y", "radius"] if zone["shape"] == "circle" else ["x", "y", "width", "depth"]
			if any(not math.isfinite(float(zone[field])) for field in fields):
				raise ValueError("zone geometry must be finite")
			if any(float(zone[field]) <= 0 for field in fields[2:]):
				raise ValueError("zone dimensions must be positive")
		objects = self.enumerate_scene() if objects is None else objects
		issues, unknowns, cache = [], [], {}
		for item in objects:
			model = int(item["model_id"])
			if model not in cache:
				cache[model] = self.engine("asset_detail", [model])["asset"].get("collision_bounds")
			bounds = cache[model]
			if not bounds:
				unknowns.append(item["object_key"])
				continue
			poly = footprint(item, bounds)
			for zone in zones:
				if item["object_key"] in zone.get("ignore_keys", []): continue
				z = float(item["position"][2])
				if zone.get("z_min") is not None and z + bounds[1][2] <= zone["z_min"]: continue
				if zone.get("z_max") is not None and z + bounds[0][2] >= zone["z_max"]: continue
				if zone["shape"] == "circle":
					blocked = circle_intersects_polygon(zone["x"], zone["y"], zone["radius"], poly)
				else:
					blocked = polygons_overlap(poly, rectangle(zone["x"], zone["y"], zone["width"],
					                                               zone["depth"], zone.get("heading", 0)))
				if blocked:
					issues.append({"code": "keep_clear_blocked", "severity": zone.get("severity", "error"),
					               "zone": zone.get("name", "unnamed"), "zone_role": zone.get("role", "keep_clear"),
					               "object_key": item["object_key"], "instance_id": item.get("instance_id")})
		return {"valid": not any(i["severity"] == "error" for i in issues),
		        "object_count": len(objects), "issues": issues, "unknown_collision_keys": unknowns,
		        "scope": "scratch collision footprints; native-world traversal requires survey/raycast"}

	def environment(self, **settings):
		previous = self.engine("environment")
		updates = {key: value for key, value in settings.items() if value is not None}
		if not updates: return previous
		fields = [updates.get(key, previous[key]) for key in ("hour", "minute", "weather_a", "weather_b", "blend")]
		return {**self.engine("environment", fields), "previous": {
			key: previous[key] for key in ("hour", "minute", "weather_a", "weather_b", "blend")}}

	def suppress_native_models(self, x, y, radius, model_ids):
		if not model_ids or len(model_ids) > 64 or not 0 < radius <= 250:
			raise ValueError("requires 1-64 model ids and radius (0,250]")
		locked = set(self.vibe.project()["locked_keys"])
		offset = 0
		while True:
			page = self.engine("inspect_zone_page", [x, y, radius, offset, 256])
			for item in page["items"]:
				if item["model_id"] in model_ids and f"runtime:{item['instance_id']}" in locked:
					raise ValueError("native suppression would affect a locked object")
			offset = page["page"]["next_offset"]
			if offset is None: break
		return self.engine("suppress_world_models", [x, y, radius, *model_ids])

	def dispatch(self, method: str, params: dict | None = None) -> Any:
		params = params or {}
		if method.startswith("vibe."):
			return self.vibe.dispatch(method[5:], params)
		if method.startswith("rig."):
			rig = ReviewRig(self)
			methods = {"rig.create": rig.create, "rig.capture": rig.capture, "rig.compare": rig.compare}
			if method not in methods: raise ValueError(f"unknown rig method: {method}")
			return methods[method](**params)
		if method.startswith("catalogue."):
			catalogue = CreativeCatalog(self)
			methods = {"catalogue.search": catalogue.search, "catalogue.palette": catalogue.palette, "catalogue.board": catalogue.board}
			if method not in methods: raise ValueError(f"unknown catalogue method: {method}")
			return methods[method](**params)
		if method.startswith("profiles."):
			methods = {"profiles.get": self.profiles.get, "profiles.anchors": self.profiles.anchors, "profiles.annotate": self.profiles.annotate,
			           "profiles.search": self.profiles.search, "profiles.index_images": self.profiles.index_images,
			           "profiles.reference_search": self.profiles.reference_search}
			if method not in methods: raise ValueError("unknown profile method")
			return methods[method](**params)
		if method == "scene.environment": return self.environment(**params)
		if method == "scene.suppress_native": return self.suppress_native_models(**params)
		if method == "health":
			return {"engine": self.engine("ping"), "database": str(self.database)}
		if method == "engine.command":
			return self.engine(params["command"], params.get("fields"))
		if method == "assets.search":
			return self.search_assets(**params)
		if method == "assets.inspect":
			return self.inspect_asset(int(params["asset_id"]),
			                          ensure_renderable=bool(params.get("ensure_renderable", False)))
		if method == "assets.preview":
			return self.render_asset_views(int(params["asset_id"]), Path(params["output_path"]),
			                               size=int(params.get("size", 512)))
		if method == "assets.similar":
			return self.assets.similar(int(params["asset_id"]), int(params.get("limit", 20)))
		if method == "discovery.start":
			return self.discovery.start(params["brief"], context_asset_ids=params.get("context_asset_ids"),
			                            constraints=params.get("constraints"),
			                            pool_limit=int(params.get("pool_limit", 2500)))
		if method == "discovery.discover":
			return self.discover_assets(params["brief"],
			                            thoroughness=params.get("thoroughness", "quick"),
			                            roles=params.get("roles"), styles=params.get("styles"),
			                            avoid=params.get("avoid"), limit=int(params.get("limit", 40)),
			                            detail=params.get("detail", "compact"))
		if method == "discovery.coverage":
			return self.discovery.coverage(params["session_id"])
		if method == "discovery.families":
			return self.discovery.families(params["session_id"], offset=int(params.get("offset", 0)),
			                               limit=int(params.get("limit", 100)))
		if method == "discovery.results":
			return self.discovery.results(params["session_id"], offset=int(params.get("offset", 0)),
			                              limit=int(params.get("limit", 100)))
		if method == "discovery.open_family":
			return self.discovery.open_family(params["session_id"], params["family"],
			                                  offset=int(params.get("offset", 0)),
			                                  limit=int(params.get("limit", 64)))
		if method == "discovery.residuals":
			return self.discovery.residuals(params["session_id"], limit=int(params.get("limit", 64)),
			                              strategy=params.get("strategy", "stratified"))
		if method == "discovery.mark":
			return self.discovery.mark(params["session_id"], params["asset_ids"],
			                         decision=params["decision"], reason=params.get("reason", ""))
		if method == "scene.apply_patch":
			return self.apply_scene_patch(params["operations"], patch_id=params.get("patch_id"),
			                              expected_revision=params.get("expected_revision"))
		if method == "scene.resolve_patch":
			return self.resolve_scene_patch(params["operations"],
			                                expected_revision=params.get("expected_revision"))
		if method == "scene.apply_plan":
			return self.apply_resolved_plan(params["plan_id"], patch_id=params.get("patch_id"))
		if method == "scene.group_create":
			return self.create_group(params["group"], params["members"])
		if method == "scene.group_list":
			return self.list_groups()
		if method == "scene.group_inspect":
			return self.inspect_group(params["group"], offset=int(params.get("offset", 0)),
			                          limit=int(params.get("limit", 50)))
		if method == "scene.group_transform":
			return self.transform_group(params["group"], dx=float(params.get("dx", 0.0)),
				dy=float(params.get("dy", 0.0)), dz=float(params.get("dz", 0.0)),
				dheading=float(params.get("dheading", 0.0)), patch_id=params.get("patch_id"),
				expected_revision=params.get("expected_revision"))
		if method == "scene.group_clone":
			return self.clone_group(params["group"], params["new_group"], params["key_prefix"],
				dx=float(params.get("dx", 0.0)), dy=float(params.get("dy", 0.0)),
				dz=float(params.get("dz", 0.0)), dheading=float(params.get("dheading", 0.0)),
				patch_id=params.get("patch_id"))
		if method == "scene.group_delete":
			return self.delete_group(params["group"], patch_id=params.get("patch_id"))
		if method == "scene.support_set":
			return self.set_support(params["child"], params["parent"])
		if method == "scene.support_snap":
			return self.snap_to_support(params["child"], params["parent"],
			                            float(params.get("clearance", 0.0)))
		if method == "scene.validate_composition":
			return self.validate_composition()
		if method == "scene.capture_views":
			return self.capture_views(Path(params["output_dir"]), float(params.get("fov", 58.0)),
			                          center=params.get("center"), span=params.get("span"))
		if method == "scene.capture_current":
			return self.client.capture_current(Path(params["output_path"]),
			                                   label=params.get("label", "current"))
		if method == "scene.raycast_segment":
			return self.client.raycast_segment(
				params["start"], params["target"],
				target_tolerance=float(params.get("target_tolerance", 1.0)))
		if method == "checkpoint.list":
			return self.checkpoint_list()
		if method == "checkpoint.save":
			return self.checkpoint_save(params["name"])
		if method == "checkpoint.restore":
			return self.checkpoint_restore(params["name"])
		if method == "scene.validate_semantics":
			return self.validate_semantic_zones(params["zones"])
		raise ValueError(f"unknown service method: {method}")
