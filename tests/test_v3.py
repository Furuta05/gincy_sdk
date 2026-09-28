import hashlib
import http.client
import json
import os
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, rsa

from gincy_sdk.cli import main
from gincy_sdk.deployment import rollback, snapshot
from gincy_sdk.live import RuntimeClient
from gincy_sdk.package import HEADER, PackageError, pack, read_package, transform_lua
from gincy_sdk.panel import make_server
from gincy_sdk.platforms import binary_info, initialize, server_binary

ROOT = Path(__file__).resolve().parents[1]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def keys(self):
        signer = ed25519.Ed25519PrivateKey.generate()
        recipient = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        for name, key in [('signer', signer), ('recipient', recipient)]:
            (self.root / (name + '.pem')).write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
            (self.root / (name + '.pub.pem')).write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        return signer

    def module(self):
        module = self.root / 'module'
        module.mkdir()
        (module / 'manifest.lua').write_text('return {id="protected",name="Protected",author="Test",version="1.0.0",gincy=">=1.0.0 <2.0.0",capabilities={},dependencies={}}')
        (module / 'sv_init.lua').write_text('MODULE.secret = "ORIGINAL_SOURCE_MARKER"\nfunction MODULE:Initialize(ctx) ctx:GetLogger():Info("ready") end\n')
        return module

    def test_protection_policy_and_native_decrypt(self):
        self.keys()
        module = self.module()
        package = pack(module, self.root / 'signer.pem', self.root / 'protected.gmod', False, self.root / 'recipient.pub.pem')
        data = package.read_bytes()
        self.assertNotIn(b'ORIGINAL_SOURCE_MARKER', data)
        result = read_package(package, self.root / 'signer.pub.pem')
        self.assertTrue(result['protected'])
        self.assertIs(result['manifest']['allow_unpack'], False)
        self.assertEqual(main(['unpack', str(package), '--public-key', str(self.root / 'signer.pub.pem'), '--output', str(self.root / 'unpacked')]), 1)
        changed = data.replace(b'"allow_unpack":false', b'"allow_unpack":true ')
        package.write_bytes(changed)
        with self.assertRaises(InvalidSignature):
            read_package(package, self.root / 'signer.pub.pem')
        package.write_bytes(data)
        verifier = os.environ.get('GINCY_PACKAGE_VERIFY')
        decryptor = os.environ.get('GINCY_PACKAGE_DECRYPT')
        if verifier:
            subprocess.run([verifier, str(package), str(self.root / 'signer.pub.pem')], check=True, capture_output=True)
        if decryptor:
            expected = hashlib.sha256(transform_lua((module / 'sv_init.lua').read_bytes())).hexdigest()
            result = subprocess.run([decryptor, str(package), str(self.root / 'signer.pub.pem'), str(self.root / 'recipient.pem'), expected], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_missing_recipient_fails_closed(self):
        self.keys()
        with self.assertRaises(PackageError):
            pack(self.module(), self.root / 'signer.pem', self.root / 'bad.gmod', False)

    def test_transformation_preserves_literals(self):
        value = b'-- remove\nlocal a="-- keep" local b=[=[-- keep\ntext]=] --[=[remove]=]\nreturn a,b'
        result = transform_lua(value)
        self.assertNotIn(b'remove', result)
        self.assertIn(b'"-- keep"', result)
        self.assertIn(b'[=[-- keep\ntext]=]', result)

    def test_files_really_restore(self):
        directory = self.root / 'modules'
        directory.mkdir()
        first = directory / 'one.lua'
        first.write_text('return 1')
        snapshot(self.root, 'first', 'MODULE_HOT_SWAP')
        first.write_text('return 2')
        (directory / 'extra.lua').write_text('return 3')
        snapshot(self.root, 'second', 'MODULE_HOT_SWAP')
        result = rollback(self.root)
        self.assertEqual(result['state'], 'ROLLED_BACK')
        self.assertEqual(first.read_text(), 'return 1')
        self.assertFalse((directory / 'extra.lua').exists())

    def test_corrupt_revision_does_not_modify_working_files(self):
        directory = self.root / 'content'
        directory.mkdir()
        active = directory / 'data.json'
        active.write_text('{"a":1}')
        snapshot(self.root, 'first', 'CONTENT_RELOAD')
        active.write_text('{"a":2}')
        snapshot(self.root, 'second', 'CONTENT_RELOAD')
        (self.root / '.gincy/history/000001/content/data.json').write_text('tampered')
        with self.assertRaises(PackageError):
            rollback(self.root)
        self.assertEqual(active.read_text(), '{"a":2}')

    def test_deployment_symlink_denied(self):
        (self.root / 'modules').mkdir()
        (self.root / 'modules/link').symlink_to('/etc/passwd')
        with self.assertRaises(PackageError):
            snapshot(self.root, 'unsafe', 'RESTART_REQUIRED')

    def test_binary_architectures(self):
        for bits, machine, expected in [(1, 3, 'x86'), (2, 62, 'x64')]:
            data = bytearray(64)
            data[:6] = b'\x7fELF' + bytes([bits, 1])
            struct.pack_into('<H', data, 18, machine)
            binary = self.root / 'srcds_linux'
            binary.write_bytes(data)
            self.assertEqual(binary_info(binary)['arch'], expected)
        binary.unlink()
        for machine, magic, expected in [(0x14c, 0x10b, 'x86'), (0x8664, 0x20b, 'x64')]:
            data = bytearray(256)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 60, 128)
            data[128:132] = b'PE\0\0'
            struct.pack_into('<H', data, 132, machine)
            struct.pack_into('<H', data, 152, magic)
            binary = self.root / 'srcds.exe'
            binary.write_bytes(data)
            self.assertEqual(server_binary(self.root)['arch'], expected)

    def test_initialization_is_idempotent(self):
        initialize(self.root)
        config = self.root / 'data/gincy/runtime.json'
        config.write_text('{"enabled":{"modern_hud":true}}')
        initialize(self.root)
        self.assertEqual(json.loads(config.read_text())['enabled'], {'modern_hud': True})
        self.assertFalse((self.root / 'data/gincy/database.json').exists())

    def test_runtime_transport_and_timeout(self):
        client = RuntimeClient(self.root, 'a' * 40)
        client.provision()
        def runtime():
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                requests = list((client.root / 'requests').glob('*.json'))
                if requests:
                    request = json.loads(requests[0].read_text())
                    self.assertEqual(request['operation'], 'status')
                    (client.root / 'responses' / requests[0].name).write_text(json.dumps({'ok': True, 'value': {'state': 'actual-test-runtime'}}))
                    return
                time.sleep(0.01)
        thread = threading.Thread(target=runtime)
        thread.start()
        self.assertEqual(client.call('status')['value']['state'], 'actual-test-runtime')
        thread.join()
        with self.assertRaises(PackageError):
            client.call('status', timeout=0.05)
        self.assertEqual(list((client.root / 'requests').glob('*.json')), [])

    def test_http_security(self):
        server = make_server(self.root, port=0, token='t' * 40)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        def request(path, payload, headers):
            connection = http.client.HTTPConnection('127.0.0.1', server.server_port)
            connection.request('POST', path, payload, headers)
            response = connection.getresponse()
            status = response.status
            response.read()
            connection.close()
            return status
        try:
            self.assertEqual(request('/api/execute', '{}', {'Content-Type': 'application/json'}), 401)
            headers = {'Authorization': 'Bearer ' + 't' * 40, 'Content-Type': 'application/json', 'Origin': 'https://attacker.invalid'}
            self.assertEqual(request('/api/execute', '{}', headers), 403)
            headers.pop('Origin')
            self.assertEqual(request('/api/execute', '{"operation":"eval"}', headers), 400)
            with self.assertRaises(PackageError):
                make_server(self.root, host='0.0.0.0', port=0, token='t' * 40)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
