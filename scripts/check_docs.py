#!/usr/bin/env python3
"""Check repository Markdown links, anchors, owner paths and discoverability.

Standard library only; no network, renderer build or GPU is used.
"""

from __future__ import annotations

import argparse
import html
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


OWNERS = (
    "docs/design/DESIGN_POLICY.md",
    "docs/design/MATERIAL_POLICY.md",
    "docs/design/RENDER_OUTPUT_POLICY.md",
    "docs/design/SHADOW_POLICY.md",
    "docs/design/WEBGPU_POLICY.md",
    "docs/design/INTEGRATION_SCOPE_POLICY.md",
    "docs/architecture/PROJECT_LAYOUT.md",
    "docs/reference/CAPABILITY_MATRIX.md",
    "docs/reference/SUPPORTED_CONFIGURATIONS.md",
    "docs/roadmap/README.md",
    "docs/roadmap/later.md",
    "docs/reports/renderer/README.md",
    "docs/releases/README.md",
    "docs/contributing/documentation.md",
    "CHANGELOG.md",
)
INLINE_LINK = re.compile(
    r'!?\[(?:\\.|[^\]\\])*\]\(\s*'
    r'(?P<url><[^>]+>|(?:\\.|[^\s()\\]|\([^()]*\))+)'
    r'(?:\s+(?:"[^"]*"|\x27[^\x27]*\x27))?\s*\)'
)
DEFINITION = re.compile(r'^ {0,3}\[([^\]]+)\]:\s*(<[^>]+>|\S+)', re.M)
REFERENCE = re.compile(r'!?\[([^\]]+)\](?:\[([^\]]*)\])?')


def prose(source: str) -> str:
    """Hide fenced examples while preserving line numbers."""
    result = []
    fence = None
    for line in source.splitlines(keepends=True):
        marker = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if fence:
            if marker and marker[1][0] == fence[0] and len(marker[1]) >= len(fence):
                fence = None
            result.append('\n' if line.endswith('\n') else '')
        elif marker:
            fence = marker[1]
            result.append('\n' if line.endswith('\n') else '')
        else:
            result.append(line)
    return ''.join(result)


def anchors(source: str) -> set[str]:
    """GitHub-style ATX heading slugs, including duplicate-heading suffixes."""
    found: set[str] = set()
    for match in re.finditer(r'^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$', prose(source), re.M):
        heading = re.sub(r'!?\[([^\]]+)\]\([^)]*\)', r'\1', match[1])
        heading = html.unescape(re.sub(r'<[^>]*>', '', heading)).lower()
        slug = ''.join(c for c in heading if c.isalnum() or c in '_- ')
        slug = slug.replace(' ', '-')
        unique = slug
        suffix = 0
        while unique in found:
            suffix += 1
            unique = f'{slug}-{suffix}'
        found.add(unique)
    found.update(re.findall(r'\b(?:id|name)=["\x27]([^"\x27]+)["\x27]', prose(source)))
    return found


def links(source: str) -> list[tuple[int, str]]:
    body = prose(source)
    body = re.sub(r'(`+)(.+?)\1', lambda m: ' ' * len(m[0]), body)
    found = [(m.start(), m['url']) for m in INLINE_LINK.finditer(body)]
    definitions = {m[1].casefold(): m[2] for m in DEFINITION.finditer(body)}
    found.extend((m.start(), m[2]) for m in DEFINITION.finditer(body))
    for match in REFERENCE.finditer(body):
        key = (match[2] or match[1]).casefold()
        if key in definitions:
            found.append((match.start(), definitions[key]))
    found.extend((m.start(), m[1]) for m in re.finditer(
        r'\b(?:href|src)=["\x27]([^"\x27]+)["\x27]', body))
    return [(body.count('\n', 0, offset) + 1, url.strip('<>')) for offset, url in found]


def check(root: Path) -> tuple[list[str], int]:
    root = root.resolve()
    pages = sorted(set(root.glob('*.md')) | set((root / 'docs').rglob('*.md'))
                   | set((root / '.github').rglob('*.md')))
    sources = {page: page.read_text(encoding='utf-8-sig') for page in pages}
    headings = {page: anchors(source) for page, source in sources.items()}
    edges: dict[Path, set[Path]] = {page: set() for page in pages}
    errors = []

    def error(page: Path, message: str) -> None:
        errors.append(f'{page.relative_to(root).as_posix()}: {message}')

    for page, source in sources.items():
        for line, destination in links(source):
            url = urlsplit(html.unescape(destination))
            if url.scheme or url.netloc:
                continue
            path = unquote(url.path)
            target = ((root / path.lstrip('/')) if path.startswith('/') else
                      (page.parent / path) if path else page).resolve()
            if not target.is_relative_to(root):
                error(page, f'{line}: link escapes repository: {destination}')
                continue
            if not target.exists():
                error(page, f'{line}: missing target: {destination}')
                continue
            if target.is_dir() and (target / 'README.md').is_file():
                target = target / 'README.md'
            if target in sources:
                edges[page].add(target)
            if url.fragment:
                if target.suffix.lower() == '.md' and target.is_file():
                    if target not in headings:
                        headings[target] = anchors(target.read_text(encoding='utf-8-sig'))
                    if unquote(url.fragment) not in headings[target]:
                        error(page, f'{line}: missing anchor: {destination}')
                elif target.is_dir():
                    error(page, f'{line}: directory has no anchor document: {destination}')

    index = root / 'docs/README.md'
    for owner in OWNERS:
        target = root / owner
        if not target.is_file():
            error(index, f'missing canonical owner: {owner}')
        elif target not in edges.get(index, set()):
            error(index, f'canonical owner is not linked: {owner}')

    # Each page must be listed by its nearest category index. Categories
    # without a README (e.g. design/) are indexed directly by docs/README.
    for page in pages:
        if not page.is_relative_to(root / 'docs') or page == index:
            continue
        parent = page.parent if page.name != 'README.md' else page.parent.parent
        while parent != root and not (parent / 'README.md').is_file():
            parent = parent.parent
        category = parent / 'README.md'
        if page not in edges.get(category, set()):
            error(page, f'not listed in category index {category.relative_to(root).as_posix()}')

    reached: set[Path] = set()
    pending = [root / 'README.md']
    while pending:
        page = pending.pop()
        if page not in reached:
            reached.add(page)
            pending.extend(edges.get(page, set()) - reached)
    for page in pages:
        if page.is_relative_to(root / 'docs') and page not in reached:
            error(page, 'orphan page: not reachable from README.md')
    return errors, len(pages)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors, count = check(args.root)
    if errors:
        print('\n'.join(errors))
        print(f'Documentation check failed: {len(errors)} error(s).')
        return 1
    print(f'Documentation check passed: {count} Markdown files; local links, anchors, owners and indexes.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
