#!/usr/bin/env python3
"""Deterministic black-box harness for Ariane's robust agent API contract.

The simulator speaks Ariane's framed Unix stream envelope. It makes transport,
pagination, camera, persistence, stable-identity, and semantic-validation
failure modes reproducible without launching RenderWare. A live bridge can be
audited with ``--audit-socket``.
"""

from __future__ import annotations

import argparse
from contextlib import AbstractContextManager
import json
import math
from pathlib import Path
import socket
import struct
import tempfile
import threading
import uuid

if __package__:
	from .ariane_ipc import ArianeClient, ArianeError, PROTOCOL_VERSION
else:
	from ariane_ipc import ArianeClient, ArianeError, PROTOCOL_VERSION


ROBUST_V2_ENGINE_COMMANDS = {
	"camera_context", "screen_to_world", "list_page", "inspect_zone_page", "validate_zone",
	"asset_probe", "resolve_placement", "capture_pose", "raycast_segment",
}
ROBUST_V1_SIDECAR_COMMANDS = {
	"layer_list", "layer_open", "checkpoint_list", "checkpoint_save", "checkpoint_restore",
}
ROBUST_MAX_RESPONSE_BYTES = 4 * 1024 * 1024


def _safe_name(value: str) -> str:
	allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
	if not value or value in {".", ".."} or any(character not in allowed for character in value):
		raise ValueError("names may contain only letters, digits, dash, and underscore")
	return value


def _cross(a: list[float], b: list[float]) -> list[float]:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
	        a[0] * b[1] - a[1] * b[0]]


def _normalize(vector: list[float]) -> list[float]:
	length = math.sqrt(sum(value * value for value in vector))
	if length <= 1.0e-12:
		raise ValueError("cannot normalize a zero vector")
	return [value / length for value in vector]


class SimulatedArianeEngine(AbstractContextManager):
	"""Stateful, restartable engine double with bounded framed responses."""

	def __init__(self, socket_path: Path, state_dir: Path,
	             response_budget: int = ROBUST_MAX_RESPONSE_BYTES):
		self.socket_path, self.state_dir = Path(socket_path), Path(state_dir)
		self.response_budget, self.revision = response_budget, 1
		self.active_layer: str | None = None
		self._stop, self._socket, self._thread = threading.Event(), None, None
		self.fail_next: dict[str, int] = {}
		self.raycast_blocker = None
		self.camera = {"position": [0.0, -10.0, 10.0], "target": [0.0, 0.0, 0.0],
		               "up": [0.0, 0.0, 1.0], "fov": 60.0,
		               "viewport": [1280, 720], "camera_revision": 0}

	def __enter__(self) -> "SimulatedArianeEngine":
		for directory in (self.state_dir / "layers", self.state_dir / "checkpoints"):
			directory.mkdir(parents=True, exist_ok=True)
		self.socket_path.unlink(missing_ok=True)
		self._stop.clear()
		self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self._socket.bind(str(self.socket_path))
		self._socket.listen(16)
		self._socket.settimeout(0.05)
		self._thread = threading.Thread(target=self._serve, daemon=True)
		self._thread.start()
		return self

	def __exit__(self, *exc: object) -> None:
		self._stop.set()
		if self._thread:
			self._thread.join(timeout=2.0)
		if self._socket:
			self._socket.close()
		self.socket_path.unlink(missing_ok=True)

	def _response(self, request_id: str, ok: bool, body: dict) -> bytes:
		return (json.dumps({"protocol_version": PROTOCOL_VERSION,
		                    "scene_revision": self.revision, "request_id": request_id,
		                    "ok": ok, **body}, separators=(",", ":")) + "\n").encode()

	def _serve(self) -> None:
		while not self._stop.is_set():
			try:
				connection, _ = self._socket.accept()
			except socket.timeout:
				continue
			with connection:
				request_id = "unknown"
				try:
					header = self._recv_exact(connection, 4)
					request_size = struct.unpack("!I", header)[0]
					if request_size < 1 or request_size > 1_048_576:
						raise ValueError("invalid framed request size")
					packet = self._recv_exact(connection, request_size)
					lines = packet.decode().splitlines()
					if len(lines) < 3 or lines[0] != f"ARIANE_IPC/{PROTOCOL_VERSION}":
						raise ValueError("unsupported or malformed protocol envelope")
					request_id, command, *fields = lines[1:]
					response = self._response(request_id, True, self.dispatch(command, fields))
				except (KeyError, OSError, TypeError, ValueError) as error:
					response = self._response(request_id, False, {"error": str(error)})
				if len(response) > self.response_budget:
					response = self._response(request_id, False, {
						"error": "response budget exceeded; request a smaller page",
						"response_budget": self.response_budget})
				connection.sendall(struct.pack("!I", len(response)) + response)

	@staticmethod
	def _recv_exact(connection: socket.socket, size: int) -> bytes:
		chunks = []
		while size:
			chunk = connection.recv(size)
			if not chunk:
				raise ValueError("framed request ended early")
			chunks.append(chunk)
			size -= len(chunk)
		return b"".join(chunks)

	def _layer_path(self, name: str) -> Path:
		return self.state_dir / "layers" / f"{_safe_name(name)}.json"

	def _load(self) -> dict:
		if self.active_layer is None:
			raise ValueError("open a layer first")
		return json.loads(self._layer_path(self.active_layer).read_text())

	def _save(self, layer: dict) -> None:
		self._layer_path(self.active_layer).write_text(json.dumps(layer, sort_keys=True))

	def _objects(self) -> list[dict]:
		return self._load()["objects"] if self.active_layer else []

	def _paginate(self, items: list[dict], offset: int, limit: int) -> dict:
		if offset < 0 or limit < 1 or limit > 256:
			raise ValueError("offset must be non-negative and limit between 1 and 256")
		limit = min(limit, 256)
		total, page = len(items), items[offset:offset + limit]
		next_offset = offset + len(page)
		return {"items": page, "page": {"offset": offset, "returned": len(page),
		        "total": total, "next_offset": next_offset if next_offset < total else None,
		        "scene_revision": self.revision}}

	def _zone(self, x: float, y: float, radius: float) -> list[dict]:
		if radius <= 0.0:
			raise ValueError("radius must be positive")
		return [item for item in self._objects() if
		        (item["position"][0] - x) ** 2 + (item["position"][1] - y) ** 2 <= radius ** 2]

	@staticmethod
	def _pose_payload(camera: dict) -> dict:
		position, target, up_reference = camera["position"], camera["target"], camera["up"]
		forward = _normalize([target[index] - position[index] for index in range(3)])
		right = _normalize(_cross(forward, up_reference))
		up = _normalize(_cross(right, forward))
		return {"position": list(position), "target": list(target), "forward": forward,
		        "right": right, "up": up, "up_reference": list(up_reference),
		        "fov": camera["fov"]}

	def dispatch(self, command: str, fields: list[str]) -> dict:
		if self.fail_next.get(command, 0):
			self.fail_next[command] -= 1
			raise ValueError(f"injected {command} failure")
		if command == "ping":
			return {"result": "pong"}
		if command == "capabilities":
			return {"transport": "unix-stream-framed-v1", "build_id": "simulated-engine-v1",
			        "commands": sorted(ROBUST_V2_ENGINE_COMMANDS | ROBUST_V1_SIDECAR_COMMANDS),
			        "limits": {"request_bytes": 1_048_576, "response_bytes": self.response_budget,
			                   "max_page_size": 256},
			        "observation": ["rgb_capture", "screen_ray_depth", "screen_ray_object_id"]}
		if command == "session_status":
			return {"active": self.active_layer is not None,
			        "session_id": self.active_layer or "", "logical_path": self.active_layer or "",
			        "physical_path": str(self._layer_path(self.active_layer)) if self.active_layer else "",
			        "scene_revision": self.revision}
		if command == "camera_context":
			return {"camera": {**self.camera, **self._pose_payload(self.camera)}}
		if command == "camera":
			self.camera.update({"position": list(map(float, fields[:3])),
			                    "target": list(map(float, fields[3:6])),
			                    "fov": float(fields[6])})
			self.camera["camera_revision"] += 1
			return {"result": "camera_set", "camera_revision": self.camera["camera_revision"]}
		if command in {"capture", "capture_pose"}:
			path = Path(fields[0])
			path.parent.mkdir(parents=True, exist_ok=True)
			# A tiny valid PNG is sufficient for provenance/transport tests.
			from PIL import Image
			original = dict(self.camera)
			label = fields[1] if len(fields) > 1 else "current"
			if command == "capture_pose":
				if len(fields) > 12 and int(fields[12]) != self.camera["camera_revision"]:
					raise ValueError("camera revision changed")
				self.camera.update({"position": list(map(float, fields[2:5])),
				                    "target": list(map(float, fields[5:8])),
				                    "up": list(map(float, fields[8:11])),
				                    "fov": float(fields[11])})
				self.camera["camera_revision"] += 1
			captured_pose = self._pose_payload(self.camera)
			capture_revision = self.camera["camera_revision"]
			Image.new("RGB", (32, 32), "#202226").save(path)
			restored = command == "capture_pose"
			if restored:
				final_revision = self.camera["camera_revision"] + 1
				self.camera = original
				self.camera["camera_revision"] = final_revision
			return {"path": str(path), "label": label, "actual_pose": captured_pose,
			        "capture_camera_revision": capture_revision,
			        "camera_revision": self.camera["camera_revision"], "restored": restored}
		if command == "screen_to_world":
			return self._screen_to_world(fields)
		if command == "raycast_segment":
			start, target = list(map(float, fields[:3])), list(map(float, fields[3:6]))
			distance = math.sqrt(sum((target[index] - start[index]) ** 2 for index in range(3)))
			if distance < 0.001:
				raise ValueError("raycast_segment endpoints must differ")
			blocked = bool(self.raycast_blocker and self.raycast_blocker(start, target))
			hit = None
			if blocked:
				hit = {"point": [(start[index] + target[index]) * 0.5 for index in range(3)],
				       "distance": distance * 0.5, "fraction": 0.5,
				       "instance_id": 9000, "model_id": 9000,
				       "name": "simulated_occluder", "agent_scene": False}
			return {"ray": {"visible": not blocked, "distance": distance,
			        "target_tolerance": float(fields[6]) if len(fields) > 6 else 1.0,
			        "hit": hit}}
		if command == "layer_list":
			return {"layers": sorted(path.stem for path in
			        (self.state_dir / "layers").glob("*.json")), "active_layer": self.active_layer}
		if command == "layer_open":
			name, self.active_layer = _safe_name(fields[0]), _safe_name(fields[0])
			if not self._layer_path(name).exists():
				self._save({"layer_id": uuid.uuid4().hex, "next_sequence": 1, "objects": []})
			self.revision += 1
			return {"layer": name, "object_count": len(self._objects())}
		if command == "place":
			return self._place(fields)
		if command in {"transform", "transform3d"}:
			return self._transform(fields, command == "transform3d")
		if command == "delete":
			return self._delete(int(fields[0]))
		if command == "clear":
			layer = self._load()
			layer["objects"] = []
			self._save(layer)
			self.revision += 1
			return {"cleared": True}
		if command == "asset_detail":
			return {"asset": {"model_id": int(fields[0]),
			        "collision_bounds": [[-0.5, -0.5, 0.0], [0.5, 0.5, 1.0]]}}
		if command == "asset_probe":
			model_id = int(fields[0])
			defined = 0 <= model_id < 50000
			return {"asset": {"id": model_id, "defined": defined,
			        "loaded_before": defined, "loaded_now": defined,
			        "renderable": defined,
			        "availability": "renderable" if defined else "catalog_only",
			        "bounds_space": "model_local_relative_to_origin",
			        "identity_axes": {"right": [1, 0, 0], "forward": [0, 1, 0], "up": [0, 0, 1]},
			        "collision_bounds": [[-0.5, -0.5, 0.0], [0.5, 0.5, 1.0]],
			        "ground_offset": 0.0}}
		if command == "resolve_placement":
			return {"placement": {"model_id": int(fields[0]),
			        "position": [float(fields[1]), float(fields[2]), 0.0 if int(fields[5]) else float(fields[3])],
			        "heading": float(fields[4]), "snap_resolved": bool(int(fields[5]))},
			        "support_evaluation": {
				        "contract": "terrain-support-v1", "profile": "prop",
				        "sample_grid": 3, "max_floating_clearance": 0.15,
				        "max_penetration": 0.25, "max_support_relief": 0.4,
				        "analyzed": True, "valid": True, "floating": False,
				        "embedded": False, "unsupported_relief": False,
				        "missing_support": False, "min_clearance": 0.0,
				        "max_clearance": 0.0, "support_relief": 0.0,
			        }}
		if command == "validate":
			issues = [{"instance_id": item["instance_id"], "code": "unsupported"}
			          for item in self._objects() if item["position"][2] > 0.01]
			seen, duplicates = {}, []
			for item in self._objects():
				signature = (item["model_id"], *item["position"], *item["rotation"])
				if signature in seen:
					duplicates.append({"a": seen[signature], "b": item["instance_id"],
					                   "model_id": item["model_id"]})
				else:
					seen[signature] = item["instance_id"]
			return {"valid": not issues and not duplicates, "support_issues": issues,
			        "building_overlaps": [], "prop_overlaps": [],
			        "exact_duplicates": duplicates}
		if command == "list_page":
			offset, limit = int(fields[0]), int(fields[1])
			items = self._objects() if len(fields) < 5 else self._zone(*map(float, fields[2:5]))
			return self._paginate(sorted(items, key=lambda item: item["object_key"]), offset, limit)
		if command in {"inspect_zone_page", "validate_zone"}:
			if len(fields) < 5:
				raise ValueError("zone page requires x, y, radius, offset, and limit")
			x, y, radius = map(float, fields[:3])
			offset, limit = map(int, fields[3:5])
			items = sorted(self._zone(x, y, radius), key=lambda item: item["object_key"])
			if command == "inspect_zone_page":
				return self._paginate(items, offset, limit)
			issues = [{"code": "clearance_blocked", "severity": "error",
			           "object_key": item["object_key"]} for item in items
			          if item["semantic_role"] in {"door_blocker", "route_blocker"}]
			result = self._paginate(issues, offset, limit)
			result.update({"valid": not issues, "summary": {"semantic_issues": len(issues)}})
			return result
		if command == "checkpoint_list":
			return {"checkpoints": sorted(path.stem for path in
			        (self.state_dir / "checkpoints").glob("*.json"))}
		if command == "checkpoint_save":
			name, layer = _safe_name(fields[0]), self._load()
			(self.state_dir / "checkpoints" / f"{name}.json").write_text(
				json.dumps({"active_layer": self.active_layer, "layer": layer}, sort_keys=True))
			return {"checkpoint": name, "object_count": len(layer["objects"])}
		if command == "checkpoint_restore":
			name = _safe_name(fields[0])
			path = self.state_dir / "checkpoints" / f"{name}.json"
			if not path.exists():
				raise ValueError("unknown checkpoint")
			payload = json.loads(path.read_text())
			self.active_layer = _safe_name(payload["active_layer"])
			self._save(payload["layer"])
			self.revision += 1
			return {"checkpoint": name, "layer": self.active_layer}
		raise ValueError(f"unknown command: {command}")

	def _place(self, fields: list[str]) -> dict:
		if len(fields) < 5:
			raise ValueError("place requires model, x, y, z, and heading")
		layer, sequence = self._load(), self._load()["next_sequence"]
		try:
			heading, semantic_role = float(fields[4]), "prop"
		except ValueError:
			# Preserve the original harness shorthand used by semantic-zone tests.
			heading, semantic_role = 0.0, fields[4]
		item = {"instance_id": sequence,
		        "object_key": f"{layer['layer_id']}:{sequence:08d}", "model_id": int(fields[0]),
		        "position": [float(value) for value in fields[1:4]],
		        "rotation": [0.0, 0.0, heading], "semantic_role": semantic_role}
		layer["objects"].append(item)
		layer["next_sequence"] += 1
		self._save(layer)
		self.revision += 1
		return {"instance_id": sequence, "object": item}

	def _transform(self, fields: list[str], full_rotation: bool) -> dict:
		minimum = 7 if full_rotation else 5
		if len(fields) < minimum:
			raise ValueError("transform fields are incomplete")
		layer, instance_id = self._load(), int(fields[0])
		item = next((item for item in layer["objects"] if item["instance_id"] == instance_id), None)
		if item is None:
			raise ValueError("unknown instance")
		item["position"] = [float(value) for value in fields[1:4]]
		item["rotation"] = ([float(value) for value in fields[4:7]] if full_rotation
		                    else [0.0, 0.0, float(fields[4])])
		self._save(layer)
		self.revision += 1
		return {"instance_id": instance_id}

	def _delete(self, instance_id: int) -> dict:
		layer = self._load()
		before = len(layer["objects"])
		layer["objects"] = [item for item in layer["objects"]
		                    if item["instance_id"] != instance_id]
		if len(layer["objects"]) == before:
			raise ValueError("unknown instance")
		self._save(layer)
		self.revision += 1
		return {"deleted": instance_id}

	def _screen_to_world(self, fields: list[str]) -> dict:
		if len(fields) < 4:
			raise ValueError("screen_to_world requires pixel x/y and viewport width/height")
		pixel_x, pixel_y, width, height = map(float, fields[:4])
		if width <= 0 or height <= 0 or pixel_x < 0 or pixel_y < 0 or pixel_x >= width or pixel_y >= height:
			raise ValueError("pixel is outside the current viewport")
		position = self.camera["position"]
		forward = _normalize([self.camera["target"][i] - position[i] for i in range(3)])
		right, tangent = _normalize(_cross(forward, self.camera["up"])), math.tan(math.pi / 6.0)
		up = _cross(right, forward)
		nx = (2.0 * (pixel_x + 0.5) / width - 1.0) * width / height * tangent
		ny = (1.0 - 2.0 * (pixel_y + 0.5) / height) * tangent
		ray = _normalize([forward[i] + nx * right[i] + ny * up[i] for i in range(3)])
		if ray[2] >= -1.0e-8:
			return {"hit": False, "reason": "no_depth"}
		depth = -position[2] / ray[2]
		world = [position[i] + depth * ray[i] for i in range(3)]
		nearest = min(self._objects(), key=lambda item: sum(
			(item["position"][i] - world[i]) ** 2 for i in range(3)), default=None)
		return {"hit": True, "world": world, "depth": depth,
		        "object_key": nearest["object_key"] if nearest else None,
		        "passes": {"depth": True, "object_id": True}}


def audit_capabilities(socket_path: Path, timeout: float = 2.0) -> dict:
	try:
		capabilities = ArianeClient(socket_path, timeout).command("capabilities")
	except (ArianeError, OSError, ValueError) as error:
		return {"ok": False, "error": str(error),
			        "missing_commands": sorted(ROBUST_V2_ENGINE_COMMANDS)}
	commands = set(capabilities.get("commands", []))
	build_id = capabilities.get("build_id")
	observation = set(capabilities.get("observation", []))
	limits = capabilities.get("limits", {})
	response_budget = limits.get("max_response_bytes", limits.get("response_bytes"))
	page_limit = limits.get("max_page_items", limits.get("max_page_size"))
	required_observation = {"rgb_capture", "screen_ray_depth", "screen_ray_object_id"}
	return {"ok": bool(build_id) and ROBUST_V2_ENGINE_COMMANDS.issubset(commands) and
	              required_observation.issubset(observation) and
	              response_budget is not None and response_budget >= ROBUST_MAX_RESPONSE_BYTES and
	              page_limit is not None and page_limit >= 256,
	        "present_commands": sorted(ROBUST_V2_ENGINE_COMMANDS & commands),
	        "missing_commands": sorted(ROBUST_V2_ENGINE_COMMANDS - commands),
	        "limits": limits, "observation": sorted(observation), "build_id": build_id}


def run_simulation() -> dict:
	with tempfile.TemporaryDirectory() as temporary:
		root, seen = Path(temporary), []
		socket_path, state_dir = root / "engine.sock", root / "state"
		with SimulatedArianeEngine(socket_path, state_dir):
			client, keys = ArianeClient(socket_path, 1.0), []
			client.command("layer_open", "motel_dirty")
			for index in range(517):
				result = client.command("place", index, index % 30, index // 30, 0,
				                        "door_blocker" if index == 7 else "prop")
				keys.append(result["object"]["object_key"])
			client.command("checkpoint_save", "reviewed")
			offset: int | None = 0
			while offset is not None:
				page = client.command("list_page", offset, 73)
				seen.extend(item["object_key"] for item in page["items"])
				offset = page["page"]["next_offset"]
			center = client.command("screen_to_world", 639.5, 359.5, 1280, 720)
			validation = client.command("validate_zone", 0, 0, 1000, 0, 50, 1)
			audit = audit_capabilities(socket_path)
		with SimulatedArianeEngine(socket_path, state_dir):
			client = ArianeClient(socket_path, 1.0)
			client.command("layer_open", "motel_dirty")
			restarted, offset = [], 0
			while offset is not None:
				page = client.command("list_page", offset, 250)
				restarted.extend(item["object_key"] for item in page["items"])
				offset = page["page"]["next_offset"]
		checks = {"pagination_exactly_once": seen == keys, "center_pixel_hits_ground": center["hit"],
		          "semantic_issue_count": validation["summary"]["semantic_issues"],
		          "stable_ids_after_restart": restarted == keys, "capabilities": audit}
		return {"ok": all((checks["pagination_exactly_once"], checks["center_pixel_hits_ground"],
		                   checks["semantic_issue_count"] == 1, checks["stable_ids_after_restart"],
		                   audit["ok"])), "checks": checks}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--audit-socket", type=Path)
	parser.add_argument("--timeout", type=float, default=2.0)
	args = parser.parse_args()
	report = audit_capabilities(args.audit_socket, args.timeout) if args.audit_socket else run_simulation()
	print(json.dumps(report, indent=2))
	return 0 if report.get("ok") else 1


if __name__ == "__main__":
	raise SystemExit(main())
