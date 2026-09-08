"""Exercise the real MCP stdio adapter, not just its Python service."""

import os
from pathlib import Path
import sys
import tempfile
import unittest

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client


AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))
from catalogue_fixture import make_catalogue


class McpStdioTests(unittest.IsolatedAsyncioTestCase):
	async def test_discovery_tools_are_typed_and_callable(self):
		with tempfile.TemporaryDirectory() as discovery_dir:
			database = make_catalogue(Path(discovery_dir))
			environment = dict(os.environ)
			environment.update({
				"ARIANE_ASSET_DB": str(database),
				"ARIANE_DISCOVERY_DIR": discovery_dir,
				"ARIANE_AGENT_STATE_DIR": str(Path(discovery_dir) / "state"),
			})
			server = StdioServerParameters(
				command=sys.executable,
				args=[str(AGENT_DIR / "ariane_mcp.py")],
				env=environment,
				cwd=str(AGENT_DIR.parents[1]),
			)
			async with stdio_client(server) as (reader, writer):
				async with ClientSession(reader, writer) as session:
					await session.initialize()
					tools = await session.list_tools()
					names = {tool.name for tool in tools.tools}
					self.assertTrue({
						"start_asset_discovery", "discovery_coverage",
						"browse_asset_families", "render_discovery_atlas",
						"audit_discovery_residuals", "mark_discovery_assets",
						"apply_scene_patch", "create_scene_group", "list_scene_groups",
						"inspect_scene_group", "transform_scene_group",
						"clone_scene_group", "delete_scene_group",
						"set_object_support", "snap_object_to_support",
						"validate_composition", "capture_current_view",
						"render_asset_views", "resolve_scene_patch",
						"apply_resolved_plan", "discover_assets",
						"capture_from_pose", "raycast_segment",
						"get_mapping_project", "update_mapping_project", "survey_mapping_area",
						"build_composition_recipe", "asset_passport", "annotate_asset_passport",
						"mapping_edit_history", "undo_mapping_edit", "review_mapping", "record_mapping_review",
						"mapping_environment", "selected_mapping_objects", "compare_mapping_variants",
                        "search_creative_catalogue", "propose_asset_palette", "render_palette_board",
                        "propose_composition_variants", "inspect_placement_anchors",
                        "create_mapping_review_rig", "capture_mapping_review_rig", "compare_mapping_review_images",
					}.issubset(names))
					recipes = await session.call_tool("list_composition_recipes", {})
					self.assertFalse(recipes.is_error)
					result = await session.call_tool("start_asset_discovery", {
						"brief": "weathered desert roadside rest area",
						"roles": ["seating", "table", "cooking_fire"],
						"styles": ["desert", "rural"],
						"avoid": ["interior", "weapon", "building"],
						"pool_limit": 100,
					})
					self.assertFalse(result.is_error)


if __name__ == "__main__":
	unittest.main()
