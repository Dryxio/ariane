#!/usr/bin/env python3
"""MCP stdio adapter for Ariane's shared local agent service."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Literal

from mcp.server import MCPServer
from mcp.server.mcpserver import Image
from PIL import Image as PILImage
from pydantic import BaseModel

if __package__:
	from .ariane_ipc import ArianeError, DEFAULT_ENGINE_SOCKET
else:
	from ariane_ipc import ArianeError, DEFAULT_ENGINE_SOCKET
if __package__:
	from .asset_index import DEFAULT_DATABASE
else:
	from asset_index import DEFAULT_DATABASE
if __package__:
	from .asset_discovery import DEFAULT_SESSION_DIR
else:
	from asset_discovery import DEFAULT_SESSION_DIR
if __package__:
	from .service import ArianeService, ScenePatchError, DEFAULT_STATE_DIR
else:
	from service import ArianeService, ScenePatchError, DEFAULT_STATE_DIR


service = ArianeService(
	Path(os.environ.get("ARIANE_ENGINE_SOCKET", DEFAULT_ENGINE_SOCKET)),
	Path(os.environ.get("ARIANE_ASSET_DB", DEFAULT_DATABASE)),
	discovery_dir=Path(os.environ.get("ARIANE_DISCOVERY_DIR", DEFAULT_SESSION_DIR)),
	state_dir=Path(os.environ.get("ARIANE_AGENT_STATE_DIR", DEFAULT_STATE_DIR)),
)
mcp = MCPServer(
	"ariane",
	description="Inspect GTA assets and safely edit an Ariane scratch layer.",
	instructions=(
		"Vibe mapping workflow: read project memory; survey the user camera/selected pixel without moving it. "
		"Translate the user's language into catalogue roles/styles/avoid while preserving their original brief. "
		"Read and annotate canonical asset previews before using uncertain fronts/anchors. "
		"Keep zones, access routes, semantic relationships, accepted choices and locks in project memory. "
		"Build a coherent palette, then block out large forms before adding small details. "
		"Use propose_asset_palette and render_palette_board for coherent visual selection. External semantic descriptions may be wrong. "
		"Use dimension-aware recipes where appropriate, inspect their orientation assumptions, resolve patches, "
		"inspect preflight, apply, render player-height and overview images, then fix concrete findings. "
		"Offer light variants when direction is ambiguous; do not exhaustively decorate every option. "
		"Review geometry and visual quality separately; never mark a visual review complete without seeing its images. "
		"Preserve locked objects, use edit history for selective undo and checkpoints for full variants. "
		"Saving is separate from committing. Do not claim HSV reference matching understands image semantics."
	),
)


class SceneOperation(BaseModel):
	"""One declarative edit; fields are validated again by Ariane before use."""
	action: Literal["place", "place_relative", "transform", "transform3d", "delete", "array_line", "array_ring"]
	key: str | None = None
	model: int | str | None = None
	instance_id: int | None = None
	group: str | None = None
	supported_by: str | None = None
	support_mode: Literal["declare_only", "snap"] = "declare_only"
	clearance: float = 0.0
	relative_to: str | None = None
	parent_anchor: str | None = None
	child_anchor: str | None = None
	relation: Literal["offset", "on_top", "in_front", "behind", "left", "right"] = "offset"
	gap: float = 0.0
	local_x: float = 0.0
	local_y: float = 0.0
	local_z: float = 0.0
	x: float | None = None
	y: float | None = None
	z: float | None = None
	x2: float | None = None
	y2: float | None = None
	z2: float | None = None
	radius: float | None = None
	count: int | None = None
	jitter: float = 0.0
	heading_noise: float = 0.0
	seed: str | int | None = None
	face: Literal["outward", "inward"] = "outward"
	pitch: float | None = None
	roll: float | None = None
	heading: float | None = None
	snap: bool = True


class SemanticZone(BaseModel):
	"""An explicit entrance, route, or keep-clear footprint in world coordinates."""
	name: str
	role: Literal["entrance", "circulation", "keep_clear"] = "keep_clear"
	shape: Literal["circle", "rect"]
	x: float
	y: float
	radius: float | None = None
	width: float | None = None
	depth: float | None = None
	heading: float = 0
	z_min: float | None = None
	z_max: float | None = None
	ignore_keys: list[str] = []
	severity: Literal["warning", "error"] = "error"


@mcp.tool()
def ariane_status() -> dict:
	"""Inspect engine capabilities, active scratch session, and scene bounds."""
	try:
		bounds = service.engine("scene_bounds")
	except ArianeError:
		bounds = None
	return {
		"capabilities": service.engine("capabilities"),
		"session": service.engine("session_status"),
		"bounds": bounds,
	}


@mcp.tool()
def search_assets(query: str, category: str | None = None,
	              max_width: float | None = None, max_depth: float | None = None,
	              has_collision: bool | None = None, defined_only: bool = True,
	              limit: int = 20) -> list[dict]:
	"""Find GTA assets by text and physical constraints before visual review."""
	return service.search_assets(query, category=category, max_width=max_width,
	                             max_depth=max_depth, has_collision=has_collision,
	                             defined_only=defined_only, limit=limit)


@mcp.tool()
def inspect_asset(asset_id: int, ensure_renderable: bool = False) -> dict | None:
	"""Return catalogue metadata plus authoritative runtime availability and origin bounds."""
	return service.inspect_asset(asset_id, ensure_renderable=ensure_renderable)


@mcp.tool(structured_output=False)
def render_asset_views(asset_id: int, output_path: str, size: int = 512) -> list:
	"""Render two local canonical asset views with identity-axis provenance."""
	result = service.render_asset_views(asset_id, Path(output_path), size=size)
	return [result, Image(path=result["path"])]


@mcp.tool()
def similar_assets(asset_id: int, limit: int = 20) -> list[dict]:
	"""Find assets with matching GTA style, texture dictionary, and scale."""
	return service.assets.similar(asset_id, limit)


@mcp.tool()
def asset_contact_sheet(asset_ids: list[int], output_path: str, columns: int = 5) -> Image:
	"""Render labeled asset thumbnails and return the sheet for visual selection."""
	result = service.assets.contact_sheet(asset_ids, Path(output_path), columns=columns)
	return Image(path=result["path"])


@mcp.tool()
def start_asset_discovery(brief: str, roles: list[str] | None = None,
	                      styles: list[str] | None = None, avoid: list[str] | None = None,
	                      context_asset_ids: list[int] | None = None,
	                      max_width: float | None = None, max_depth: float | None = None,
	                      has_collision: bool | None = None, pool_limit: int = 2500) -> dict:
	"""Start a high-recall catalogue session and report auditable coverage.

	After observing the scene, put scene context in ``brief`` and explicitly list
	the prop roles wanted. Use ``avoid`` for visually inappropriate classes. The session
	considers every eligible non-LOD asset, retains diverse family representatives,
	and tracks what Codex has and has not visually reviewed.
	"""
	constraints = {key: value for key, value in {
		"max_width": max_width, "max_depth": max_depth, "has_collision": has_collision,
	}.items() if value is not None}
	structured_brief = {"scene": brief, "roles": roles or [], "styles": styles or [],
	                    "avoid": avoid or []}
	return service.discovery.start(structured_brief, context_asset_ids=context_asset_ids,
	                               constraints=constraints, pool_limit=pool_limit)


@mcp.tool()
def discovery_coverage(session_id: str) -> dict:
	"""Show reviewed assets/families, remaining branches, channels, and residual risk."""
	return service.discovery.coverage(session_id)


@mcp.tool()
def browse_asset_families(session_id: str, offset: int = 0, limit: int = 100) -> dict:
	"""List ranked visual/functional families without collapsing their members."""
	return service.discovery.families(session_id, offset=offset, limit=limit)


@mcp.tool()
def discovery_candidates(session_id: str, offset: int = 0, limit: int = 100) -> list[dict]:
	"""Return fused candidates with scores and independent retrieval-channel evidence."""
	return service.discovery.results(session_id, offset=offset, limit=limit)["assets"]


@mcp.tool()
def open_asset_family(session_id: str, family: str, offset: int = 0, limit: int = 64) -> list[dict]:
	"""Open one family, mark its page reviewed, and return full asset passports."""
	return service.discovery.open_family(session_id, family, offset=offset, limit=limit)["assets"]


@mcp.tool()
def render_discovery_atlas(session_id: str, output_path: str, family_limit: int = 64) -> Image:
	"""Render one representative from each high-priority family for Codex visual review."""
	result = service.discovery.atlas(session_id, Path(output_path), family_limit=family_limit)
	return Image(path=result["path"])


@mcp.tool()
def audit_discovery_residuals(session_id: str, limit: int = 64,
	                          strategy: Literal["stratified", "unknown"] = "stratified") -> list[dict]:
	"""Sample outside the candidate pool across families to expose retrieval blind spots."""
	return service.discovery.residuals(session_id, limit=limit, strategy=strategy)["assets"]


@mcp.tool()
def mark_discovery_assets(session_id: str, asset_ids: list[int],
	                      decision: Literal["shortlist", "rejected"], reason: str = "") -> dict:
	"""Persist Codex's visual selection or rejection rationale in the coverage ledger."""
	return service.discovery.mark(session_id, asset_ids, decision=decision, reason=reason)


@mcp.tool()
def discover_assets(brief: str, thoroughness: Literal["quick", "broad", "exhaustive"] = "quick",
	                roles: list[str] | None = None, styles: list[str] | None = None,
	                avoid: list[str] | None = None, limit: int = 40,
	                detail: Literal["compact", "full"] = "compact") -> dict:
	"""Discover diverse concepts; compact results are token-safe, full keeps complete passports."""
	return service.discover_assets(brief, thoroughness=thoroughness, roles=roles,
	                               styles=styles, avoid=avoid, limit=limit, detail=detail)


@mcp.tool()
def begin_edit_session(name: str = "agent-proposal") -> dict:
	"""Snapshot the active agent scene and begin an undoable scratch proposal."""
	return service.engine("session_begin", [name])


@mcp.tool()
def camera_context() -> dict:
	"""Read the exact live camera pose, basis, FOV, aspect ratio, and viewport."""
	return service.engine("camera_context")


@mcp.tool()
def screen_to_world(pixel_x: float, pixel_y: float,
	                viewport_width: int, viewport_height: int) -> dict:
	"""Resolve an image pixel to visible geometry or the terrain beneath that ray."""
	return service.engine("screen_to_world", [pixel_x, pixel_y, viewport_width, viewport_height])


@mcp.tool()
def raycast_segment(start_x: float, start_y: float, start_z: float,
	                target_x: float, target_y: float, target_z: float,
	                target_tolerance: float = 1.0) -> dict:
	"""Test whether world geometry occludes a segment without moving the live camera."""
	return service.client.raycast_segment(
		[start_x, start_y, start_z], [target_x, target_y, target_z],
		target_tolerance=target_tolerance)


@mcp.tool()
def apply_scene_patch(operations: list[SceneOperation], patch_id: str | None = None,
	                  expected_revision: int | None = None) -> dict:
	"""Apply a replay-safe patch using stable caller-owned keys.

	Set ``expected_revision`` after inspection to reject stale plans. Reusing the
	same ``patch_id`` returns the original receipt without duplicating mutations.
	"""
	try:
		return service.apply_scene_patch(
			[operation.model_dump(exclude_none=True) for operation in operations],
			patch_id=patch_id, expected_revision=expected_revision)
	except ScenePatchError as error:
		return error.payload


@mcp.tool()
def resolve_scene_patch(operations: list[SceneOperation],
	                    expected_revision: int | None = None) -> dict:
	"""Resolve and fully prevalidate a revision-bound plan without mutating the scene."""
	try:
		return service.resolve_scene_patch(
			[operation.model_dump(exclude_none=True) for operation in operations],
			expected_revision=expected_revision)
	except ScenePatchError as error:
		return error.payload


@mcp.tool()
def apply_resolved_plan(plan_id: str, patch_id: str | None = None) -> dict:
	"""Apply a prior resolved plan only if its scratch session and revision still match."""
	try:
		return service.apply_resolved_plan(plan_id, patch_id=patch_id)
	except ScenePatchError as error:
		return error.payload


@mcp.tool()
def create_scene_group(group: str, members: list[str | int]) -> dict:
	"""Create or replace a semantic group from stable keys or runtime IDs."""
	return service.create_group(group, members)


@mcp.tool()
def list_scene_groups() -> dict:
	"""List semantic groups in the active scratch composition."""
	return service.list_groups()


@mcp.tool()
def inspect_scene_group(group: str, offset: int = 0, limit: int = 50) -> dict:
	"""Inspect one revision-friendly page of a semantic group."""
	return service.inspect_group(group, offset=offset, limit=limit)


@mcp.tool()
def transform_scene_group(group: str, dx: float = 0.0, dy: float = 0.0,
	                      dz: float = 0.0, dheading: float = 0.0,
	                      patch_id: str | None = None,
	                      expected_revision: int | None = None) -> dict:
	"""Move and rotate a complete composition around its centroid."""
	return service.transform_group(group, dx=dx, dy=dy, dz=dz, dheading=dheading,
	                               patch_id=patch_id, expected_revision=expected_revision)


@mcp.tool()
def clone_scene_group(group: str, new_group: str, key_prefix: str,
	                  dx: float = 0.0, dy: float = 0.0, dz: float = 0.0,
	                  dheading: float = 0.0, patch_id: str | None = None) -> dict:
	"""Clone a composition with new stable keys and an optional rigid transform."""
	return service.clone_group(group, new_group, key_prefix, dx=dx, dy=dy, dz=dz,
	                           dheading=dheading, patch_id=patch_id)


@mcp.tool()
def delete_scene_group(group: str, patch_id: str | None = None) -> dict:
	"""Delete every live object in a semantic group."""
	return service.delete_group(group, patch_id=patch_id)


@mcp.tool()
def set_object_support(child: str | int, parent: str | int) -> dict:
	"""Declare that a child is intentionally supported by another scene object."""
	return service.set_support(child, parent)


@mcp.tool()
def snap_object_to_support(child: str | int, parent: str | int,
	                       clearance: float = 0.0) -> dict:
	"""Place a child on the collision top of a parent and record the support relation."""
	return service.snap_to_support(child, parent, clearance)


@mcp.tool()
def validate_composition() -> dict:
	"""Validate support, duplicates, and collisions with shelter containment semantics."""
	return service.validate_composition()


@mcp.tool()
def inspect_zone(x: float, y: float, radius: float, offset: int = 0,
	             limit: int = 64) -> dict:
	"""Inspect one bounded page of nearby native and scratch objects."""
	return service.engine("inspect_zone_page", [x, y, radius, offset, limit])


@mcp.tool()
def list_scene_objects(offset: int = 0, limit: int = 64,
	                   x: float | None = None, y: float | None = None,
	                   radius: float | None = None) -> dict:
	"""List one revisioned page of scratch objects, optionally scoped to a circle."""
	fields: list[object] = [offset, limit]
	if x is not None and y is not None and radius is not None:
		fields.extend([x, y, radius])
	return service.engine("list_page", fields)


@mcp.tool()
def analyze_placement(instance_id: int, grid_size: int = 3) -> dict:
	"""Measure terrain support, signed clearance, relief, normals, and slope below an object."""
	return service.engine("analyze_placement", [instance_id, grid_size])


@mcp.tool()
def fit_object_to_terrain(instance_id: int, profile: Literal["building", "prop"],
	                      search_radius: float = 30.0, search_step: float = 3.0,
	                      max_support_relief: float | None = None) -> dict:
	"""Relocate an upright building to flat support or ground-align a small prop."""
	fields: list[object] = [instance_id, profile, search_radius, search_step]
	if max_support_relief is not None:
		fields.append(max_support_relief)
	return service.engine("fit_terrain", fields)


@mcp.tool()
def validate_scene() -> dict:
	"""Run the primary validator for terrain, supports, duplicates, and overlaps."""
	return service.validate_composition()


@mcp.tool()
def validate_zone(x: float, y: float, radius: float, offset: int = 0,
	              limit: int = 64) -> dict:
	"""Validate one bounded page for floating, embedding, and unsupported slope."""
	return service.engine("validate_zone", [x, y, radius, offset, limit])


@mcp.tool()
def validate_semantic_zones(zones: list[SemanticZone]) -> dict:
	"""Detect props blocking explicitly declared entrances, routes, or clear areas."""
	return service.validate_semantic_zones([
		zone.model_dump(exclude_none=True) for zone in zones
	])


@mcp.tool()
def list_checkpoints() -> dict:
	"""List persistent scratch manifests available across Ariane restarts."""
	return service.checkpoint_list()


@mcp.tool()
def save_checkpoint(name: str) -> dict:
	"""Atomically persist the current scratch layer without saving the IPL."""
	return service.checkpoint_save(name)


@mcp.tool()
def restore_checkpoint(name: str) -> dict:
	"""Replace the active scratch proposal from a persistent checkpoint manifest."""
	return service.checkpoint_restore(name)


@mcp.tool(structured_output=False)
def capture_current_view(output_path: str, label: str = "current") -> list:
	"""Capture the untouched live viewport and return its exact pose provenance."""
	result = service.client.capture_current(Path(output_path), label=label)
	return [result, Image(path=result["path"])]


@mcp.tool(structured_output=False)
def capture_from_pose(output_path: str, label: str,
	                  position_x: float, position_y: float, position_z: float,
	                  target_x: float, target_y: float, target_z: float,
	                  up_x: float = 0.0, up_y: float = 0.0, up_z: float = 1.0,
	                  fov: float = 58.0,
	                  expected_camera_revision: int | None = None) -> list:
	"""Capture one chosen pose and conditionally restore the untouched live viewport."""
	result = service.client.capture_at_pose(
		Path(output_path), position=[position_x, position_y, position_z],
		target=[target_x, target_y, target_z], up=[up_x, up_y, up_z], fov=fov,
		label=label, expected_camera_revision=expected_camera_revision)
	return [result, Image(path=result["path"])]


@mcp.tool(structured_output=False)
def render_scene_views(output_directory: str, fov: float = 58.0,
	                   center_x: float | None = None, center_y: float | None = None,
	                   center_z: float | None = None, span: float | None = None) -> list:
	"""Render four provenance-bound views; an explicit center enables empty-scene survey."""
	center = None
	if center_x is not None and center_y is not None:
		center = [center_x, center_y, center_z if center_z is not None else 0.0]
	manifest = service.capture_views(Path(output_directory), fov, center=center, span=span)
	views = []
	for item in manifest["views"]:
		name, path = item["label"], item["path"]
		view = PILImage.open(path).convert("RGB")
		# Full-resolution captures stay on disk; the MCP review sheet is bounded so
		# one visual feedback pass does not consume tens of megabytes of context.
		view.thumbnail((960, 540), PILImage.Resampling.LANCZOS)
		views.append((name, view))
	width = max(view.width for _, view in views)
	height = max(view.height for _, view in views)
	sheet = PILImage.new("RGB", (width * 2, (height + 28) * 2), "#17191d")
	from PIL import ImageDraw
	draw = ImageDraw.Draw(sheet)
	for index, (name, view) in enumerate(views):
		x = index % 2 * width
		y = index // 2 * (height + 28)
		sheet.paste(view, (x, y))
		draw.rectangle((x, y + height, x + width, y + height + 28), fill="#111215")
		framing = manifest["views"][index].get("framing", {})
		angle = float(framing.get("azimuth_adjustment_degrees", 0.0))
		caption = name if abs(angle) < 0.5 else f"{name} · auto-orbit {angle:+.0f}°"
		draw.text((x + 8, y + height + 7), caption, fill="white")
	output = Path(output_directory).resolve() / "review-sheet.png"
	sheet.save(output)
	manifest["review_sheet"] = {"path": str(output), "tiles": [
		{"label": item["label"], "path": item["path"],
		 "actual_pose": item.get("actual_pose"),
		 "capture_camera_revision": item.get("capture_camera_revision"),
		 "framing": item.get("framing")}
		for item in manifest["views"]
	]}
	Path(manifest["manifest_path"]).write_text(
		__import__("json").dumps(manifest, indent=2), encoding="utf-8")
	return [manifest, Image(path=output)]


@mcp.tool()
def finish_edit_session(commit: bool) -> dict:
	"""Commit the visible proposal or restore the exact pre-session snapshot."""
	return service.engine("session_commit" if commit else "session_rollback")


@mcp.tool()
def save_scene() -> dict:
	"""Persist the committed agent IPL. Active scratch sessions cannot be saved."""
	return service.engine("save")


@mcp.tool()
def get_mapping_project() -> dict:
    """Read the persistent brief, palette, zones, relations, locks and creative phase."""
    return service.vibe.project()


@mcp.tool()
def update_mapping_project(changes: dict, expected_revision: int | None = None) -> dict:
    """Merge project fields. List fields replace their previous values. Preserve user choices and original brief."""
    return service.vibe.update_project(changes, expected_revision)


@mcp.tool(structured_output=False)
def survey_mapping_area(output_directory: str, radius: float = 30, grid: int = 7,
                        pixel_x: float | None = None, pixel_y: float | None = None) -> list:
    """Observe the camera target or selected pixel: annotated RGB, sampled depth/IDs, terrain and native objects."""
    result = service.vibe.observe(output_directory, radius, grid, pixel_x, pixel_y)
    # Full spatial payload stays available on disk; avoid duplicating hundreds of rays in the model context.
    return [{"center": result["center"], "scene_revision": result["scene_revision"],
             "objects": result["objects"][:64], "total_objects": result["total_objects"],
             "surface": result["surface"], "survey_path": str(Path(output_directory).resolve() / "survey.json"),
             "limitations": result["limitations"]}, Image(path=result["images"]["annotated.png"])]


@mcp.tool()
def list_composition_recipes() -> dict:
    """List supported parametric compositions and their required palette roles."""
    return service.vibe.dispatch("recipe.list", {})


@mcp.tool()
def build_composition_recipe(name: Literal["terrace", "market", "checkpoint", "alley", "fence", "camp", "logistics", "outpost", "garden"],
                             palette: dict[str, int], key: str, x: float, y: float,
                             heading: float = 0, count: int = 3, aisle: float = 3,
                             gap: float = 0.25, seed: str = "0",
                             points: list[list[float]] | None = None, gate_width: float = 0) -> dict:
    """Generate a dimension-aware patch without applying it. Merge its circulation constraints into project zones."""
    return service.vibe.recipe(name, palette, key=key, x=x, y=y, heading=heading,
                               count=count, aisle=aisle, gap=gap, seed=seed, points=points, gate_width=gate_width)


@mcp.tool()
def asset_passport(asset_id: int) -> dict:
    """Read measured catalogue metadata, sourced visual annotations, functional anchors and explicit unknowns."""
    return service.profiles.get(asset_id)


@mcp.tool()
def annotate_asset_passport(asset_id: int, features: dict, source: str, confidence: float = 0.7) -> dict:
    """Store visually observed features. Anchors are local xyz; front_heading is degrees from model +Y. Include image provenance."""
    return service.profiles.annotate(asset_id, features, source, confidence)


@mcp.tool()
def search_annotated_assets(query: str, limit: int = 30) -> list[dict]:
    """Search learned visual/functional annotations, including the user's own vocabulary."""
    return service.profiles.search(query, limit)


@mcp.tool()
def index_asset_reference_images(directory: str) -> dict:
    """Build local colour descriptors from MODEL_ID.png thumbnails, once per catalogue."""
    return service.profiles.index_images(directory)


@mcp.tool()
def find_assets_for_reference(image_path: str, limit: int = 30, candidates: list[int] | None = None) -> dict:
    """Colour-match a reference to indexed thumbnails. Supply semantic candidate IDs and visually judge the shortlist."""
    return service.profiles.reference_search(image_path, limit, candidates)


@mcp.tool()
def mapping_edit_history() -> dict:
    """List journalled patches and incomplete/failed attempts in the current map."""
    return service.vibe.history()


@mcp.tool()
def undo_mapping_edit(edit_id: str) -> dict:
    """Undo only one patch; refuses to overwrite later changes to its objects or locked elements."""
    return service.vibe.undo(edit_id)


@mcp.tool(structured_output=False)
def review_mapping(output_directory: str, center: list[float] | None = None, span: float | None = None) -> list:
    """Capture geometry/circulation checks and four views for an explicit visual review."""
    report = service.vibe.review(output_directory, center, span)
    return [report, *[Image(path=v["path"]) for v in report["captures"]["views"]]]


@mcp.tool()
def record_mapping_review(review_path: str, scores: dict[str, float], findings: list[dict], accepted: bool = False) -> dict:
    """Record image-grounded criterion scores and specific fixes. Refuses stale scene/project reviews."""
    return service.vibe.record_review(review_path, scores, findings, accepted)


@mcp.tool()
def mapping_environment(hour: int | None = None, minute: int | None = None,
                        weather_a: int | None = None, weather_b: int | None = None,
                        blend: float | None = None) -> dict:
    """Read available weather names or change only specified atmosphere fields; returns the previous state."""
    return service.environment(hour=hour, minute=minute, weather_a=weather_a, weather_b=weather_b, blend=blend)


@mcp.tool()
def suppress_native_models(x: float, y: float, radius: float, model_ids: list[int]) -> dict:
    """Temporarily hide an explicit native-model whitelist in a scratch session. Rollback restores it."""
    return service.suppress_native_models(x, y, radius, model_ids)


@mcp.tool()
def open_mapping_scene(logical_path: str, physical_path: str) -> dict:
    """Open an agent scratch IPL destination. Refuses to replace an active session."""
    if service.engine("session_status").get("active"):
        raise ValueError("finish the current scratch session before opening a different scene")
    return service.engine("scene", [logical_path, str(Path(physical_path).resolve())])


@mcp.tool()
def compare_mapping_variants(first: str, second: str) -> dict:
    """Compare two named checkpoints using durable keys and their saved creative brief."""
    return service.vibe.compare_variants(first, second)


@mcp.tool()
def export_mapping_review_bundle(output_directory: str) -> dict:
    """Write live scene/project/asset manifests and the last saved IPL, clearly distinguished."""
    return service.vibe.export_bundle(output_directory)


@mcp.tool()
def check_mapping_route(points: list[list[float]], width: float = 1.2, height: float = 1.8) -> dict:
    """Probe a ground-level xyz route against native and scratch collision, at foot/head height and lateral offsets."""
    return service.vibe.check_route(points, width, height)

@mcp.tool()
def selected_mapping_objects() -> dict:
    """Read the user's editor selection to ground instructions such as 'keep this' or 'move these'."""
    return service.engine("selection")

@mcp.tool()
def search_creative_catalogue(query: str, role: str | None = None, styles: list[str] | None = None,
                              avoid: list[str] | None = None, limit: int = 24,
                              max_dimensions: list[float] | None = None) -> dict:
    """Search French/English concepts. Hard role/size/exclusion filters, sourced observations ranked first."""
    return service.dispatch("catalogue.search", locals())


@mcp.tool()
def propose_asset_palette(brief: str, roles: list[str], styles: list[str] | None = None,
                          avoid: list[str] | None = None, per_role: int = 4,
                          max_dimensions: list[float] | None = None) -> dict:
    """Return coherent candidates grouped by role. Missing roles stay explicit; inspect a visual board before choosing."""
    return service.dispatch("catalogue.palette", locals())


@mcp.tool(structured_output=False)
def render_palette_board(asset_ids: list[int], output_directory: str, size: int = 192) -> list:
    """Render a labelled comparison board with two native views per asset and dimensions."""
    report = service.dispatch("catalogue.board", locals())
    return [report, Image(path=report["path"])]


@mcp.tool()
def propose_composition_variants(name: str, palette: dict[str, int], options: dict, variants: int = 3) -> dict:
    """Generate compact/balanced/open alternatives without changing the scene. Options include key,x,y,count."""
    return service.vibe.recipe_variants(name, palette, options, variants)


@mcp.tool()
def create_mapping_review_rig(capture_manifest: str, output_path: str) -> dict:
    """Freeze already reviewed camera poses for subsequent variant comparisons."""
    return service.dispatch("rig.create", locals())


@mcp.tool()
def capture_mapping_review_rig(rig_path: str, output_directory: str) -> dict:
    """Capture exactly the same views, preserving the live camera and recording atmosphere."""
    return service.dispatch("rig.capture", locals())


@mcp.tool(structured_output=False)
def compare_mapping_review_images(capture_paths: list[str], output_path: str) -> list:
    """Show two or three variants side by side; refuses differing camera rigs, atmosphere or scene."""
    report = service.dispatch("rig.compare", locals())
    return [report, Image(path=report["path"])]


@mcp.tool()
def inspect_placement_anchors(asset_id: int) -> dict:
    """Get measured bounds anchors and separately sourced functional anchors. Use names with place_relative."""
    return service.profiles.anchors(asset_id)


def main():
	mcp.run(transport="stdio")


if __name__ == "__main__":
	main()
