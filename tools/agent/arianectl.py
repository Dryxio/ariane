#!/usr/bin/env python3
"""Canonical CLI for Ariane's local agent API and asset index."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import uuid

if __package__:
	from .ariane_ipc import ArianeClient, ArianeError, DEFAULT_ENGINE_SOCKET
else:
	from ariane_ipc import ArianeClient, ArianeError, DEFAULT_ENGINE_SOCKET
if __package__:
	from .asset_index import AssetIndex, DEFAULT_DATABASE, DEFAULT_GTASTUFF, build_index
else:
	from asset_index import AssetIndex, DEFAULT_DATABASE, DEFAULT_GTASTUFF, build_index
if __package__:
	from .asset_discovery import AssetDiscovery, DEFAULT_SESSION_DIR
else:
	from asset_discovery import AssetDiscovery, DEFAULT_SESSION_DIR
if __package__:
	from .service import ArianeService, ScenePatchError, DEFAULT_STATE_DIR
else:
	from service import ArianeService, ScenePatchError, DEFAULT_STATE_DIR


def emit(payload: object) -> None:
	print(json.dumps(payload, indent=2, ensure_ascii=False))


def add_engine_commands(sub: argparse._SubParsersAction) -> None:
	sub.add_parser("ping")
	sub.add_parser("capabilities")
	scene = sub.add_parser("scene")
	scene.add_argument("logical")
	scene.add_argument("physical", type=Path)
	session = sub.add_parser("session")
	session_sub = session.add_subparsers(dest="session_command", required=True)
	begin = session_sub.add_parser("begin")
	begin.add_argument("name", nargs="?")
	session_sub.add_parser("status")
	session_sub.add_parser("commit")
	session_sub.add_parser("rollback")
	place = sub.add_parser("place")
	place.add_argument("model")
	for name in ("x", "y", "z", "heading"): place.add_argument(name, type=float)
	place.add_argument("--no-snap", action="store_true")
	batch = sub.add_parser("batch")
	batch.add_argument("file", type=Path)
	transform = sub.add_parser("transform")
	transform.add_argument("instance_id", type=int)
	for name in ("x", "y", "z", "heading"): transform.add_argument(name, type=float)
	transform.add_argument("--no-snap", action="store_true")
	transform3d = sub.add_parser("transform3d")
	transform3d.add_argument("instance_id", type=int)
	for name in ("x", "y", "z", "pitch", "roll", "yaw"): transform3d.add_argument(name, type=float)
	analysis = sub.add_parser("analyze-placement")
	analysis.add_argument("instance_id", type=int); analysis.add_argument("--grid", type=int, default=3)
	fit = sub.add_parser("fit-terrain")
	fit.add_argument("instance_id", type=int); fit.add_argument("profile", choices=("building", "prop"))
	fit.add_argument("--radius", type=float, default=30.0); fit.add_argument("--step", type=float, default=3.0)
	fit.add_argument("--max-relief", type=float)
	delete = sub.add_parser("delete")
	delete.add_argument("instance_id", type=int)
	suppress = sub.add_parser("suppress-world-models")
	suppress.add_argument("x", type=float); suppress.add_argument("y", type=float)
	suppress.add_argument("radius", type=float); suppress.add_argument("model_ids", type=int, nargs="+")
	sub.add_parser("clear")
	sub.add_parser("list")
	list_page = sub.add_parser("list-page")
	list_page.add_argument("--offset", type=int, default=0); list_page.add_argument("--limit", type=int, default=64)
	list_page.add_argument("--x", type=float); list_page.add_argument("--y", type=float)
	list_page.add_argument("--radius", type=float)
	sub.add_parser("bounds")
	sub.add_parser("validate")
	validate_zone = sub.add_parser("validate-zone")
	validate_zone.add_argument("x", type=float); validate_zone.add_argument("y", type=float)
	validate_zone.add_argument("radius", type=float); validate_zone.add_argument("--offset", type=int, default=0)
	validate_zone.add_argument("--limit", type=int, default=64)
	validate_semantics = sub.add_parser("validate-semantics")
	validate_semantics.add_argument("zones", type=Path,
	                                help="JSON array of explicit entrance/route/keep-clear zones")
	zone = sub.add_parser("inspect-zone")
	zone.add_argument("x", type=float); zone.add_argument("y", type=float)
	zone.add_argument("radius", type=float); zone.add_argument("--offset", type=int, default=0)
	zone.add_argument("--limit", type=int, default=64)
	detail = sub.add_parser("asset-detail")
	detail.add_argument("model")
	camera = sub.add_parser("camera")
	for name in ("px", "py", "pz", "tx", "ty", "tz", "fov"): camera.add_argument(name, type=float)
	sub.add_parser("camera-context")
	screen = sub.add_parser("screen-to-world")
	screen.add_argument("pixel_x", type=float); screen.add_argument("pixel_y", type=float)
	screen.add_argument("width", type=int); screen.add_argument("height", type=int)
	raycast = sub.add_parser("raycast-segment")
	for name in ("start_x", "start_y", "start_z", "target_x", "target_y", "target_z"):
		raycast.add_argument(name, type=float)
	raycast.add_argument("--target-tolerance", type=float, default=1.0)
	capture = sub.add_parser("capture")
	capture.add_argument("path", type=Path)
	capture.add_argument("--label", default="current")
	capture_pose = sub.add_parser("capture-pose")
	capture_pose.add_argument("path", type=Path)
	for name in ("px", "py", "pz", "tx", "ty", "tz"): capture_pose.add_argument(name, type=float)
	capture_pose.add_argument("--fov", type=float, default=58.0)
	capture_pose.add_argument("--label", default="custom")
	capture_pose.add_argument("--expected-camera-revision", type=int)
	views = sub.add_parser("capture-views")
	views.add_argument("directory", type=Path); views.add_argument("--fov", type=float, default=58.0)
	views.add_argument("--center", type=float, nargs=3, metavar=("X", "Y", "Z"))
	views.add_argument("--span", type=float)
	sub.add_parser("save")


def engine_command(client: ArianeClient, args: argparse.Namespace) -> dict:
	command = args.command
	if command in {"ping", "capabilities", "clear", "list", "validate", "save"}:
		return client.command(command)
	if command == "scene": return client.command("scene", args.logical, args.physical.resolve())
	if command == "session":
		fields = [args.name] if args.session_command == "begin" and args.name else []
		return client.command(f"session_{args.session_command}", *fields)
	if command == "place":
		return client.command("place", args.model, args.x, args.y, args.z, args.heading, int(not args.no_snap))
	if command == "batch":
		rows = [line for line in args.file.read_text(encoding="utf-8").splitlines()
		        if line.strip() and not line.lstrip().startswith("#")]
		return client.command("batch", *rows)
	if command == "transform":
		return client.command("transform", args.instance_id, args.x, args.y, args.z,
		                      args.heading, int(not args.no_snap))
	if command == "transform3d":
		return client.command("transform3d", args.instance_id, args.x, args.y, args.z,
		                      args.pitch, args.roll, args.yaw)
	if command == "analyze-placement":
		return client.command("analyze_placement", args.instance_id, args.grid)
	if command == "fit-terrain":
		fields = [args.instance_id, args.profile, args.radius, args.step]
		if args.max_relief is not None: fields.append(args.max_relief)
		return client.command("fit_terrain", *fields)
	if command == "delete": return client.command("delete", args.instance_id)
	if command == "suppress-world-models":
		return client.command("suppress_world_models", args.x, args.y, args.radius, *args.model_ids)
	if command == "bounds": return client.command("scene_bounds")
	if command == "list-page":
		fields = [args.offset, args.limit]
		if args.x is not None and args.y is not None and args.radius is not None:
			fields.extend([args.x, args.y, args.radius])
		return client.command("list_page", *fields)
	if command == "inspect-zone":
		return client.command("inspect_zone_page", args.x, args.y, args.radius, args.offset, args.limit)
	if command == "validate-zone":
		return client.command("validate_zone", args.x, args.y, args.radius, args.offset, args.limit)
	if command == "asset-detail": return client.command("asset_detail", args.model)
	if command == "camera":
		return client.command("camera", args.px, args.py, args.pz, args.tx, args.ty, args.tz, args.fov)
	if command == "camera-context": return client.command("camera_context")
	if command == "screen-to-world":
		return client.command("screen_to_world", args.pixel_x, args.pixel_y, args.width, args.height)
	if command == "raycast-segment":
		return {"ray": client.raycast_segment(
			[args.start_x, args.start_y, args.start_z],
			[args.target_x, args.target_y, args.target_z],
			target_tolerance=args.target_tolerance)}
	if command == "capture": return client.capture_current(args.path, label=args.label)
	if command == "capture-pose":
		return client.capture_at_pose(args.path, position=[args.px, args.py, args.pz],
		                              target=[args.tx, args.ty, args.tz], fov=args.fov,
		                              label=args.label,
		                              expected_camera_revision=args.expected_camera_revision)
	if command == "capture-views":
		return client.capture_views(args.directory, args.fov, center=args.center, span=args.span)
	raise ValueError(f"unsupported command: {command}")


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--socket", type=Path, default=DEFAULT_ENGINE_SOCKET)
	parser.add_argument("--timeout", type=float, default=30.0)
	parser.add_argument("--db", type=Path, default=DEFAULT_DATABASE)
	parser.add_argument("--discovery-dir", type=Path, default=DEFAULT_SESSION_DIR)
	parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE_DIR)
	sub = parser.add_subparsers(dest="command", required=True)
	add_engine_commands(sub)
	call = sub.add_parser("call", help="Call a shared service method; JSON parameters inline or from a file")
	call.add_argument("method")
	inputs = call.add_mutually_exclusive_group()
	inputs.add_argument("--params", default="{}")
	inputs.add_argument("--file", type=Path)
	survey = sub.add_parser("survey", help="Observe the place under the camera")
	survey.add_argument("output_directory")
	survey.add_argument("--radius", type=float, default=30)
	survey.add_argument("--grid", type=int, default=7)
	survey.add_argument("--pixel-x", type=float); survey.add_argument("--pixel-y", type=float)
	environment = sub.add_parser("environment")
	environment.add_argument("--hour", type=int); environment.add_argument("--minute", type=int)
	environment.add_argument("--weather-a", type=int); environment.add_argument("--weather-b", type=int)
	environment.add_argument("--blend", type=float)
	assets = sub.add_parser("assets")
	asset_sub = assets.add_subparsers(dest="asset_command", required=True)
	index = asset_sub.add_parser("index")
	sources = index.add_mutually_exclusive_group()
	sources.add_argument("--gtastuff", type=Path, default=DEFAULT_GTASTUFF)
	sources.add_argument("--gta-dir", type=Path, help="build a basic catalogue from your GTA data/*.ide files")
	index.add_argument("--semantic-assets", type=Path,
	                   help="optional AI-enriched asset JSON; kept external when its license is unknown")
	search = asset_sub.add_parser("search")
	search.add_argument("query", nargs="?", default="")
	search.add_argument("--category"); search.add_argument("--max-width", type=float)
	search.add_argument("--max-depth", type=float); search.add_argument("--has-collision", action="store_true")
	search.add_argument("--include-lod", action="store_true"); search.add_argument("--limit", type=int, default=30)
	search.add_argument("--defined-only", action="store_true",
	                    help="only return models known to the running engine")
	inspect = asset_sub.add_parser("inspect"); inspect.add_argument("asset_id", type=int)
	inspect.add_argument("--ensure-renderable", action="store_true")
	preview = asset_sub.add_parser("preview"); preview.add_argument("asset_id", type=int)
	preview.add_argument("--output", type=Path, required=True); preview.add_argument("--size", type=int, default=512)
	similar = asset_sub.add_parser("similar"); similar.add_argument("asset_id", type=int)
	similar.add_argument("--limit", type=int, default=20)
	contact = asset_sub.add_parser("contact-sheet")
	contact.add_argument("asset_ids", type=int, nargs="+")
	contact.add_argument("--output", type=Path, required=True)
	contact.add_argument("--columns", type=int, default=5)
	contact.add_argument("--tile-size", type=int, default=220)
	contact.add_argument("--thumbnail-dir", type=Path)
	discovery = sub.add_parser("discovery")
	discovery_sub = discovery.add_subparsers(dest="discovery_command", required=True)
	discovery_start = discovery_sub.add_parser("start")
	discovery_start.add_argument("brief")
	discovery_start.add_argument("--role", action="append", default=[])
	discovery_start.add_argument("--style", action="append", default=[])
	discovery_start.add_argument("--avoid", action="append", default=[])
	discovery_start.add_argument("--context", type=int, nargs="*", default=[])
	discovery_start.add_argument("--pool-limit", type=int, default=2500)
	discovery_start.add_argument("--has-collision", action="store_true")
	discovery_start.add_argument("--max-width", type=float)
	discovery_start.add_argument("--max-depth", type=float)
	discovery_coverage = discovery_sub.add_parser("coverage")
	discovery_coverage.add_argument("session_id")
	discovery_families = discovery_sub.add_parser("families")
	discovery_families.add_argument("session_id")
	discovery_families.add_argument("--offset", type=int, default=0)
	discovery_families.add_argument("--limit", type=int, default=100)
	discovery_results = discovery_sub.add_parser("results")
	discovery_results.add_argument("session_id")
	discovery_results.add_argument("--offset", type=int, default=0)
	discovery_results.add_argument("--limit", type=int, default=100)
	discovery_open = discovery_sub.add_parser("open")
	discovery_open.add_argument("session_id"); discovery_open.add_argument("family")
	discovery_open.add_argument("--offset", type=int, default=0)
	discovery_open.add_argument("--limit", type=int, default=64)
	discovery_atlas = discovery_sub.add_parser("atlas")
	discovery_atlas.add_argument("session_id"); discovery_atlas.add_argument("--output", type=Path, required=True)
	discovery_atlas.add_argument("--family-limit", type=int, default=64)
	discovery_atlas.add_argument("--thumbnail-dir", type=Path)
	discovery_residuals = discovery_sub.add_parser("residuals")
	discovery_residuals.add_argument("session_id")
	discovery_residuals.add_argument("--limit", type=int, default=64)
	discovery_residuals.add_argument("--strategy", choices=("stratified", "unknown"), default="stratified")
	discovery_mark = discovery_sub.add_parser("mark")
	discovery_mark.add_argument("session_id")
	discovery_mark.add_argument("decision", choices=("shortlist", "rejected"))
	discovery_mark.add_argument("asset_ids", type=int, nargs="+")
	discovery_mark.add_argument("--reason", default="")
	checkpoint = sub.add_parser("checkpoint")
	checkpoint_sub = checkpoint.add_subparsers(dest="checkpoint_command", required=True)
	checkpoint_sub.add_parser("list")
	checkpoint_save = checkpoint_sub.add_parser("save"); checkpoint_save.add_argument("name")
	checkpoint_restore = checkpoint_sub.add_parser("restore"); checkpoint_restore.add_argument("name")
	patch = sub.add_parser("patch")
	patch.add_argument("file", type=Path, help="JSON list or {operations,patch_id,expected_revision}")
	resolve_patch = sub.add_parser("resolve-patch")
	resolve_patch.add_argument("file", type=Path,
	                           help="JSON list or {operations,expected_revision}; never mutates the scene")
	apply_plan = sub.add_parser("apply-plan")
	apply_plan.add_argument("plan_id"); apply_plan.add_argument("--patch-id")
	groups = sub.add_parser("groups")
	group_sub = groups.add_subparsers(dest="group_command", required=True)
	group_sub.add_parser("list")
	group_create = group_sub.add_parser("create"); group_create.add_argument("group")
	group_create.add_argument("members", nargs="+")
	group_inspect = group_sub.add_parser("inspect"); group_inspect.add_argument("group")
	group_inspect.add_argument("--offset", type=int, default=0); group_inspect.add_argument("--limit", type=int, default=50)
	group_transform = group_sub.add_parser("transform"); group_transform.add_argument("group")
	for name in ("dx", "dy", "dz", "dheading"): group_transform.add_argument(f"--{name}", type=float, default=0.0)
	group_transform.add_argument("--patch-id"); group_transform.add_argument("--expected-revision", type=int)
	group_clone = group_sub.add_parser("clone"); group_clone.add_argument("group")
	group_clone.add_argument("new_group"); group_clone.add_argument("key_prefix")
	for name in ("dx", "dy", "dz", "dheading"): group_clone.add_argument(f"--{name}", type=float, default=0.0)
	group_clone.add_argument("--patch-id")
	group_delete = group_sub.add_parser("delete"); group_delete.add_argument("group"); group_delete.add_argument("--patch-id")
	support = sub.add_parser("support")
	support_sub = support.add_subparsers(dest="support_command", required=True)
	support_set = support_sub.add_parser("set"); support_set.add_argument("child"); support_set.add_argument("parent")
	support_snap = support_sub.add_parser("snap"); support_snap.add_argument("child"); support_snap.add_argument("parent")
	support_snap.add_argument("--clearance", type=float, default=0.0)
	sub.add_parser("validate-composition")
	args = parser.parse_args(argv)
	try:
		if args.command in {"call", "survey", "environment"}:
			service = ArianeService(args.socket, args.db, args.timeout, args.discovery_dir, args.state_dir)
			if args.command == "call":
				params = json.loads(args.file.read_text() if args.file else args.params)
				if not isinstance(params, dict): raise ValueError("parameters must be a JSON object")
				payload = service.dispatch(args.method, params)
			elif args.command == "survey":
				payload = service.vibe.observe(args.output_directory, args.radius, args.grid, args.pixel_x, args.pixel_y)
			else:
				settings = {key: getattr(args, key) for key in ("hour", "minute", "weather_a", "weather_b", "blend")
				            if getattr(args, key) is not None}
				payload = service.environment(**settings)
		elif args.command == "assets":
			if args.asset_command == "index": payload = build_index(
				args.gtastuff, args.db, semantic_assets=args.semantic_assets, gta_dir=args.gta_dir)
			elif args.asset_command == "search":
				service = ArianeService(args.socket, args.db, args.timeout,
				                        args.discovery_dir, args.state_dir)
				if args.defined_only:
					payload = {"assets": service.search_assets(
						args.query, category=args.category, max_width=args.max_width,
						max_depth=args.max_depth, has_collision=True if args.has_collision else None,
						defined_only=True, limit=args.limit)}
				else:
					payload = {"assets": AssetIndex(args.db).search(
						args.query, category=args.category, max_width=args.max_width,
						max_depth=args.max_depth, has_collision=True if args.has_collision else None,
						exclude_lod=not args.include_lod, limit=args.limit)}
			elif args.asset_command == "inspect":
				if args.ensure_renderable:
					payload = {"asset": ArianeService(args.socket, args.db, args.timeout,
						args.discovery_dir, args.state_dir).inspect_asset(
						args.asset_id, ensure_renderable=True)}
				else: payload = {"asset": AssetIndex(args.db).inspect(args.asset_id)}
			elif args.asset_command == "preview":
				payload = ArianeService(args.socket, args.db, args.timeout,
					args.discovery_dir, args.state_dir).render_asset_views(
					args.asset_id, args.output, size=args.size)
			elif args.asset_command == "similar": payload = {"assets": AssetIndex(args.db).similar(args.asset_id, args.limit)}
			else: payload = AssetIndex(args.db).contact_sheet(
				args.asset_ids, args.output, columns=args.columns, tile_size=args.tile_size,
				thumbnail_dir=args.thumbnail_dir)
		elif args.command == "discovery":
			discovery_service = AssetDiscovery(AssetIndex(args.db), session_dir=args.discovery_dir)
			if args.discovery_command == "start":
				constraints = {key: value for key, value in {
					"has_collision": True if args.has_collision else None,
					"max_width": args.max_width, "max_depth": args.max_depth,
				}.items() if value is not None}
				brief = {"scene": args.brief, "roles": args.role, "styles": args.style,
				         "avoid": args.avoid} if args.role or args.style or args.avoid else args.brief
				payload = discovery_service.start(brief, context_asset_ids=args.context,
				                                  constraints=constraints, pool_limit=args.pool_limit)
			elif args.discovery_command == "coverage": payload = discovery_service.coverage(args.session_id)
			elif args.discovery_command == "families": payload = discovery_service.families(
				args.session_id, offset=args.offset, limit=args.limit)
			elif args.discovery_command == "results": payload = discovery_service.results(
				args.session_id, offset=args.offset, limit=args.limit)
			elif args.discovery_command == "open": payload = discovery_service.open_family(
				args.session_id, args.family, offset=args.offset, limit=args.limit)
			elif args.discovery_command == "atlas": payload = discovery_service.atlas(
				args.session_id, args.output, family_limit=args.family_limit,
				thumbnail_dir=args.thumbnail_dir)
			elif args.discovery_command == "residuals": payload = discovery_service.residuals(
				args.session_id, limit=args.limit, strategy=args.strategy)
			else: payload = discovery_service.mark(
				args.session_id, args.asset_ids, decision=args.decision, reason=args.reason)
		elif args.command in {"checkpoint", "validate-semantics", "patch", "resolve-patch", "apply-plan", "groups", "support",
		                    "validate-composition"}:
			checkpoint_service = ArianeService(args.socket, args.db, args.timeout,
			                                   args.discovery_dir, args.state_dir)
			if args.command == "validate-semantics":
				payload = checkpoint_service.validate_semantic_zones(
					json.loads(args.zones.read_text(encoding="utf-8")))
			elif args.command == "patch":
				document = json.loads(args.file.read_text(encoding="utf-8"))
				if isinstance(document, list): document = {"operations": document}
				payload = checkpoint_service.apply_scene_patch(document["operations"],
					patch_id=document.get("patch_id"), expected_revision=document.get("expected_revision"))
			elif args.command == "resolve-patch":
				document = json.loads(args.file.read_text(encoding="utf-8"))
				if isinstance(document, list): document = {"operations": document}
				payload = checkpoint_service.resolve_scene_patch(
					document["operations"], expected_revision=document.get("expected_revision"))
			elif args.command == "apply-plan":
				payload = checkpoint_service.apply_resolved_plan(args.plan_id, patch_id=args.patch_id)
			elif args.command == "groups":
				if args.group_command == "list": payload = checkpoint_service.list_groups()
				elif args.group_command == "create":
					members = [int(value) if value.isdigit() else value for value in args.members]
					payload = checkpoint_service.create_group(args.group, members)
				elif args.group_command == "inspect":
					payload = checkpoint_service.inspect_group(args.group, offset=args.offset, limit=args.limit)
				elif args.group_command == "transform":
					payload = checkpoint_service.transform_group(args.group, dx=args.dx, dy=args.dy,
						dz=args.dz, dheading=args.dheading, patch_id=args.patch_id,
						expected_revision=args.expected_revision)
				elif args.group_command == "clone":
					payload = checkpoint_service.clone_group(args.group, args.new_group, args.key_prefix,
						dx=args.dx, dy=args.dy, dz=args.dz, dheading=args.dheading,
						patch_id=args.patch_id)
				else: payload = checkpoint_service.delete_group(args.group, patch_id=args.patch_id)
			elif args.command == "support":
				child = int(args.child) if args.child.isdigit() else args.child
				parent = int(args.parent) if args.parent.isdigit() else args.parent
				if args.support_command == "set": payload = checkpoint_service.set_support(child, parent)
				else: payload = checkpoint_service.snap_to_support(child, parent, args.clearance)
			elif args.command == "validate-composition": payload = checkpoint_service.validate_composition()
			elif args.checkpoint_command == "list": payload = checkpoint_service.checkpoint_list()
			elif args.checkpoint_command == "save": payload = checkpoint_service.checkpoint_save(args.name)
			else: payload = checkpoint_service.checkpoint_restore(args.name)
		elif args.command in {"place", "transform", "transform3d", "delete", "clear", "suppress-world-models"}:
			service = ArianeService(args.socket, args.db, args.timeout, args.discovery_dir, args.state_dir)
			if args.command == "clear": payload = service.engine("clear")
			elif args.command == "suppress-world-models":
				payload = service.suppress_native_models(args.x, args.y, args.radius, args.model_ids)
			else:
				op = {"action": args.command}
				if args.command == "place": op.update(key=f"cli:{uuid.uuid4().hex}", model=args.model)
				else: op["instance_id"] = args.instance_id
				if args.command != "delete":
					op.update(x=args.x, y=args.y, z=args.z, heading=args.yaw if args.command == "transform3d" else args.heading,
					          snap=not getattr(args, "no_snap", True))
				if args.command == "transform3d": op.update(pitch=args.pitch, roll=args.roll, snap=False)
				payload = service.apply_scene_patch([op])
				if payload.get("objects"): payload["instance_id"] = payload["objects"][0]["instance_id"]
		else:
			payload = engine_command(ArianeClient(args.socket, args.timeout), args)
		emit(payload)
		return 0
	except (ArianeError, FileNotFoundError, OSError, RuntimeError, ValueError) as error:
		if isinstance(error, ScenePatchError): emit(error.payload)
		elif isinstance(error, ArianeError) and error.response:
			emit({"ok": False, "error": str(error), **{
				key: value for key, value in error.response.items()
				if key in {"scene_revision", "camera_revision"}}})
		else: emit({"ok": False, "error": str(error)})
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
