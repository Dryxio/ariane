"""Measured asset passports and explicitly sourced visual annotations.

No model is required: the calling visual agent annotates canonical previews.
Reference matching is a colour/shape shortlist, never a semantic verdict.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image


def bundled_annotations():
    return json.loads((Path(__file__).parent / "reference-data" / "reviewed-assets.json").read_text())


class AssetProfiles:
    def __init__(self, service):
        self.service = service
        self.root = service.state_dir / "asset-profiles"

    def get(self, asset_id: int) -> dict:
        asset_id = int(asset_id)
        asset = self.service.assets.inspect(asset_id)
        if asset is None:
            raise ValueError(f"unknown asset: {asset_id}")
        path = self.root / f"{asset_id}.json"
        annotation = json.loads(path.read_text()) if path.exists() else bundled_annotations().get(str(asset_id))
        return {"asset": asset, "annotation": annotation,
                "evidence": {"dimensions": "catalogue geometry", "role_tags": "keyword heuristic",
                             "visual_annotations": "unreviewed" if not annotation else annotation["source"]},
                "unknowns": [field for field in ("front_heading", "anchors", "materials", "colours")
                             if not annotation or field not in annotation["features"]]}

    def annotate(self, asset_id: int, features: dict, source: str, confidence: float = 0.7) -> dict:
        self.get(asset_id)
        allowed = {"description", "materials", "colours", "styles", "roles", "placement",
                   "front_heading", "anchors", "compatible_models", "notes"}
        if set(features) - allowed:
            raise ValueError(f"unknown profile fields: {sorted(set(features) - allowed)}")
        if not source.strip() or not 0 <= confidence <= 1:
            raise ValueError("annotations require a source and confidence in [0,1]")
        if "front_heading" in features and not math.isfinite(float(features["front_heading"])):
            raise ValueError("front_heading must be finite degrees from model +Y")
        if "anchors" in features and not isinstance(features["anchors"], dict):
            raise ValueError("anchors must map names to local xyz")
        for name, anchor in features.get("anchors", {}).items():
            if name.startswith("bounds."):
                raise ValueError("bounds. is reserved for measured geometric anchors")
            if len(anchor) != 3 or not all(math.isfinite(float(v)) for v in anchor):
                raise ValueError(f"anchor {name} must be model-local xyz")
        for field in ("materials", "colours", "styles", "roles", "placement", "compatible_models"):
            if field in features and not isinstance(features[field], list):
                raise ValueError(f"{field} must be a list")
        for field in ("materials", "colours", "styles", "roles", "placement"):
            if any(not isinstance(v,str) or not v.strip() for v in features.get(field, [])):
                raise ValueError(f"{field} must contain nonempty strings")
        if any(type(v) is not int or v < 0 for v in features.get("compatible_models", [])):
            raise ValueError("compatible_models must contain nonnegative model ids")
        previous = self.get(asset_id)["annotation"]
        payload = {"asset_id": int(asset_id), "source": source, "confidence": confidence,
                   "field_sources": {**(previous or {}).get("field_sources", {
                       field: {"source": previous["source"], "confidence": previous["confidence"]}
                       for field in (previous or {}).get("features", {})}),
                       **{field: {"source": source, "confidence": confidence} for field in features}},
                   "features": {**(previous or {}).get("features", {}), **features}, "revision": (previous or {}).get("revision", 0) + 1}
        self.service._atomic_json(self.root / f"{int(asset_id)}.json", payload)
        return payload

    def anchors(self, asset_id: int) -> dict:
        passport = self.get(asset_id)
        runtime = self.service.probe_asset(int(asset_id), ensure_renderable=True)
        bounds = runtime.get("collision_bounds")
        geometric = {"bounds.origin": [0,0,0]}
        if bounds:
            low, high = bounds
            center = [(a+b)/2 for a,b in zip(low,high)]
            geometric.update({"bounds.center": center,
                "bounds.base": [center[0],center[1],low[2]],
                "bounds.top": [center[0],center[1],high[2]],
                "bounds.plus_y": [center[0],high[1],center[2]],
                "bounds.minus_y": [center[0],low[1],center[2]],
                "bounds.plus_x": [high[0],center[1],center[2]],
                "bounds.minus_x": [low[0],center[1],center[2]]})
        annotation = passport["annotation"] or {}
        return {"asset_id":int(asset_id),"geometric":geometric,
                "functional":annotation.get("features",{}).get("anchors",{}),
                "front_heading":annotation.get("features",{}).get("front_heading"),
                "source":annotation.get("source"),
                "limitation":"Bounds anchors are geometric extrema, not verified doors, mount points or usable surfaces."}

    def resolve_anchor(self, asset_id: int, name: str):
        anchors = self.anchors(asset_id)
        values = anchors["geometric"] if name.startswith("bounds.") else anchors["functional"]
        if name not in values:
            raise ValueError("unknown anchor; inspect bounds anchors or annotate the functional anchor first")
        return values[name]

    def search(self, query: str, limit: int = 30) -> list[dict]:
        tokens = set(query.lower().split())
        scored = []
        annotations = bundled_annotations()
        for path in self.root.glob("*.json"):
            if not path.stem.isdigit():
                continue
            annotations[path.stem] = json.loads(path.read_text())
        for item in annotations.values():
            text = json.dumps(item["features"], ensure_ascii=False).lower()
            score = sum(token in text for token in tokens) * item["confidence"]
            if score:
                scored.append((score, item))
        return [item for _, item in sorted(scored, key=lambda pair: (-pair[0], pair[1]["asset_id"]))[:limit]]

    @staticmethod
    def descriptor(path: Path) -> list[float]:
        with Image.open(path) as source:
            rgba = source.convert("RGBA")
            rgba.thumbnail((128, 128))
            rgb = Image.new("RGB", rgba.size, (128, 128, 128))
            rgb.paste(rgba, mask=rgba.getchannel("A"))
            pixels = list(rgb.convert("HSV").getdata())
        histogram = [0.0] * 48
        for h, s, v in pixels:
            histogram[min(15, h // 16)] += 1
            histogram[16 + min(15, s // 16)] += 1
            histogram[32 + min(15, v // 16)] += 1
        norm = math.sqrt(sum(value * value for value in histogram)) or 1.0
        return [value / norm for value in histogram]

    def index_images(self, directory: str) -> dict:
        catalogue = {int(a["id"]) for a in self.service.assets.catalog()}
        descriptors, errors = {}, []
        for path in sorted(Path(directory).glob("*.png")):
            if not path.stem.isdigit() or int(path.stem) not in catalogue:
                continue
            try:
                descriptors[path.stem] = self.descriptor(path)
            except (OSError, ValueError) as error:
                errors.append({"path": str(path), "error": str(error)})
        if not descriptors:
            raise ValueError("no catalogue thumbnails found; expected MODEL_ID.png")
        self.service._atomic_json(self.root / "visual-index.json", {
            "method": "hsv-histogram-v1", "directory": str(Path(directory).resolve()),
            "descriptors": descriptors})
        return {"indexed": len(descriptors), "errors": errors, "method": "hsv-histogram-v1"}

    def remember_preview(self, asset_id: int, path: Path):
        target = self.root / "visual-index.json"
        index = json.loads(target.read_text()) if target.exists() else {
            "method": "hsv-histogram-v1", "descriptors": {}, "sources": {}}
        index["descriptors"][str(asset_id)] = self.descriptor(path)
        index.setdefault("sources", {})[str(asset_id)] = str(path)
        self.service._atomic_json(target, index)

    def reference_search(self, image_path: str, limit: int = 30, candidates: list[int] | None = None) -> dict:
        index = json.loads((self.root / "visual-index.json").read_text())
        query = self.descriptor(Path(image_path))
        eligible = set(candidates) if candidates is not None else None
        scores = [(sum(a * b for a, b in zip(query, vector)), int(key))
                  for key, vector in index["descriptors"].items()
                  if eligible is None or int(key) in eligible]
        return {"method": index["method"], "requires_visual_review": True,
                "limitation": "Colour shortlist only; use agent vision for semantics and shape.",
                "assets": [{"asset_id": key, "score": round(score, 4)}
                           for score, key in sorted(scores, reverse=True)[:max(1, min(limit, 100))]]}
