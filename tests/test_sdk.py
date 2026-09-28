import hashlib
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from gincy_sdk.package import HEADER, ENTRY, ManifestParser, PackageError, load_source, pack, read_package
from gincy_sdk.cli import main

ROOT = Path(__file__).resolve().parents[1]

class SDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.key = Ed25519PrivateKey.generate()
        self.private = self.root / 'key.pem'
        self.public = self.root / 'key.pub.pem'
        self.private.write_bytes(self.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.public.write_bytes(self.key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        self.package = pack(ROOT / 'examples/gincy_survival_example', self.private, self.root / 'survival.gmod')

    def tearDown(self):
        self.temp.cleanup()

    def native(self, path, expected):
        binary = os.environ.get('GINCY_PACKAGE_VERIFY')
        if binary:
            result = subprocess.run([binary, str(path), str(self.public)], capture_output=True)
            self.assertEqual(result.returncode == 0, expected, result.stderr.decode())

    def test_roundtrip_native_and_reproducible(self):
        result = read_package(self.package, self.public)
        self.assertEqual(result['signature'], 'VALID')
        self.assertEqual(result['manifest']['id'], 'survival_example')
        self.assertEqual(len(result['entries']), 4)
        self.native(self.package, True)
        second = pack(ROOT / 'examples/gincy_survival_example', self.private, self.root / 'second.gmod')
        self.assertEqual(self.package.read_bytes(), second.read_bytes())
        self.assertEqual(main(['unpack', str(self.package), '--public-key', str(self.public), '--output', str(self.root / 'source')]), 0)
        manifest, files = load_source(self.root / 'source')
        self.assertEqual(len(files), 4)
        self.assertEqual(manifest['id'], 'survival_example')

    def test_tamper_and_truncation(self):
        original = self.package.read_bytes()
        for index in (0, 8, 12, 28, 64, len(original)//2, len(original)-1):
            data = bytearray(original)
            data[index] ^= 1
            path = self.root / 'tampered.gmod'
            path.write_bytes(data)
            with self.assertRaises(Exception):
                read_package(path, self.public)
            self.native(path, False)
        for length in (0, 16, 59, 124, len(original)-1):
            path = self.root / 'truncated.gmod'
            path.write_bytes(original[:length])
            with self.assertRaises(Exception):
                read_package(path, self.public)
            self.native(path, False)

    def test_signed_bad_hash_and_path(self):
        data = bytearray(self.package.read_bytes()[:-64])
        metadata_size = HEADER.unpack_from(data)[3]
        offset = HEADER.size + metadata_size
        data[offset + 12] ^= 1
        target = self.root / 'bad-hash.gmod'
        target.write_bytes(data + self.key.sign(bytes(data)))
        with self.assertRaises(PackageError):
            read_package(target, self.public)
        self.native(target, False)
        data = bytearray(self.package.read_bytes()[:-64])
        data[offset + ENTRY.size:offset + ENTRY.size + 3] = b'../'
        target.write_bytes(data + self.key.sign(bytes(data)))
        with self.assertRaises(PackageError):
            read_package(target, self.public)
        self.native(target, False)

    def test_untrusted(self):
        other = Ed25519PrivateKey.generate().public_key()
        self.public.write_bytes(other.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaises(PackageError):
            read_package(self.package, self.public)
        self.native(self.package, False)

    def test_literal_manifest(self):
        for source in ('return os.execute("id")', 'return {id="a",id="b"}', 'return {id=function() end}', 'return {id="a"} print("x")'):
            with self.assertRaises((PackageError, IndexError)):
                ManifestParser(source).parse()

    def test_first_addon(self):
        previous = Path.cwd()
        os.chdir(self.root)
        try:
            self.assertEqual(main(['init', 'hello']), 0)
            manifest, files = load_source('hello')
            self.assertEqual(manifest['id'], 'hello')
            path = pack('hello', self.private, 'hello.gmod')
            self.native(path.resolve(), True)
        finally:
            os.chdir(previous)

if __name__ == '__main__':
    unittest.main()
