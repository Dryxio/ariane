"""Persistent creative context, spatial observation, recipes and review workflow."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import time
import uuid
from pathlib import Path

from PIL import Image, ImageDraw

if __package__:
	from .asset_profiles import AssetProfiles
else:
	from asset_profiles import AssetProfiles
if __package__:
	from .scene_recipes import RECIPES, build_recipe
else:
	from scene_recipes import RECIPES, build_recipe


class VibeWorkflow:
    def __init__(self, service):
        self.s = service

    def identity(self):
        session = self.s.engine("session_status")
        identity = json.dumps([session.get("logical_path"), session.get("physical_path")])
        return hashlib.sha256(identity.encode()).hexdigest()[:24]

    def directory(self):
        return self.s.state_dir / "projects" / self.identity()

    def project(self):
        path = self.directory() / "project.json"
        return json.loads(path.read_text()) if path.exists() else {
            "schema": 1, "revision": 0, "brief": "", "styles": [], "avoid": [],
            "zones": [], "relations": [], "palette": {}, "references": [],
            "locked_keys": [], "locked_groups": [], "notes": [], "phase": "observe"}

    def update_project(self, changes: dict, expected_revision: int | None = None):
        project = self.project()
        if expected_revision is not None and project["revision"] != expected_revision:
            raise ValueError("project revision changed; read current project before updating")
        allowed = set(project) - {"schema", "revision"}
        if set(changes) - allowed:
            raise ValueError(f"unknown project fields: {sorted(set(changes) - allowed)}")
        for field in ("styles", "avoid", "zones", "relations", "references", "locked_keys", "locked_groups", "notes"):
            if field in changes and not isinstance(changes[field], list):
                raise ValueError(f"{field} must be a list")
        if "palette" in changes and not isinstance(changes["palette"], dict):
            raise ValueError("palette must be a role to asset-id mapping")
        if "phase" in changes and changes["phase"] not in {"observe", "palette", "blockout", "compose", "detail", "review", "accepted"}:
            raise ValueError("unknown creative phase")
        for zone in changes.get("zones", []):
            if not isinstance(zone, dict) or zone.get("shape") not in {"rect", "circle"}:
                raise ValueError("zones must be explicit rectangle/circle constraints")
            fields = ["x", "y", "radius"] if zone["shape"] == "circle" else ["x", "y", "width", "depth"]
            if any(field not in zone or not math.isfinite(float(zone[field])) for field in fields):
                raise ValueError("zone geometry must contain finite dimensions and coordinates")
            if any(float(zone[field]) <= 0 for field in fields[2:]):
                raise ValueError("zone dimensions must be positive")
        for field in ("locked_keys", "locked_groups"):
            for key in changes.get(field, []):
                self.s._safe_key(key)
        project.update(changes)
        project["revision"] += 1
        self.s._atomic_json(self.directory() / "project.json", project)
        return project

    def guard(self, operations, state):
        project = self.project()
        locked = set(project["locked_keys"])
        for group in project["locked_groups"]:
            locked.update(state.get("groups", {}).get(group, []))
        for op in operations:
            key = op.get("key")
            if key is None and op.get("instance_id") is not None:
                key, _ = self.s._resolve_ref(int(op["instance_id"]), state)
            if op.get("group") in project["locked_groups"] or key in locked:
                raise ValueError(f"locked project element: {key or op.get('group')}")
            if op.get("action") in {"delete", "transform", "transform3d"}:
                if any(parent == key and child in locked for child, parent in state.get("supports", {}).items()):
                    raise ValueError(f"cannot alter support of locked child: {key}")

    def snapshot(self):
        state, _, session = self.s._load_composition()
        return {"objects": self.s.enumerate_scene(), "groups": copy.deepcopy(state["groups"]),
                "supports": copy.deepcopy(state["supports"]), "scene_revision": session["scene_revision"]}

    @staticmethod
    def signature(item):
        if item is None:
            return None
        return (int(item["model_id"]), tuple(round(float(v), 4) for v in item["position"]),
                tuple(round(float(v), 4) for v in item["rotation"]))

    def history(self):
        items = []
        for path in (self.directory() / "history").glob("*.json"):
            record = json.loads(path.read_text())
            items.append({key: record.get(key) for key in
                          ("id", "patch_id", "created_at", "status", "changed_keys", "scene_revision")})
        return {"edits": sorted(items, key=lambda item: item["created_at"])}

    def compare_variants(self, first: str, second: str):
        def read(name):
            name = self.s._safe_checkpoint_name(name)
            return json.loads((self.s.checkpoint_dir / f"{name}.json").read_text())
        a, b = read(first), read(second)
        if a["source_scene"] != b["source_scene"]:
            raise ValueError("variants belong to different scenes")
        aa = {i["object_key"]: i for i in a["objects"]}
        bb = {i["object_key"]: i for i in b["objects"]}
        return {"first": first, "second": second,
                "added": sorted(bb.keys() - aa.keys()), "removed": sorted(aa.keys() - bb.keys()),
                "changed": sorted(k for k in aa.keys() & bb.keys() if self.signature(aa[k]) != self.signature(bb[k])),
                "projects": [a.get("project"), b.get("project")]}

    def export_bundle(self, output_directory: str):
        """Export an independent review package; saving the game IPL stays explicit."""
        import shutil
        output = Path(output_directory).resolve()
        output.mkdir(parents=True, exist_ok=True)
        snapshot = self.snapshot()
        self.s._atomic_json(output / "scene.json", snapshot)
        self.s._atomic_json(output / "project.json", self.project())
        self.s._atomic_json(output / "history.json", self.history())
        model_ids = sorted({int(item["model_id"]) for item in snapshot["objects"]})
        self.s._atomic_json(output / "assets.json", [self.s.profiles.get(i) for i in model_ids])
        session = self.s.engine("session_status")
        scene_path = Path(session.get("physical_path") or "")
        copied = False
        if scene_path.is_file():
            destination = output / scene_path.name
            if scene_path.resolve() != destination.resolve():
                shutil.copy2(scene_path, destination)
            copied = True
        return {"directory": str(output), "object_count": len(snapshot["objects"]),
                "saved_ipl_copied": copied,
                "note": "scene.json is live; copied IPL is the last saved version. Commit/save explicitly for game output."}

    def begin_record(self, patch_id, operations):
        record = {"id": f"edit-{uuid.uuid4().hex[:16]}", "patch_id": patch_id,
                  "created_at": time.time(), "status": "pending", "operations": operations,
                  "before": self.snapshot()}
        path = self.directory() / "history" / f"{record['id']}.json"
        self.s._atomic_json(path, record)
        return path, record

    def finish_record(self, path, record, receipt):
        record["after"] = self.snapshot()
        before = {i["object_key"]: i for i in record["before"]["objects"]}
        after = {i["object_key"]: i for i in record["after"]["objects"]}
        record.update(status="applied", scene_revision=receipt["scene_revision"],
                      changed_keys=sorted(k for k in before.keys() | after.keys()
                                          if self.signature(before.get(k)) != self.signature(after.get(k))))
        self.s._atomic_json(path, record)

    def undo(self, edit_id: str):
        if not __import__("re").fullmatch(r"edit-[0-9a-f]{16}", edit_id):
            raise ValueError("invalid edit id")
        path = self.directory() / "history" / f"{edit_id}.json"
        record = json.loads(path.read_text())
        if record["status"] != "applied":
            raise ValueError("only an applied edit can be undone")
        current = self.snapshot()
        live = {i["object_key"]: i for i in current["objects"]}
        before = {i["object_key"]: i for i in record["before"]["objects"]}
        after = {i["object_key"]: i for i in record["after"]["objects"]}
        changed = set(record["changed_keys"])
        for key in changed:
            if self.signature(live.get(key)) != self.signature(after.get(key)):
                raise ValueError(f"later edit changed {key}; undo would overwrite it")
        if any(parent in changed and child not in changed for child, parent in current["supports"].items()):
            raise ValueError("later supported objects depend on this edit")
        operations = []
        for key in sorted(changed):
            old = before.get(key)
            if old is None:
                operations.append({"action": "delete", "key": key})
            else:
                if key in live and int(live[key]["model_id"]) != int(old["model_id"]):
                    raise ValueError("model replacement needs a checkpoint restore")
                op = {"action": "transform" if key in live else "place", "key": key,
                      "model": old["model_id"], "x": old["position"][0], "y": old["position"][1],
                      "z": old["position"][2], "heading": old["rotation"][2], "snap": False}
                if any(abs(v) > 0.001 for v in old["rotation"][:2]):
                    if key not in live:
                        raise ValueError("use checkpoint restore for deleted tilted objects")
                    op.update(action="transform3d", pitch=old["rotation"][0], roll=old["rotation"][1])
                operations.append(op)
        if not operations:
            raise ValueError("edit has no object changes")
        receipt = self.s.apply_scene_patch(operations, patch_id=f"undo:{edit_id}",
                                           expected_revision=current["scene_revision"])
        state, state_path, _ = self.s._load_composition()
        for group in set(state["groups"]) | set(record["before"]["groups"]):
            members = [k for k in state["groups"].get(group, []) if k not in changed]
            members += [k for k in record["before"]["groups"].get(group, []) if k in changed]
            state["groups"][group] = list(dict.fromkeys(members))
        for key in changed:
            state["supports"].pop(key, None)
            if key in record["before"]["supports"]:
                state["supports"][key] = record["before"]["supports"][key]
        self.s._save_composition(state_path, state)
        record["status"] = "undone"
        self.s._atomic_json(path, record)
        return receipt

    def recipe(self, name: str, palette: dict, **options):
        dimensions, fronts, centers = {}, {}, {}
        for role, asset_id in palette.items():
            passport = AssetProfiles(self.s).get(int(asset_id))
            asset = passport["asset"]
            dimensions[role] = [asset.get("width"), asset.get("depth"), asset.get("height")]
            runtime = self.s.probe_asset(int(asset_id), ensure_renderable=True)
            if not runtime.get("renderable"):
                raise ValueError(f"unrenderable palette asset: {asset_id}")
            bounds = runtime.get("collision_bounds")
            if bounds:
                dimensions[role] = [bounds[1][axis] - bounds[0][axis] for axis in range(3)]
                centers[role] = [(bounds[0][axis] + bounds[1][axis]) / 2 for axis in range(2)]
            annotation = passport["annotation"]
            if annotation and "front_heading" in annotation["features"]:
                fronts[role] = annotation["features"]["front_heading"]
        self.s._safe_key(options["key"])
        result = build_recipe(name, palette, dimensions, front_headings=fronts, **options)
        # Recipe coordinates describe footprint centres, whereas the engine places model origins.
        by_model = {int(palette[role]): center for role, center in centers.items()}
        for operation in result["operations"]:
            u, v = by_model.get(operation["model"], [0, 0])
            c, s = math.cos(math.radians(operation["heading"])), math.sin(math.radians(operation["heading"]))
            operation["x"] -= u*c - v*s
            operation["y"] -= u*s + v*c
        return result

    def recipe_variants(self, name: str, palette: dict, options: dict, variants: int = 3):
        """Build alternative patches and circulation constraints, without mutations."""
        if not 2 <= variants <= 3:
            raise ValueError("variants must be 2 or 3")
        results = []
        for i in range(variants):
            settings = dict(options)
            settings["seed"] = str(options.get("seed", "0")) + f":{i}"
            settings["aisle"] = float(options.get("aisle", 3)) + i * .75
            settings["gap"] = float(options.get("gap", .25)) + i * .2
            result = self.recipe(name, palette, **settings)
            results.append({"label": ["compact", "balanced", "open"][i], **result})
        return {"variants": results, "applied": False,
                "next_step": "Resolve each candidate with its circulation constraints; capture selected trials using one fixed review rig."}

    def check_route(self, points: list[list[float]], width: float = 1.2, height: float = 1.8):
        if not 2 <= len(points) <= 30 or not 0.2 <= width <= 15 or not 0.5 <= height <= 5:
            raise ValueError("route needs 2-30 xyz points, width .2-15 and height .5-5")
        if any(len(p) != 3 or not all(math.isfinite(float(v)) for v in p) for p in points):
            raise ValueError("route points must be finite ground-level xyz")
        revision = self.s.engine("session_status")["scene_revision"]
        blockers = []
        for index, (a, b) in enumerate(zip(points, points[1:])):
            dx, dy = b[0]-a[0], b[1]-a[1]
            length = math.hypot(dx, dy)
            if length < .01:
                raise ValueError("route segment must have nonzero horizontal length")
            for lateral in (-width/2, 0, width/2):
                for elevation in (.25, height):
                    offset = [-dy/length*lateral, dx/length*lateral, elevation]
                    ray = self.s.client.raycast_segment([a[j]+offset[j] for j in range(3)],
                                                         [b[j]+offset[j] for j in range(3)], target_tolerance=.05)
                    if not ray["visible"]:
                        blockers.append({"segment": index, "lateral": lateral, "height": elevation, **ray})
        if self.s.engine("session_status")["scene_revision"] != revision:
            raise ValueError("scene changed during route check")
        return {"clear_samples": not blockers, "blockers": blockers, "scene_revision": revision,
                "limitation": "Six collision rays per segment; not a swept capsule or proof of walkable ground."}

    def observe(self, output_directory: str, radius: float = 30, grid: int = 7,
                pixel_x: float | None = None, pixel_y: float | None = None):
        if not 2 <= grid <= 15 or not 1 <= radius <= 250:
            raise ValueError("grid must be 2-15 and radius 1-250")
        output = Path(output_directory).resolve()
        output.mkdir(parents=True, exist_ok=True)
        start = self.s.engine("camera_context")
        camera = start["camera"]
        width, height = camera["viewport"]
        target = self.s.engine("screen_to_world", [width / 2 if pixel_x is None else pixel_x,
                             height / 2 if pixel_y is None else pixel_y, width, height])
        if not target.get("hit"):
            raise ValueError("camera ray misses geometry; point at the ground or select a visible pixel")
        center = target["point"]
        current = self.s.client.capture_current(output / "current.png", label="user-view")
        scene_revision = current["scene_revision"]
        camera_revision = camera["camera_revision"]
        visible = self.s.engine("screen_grid", [24, 18, camera_revision])
        surface = self.s.engine("surface_grid", [*center, radius, grid])
        items, offset, total = [], 0, 0
        while len(items) < 1024:
            page = self.s.engine("inspect_zone_page", [*center[:2], radius, offset, 256])
            if page["scene_revision"] != scene_revision:
                raise ValueError("scene changed during survey; retry")
            items.extend(page["items"])
            total = page["page"]["total"]
            offset = page["page"]["next_offset"]
            if offset is None:
                break
        end = self.s.engine("camera_context")
        if end["camera"]["camera_revision"] != camera_revision or end["scene_revision"] != scene_revision:
            raise ValueError("camera or scene changed during survey; retry")
        with Image.open(current["path"]) as source:
            annotated = source.convert("RGB")
        draw = ImageDraw.Draw(annotated)
        seen = set()
        for sample in visible["samples"]:
            if not sample.get("hit") or sample["instance_id"] in seen:
                continue
            seen.add(sample["instance_id"])
            px, py = sample["pixel"]
            px, py = px * annotated.width / width, py * annotated.height / height
            label = str(sample["instance_id"])
            draw.rectangle((px, py, px + 9 * len(label) + 6, py + 18), fill="#17191d")
            draw.text((px + 3, py + 2), label, fill="#ffd36a")
        annotated.save(output / "annotated.png")
        depth = Image.new("L", (24, 18))
        ids = Image.new("RGB", (24, 18))
        max_depth = max([s.get("depth", 0) for s in visible["samples"]] + [1])
        for i, sample in enumerate(visible["samples"]):
            if sample.get("hit"):
                depth.putpixel((i % 24, i // 24), max(1, int(255 * (1 - sample["depth"] / max_depth))))
                value = int(sample["instance_id"]) * 2654435761
                ids.putpixel((i % 24, i // 24), ((value >> 16) & 255, (value >> 8) & 255, value & 255))
        depth.resize((480, 360), Image.Resampling.NEAREST).save(output / "depth.png")
        ids.resize((480, 360), Image.Resampling.NEAREST).save(output / "object-ids.png")
        payload = {"center": center, "radius": radius, "camera": camera,
                   "scene_revision": scene_revision, "target": target, "objects": items,
                   "total_objects": total, "truncated": total > len(items),
                   "visible_samples": visible["samples"], "surface": surface,
                   "images": {name: str(output / name) for name in
                              ("current.png", "annotated.png", "depth.png", "object-ids.png")},
                   "limitations": ["Depth/object maps are sampled, not full-resolution segmentation.",
                                   "Surface samples are geometry; agent must identify roads, entrances and usable areas."]}
        self.s._atomic_json(output / "survey.json", payload)
        return payload

    def review(self, output_directory: str, center: list[float] | None = None, span: float | None = None):
        output = Path(output_directory).resolve()
        project = self.project()
        validation = self.s.validate_composition()
        semantic = self.s.validate_semantic_zones(project["zones"]) if project["zones"] else None
        captures = self.s.capture_views(output, center=center, span=span)
        report = {"project_revision": project["revision"], "brief": project["brief"],
                  "validation": validation, "circulation": semantic, "captures": captures,
                  "visual_review": {"status": "pending", "criteria": [
                      "brief fidelity", "asset/style coherence", "scale and hierarchy", "player access",
                      "spacing and repetition", "detail placement", "preserved user choices"]}}
        self.s._atomic_json(output / "review.json", report)
        return report

    def record_review(self, review_path: str, scores: dict, findings: list[dict], accepted: bool = False):
        path = Path(review_path)
        report = json.loads(path.read_text())
        if report["project_revision"] != self.project()["revision"]:
            raise ValueError("project changed since review capture")
        if self.s.engine("session_status")["scene_revision"] != report["captures"]["scene_revision"]:
            raise ValueError("scene changed since review capture")
        required = set(report["visual_review"]["criteria"])
        if set(scores) != required or any(not 1 <= float(v) <= 5 for v in scores.values()):
            raise ValueError("score every review criterion from 1 to 5")
        if accepted and (not report["validation"]["valid"] or
                         report.get("circulation") and not report["circulation"]["valid"]):
            raise ValueError("resolve blocking geometry/circulation findings before accepting")
        report["visual_review"].update(status="reviewed", scores=scores, findings=findings,
                                       accepted=accepted, source="calling visual agent or human")
        self.s._atomic_json(path, report)
        return report

    def dispatch(self, method: str, params: dict):
        methods = {"project.get": self.project, "project.update": self.update_project,
                   "history.list": self.history, "history.undo": self.undo,
                   "recipe.build": self.recipe, "recipe.variants": self.recipe_variants, "survey": self.observe,
                   "review.capture": self.review, "review.record": self.record_review,
                   "variants.compare": self.compare_variants, "export": self.export_bundle,
                   "route.check": self.check_route}
        if method == "recipe.list":
            return RECIPES
        if method not in methods:
            raise ValueError(f"unknown vibe method: {method}")
        return methods[method](**params)
