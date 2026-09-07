#!/usr/bin/env python3
"""Tiny CLI for Ariane's first prompt-to-build agent checkpoint."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import time
import uuid


def send(bridge: Path, command: str, fields: list[str], timeout: float) -> dict:
	bridge.mkdir(parents=True, exist_ok=True)
	request_id = uuid.uuid4().hex
	request = bridge / "request.txt"
	response = bridge / "response.json"
	temporary = bridge / f"request-{request_id}.tmp"
	response.unlink(missing_ok=True)
	temporary.write_text("\n".join([request_id, command, *fields]) + "\n", encoding="utf-8")
	os.replace(temporary, request)
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		if response.exists():
			try:
				payload = json.loads(response.read_text(encoding="utf-8"))
			except (json.JSONDecodeError, OSError):
				time.sleep(0.03)
				continue
			if payload.get("request_id") == request_id:
				return payload
		time.sleep(0.03)
	raise TimeoutError(f"Ariane did not answer {command!r} within {timeout:g}s")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bridge", type=Path, required=True, help="Directory passed to Ariane --agent-bridge")
	parser.add_argument("--timeout", type=float, default=30.0)
	sub = parser.add_subparsers(dest="command", required=True)
	sub.add_parser("ping")

	scene = sub.add_parser("scene")
	scene.add_argument("logical")
	scene.add_argument("physical")

	catalog = sub.add_parser("catalog")
	catalog.add_argument("query", nargs="?", default="")
	catalog.add_argument("--limit", type=int, default=50)

	place = sub.add_parser("place")
	place.add_argument("model")
	place.add_argument("x", type=float)
	place.add_argument("y", type=float)
	place.add_argument("z", type=float)
	place.add_argument("heading", type=float)
	place.add_argument("--no-snap", action="store_true")

	batch = sub.add_parser("batch")
	batch.add_argument("file", type=Path, help="TSV: model x y z heading snap [agent notes]")

	transform = sub.add_parser("transform")
	transform.add_argument("instance_id", type=int)
	transform.add_argument("x", type=float)
	transform.add_argument("y", type=float)
	transform.add_argument("z", type=float)
	transform.add_argument("heading", type=float)
	transform.add_argument("--no-snap", action="store_true")

	delete = sub.add_parser("delete")
	delete.add_argument("instance_id", type=int)
	sub.add_parser("clear")
	sub.add_parser("list")

	camera = sub.add_parser("camera")
	for name in ("px", "py", "pz", "tx", "ty", "tz", "fov"):
		camera.add_argument(name, type=float)

	capture = sub.add_parser("capture")
	capture.add_argument("path")
	sub.add_parser("save")

	args = parser.parse_args()
	fields: list[str] = []
	if args.command == "scene":
		fields = [args.logical, str(Path(args.physical).resolve())]
	elif args.command == "catalog":
		fields = [args.query, str(args.limit)]
	elif args.command == "place":
		fields = [args.model, args.x, args.y, args.z, args.heading, int(not args.no_snap)]
	elif args.command == "batch":
		fields = [line for line in args.file.read_text(encoding="utf-8").splitlines()
		          if line.strip() and not line.lstrip().startswith("#")]
	elif args.command == "transform":
		fields = [args.instance_id, args.x, args.y, args.z, args.heading, int(not args.no_snap)]
	elif args.command == "delete":
		fields = [args.instance_id]
	elif args.command == "camera":
		fields = [args.px, args.py, args.pz, args.tx, args.ty, args.tz, args.fov]
	elif args.command == "capture":
		fields = [str(Path(args.path).resolve())]
	fields = [str(value) for value in fields]
	payload = send(args.bridge.resolve(), args.command, fields, args.timeout)
	print(json.dumps(payload, indent=2, ensure_ascii=False))
	return 0 if payload.get("ok") else 1


if __name__ == "__main__":
	raise SystemExit(main())
