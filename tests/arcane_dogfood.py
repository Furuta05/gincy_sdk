import json
import unittest
from pathlib import Path

from gincy_sdk.package import load_source


class ArcaneDogfood(unittest.TestCase):
    def test_public_api_only(self):
        root = Path(__file__).resolve().parents[1] / "examples/arcane_combat"
        manifest, entries = load_source(root)
        self.assertEqual(manifest["id"], "arcane_combat")
        self.assertGreaterEqual(len(entries), 3)
        source = "\n".join(data.decode() for _, _, data in entries)
        self.assertNotIn("Gincy.Internal", source)
        for token in ("Content:Register", "Network:Register", "Interactions:Register", "SetData"):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()
