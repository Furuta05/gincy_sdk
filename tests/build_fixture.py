import os
import subprocess
import tempfile
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from gincy_sdk.package import pack, read_package
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    key = Ed25519PrivateKey.generate()
    private = Path(directory) / 'build.pem'
    private.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    public = root / 'examples/example-signing.pub.pem'
    public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    (root / 'examples/arcane-signing.pub.pem').write_bytes(public.read_bytes())
    output = pack(root / 'examples/gincy_survival_example', private, root / 'examples/dist/gincy_survival_example.gmod')
    if os.environ.get('GINCY_PACKAGE_VERIFY'):
        subprocess.run([os.environ['GINCY_PACKAGE_VERIFY'], str(output), str(public)], check=True)
    pack(root / 'examples/arcane_combat', private, root / 'examples/dist/arcane_combat.gmod')
    package = read_package(output, public)
    def lua(value):
        if isinstance(value, str):
            return '"' + ''.join('\\%03d' % b for b in value.encode()) + '"'
        if isinstance(value, dict):
            return '{' + ','.join('['+lua(k)+']='+lua(v) for k,v in value.items()) + '}'
        if isinstance(value, list):
            return '{' + ','.join(lua(v) for v in value) + '}'
        if isinstance(value, bool):
            return 'true' if value else 'false'
        return str(value)
    fixture = {'manifest':package['manifest'],'files':[{'path':p,'realm':r,'data':d.decode()} for p,r,d in package['entries']], 'digest':package['digest'],'signer':package['signer'],'verified':True}
    (root / 'tests/package_fixture.lua').write_text('return '+lua(fixture)+'\n')
