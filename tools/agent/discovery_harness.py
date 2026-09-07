#!/usr/bin/env python3
"""Closed-loop recall and stability benchmark for Codex-first asset discovery."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics

if __package__:
	from .asset_discovery import AssetDiscovery
else:
	from asset_discovery import AssetDiscovery
if __package__:
	from .asset_index import AssetIndex, DEFAULT_DATABASE
else:
	from asset_index import AssetIndex, DEFAULT_DATABASE


CASES = [
	{"name": "picnic_table", "expected": [1281], "queries": [
		"rustic outdoor picnic table for a desert motel",
		"weathered wooden seating and table with sun shade",
		"small countryside rest area furniture for eating outside",
	]},
	{"name": "barbecue", "expected": [1481], "queries": [
		"small outdoor barbecue grill",
		"metal charcoal cooker for a campsite",
		"rural motel prop used to cook food outside",
	]},
	{"name": "trash_bin", "expected": [1359], "queries": [
		"wire mesh public trash can",
		"small outdoor rubbish bin beside a picnic table",
		"street furniture for motel guest waste",
	]},
	{"name": "park_bench", "expected": [1280], "queries": [
		"wooden park bench for outdoor seating",
		"long public seat with backrest",
		"simple roadside bench for resting travellers",
	]},
	{"name": "woodpile", "expected": [1463], "queries": [
		"small stacked firewood pile",
		"cut logs stored beside a rural building",
		"weathered wood fuel clutter for a farm yard",
	]},
	{"name": "barrel", "expected": [1217, 1218, 1222, 935, 3632], "queries": [
		"rusty industrial oil barrel",
		"weathered metal drum used as rural clutter",
		"small cylindrical fuel container for a yard",
	]},
	{"name": "farm_cart", "expected": [1458], "queries": [
		"old wooden farm cart wagon",
		"rustic two wheel vehicle prop for countryside",
		"abandoned wooden trailer carrying rural supplies",
	]},
	{"name": "western_saloon", "expected": [3249], "queries": [
		"western desert saloon building",
		"old west timber frontier storefront",
		"rural cowboy bar structure for a desert village",
	]},
	{"name": "teepee_sign", "expected": [11432], "queries": [
		"tee pee motel roadside sign",
		"desert tourist attraction sign near native tents",
		"tall quirky motel advertisement in the countryside",
	]},
	{"name": "vending_machine", "expected": [1209, 1776], "queries": [
		"drink vending machine",
		"coin operated soda dispenser for motel guests",
		"commercial refreshment machine placed outside",
	]},
]

ATLAS_SCENARIOS = [
	("desert_motel", {"scene": "outdoor guest area at a weathered desert teepee motel", "roles": ["seating", "table", "cooking_fire", "waste", "lighting", "signage", "decoration"], "styles": ["desert", "rural", "weathered"], "avoid": ["interior", "weapon", "building"]}, [11431, 11432]),
	("military_base", {"scene": "DayZ-style military checkpoint", "roles": ["fence_barrier", "container", "lighting", "signage", "utility"], "styles": ["military", "industrial", "weathered"], "avoid": ["interior", "weapon", "building"]}, []),
	("rural_farm", {"scene": "working rural farm yard", "roles": ["container", "wood_fuel", "seating", "vegetation", "utility", "vehicle_prop"], "styles": ["rural", "weathered"], "avoid": ["interior", "weapon"]}, []),
	("urban_alley", {"scene": "dense Los Santos back alley", "roles": ["waste", "utility", "fence_barrier", "container", "lighting", "signage"], "styles": ["urban", "industrial", "weathered"], "avoid": ["interior", "weapon", "building"]}, []),
	("beach_camp", {"scene": "small informal beach campsite", "roles": ["seating", "table", "cooking_fire", "lighting", "vegetation", "container"], "styles": ["coastal", "rural"], "avoid": ["interior", "weapon", "building", "road"]}, []),
	("industrial_yard", {"scene": "heavy industrial storage yard", "roles": ["container", "utility", "fence_barrier", "signage", "lighting"], "styles": ["industrial", "weathered"], "avoid": ["interior", "weapon", "building"]}, []),
]


def _rank(ids: list[int], expected: set[int]) -> int | None:
	for rank, asset_id in enumerate(ids, 1):
		if asset_id in expected:
			return rank
	return None


def _rank_family(families: list[dict], expected: set[str]) -> int | None:
	for rank, family in enumerate(families, 1):
		if family["family"] in expected:
			return rank
	return None


def _all_results(discovery: AssetDiscovery, session_id: str, total: int) -> list[dict]:
	results = []
	for offset in range(0, total, 1000):
		results.extend(discovery.results(session_id, offset=offset, limit=min(1000, total - offset))["assets"])
	return results


def run(args: argparse.Namespace) -> dict:
	index = AssetIndex(args.db)
	args.output.mkdir(parents=True, exist_ok=True)
	session_dir = args.output / "sessions"
	discovery = AssetDiscovery(index, session_dir=session_dir)
	case_results = []
	deterministic = True
	coverage_failures = []
	for case in CASES:
		for query in case["queries"]:
			expected = set(case["expected"])
			expected_families = {asset["family_key"] for asset_id in expected
			                     if (asset := index.inspect(asset_id))}
			baseline_ids = [int(asset["id"]) for asset in index.search(query, limit=1500)]
			baseline_rank = _rank(baseline_ids, expected)
			iteration_rankings = []
			family_rankings = []
			family_reachable = []
			first_ids = None
			for _ in range(args.iterations):
				coverage = discovery.start(query, pool_limit=args.pool_limit)
				if coverage["considered_total"] != coverage["eligible_total"]:
					coverage_failures.append({"case": case["name"], "query": query, "coverage": coverage})
				results = _all_results(discovery, coverage["session_id"], coverage["candidate_pool_count"])
				ids = [int(asset["id"]) for asset in results]
				iteration_rankings.append(_rank(ids, expected))
				families = discovery.families(coverage["session_id"], limit=2000)["families"]
				family_rankings.append(_rank_family(families, expected_families))
				family_reachable.append(expected_families <= {item["family"] for item in families})
				if first_ids is None:
					first_ids = ids
				elif ids != first_ids:
					deterministic = False
			case_results.append({
				"case": case["name"], "query": query, "expected": sorted(expected),
				"baseline_rank": baseline_rank, "discovery_ranks": iteration_rankings,
				"best_discovery_rank": min((rank for rank in iteration_rankings if rank is not None), default=None),
				"family_ranks": family_rankings,
				"best_family_rank": min((rank for rank in family_rankings if rank is not None), default=None),
				"family_reachable": all(family_reachable),
			})

	atlases = []
	if args.render_atlases:
		for name, brief, context in ATLAS_SCENARIOS:
			coverage = discovery.start(brief, context_asset_ids=context, pool_limit=args.pool_limit)
			output = args.output / f"atlas-{name}.png"
			atlas = discovery.atlas(coverage["session_id"], output, family_limit=args.atlas_families,
			                        thumbnail_dir=args.thumbnail_dir)
			residual = discovery.residuals(coverage["session_id"], limit=32)
			atlases.append({"name": name, "brief": brief, "coverage": coverage,
			                "atlas": atlas["path"],
			                "residual_sample_ids": [asset["id"] for asset in residual["assets"]]})

	def hit_at(field: str, cutoff: int) -> float:
		hits = sum(result[field] is not None and result[field] <= cutoff for result in case_results)
		return hits / len(case_results)

	ranks = [result["best_discovery_rank"] for result in case_results if result["best_discovery_rank"] is not None]
	report = {
		"generated_at": datetime.now(timezone.utc).isoformat(),
		"database": str(Path(args.db).resolve()), "case_count": len(case_results),
		"iterations": args.iterations, "pool_limit": args.pool_limit,
		"metrics": {
			"baseline_hit_at_50": hit_at("baseline_rank", 50),
			"baseline_hit_at_200": hit_at("baseline_rank", 200),
			"discovery_hit_at_50": hit_at("best_discovery_rank", 50),
			"discovery_hit_at_200": hit_at("best_discovery_rank", 200),
			"discovery_hit_in_pool": hit_at("best_discovery_rank", args.pool_limit),
			"family_hit_at_50": hit_at("best_family_rank", 50),
			"family_hit_at_200": hit_at("best_family_rank", 200),
			"family_reachable": sum(result["family_reachable"] for result in case_results) / len(case_results),
			"median_discovery_rank": statistics.median(ranks) if ranks else None,
			"deterministic": deterministic, "coverage_failures": len(coverage_failures),
		},
		"cases": case_results, "coverage_failures": coverage_failures, "atlases": atlases,
	}
	(args.output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
	lines = ["# Asset discovery harness", "", f"Cases: {len(case_results)}; iterations: {args.iterations}", "",
	         "## Metrics", ""]
	for key, value in report["metrics"].items():
		lines.append(f"- {key}: {value}")
	lines.extend(("", "## Manual atlas review", ""))
	for atlas in atlases:
		lines.append(f"- {atlas['name']}: `{atlas['atlas']}`")
	lines.extend(("", "## Misses", ""))
	for result in case_results:
		if result["best_discovery_rank"] is None or result["best_discovery_rank"] > 200:
			lines.append(f"- {result['case']}: rank={result['best_discovery_rank']} — {result['query']}")
	(args.output / "manual-review.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
	return report


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--db", type=Path, default=DEFAULT_DATABASE)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--iterations", type=int, default=3)
	parser.add_argument("--pool-limit", type=int, default=2500)
	parser.add_argument("--render-atlases", action="store_true")
	parser.add_argument("--atlas-families", type=int, default=64)
	parser.add_argument("--thumbnail-dir", type=Path)
	args = parser.parse_args()
	report = run(args)
	print(json.dumps(report["metrics"], indent=2))
	return 0 if not report["coverage_failures"] and report["metrics"]["deterministic"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
