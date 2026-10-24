"""The catalog: machines published from their own repositories, and every identity
reserved for one.

catalog/machines.json lists each machine once, for good:

- reserved: an ID and identity held for a machine being written;
- published: also a repository, the release tag and its commit, and the
  machine's folder in that repository. Builds fetch exactly that commit;
- retired: no longer built, but its ID and identity are never given to
  another machine, so old projects never load a different one.

Bundled machines (machines/) hold their IDs and identities the same way.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from .definitions import stock_name
from .formats import require
from .machines import COLLECTION, check_identity, discovered, machine_at, read_collection
from .pipeline import NO_WINDOW, ROOT, config

CATALOG = ROOT / 'catalog/machines.json'
# Fetched machine sources: third-party code, never firmware, so it may live in local/.
CACHE = ROOT / 'local/catalog'
STATUSES = ('reserved', 'published', 'retired')
COMMON = {'id', 'kind', 'identity', 'maintainer', 'status'}
RELEASE = {'repository', 'tag', 'commit', 'path'}
REPOSITORY = re.compile(r'https://github\.com/[A-Za-z0-9-]+/[A-Za-z0-9._-]+')


def identity_key(kind, identity):
    """What an identity occupies: its engine or effect ID, and a synth's own page."""
    keys = {('effect', identity['effect'])} if kind == 'effect' else {('engine', identity['engine'])}
    if kind == 'synth':
        keys.add(('page', identity['page']))
    return keys


def validate(value, bundled=None):
    """A catalog document's entries, checked against each other and the bundled machines."""
    require(isinstance(value, dict) and set(value) == {'schema', 'machines'} and value['schema'] == 1 and
            isinstance(value['machines'], list), 'Unsupported catalog')
    rules = config()['identities']
    bundled = discovered() if bundled is None else bundled
    names = {m.id: 'bundled' for m in bundled}
    held = {key: m.id for m in bundled for key in identity_key(m.kind, m.identity)}
    for entry in value['machines']:
        require(isinstance(entry, dict) and isinstance(entry.get('id'), str), 'Catalog entries need an id')
        name = entry['id']
        status = entry.get('status')
        require(status in STATUSES, f'{name}: status is one of {", ".join(STATUSES)}')
        required = COMMON | (RELEASE if status == 'published' else set())
        allowed = COMMON | RELEASE if status == 'retired' else required
        require(required <= set(entry) <= allowed, f'{name}: catalog fields for a {status} machine: '
                + ', '.join(sorted(required)))
        stock_name(name)
        require(name not in names, f'{name}: the ID is already {names.get(name)}')
        names[name] = 'listed in the catalog'
        require(entry['kind'] in ('synth', 'sample', 'effect'), f'{name}: unknown kind')
        check_identity(rules, entry['kind'], entry['identity'])
        for key in identity_key(entry['kind'], entry['identity']):
            require(key not in held, f'{name}: {key[0]} {key[1]} is held by {held.get(key)}')
            held[key] = name
        require(isinstance(entry['maintainer'], str) and re.fullmatch(r'[A-Za-z0-9-]{1,39}', entry['maintainer']),
                f'{name}: maintainer is a GitHub user name')
        if 'repository' in entry:
            require(isinstance(entry['repository'], str) and REPOSITORY.fullmatch(entry['repository']),
                    f'{name}: repository is https://github.com/<owner>/<repo>')
            require(isinstance(entry['commit'], str) and re.fullmatch(r'[0-9a-f]{40}', entry['commit']),
                    f'{name}: commit is the full SHA of the tagged commit')
            require(isinstance(entry['tag'], str) and re.fullmatch(r'[A-Za-z0-9._/-]{1,64}', entry['tag']),
                    f'{name}: invalid tag')
            path = entry['path']
            require(isinstance(path, str) and re.fullmatch(r'[A-Za-z0-9_./-]+', path) and
                    '..' not in path.split('/') and not path.startswith('/'),
                    f'{name}: path is the machine folder inside the repository, such as "." or "machines/<id>"')
    return value['machines']


# A machine's status only moves forward; retired is final.
MOVES = {'reserved': {'reserved', 'published', 'retired'}, 'published': {'published', 'retired'},
         'retired': {'retired'}}


def changes(before, after):
    """An updated catalog keeps every machine it held, with its kind and identity, and only
    moves a status forward."""
    current = {e['id']: e for e in after}
    for entry in before:
        name = entry['id']
        require(name in current, f'{name}: catalog entries are never removed; retire the machine instead')
        now = current[name]
        require(now['kind'] == entry['kind'] and now['identity'] == entry['identity'],
                f'{name}: a published ID keeps its kind and identity; release a new machine instead')
        require(now['status'] in MOVES[entry['status']], f"{name}: {entry['status']} cannot become {now['status']}")


def load(path=CATALOG):
    return validate(json.loads(Path(path).read_text(encoding='utf-8')))


def held_identities(path=CATALOG):
    """Every identity the catalog holds, whatever its status, by kind."""
    return [(entry['kind'], entry['identity']) for entry in load(path)]


def git(*arguments, cwd=None):
    result = subprocess.run(['git', *arguments], cwd=cwd, capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW)
    require(result.returncode == 0, 'git ' + ' '.join(arguments[:2]) + ' failed: ' + result.stderr.strip())
    return result.stdout


def tag_commit(repository, tag):
    """The commit a remote tag names, peeled through an annotated tag."""
    lines = git('ls-remote', '--tags', repository, f'refs/tags/{tag}', f'refs/tags/{tag}^{{}}').split('\n')
    found = dict(line.split('\t')[::-1] for line in lines if '\t' in line)
    return found.get(f'refs/tags/{tag}^{{}}') or found.get(f'refs/tags/{tag}')


def fetch(entry, cache=CACHE):
    """One published machine's folder at exactly its catalog commit, checked against its entry."""
    folder = Path(cache) / f"{entry['id']}-{entry['commit'][:12]}"
    if not (folder / '.git').is_dir():
        folder.mkdir(parents=True, exist_ok=True)
        git('init', '--quiet', cwd=folder)
        git('fetch', '--quiet', '--depth', '1', entry['repository'], entry['commit'], cwd=folder)
        git('-c', 'advice.detachedHead=false', 'checkout', '--quiet', 'FETCH_HEAD', cwd=folder)
    require(git('rev-parse', 'HEAD', cwd=folder).strip() == entry['commit'],
            f"{entry['id']}: the fetched commit differs from the catalog's")
    require(not git('status', '--porcelain', '--ignored', cwd=folder).strip(),
            f"{entry['id']}: the fetched folder {folder} was changed; delete it to fetch again")
    m = machine_at(folder / entry['path'])
    require(m.id == entry['id'] and m.kind == entry['kind'] and m.identity == entry['identity'],
            f"{entry['id']}: the machine at the pinned commit differs from its catalog entry")
    return m


def pinned(m, path=CATALOG):
    """Whether a machine folder is a published catalog machine at its pinned commit, unchanged."""
    entry = next((e for e in load(path) if e['id'] == m.id and e['status'] == 'published'), None)
    if entry is None:
        return False
    root = m.root
    while root != root.parent and not (root / '.git').is_dir():
        root = root.parent
    try:
        return (git('rev-parse', 'HEAD', cwd=root).strip() == entry['commit'] and
                not git('status', '--porcelain', '--ignored', cwd=root).strip() and
                m.root == (root / entry['path']).resolve())
    except ValueError:
        return False


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py catalog', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    command = parser.add_subparsers(dest='command', required=True)
    checking = command.add_parser('check', help='validate catalog/machines.json against itself and the bundled machines')
    checking.add_argument('--base', help='a git revision to compare with, such as the pull request target')
    fetching = command.add_parser('fetch', help='fetch every published machine at its pinned commit')
    testing = command.add_parser('test', help='fetch, then check every published machine with sdk.py test')
    for sub in (fetching, testing):
        sub.add_argument('--cache', type=Path, default=CACHE, help='fetched machines (default: local/catalog)')
        sub.add_argument('--tags', action='store_true', help='also confirm each release tag still names its commit')
    testing.add_argument('--cxx', default='clang++', help='Clang++ with wasm32/wasm-ld, for synth previews')
    testing.add_argument('--host-cxx', default='c++', help="host C++11 compiler for each machine's tests")
    args = parser.parse_args(argv)
    try:
        entries = load()
        if args.command == 'check':
            if args.base:
                # A base without a catalog held nothing yet.
                listed = git('ls-tree', '--name-only', args.base, 'catalog/machines.json', cwd=ROOT).strip()
                before = json.loads(git('show', f'{args.base}:catalog/machines.json', cwd=ROOT)) if listed else None
                changes(validate(before) if before else [], entries)
            print(f'PASS: {len(entries)} catalog entr{"y" if len(entries) == 1 else "ies"}; '
                  'IDs and identities are unique, including against the bundled machines.')
            return 0
        published = [e for e in entries if e['status'] == 'published']
        folders = []
        for entry in published:
            if args.tags:
                require(tag_commit(entry['repository'], entry['tag']) == entry['commit'],
                        f"{entry['id']}: tag {entry['tag']} no longer names the catalog's commit")
            m = fetch(entry, args.cache)
            folders.append(m.root)
            print(f"Fetched {m.id} {entry['tag']} ({entry['commit'][:12]}): {m.root}")
        # The bundled machines, then every published machine: a collection for any build command.
        collection = Path(args.cache) / 'collection.json'
        entries = read_collection(COLLECTION) + [{'path': os.path.relpath(f, Path(args.cache).resolve())}
                                                 for f in folders]
        collection.parent.mkdir(parents=True, exist_ok=True)
        collection.write_text(json.dumps({'schema': 1, 'machines': entries}, indent=2) + '\n', encoding='utf-8')
        print(f'Collection: {collection} (pass it to build, compile or kit with --collection)')
        if args.command == 'test' and folders:
            from .machine_test import main as test
            return test([*(f'--machine={folder}' for folder in folders), '--cxx', args.cxx,
                         '--host-cxx', args.host_cxx])
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
