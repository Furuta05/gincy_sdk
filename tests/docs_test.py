import importlib.util
import unittest
from html.parser import HTMLParser
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class Page(HTMLParser):
    def __init__(self,text):
        super().__init__()
        self.ids=[]
        self.links=[]
        self.feed(text)
    def handle_starttag(self,tag,attrs):
        attrs=dict(attrs)
        if 'id' in attrs: self.ids.append(attrs['id'])
        if 'href' in attrs: self.links.append(attrs['href'])

class DocsTests(unittest.TestCase):
    def test_generated_document_matches_source(self):
        spec=importlib.util.spec_from_file_location('docs_build',ROOT/'docs-src/build.py')
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual((ROOT/'docs-site/index.html').read_text(encoding='utf-8'),module.render())
    def test_offline_anchors(self):
        page=Page((ROOT/'docs-site/index.html').read_text(encoding='utf-8'))
        self.assertEqual(len(page.ids),len(set(page.ids)))
        self.assertGreater(len(page.links),10)
        for link in page.links:
            self.assertTrue(link.startswith('#'))
            self.assertIn(link[1:],page.ids)
    def test_release_boundaries_visible(self):
        text=(ROOT/'docs-src/manual.md').read_text(encoding='utf-8')
        for value in ('CompileString','PostgreSQL','legacy_rpg','gincy install','gincy module','gincy rollback','GINCY_MANAGEMENT_TOKEN','SRCDS','RegisterStream','quantum_fishing','gincy doctor'):
            self.assertIn(value,text)
        self.assertIn('3.1.0',(ROOT/'README.md').read_text())

if __name__=='__main__': unittest.main()
