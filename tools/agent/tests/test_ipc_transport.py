"""Exercise the real C++ framing/auth transport without renderer/game assets.

Run with python -m unittest discover -s tools/agent/tests -p test_ipc_transport.py.
CXX may select clang++/g++ (or cl from a Visual Studio developer shell).
"""
from pathlib import Path
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ariane_ipc import ArianeClient, ArianeError

TOKEN = "transport-test-token-" + "a" * 32


def harness_source():
    source = (Path(__file__).resolve().parents[2] / "euryopa/agentbridge.cpp").read_text()
    # Compile the exact production transport functions, with only engine globals
    # and the game-command dispatcher replaced by a ping/large-reply fixture.
    includes = source[:source.index('static const int AGENT_PROTOCOL_VERSION')]
    includes = includes.replace('#include "euryopa.h"', '').replace('#include "agentbridge.h"', '')
    globals_ = source[source.index('#ifdef _WIN32\ntypedef SOCKET'):source.index('struct AgentSceneSnapshot')]
    escape = source[source.index('static std::string\njsonEscape'):source.index('static AgentCameraPose\n')]
    transport = source[source.index('static void\nwriteResponse'):source.index('static void\ninitializeBridge')]
    reader = source[source.index('static void\nsplitRequestContent'):source.index('\nstatic bool\nparseInt')]
    return includes + '''
#include <thread>
#include <chrono>
#include <cstdint>
#define nil nullptr
#define log printf
typedef uint32_t uint32;
static const int AGENT_PROTOCOL_VERSION = 1;
static const size_t AGENT_MAX_RESPONSE_BYTES = 4 * 1024 * 1024;
static uint32 gAgentSceneRevision;
static char gAgentSocketPath[1024], gAgentBridgeDirectory[1024];
''' + globals_ + escape + transport + reader + '''
int main(int argc, char **argv) {
    bool ready;
#ifdef _WIN32
    ready = initializeAgentTcp(argv[1]);
#else
    if(argc > 2){
        strncpy(gAgentSocketPath, argv[2], sizeof(gAgentSocketPath)-1);
        ready = initializeAgentSocket();
    }else ready = initializeAgentTcp(argv[1]);
#endif
    if(!ready) return 2;
    puts("READY"); fflush(stdout);
    for(;;){
        std::vector<std::string> lines;
        if(readRequest(lines)){
            std::string body = lines.size() > 1 && lines[1] == "large" ?
                "\\\"data\\\":\\\"" + std::string(300000, 'x') + "\\\"" : "\\\"pong\\\":true";
            writeResponse(lines[0], true, body);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
'''


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        source = cls.root / 'transport.cpp'
        source.write_text(harness_source())
        cls.binary = cls.root / ('transport.exe' if os.name == 'nt' else 'transport')
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++') or shutil.which('cl')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required for production transport tests')
        if Path(compiler).stem.lower() == 'cl':
            command = [compiler, '/nologo', '/EHsc', '/std:c++14', str(source), '/Fe:' + str(cls.binary), 'ws2_32.lib']
        else:
            command = [compiler, '-std=c++11', str(source), '-o', str(cls.binary)]
            if os.name == 'nt': command += ['-lws2_32']
        subprocess.run(command, check=True, capture_output=True, cwd=cls.root)

    @classmethod
    def tearDownClass(cls):
        # Windows can retain an executable image handle briefly after wait().
        for attempt in range(20):
            try:
                cls.tmp.cleanup()
                break
            except PermissionError:
                if attempt == 19: raise
                time.sleep(0.1)

    def setUp(self):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            self.port = probe.getsockname()[1]
        self.env = patch.dict(os.environ, {'ARIANE_ENGINE_TCP_PORT': str(self.port), 'ARIANE_ENGINE_TOKEN': TOKEN})
        self.env.start()
        self.server = subprocess.Popen([str(self.binary), str(self.port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(self.server.stdout.readline().strip(), b'READY')

    def tearDown(self):
        self.server.terminate()
        self.server.communicate(timeout=5)
        self.env.stop()

    def test_ping_and_large_reply(self):
        self.assertTrue(ArianeClient().command('ping')['pong'])
        self.assertEqual(len(ArianeClient().command('large')['data']), 300000)

    def test_wrong_token_rejected_then_valid_client_recovers(self):
        with patch.dict(os.environ, {'ARIANE_ENGINE_TOKEN': 'b' * 40}):
            with self.assertRaisesRegex(ArianeError, 'authentication failed'):
                ArianeClient().command('ping')
        self.assertTrue(ArianeClient().command('ping')['pong'])

    def test_no_auth_cannot_dispatch(self):
        with socket.create_connection(('127.0.0.1', self.port)) as client:
            payload = b'ARIANE_IPC/1\nrequest\nping'
            client.sendall(struct.pack('!I', len(payload)) + payload)
            size = struct.unpack('!I', ArianeClient._recv_exact(client, 4))[0]
            response = json.loads(ArianeClient._recv_exact(client, size))
            self.assertFalse(response['ok'])
            self.assertEqual(response['error'], 'authentication failed')

    def test_missing_command_rejected_and_server_recovers(self):
        with socket.create_connection(('127.0.0.1', self.port)) as client:
            payload = f'ARIANE_AUTH/1 {TOKEN}\nARIANE_IPC/1\nrequest'.encode()
            client.sendall(struct.pack('!I', len(payload)) + payload)
            size = struct.unpack('!I', ArianeClient._recv_exact(client, 4))[0]
            response = json.loads(ArianeClient._recv_exact(client, size))
            self.assertEqual(response['error'], 'missing request id or command')
            self.assertEqual(client.recv(1), b'')
        self.assertTrue(ArianeClient().command('ping')['pong'])

    def test_fragmented_frame(self):
        with socket.create_connection(('127.0.0.1', self.port)) as client:
            payload = f'ARIANE_AUTH/1 {TOKEN}\nARIANE_IPC/1\nfragmented\nping'.encode()
            frame = struct.pack('!I', len(payload)) + payload
            for byte in frame:
                client.sendall(bytes([byte]))
            size = struct.unpack('!I', ArianeClient._recv_exact(client, 4))[0]
            self.assertTrue(json.loads(ArianeClient._recv_exact(client, size))['pong'])

    def test_oversized_frame_closed_and_server_recovers(self):
        with socket.create_connection(('127.0.0.1', self.port)) as client:
            client.sendall(struct.pack('!I', 1048577))
            self.assertEqual(client.recv(1), b'')
        self.assertTrue(ArianeClient().command('ping')['pong'])

    def test_partial_header_times_out_and_server_recovers(self):
        with socket.create_connection(('127.0.0.1', self.port)) as client:
            client.settimeout(3)
            client.sendall(b'\x00')
            self.assertEqual(client.recv(1), b'')
        self.assertTrue(ArianeClient().command('ping')['pong'])

    def test_invalid_configuration_fails_closed(self):
        for token in ('', 'short', 'a' * 257, 'a' * 40 + '\n'):
            with patch.dict(os.environ, {'ARIANE_ENGINE_TOKEN': token}):
                with self.assertRaises(ValueError): ArianeClient()
                result = subprocess.run([str(self.binary), str(self.port)], capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)
        for port in ('0', '65536', 'abc', ''):
            with patch.dict(os.environ, {'ARIANE_ENGINE_TCP_PORT': port}):
                with self.assertRaises(ValueError): ArianeClient()

    @unittest.skipIf(os.name == 'nt', 'Unix socket regression applies to Unix hosts')
    def test_unix_socket_default(self):
        self.server.terminate()
        self.server.communicate(timeout=5)
        path = str(self.root / 'engine.sock')
        self.server = subprocess.Popen([str(self.binary), 'unused', path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(self.server.stdout.readline().strip(), b'READY')
        with patch.dict(os.environ):
            os.environ.pop('ARIANE_ENGINE_TCP_PORT', None)
            self.assertTrue(ArianeClient(Path(path)).command('ping')['pong'])
            self.assertEqual(len(ArianeClient(Path(path)).command('large')['data']), 300000)
        self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)


if __name__ == '__main__': unittest.main()
