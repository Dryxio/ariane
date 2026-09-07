"""Auditable, high-recall discovery sessions over the GTA asset catalogue."""

from __future__ import annotations

from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import uuid
from typing import Any

if __package__:
	from .asset_index import AssetIndex, ROLE_KEYWORDS, STYLE_KEYWORDS
else:
	from asset_index import AssetIndex, ROLE_KEYWORDS, STYLE_KEYWORDS


DEFAULT_SESSION_DIR = Path.home() / ".cache" / "ariane-agent" / "discoveries"

CONCEPT_ALIASES = {
	"seating": ("seating", "seat", "bench", "chair", "stool", "picnic table", "outdoor furniture", "rest area", "resting", "traveller"),
	"table": ("table", "desk", "counter", "picnic table", "eating outside"),
	"cooking_fire": ("cook", "cooking", "food outside", "barbecue", "barbeque", "bbq", "grill", "campfire", "fire pit"),
	"waste": ("waste", "trash", "garbage", "rubbish", "bin", "dumpster"),
	"container": ("container", "barrel", "drum", "crate", "box", "pallet", "storage"),
	"lighting": ("lighting", "lamp", "lantern", "light", "torch", "neon"),
	"signage": ("signage", "sign", "billboard", "banner", "advertisement"),
	"vegetation": ("vegetation", "plant", "tree", "bush", "cactus", "grass"),
	"rock": ("rock", "boulder", "stone"),
	"fence_barrier": ("fence", "barrier", "wall", "gate", "railing"),
	"vehicle_prop": ("vehicle prop", "cart", "wagon", "trailer", "wheel"),
	"wood_fuel": ("firewood", "woodpile", "cut logs", "log pile", "timber pile", "wood fuel"),
	"utility": ("utility", "vending machine", "phone", "generator", "pump", "tank"),
	"decoration": ("decoration", "ornament", "statue", "flag", "fountain"),
	"building": ("building", "house", "shop", "motel", "hotel", "office", "shed", "garage"),
}


def _tokens(text: str) -> set[str]:
	return {token for token in re.findall(r"[a-z0-9]+", text.lower()) if len(token) > 1}


def _flatten_brief(value: Any) -> str:
	if isinstance(value, str):
		return value
	if isinstance(value, dict):
		return " ".join(_flatten_brief(item) for item in value.values())
	if isinstance(value, (list, tuple)):
		return " ".join(_flatten_brief(item) for item in value)
	return str(value) if value is not None else ""


def _string_list(value: Any) -> list[str]:
	if value is None:
		return []
	if isinstance(value, str):
		value = [value]
	return [str(item).strip().lower().replace(" ", "_") for item in value if str(item).strip()]


def _concepts(text: str) -> tuple[list[str], list[str]]:
	normalized = text.lower().replace("_", " ")
	roles = []
	for role, aliases in CONCEPT_ALIASES.items():
		keywords = aliases + ROLE_KEYWORDS.get(role, ())
		if any(keyword in normalized for keyword in keywords):
			roles.append(role)
	styles = [style for style, words in STYLE_KEYWORDS.items()
	          if style in normalized or any(word.replace("_", " ") in normalized for word in words)]
	return roles, styles


class AssetDiscovery:
	def __init__(self, index: AssetIndex, *, session_dir: Path = DEFAULT_SESSION_DIR):
		self.index = index
		self.session_dir = Path(session_dir)
		self.session_dir.mkdir(parents=True, exist_ok=True)

	def _path(self, session_id: str) -> Path:
		if not re.fullmatch(r"[a-zA-Z0-9_-]+", session_id):
			raise ValueError("invalid discovery session id")
		return self.session_dir / f"{session_id}.json"

	def _load(self, session_id: str) -> dict:
		path = self._path(session_id)
		if not path.exists():
			raise FileNotFoundError(f"discovery session not found: {session_id}")
		return json.loads(path.read_text(encoding="utf-8"))

	def _save(self, session: dict) -> None:
		path = self._path(session["session_id"])
		temporary = path.with_suffix(".json.tmp")
		temporary.write_text(json.dumps(session, indent=2, ensure_ascii=False), encoding="utf-8")
		temporary.replace(path)

	def start(self, brief: str | dict, *, context_asset_ids: list[int] | None = None,
	          constraints: dict | None = None, pool_limit: int = 2500,
	          use_semantic: bool = True) -> dict:
		if isinstance(brief, dict):
			# Keep scene context separate from requested roles. In particular, an
			# outdoor scene containing a motel must not become a search for another
			# motel building, and negative guidance must never enter the FTS query.
			brief_text = _flatten_brief({key: brief.get(key) for key in
			                            ("scene", "request", "description") if brief.get(key)}).strip()
			explicit_roles = _string_list(brief.get("roles"))
			explicit_styles = _string_list(brief.get("styles"))
			avoid = _string_list(brief.get("avoid"))
		else:
			brief_text = _flatten_brief(brief).strip()
			explicit_roles, explicit_styles, avoid = [], [], []
		if not brief_text:
			raise ValueError("discovery brief cannot be empty")
		constraints = constraints or {}
		context_asset_ids = sorted({int(item) for item in (context_asset_ids or [])})
		assets = self.index.catalog()
		eligible = []
		for asset in assets:
			if constraints.get("has_collision") is not None and bool(asset["has_collision"]) != bool(constraints["has_collision"]):
				continue
			if constraints.get("max_width") is not None and (asset.get("width") or float("inf")) > float(constraints["max_width"]):
				continue
			if constraints.get("max_depth") is not None and (asset.get("depth") or float("inf")) > float(constraints["max_depth"]):
				continue
			eligible.append(asset)
		eligible_by_id = {int(asset["id"]): asset for asset in eligible}
		inferred_roles, inferred_styles = _concepts(brief_text)
		roles = explicit_roles or inferred_roles
		styles = explicit_styles or inferred_styles
		channel_ranks: dict[str, dict[int, int]] = {}

		# FTS includes names, legacy metadata, weak labels, and optional AI captions.
		queries = [brief_text]
		queries.extend(role.replace("_", " ") for role in roles)
		queries.extend(alias for role in roles for alias in CONCEPT_ALIASES.get(role, ())[:5])
		queries.extend(styles)
		lexical_ids = []
		seen = set()
		for query in queries:
			for asset in self.index.search(query, limit=600):
				asset_id = int(asset["id"])
				if asset_id in eligible_by_id and asset_id not in seen:
					seen.add(asset_id)
					lexical_ids.append(asset_id)
		channel_ranks["lexical"] = {asset_id: rank for rank, asset_id in enumerate(lexical_ids, 1)}

		weak_ids = []
		for asset in eligible:
			asset_roles = set(str(asset.get("role_tags") or "").split())
			asset_styles = set(str(asset.get("style_tags") or "").split())
			role_hits = len(set(roles) & asset_roles)
			style_hits = len(set(styles) & asset_styles)
			if role_hits or style_hits:
				weak_ids.append((int(asset["id"]), role_hits * 4 + style_hits * 2 + min(int(asset.get("native_count") or 0), 3)))
		weak_ids.sort(key=lambda item: (-item[1], item[0]))
		channel_ranks["weak_labels"] = {asset_id: rank for rank, (asset_id, _) in enumerate(weak_ids, 1)}

		if context_asset_ids:
			native = [int(asset["id"]) for asset in self.index.native_neighbors(context_asset_ids, 1000)
			             if int(asset["id"]) in eligible_by_id]
			channel_ranks["native_context"] = {asset_id: rank for rank, asset_id in enumerate(native, 1)}

		families: dict[str, list[int]] = defaultdict(list)
		for asset in eligible:
			families[str(asset["family_key"])].append(int(asset["id"]))
		# Every family contributes a deterministic representative. This long-tail
		# channel prevents captions and rankers from silently hiding a family.
		exploration_ids = []
		for family in sorted(families):
			members = sorted(families[family], key=lambda asset_id: (
				-int(eligible_by_id[asset_id].get("native_count") or 0), asset_id))
			exploration_ids.extend(members[:2])
		channel_ranks["family_exploration"] = {asset_id: rank for rank, asset_id in enumerate(exploration_ids, 1)}

		weights = {"lexical": 1.0, "weak_labels": 0.9,
		           "native_context": 0.75, "family_exploration": 0.35}
		scored = []
		for asset_id, asset in eligible_by_id.items():
			channels = {channel: rank_map[asset_id] for channel, rank_map in channel_ranks.items()
			            if asset_id in rank_map}
			score = sum(weights[channel] / (60.0 + rank) for channel, rank in channels.items())
			asset_roles = set(str(asset.get("role_tags") or "").split())
			asset_styles = set(str(asset.get("style_tags") or "").split())
			if explicit_roles and asset_roles.intersection(roles):
				score *= 1.8
			if explicit_styles and asset_styles.intersection(styles):
				score *= 1.2
			avoid_haystack = " ".join(str(asset.get(key) or "") for key in (
				"name", "category", "semantic_category", "role_tags", "style_tags",
				"placement_tags", "semantic_description")).lower().replace("_", " ")
			if any(re.search(rf"(?<![a-z0-9]){re.escape(term.replace('_', ' '))}(?![a-z0-9])",
			                 avoid_haystack) for term in avoid):
				score *= 0.02
			if channels:
				scored.append((asset_id, score, channels))
		scored.sort(key=lambda item: (-item[1], item[0]))
		pool_limit = max(100, min(int(pool_limit), len(eligible)))
		scored_by_id = {asset_id: (asset_id, score, channels) for asset_id, score, channels in scored}
		# Reserve one slot per family whenever the pool is large enough. The best
		# member is used, not merely the most common native asset in that family.
		representatives = []
		for family, members in families.items():
			choices = [scored_by_id[asset_id] for asset_id in members if asset_id in scored_by_id]
			if choices:
				representatives.append(max(choices, key=lambda item: (item[1], -item[0])))
		representatives.sort(key=lambda item: (-item[1], item[0]))
		pool = representatives[:pool_limit]
		pool_ids = {asset_id for asset_id, _, _ in pool}
		for item in scored:
			if len(pool) >= pool_limit:
				break
			if item[0] not in pool_ids:
				pool.append(item)
				pool_ids.add(item[0])
		pool.sort(key=lambda item: (-item[1], item[0]))
		candidates = [{"asset_id": asset_id, "score": round(score, 8), "channels": channels,
		               "family": eligible_by_id[asset_id]["family_key"]} for asset_id, score, channels in pool]
		session_id = f"discovery-{uuid.uuid4().hex[:12]}"
		session = {
			"version": 1, "session_id": session_id,
			"created_at": datetime.now(timezone.utc).isoformat(),
			"brief": brief, "brief_text": brief_text, "roles": roles, "styles": styles,
			"avoid": avoid,
			"constraints": constraints, "context_asset_ids": context_asset_ids,
			"eligible_total": len(eligible), "considered_total": len(eligible),
			"catalog_family_count": len(families), "candidate_pool_count": len(candidates),
			"channels": {name: len(values) for name, values in channel_ranks.items()},
			"semantic_error": None, "candidates": candidates,
			"reviewed_families": [], "reviewed_assets": [], "shortlist": {}, "rejected": {},
		}
		self._save(session)
		return self.coverage(session_id)

	def coverage(self, session_id: str) -> dict:
		session = self._load(session_id)
		candidate_families = sorted({item["family"] for item in session["candidates"]})
		reviewed = set(session["reviewed_families"])
		return {
			"session_id": session_id, "brief": session["brief"], "roles": session["roles"],
			"styles": session["styles"], "avoid": session.get("avoid", []),
			"eligible_total": session["eligible_total"],
			"considered_total": session["considered_total"],
			"candidate_pool_count": session["candidate_pool_count"],
			"catalog_family_count": session["catalog_family_count"],
			"candidate_family_count": len(candidate_families),
			"families_reviewed": len(reviewed),
			"families_remaining": len(set(candidate_families) - reviewed),
			"assets_reviewed": len(set(session["reviewed_assets"])),
			"shortlist_count": len(session["shortlist"]), "rejected_count": len(session["rejected"]),
			"channels": session["channels"], "semantic_error": session.get("semantic_error"),
		}

	def families(self, session_id: str, *, offset: int = 0, limit: int = 100) -> dict:
		session = self._load(session_id)
		catalog = self.index.catalog()
		assets = {int(asset["id"]): asset for asset in catalog}
		catalog_counts: dict[str, int] = defaultdict(int)
		for asset in catalog:
			catalog_counts[str(asset["family_key"])] += 1
		groups: dict[str, list[dict]] = defaultdict(list)
		for candidate in session["candidates"]:
			groups[candidate["family"]].append(candidate)
		rows = []
		for family, candidates in groups.items():
			candidates.sort(key=lambda item: (-item["score"], item["asset_id"]))
			representative = assets.get(candidates[0]["asset_id"], {})
			rows.append({"family": family, "candidate_count": len(candidates),
			             "catalog_count": catalog_counts[family],
			             "representative_id": candidates[0]["asset_id"],
			             "representative_name": representative.get("name"),
			             "best_score": candidates[0]["score"],
			             "reviewed": family in session["reviewed_families"]})
		rows.sort(key=lambda item: (-item["best_score"], item["family"]))
		return {"session_id": session_id, "total": len(rows), "offset": offset,
		        "families": rows[offset:offset + max(1, min(limit, 2000))]}

	def results(self, session_id: str, *, offset: int = 0, limit: int = 100) -> dict:
		"""Return the fused ranking while preserving each contributing channel."""
		session = self._load(session_id)
		page = session["candidates"][offset:offset + max(1, min(limit, 1000))]
		assets = []
		for candidate in page:
			asset = self.index.inspect(candidate["asset_id"])
			if asset:
				asset["discovery_score"] = candidate["score"]
				asset["discovery_channels"] = candidate["channels"]
				assets.append(asset)
		return {"session_id": session_id, "total": len(session["candidates"]),
		        "offset": offset, "assets": assets}

	def open_family(self, session_id: str, family: str, *, offset: int = 0, limit: int = 64) -> dict:
		session = self._load(session_id)
		candidate_by_id = {int(item["asset_id"]): item for item in session["candidates"]
		                   if item["family"] == family}
		# The family is a navigation node, never a lossy cluster. All catalogue
		# members remain pageable even when no retriever scored them.
		members = self.index.family_assets(family)
		members.sort(key=lambda asset: (
			-int(int(asset["id"]) in candidate_by_id),
			-float(candidate_by_id.get(int(asset["id"]), {}).get("score", 0.0)),
			-int(asset.get("native_count") or 0), int(asset["id"])))
		assets = members[offset:offset + max(1, min(limit, 200))]
		for asset in assets:
			candidate = candidate_by_id.get(int(asset["id"]))
			asset["discovery_score"] = candidate["score"] if candidate else 0.0
			asset["discovery_channels"] = candidate["channels"] if candidate else {"family_expansion": True}
		if family not in session["reviewed_families"]:
			session["reviewed_families"].append(family)
		session["reviewed_assets"] = sorted(set(session["reviewed_assets"]) | {int(item["id"]) for item in assets})
		self._save(session)
		return {"session_id": session_id, "family": family, "total": len(members),
		        "offset": offset, "assets": assets}

	def residuals(self, session_id: str, *, limit: int = 64, strategy: str = "stratified") -> dict:
		session = self._load(session_id)
		pool_ids = {int(item["asset_id"]) for item in session["candidates"]}
		assets = [asset for asset in self.index.catalog() if int(asset["id"]) not in pool_ids]
		seed = session_id.encode("utf-8")
		if strategy == "unknown":
			assets.sort(key=lambda asset: (bool(asset.get("semantic_description")),
			                                 hashlib.sha256(seed + str(asset["id"]).encode()).digest()))
		else:
			assets.sort(key=lambda asset: (asset["family_key"],
			                                 hashlib.sha256(seed + str(asset["id"]).encode()).digest()))
		# Round-robin families gives the residual audit breadth instead of another ranking.
		by_family: dict[str, list[dict]] = defaultdict(list)
		for asset in assets:
			by_family[asset["family_key"]].append(asset)
		sample = []
		while len(sample) < min(limit, len(assets)) and by_family:
			for family in list(sorted(by_family)):
				if by_family[family]:
					sample.append(by_family[family].pop(0))
				if not by_family[family]:
					del by_family[family]
				if len(sample) >= limit:
					break
		return {"session_id": session_id, "strategy": strategy,
		        "residual_total": len(assets), "assets": sample}

	def mark(self, session_id: str, asset_ids: list[int], *, decision: str, reason: str = "") -> dict:
		if decision not in {"shortlist", "rejected"}:
			raise ValueError("decision must be shortlist or rejected")
		session = self._load(session_id)
		bucket = session[decision]
		for asset_id in asset_ids:
			bucket[str(int(asset_id))] = reason
		session["reviewed_assets"] = sorted(set(session["reviewed_assets"]) | {int(item) for item in asset_ids})
		self._save(session)
		return self.coverage(session_id)

	def atlas(self, session_id: str, output: Path, *, family_limit: int = 64,
	          thumbnail_dir: Path | None = None) -> dict:
		session = self._load(session_id)
		family_limit = max(1, min(int(family_limit), 512))
		catalog = {int(asset["id"]): asset for asset in self.index.catalog()}
		family_rows = self.families(session_id, limit=2000)["families"]
		family_by_name = {row["family"]: row for row in family_rows}
		selected: list[dict] = []
		selected_families: set[str] = set()

		# A global top-k tends to spend the whole visual budget on the strongest
		# noun (often signs or barriers). Round-robin the roles Codex requested so
		# one atlas actually exposes the full palette needed to compose a scene.
		queues: dict[str, list[dict]] = {}
		for role in session.get("roles", []):
			queues[role] = [candidate for candidate in session["candidates"]
			                if role in str(catalog.get(int(candidate["asset_id"]), {}).get(
			                "role_tags") or "").split()]
		while len(selected) < family_limit and any(queues.values()):
			for role in session.get("roles", []):
				queue = queues.get(role, [])
				while queue and queue[0]["family"] in selected_families:
					queue.pop(0)
				if queue:
					candidate = queue.pop(0)
					selected.append(candidate)
					selected_families.add(candidate["family"])
				if len(selected) >= family_limit:
					break
		for candidate in session["candidates"]:
			if len(selected) >= family_limit:
				break
			if candidate["family"] not in selected_families:
				selected.append(candidate)
				selected_families.add(candidate["family"])

		families = [family_by_name[item["family"]] for item in selected
		            if item["family"] in family_by_name]
		asset_ids = [int(item["asset_id"]) for item in selected[:family_limit]]
		result = self.index.contact_sheet(asset_ids, output, columns=8, tile_size=220,
		                                  thumbnail_dir=thumbnail_dir)
		result.update({"session_id": session_id, "family_count": len(families),
		               "families": families, "strategy": "role_balanced"})
		return result
