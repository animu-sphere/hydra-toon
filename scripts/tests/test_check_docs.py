"""Regression checks for docs lint using disposable miniature repositories."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location('check_docs', Path(__file__).parents[1] / 'check_docs.py')
docs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docs)


class MarkdownTests(unittest.TestCase):
    def test_links_ignore_examples_and_read_references(self):
        source = '''# Page
`[example](missing.md)`
~~~md
[example](also-missing.md)
~~~
[real](target.md#part)
[other][ref]
[ref]: <with space.md>
'''
        found = {url for _, url in docs.links(source)}
        self.assertEqual(found, {'target.md#part', 'with space.md'})

    def test_heading_slugs_include_duplicates_and_unicode(self):
        source = '# A — `B`\n## A — B\n## 日本語\n```md\n# Hidden\n```\n'
        self.assertEqual(docs.anchors(source), {'a--b', 'a--b-1', '日本語'})


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        for name in docs.OWNERS:
            self.write(name, '# Page\n')
        self.write('README.md', '# Repository\n[Docs](docs/)\n')
        self.write('docs/README.md', '# Docs\n' + ''.join(
            f'[owner]({Path(name).relative_to("docs").as_posix() if name.startswith("docs/") else "../" + name})\n'
            for name in docs.OWNERS))
        self.write('docs/roadmap/README.md', '# Roadmap\n[v](v0.5.0.md)\n[later](later.md)\n')
        self.write('docs/reports/README.md', '# Reports\n[Renderer](renderer/)\n')
        with (self.root / 'docs/README.md').open('a', encoding='utf-8') as stream:
            stream.write('[Reports](reports/)\n')

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')

    def errors(self):
        return '\n'.join(docs.check(self.root)[0])

    def test_valid_repository_and_encoded_anchor(self):
        self.write('docs/roadmap/later.md', '# Later\n## 日本語\n[local](#%E6%97%A5%E6%9C%AC%E8%AA%9E)\n')
        self.assertEqual(self.errors(), '')

    def test_missing_local_target_and_anchor(self):
        self.write('docs/roadmap/later.md', '# Later\n[bad](missing.md)\n[bad](v0.5.0.md#absent)\n')
        errors = self.errors()
        self.assertIn('missing target: missing.md', errors)
        self.assertIn('missing anchor: v0.5.0.md#absent', errors)

    def test_missing_and_unlinked_canonical_owners(self):
        (self.root / 'docs/design/MATERIAL_POLICY.md').unlink()
        index = self.root / 'docs/README.md'
        index.write_text(index.read_text(encoding='utf-8').replace(
            '[owner](design/DESIGN_POLICY.md)\n', ''), encoding='utf-8')
        errors = self.errors()
        self.assertIn('missing canonical owner: docs/design/MATERIAL_POLICY.md', errors)
        self.assertIn('canonical owner is not linked: docs/design/DESIGN_POLICY.md', errors)

    def test_unindexed_cycle_is_still_orphaned(self):
        self.write('docs/design/one.md', '# One\n[two](two.md)\n')
        self.write('docs/design/two.md', '# Two\n[one](one.md)\n')
        errors = self.errors()
        self.assertIn('not listed in category index docs/README.md', errors)
        self.assertIn('docs/design/one.md: orphan page', errors)
        self.assertIn('docs/design/two.md: orphan page', errors)

    def test_links_must_stay_in_repository(self):
        self.write('README.md', '# Repository\n[escape](../outside.md)\n[Docs](docs/)\n')
        self.assertIn('link escapes repository', self.errors())


if __name__ == '__main__':
    unittest.main()
