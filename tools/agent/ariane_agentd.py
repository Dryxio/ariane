#!/usr/bin/env python3
"""Private local JSON-lines daemon for persistent Ariane agent integrations."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socketserver

if __package__:
	from .ariane_ipc import DEFAULT_ENGINE_SOCKET
else:
	from ariane_ipc import DEFAULT_ENGINE_SOCKET
if __package__:
	from .asset_index import DEFAULT_DATABASE
else:
	from asset_index import DEFAULT_DATABASE
if __package__:
	from .service import ArianeService, ScenePatchError
else:
	from service import ArianeService, ScenePatchError


class AgentRequestHandler(socketserver.StreamRequestHandler):
	def handle(self) -> None:
		for raw_line in self.rfile:
			request: object = None
			try:
				request = json.loads(raw_line)
				if not isinstance(request, dict):
					raise ValueError("request must be a JSON object")
				result = self.server.service.dispatch(request["method"], request.get("params"))
				response = {"id": request.get("id"), "ok": True, "result": result}
			except (KeyError, OSError, RuntimeError, ValueError) as error:
				request_id = request.get("id") if isinstance(request, dict) else None
				if isinstance(error, ScenePatchError):
					response = {"id": request_id, **error.payload}
				else: response = {"id": request_id, "ok": False, "error": str(error)}
			self.wfile.write((json.dumps(response, separators=(",", ":")) + "\n").encode("utf-8"))


class AgentServer(socketserver.ThreadingUnixStreamServer):
	allow_reuse_address = True
	request_queue_size = 8

	def __init__(self, address: str, service: ArianeService):
		self.service = service
		super().__init__(address, AgentRequestHandler)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--listen", type=Path, default=Path("/tmp/ariane-agentd-v1.sock"))
	parser.add_argument("--engine-socket", type=Path, default=DEFAULT_ENGINE_SOCKET)
	parser.add_argument("--db", type=Path, default=DEFAULT_DATABASE)
	args = parser.parse_args()
	listen = args.listen.resolve()
	listen.unlink(missing_ok=True)
	server = AgentServer(str(listen), ArianeService(args.engine_socket, args.db))
	os.chmod(listen, 0o600)
	try:
		try:
			server.serve_forever()
		except KeyboardInterrupt:
			pass
	finally:
		server.server_close()
		listen.unlink(missing_ok=True)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
