"""The catalog: machines and extensions published from their own repositories, by ID.

catalog/machines.json lists each machine once, for good, and catalog/extensions.json
each firmware extension, in the same format (kind "extension", no cpu):

- reserved: an ID held for a machine being written;
- draft: a release pinned for review, like a published one, but not built yet;
- published: also a repository, the release tag and its commit, and the
  machine's folder in that repository. Builds fetch exactly that commit;
- retired: no longer built, but its ID is never given to another machine.

Open-XY's own machines are catalog entries like any other. A machine's engine,
effect and page numbers are not the catalog's: each firmware's layout gives them
(tooling.machines.assign_numbers).
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

from .definitions import stock_name
from . import cpu_measurement
from .formats import require
from .machines import machine_at, relative_path
from .pipeline import NO_WINDOW, ROOT

CATALOG = ROOT / 'catalog/machines.json'
EXTENSION_CATALOG = ROOT / 'catalog/extensions.json'
# Each catalog file's list and the kinds its entries may have.
KINDS = {'machines': ('synth', 'sample', 'effect', 'player'), 'extensions': ('extension',)}
# Fetched machine sources: third-party code, never firmware, so it may live in local/.
CACHE = ROOT / 'local/catalog'
STATUSES = ('reserved', 'draft', 'published', 'retired')
COMMON = {'id', 'kind', 'maintainer', 'status'}
RELEASE = {'repository', 'tag', 'commit', 'path'}
# A release's fields; builder/core.mjs validSource checks kits with the same patterns
# (tests/test_catalog.py compares them).
REPOSITORY = r'https://github\.com/[A-Za-z0-9-]+/(?!\.\.?$)[A-Za-z0-9._-]+'
TAG = r'[A-Za-z0-9._/-]{1,64}'
COMMIT = r'[0-9a-f]{40}'
MAINTAINER = r'[A-Za-z0-9-]{1,39}'
# Catalog machines that are Open-XY's own rather than community ones, by
# ID. Maintainers keep this list; a catalog entry has no field to claim it, and
# who hosts the repository does not decide it.
OFFICIAL = ('fm2op', 'macroosc', 'karplus', 'timestretch', 'ringmod', 'pingpong')
OFFICIAL_EXTENSIONS = ('capture',)


def official(entry):
    """Whether a catalog entry is one of Open-XY's own machines or extensions."""
    return entry['id'] in (OFFICIAL_EXTENSIONS if entry.get('kind') == 'extension' else OFFICIAL)


def validate(value, listed='machines'):
    """A catalog document's entries, checked against each other. `listed` is its
    list: machines, or extensions."""
    require(isinstance(value, dict) and set(value) == {'schema', listed} and value['schema'] == 1 and
            isinstance(value[listed], list), 'Unsupported catalog')
    names = {}
    for entry in value[listed]:
        require(isinstance(entry, dict) and isinstance(entry.get('id'), str), 'Catalog entries need an id')
        name = entry['id']
        status = entry.get('status')
        require(status in STATUSES, f'{name}: status is one of {", ".join(STATUSES)}')
        required = COMMON | (RELEASE if status in ('draft', 'published') else set())
        # A retired machine keeps its whole last release, or none of it.
        if status == 'retired' and RELEASE & set(entry):
            required = COMMON | RELEASE
        if 'cpu' in entry:
            require(listed == 'machines' and entry.get('kind') != 'player', f'{name}: extensions and players carry no audio cpu measurement')
            require('commit' in entry and status != 'reserved', f'{name}: cpu needs a pinned release')
            cpu_measurement.validate(entry['cpu'], entry.get('kind'))
            required = required | {'cpu'}
        require(set(entry) == required, f'{name}: catalog fields for a {status} machine: '
                + ', '.join(sorted(required)))
        if listed == 'machines':
            stock_name(name)
        else:
            from .extensions import NAME, RESERVED
            require(NAME.fullmatch(name) and name not in RESERVED, f'{name}: invalid extension ID')
        require(name not in names, f'{name}: the ID is already {names.get(name)}')
        names[name] = 'listed in the catalog'
        require(entry['kind'] in KINDS[listed], f'{name}: unknown kind')
        require(isinstance(entry['maintainer'], str) and re.fullmatch(MAINTAINER, entry['maintainer']),
                f'{name}: maintainer is a GitHub user name')
        if 'repository' in entry:
            require(isinstance(entry['repository'], str) and re.fullmatch(REPOSITORY, entry['repository']),
                    f'{name}: repository is https://github.com/<owner>/<repo>')
            require(isinstance(entry['commit'], str) and re.fullmatch(COMMIT, entry['commit']),
                    f'{name}: commit is the full SHA of the tagged commit')
            require(isinstance(entry['tag'], str) and re.fullmatch(TAG, entry['tag']),
                    f'{name}: invalid tag')
            require(relative_path(entry['path']),
                    f'{name}: path is the {listed[:-1]} folder inside the repository, such as "." or "{listed}/<id>"')
    return value[listed]


# A machine's status moves forward, except that a published machine may go back to
# draft until it is ready, keeping its ID; retired is final.
MOVES = {'reserved': {'reserved', 'draft', 'published', 'retired'},
         'draft': {'draft', 'published', 'retired'}, 'published': {'published', 'draft', 'retired'},
         'retired': {'retired'}}


def changes(before, after, allow_carried_cpu=False):
    """An updated catalog keeps every machine it held, with its kind, and only moves a
    status forward. A catalog maintainer may let an update keep its previous cpu record."""
    current = {e['id']: e for e in after}
    previous = {e['id']: e for e in before}
    for entry in after:
        # Extensions run on the controller path, not in audio: they carry no cpu record.
        if entry['kind'] in ('extension', 'player'):
            continue
        if entry['status'] in ('draft', 'published') and entry != previous.get(entry['id']):
            require('cpu' in entry, f"{entry['id']}: new or updated submissions require hardware cpu measurements (docs/profiling.md)")
            # With a maintainer's approval, an update may carry its previous measurement over
            # unchanged, still naming the commit it measured; otherwise it measures the pinned commit.
            measured = lambda cpu: {k: v for k, v in cpu.items() if k != 'report'} if isinstance(cpu, dict) else cpu
            unchanged = measured(entry['cpu']) == measured(previous.get(entry['id'], {}).get('cpu'))
            # Carrying a record over takes the record exactly as it was, evidence link included.
            carried = allow_carried_cpu and entry['cpu'] == previous.get(entry['id'], {}).get('cpu')
            # A fresh synth measurement is a control sweep; an unchanged one may predate it.
            cpu_measurement.validate(entry['cpu'], entry['kind'], legacy=unchanged)
            require(carried or entry['cpu']['commit'] == entry['commit'],
                    f"{entry['id']}: cpu must measure the pinned commit; remeasure when updating "
                    "(a catalog maintainer can approve keeping the previous measurement)")
    for entry in before:
        name = entry['id']
        require(name in current, f'{name}: catalog entries are never removed; retire the machine instead')
        now = current[name]
        require(now['kind'] == entry['kind'], f'{name}: a published ID keeps its kind; release a new machine instead')
        # A new repository or maintainer is a handover, which a maintainer of the
        # catalog approves on its own, never as part of an update.
        for key in ('repository', 'maintainer'):
            require(key not in entry or now.get(key, entry[key]) == entry[key],
                    f'{name}: {key} changed; hand a machine over in a pull request of its own, approved by a catalog maintainer')
        require(now['status'] in MOVES[entry['status']], f"{name}: {entry['status']} cannot become {now['status']}")


def control_changes(before, after):
    """How a release changes a machine's parameters, which projects save by index: changes
    that alter what a saved value means, and notes on those that do not."""
    changed, notes = [], []
    ids = [p['id'] for p in after]
    for index, old in enumerate(before):
        name = f"parameter {index} ({old['id']})"
        new = after[index] if index < len(after) else None
        if new is None or new['id'] != old['id']:
            if old['id'] in ids:
                changed.append(f"{name}: moved to parameter {ids.index(old['id'])}")
            elif new is not None and new['id'] not in {p['id'] for p in before}:
                changed.append(f"{name}: id changed to {new['id']}")
            else:
                changed.append(f'{name}: removed')
            continue
        if new['mapping'] != old['mapping']:
            changed.append(f"{name}: mapping changed from {json.dumps(old['mapping'])} to {json.dumps(new['mapping'])}")
        if new.get('control') != old.get('control'):
            changed.append(f"{name}: control position changed from {old.get('control')} to {new.get('control')}")
        for key in ('label', 'display', 'default'):
            if new[key] != old[key]:
                notes.append(f'{name}: {key} {old[key]} -> {new[key]}')
    notes += [f"parameter {index} ({p['id']}): added" for index, p in enumerate(after) if index >= len(before)]
    return changed, notes


def release_controls(before, after, cache=CACHE, allow=False):
    """Fetches both releases of every published machine whose commit moved and compares
    their parameters. Control changes fail unless `allow` (a catalog maintainer's approval);
    returns the lines to report."""
    previous = {e['id']: e for e in before if 'commit' in e}
    report, refused = [], []
    for entry in after:
        old = previous.get(entry['id'])
        if (entry['kind'] == 'extension' or entry['status'] != 'published' or old is None or
                old['commit'] == entry['commit']):
            continue
        changed, notes = control_changes(fetch(old, cache).definition['parameters'],
                                         fetch(entry, cache).definition['parameters'])
        release = f"{entry['id']} {old['tag']} -> {entry['tag']}"
        report += [f'{release}: {line}' for line in changed + notes]
        refused += [f'{release}: {line}' for line in changed]
    require(allow or not refused, 'saved projects would load different controls:\n  ' + '\n  '.join(refused) +
            '\nKeep each parameter at its index with its id and mapping, or have a catalog maintainer '
            'approve the change with the controls-changed label')
    return report


def load(path=CATALOG):
    return validate(json.loads(Path(path).read_text(encoding='utf-8')))


def load_extensions(path=EXTENSION_CATALOG):
    """The extension catalog's entries; none before it exists."""
    path = Path(path)
    return validate(json.loads(path.read_text(encoding='utf-8')), 'extensions') if path.exists() else []


# Remote machine repositories are public: never ask for credentials, which would
# wait invisibly for input, and check files out exactly as committed on every host.
GIT_ENVIRONMENT = dict(os.environ, GIT_TERMINAL_PROMPT='0', GCM_INTERACTIVE='never')
GIT_SETTINGS = ('-c', 'credential.helper=', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf')


def git(*arguments, cwd=None):
    result = subprocess.run(['git', *GIT_SETTINGS, *arguments], cwd=cwd, capture_output=True, text=True,
                            timeout=600, env=GIT_ENVIRONMENT, creationflags=NO_WINDOW)
    require(result.returncode == 0, 'git ' + ' '.join(arguments[:2]) + ' failed: ' + result.stderr.strip())
    return result.stdout


def tag_commit(repository, tag):
    """The commit a remote tag names, peeled through an annotated tag."""
    lines = git('ls-remote', '--tags', repository, f'refs/tags/{tag}', f'refs/tags/{tag}^{{}}').split('\n')
    found = dict(line.split('\t')[::-1] for line in lines if '\t' in line)
    return found.get(f'refs/tags/{tag}^{{}}') or found.get(f'refs/tags/{tag}')


def fetch(entry, cache=CACHE):
    """One published machine's or extension's folder at exactly its catalog commit,
    checked against its entry."""
    extension = entry['kind'] == 'extension'
    folder = Path(cache) / f"{'extension-' if extension else ''}{entry['id']}-{entry['commit'][:12]}"
    if folder.exists() and not head(folder):
        shutil.rmtree(folder)  # an interrupted fetch: start again
    if not folder.exists():
        partial = folder.with_name(folder.name + '.partial')
        shutil.rmtree(partial, ignore_errors=True)
        partial.mkdir(parents=True)
        git('init', '--quiet', cwd=partial)
        git('fetch', '--quiet', '--depth', '1', entry['repository'], entry['commit'], cwd=partial)
        git('-c', 'advice.detachedHead=false', 'checkout', '--quiet', 'FETCH_HEAD', cwd=partial)
        partial.rename(folder)
    require(head(folder) == entry['commit'], f"{entry['id']}: the fetched commit differs from the catalog's")
    require(untouched(folder), f"{entry['id']}: the fetched folder {folder} was changed; delete it to fetch again")
    if extension:
        from .extensions import extension_at
        e = extension_at(folder / entry['path'])
        require(e.id == entry['id'], f"{entry['id']}: the extension at the pinned commit differs from its catalog entry")
        return e
    m = machine_at(folder / entry['path'])
    require(m.id == entry['id'] and m.kind == entry['kind'],
            f"{entry['id']}: the machine at the pinned commit differs from its catalog entry")
    return m


def remove_tree(folder):
    """Deletes a fetched folder, including the files git keeps read-only on Windows."""
    def writable(function, path, _info):
        os.chmod(path, stat.S_IWRITE)
        function(path)
    shutil.rmtree(folder, onerror=writable)


def prune(cache, kept):
    """Deletes the cache's fetched folders that hold none of `kept`; their names."""
    cache = Path(cache).resolve()
    keep = {Path(folder).resolve().relative_to(cache).parts[0] for folder in kept}
    removed = [folder for folder in sorted(cache.iterdir()) if folder.is_dir() and folder.name not in keep]
    for folder in removed:
        remove_tree(folder)
    return [folder.name for folder in removed]


def head(folder):
    """The commit a git folder has checked out, or None."""
    try:
        return git('rev-parse', '--verify', '--quiet', 'HEAD', cwd=folder).strip() or None
    except ValueError:
        return None


def untouched(folder):
    """No file differs from the commit, none is added or ignored, and none is hidden from
    git status by assume-unchanged or skip-worktree."""
    flags = [line[:1] for line in git('ls-files', '-v', cwd=folder).splitlines()]
    return (not git('status', '--porcelain', '--ignored', cwd=folder).strip() and
            all(flag == 'H' for flag in flags))


def pinned(m, entries):
    """Whether a machine or extension folder is a published catalog entry (of `entries`,
    from load() or load_extensions()) at its pinned commit, unchanged."""
    entry = next((e for e in entries if e['id'] == m.id and e['status'] == 'published'), None)
    if entry is None:
        return False
    root = m.root
    while root != root.parent and not (root / '.git').exists():
        root = root.parent
    try:
        return head(root) == entry['commit'] and untouched(root) and m.root == (root / entry['path']).resolve()
    except (ValueError, OSError, subprocess.SubprocessError):
        return False


def base_entries(base, listed, path):
    """A catalog file's entries at a git revision; a revision without the file held none."""
    name = path.relative_to(ROOT).as_posix()
    present = git('ls-tree', '--name-only', base, name, cwd=ROOT).strip()
    before = json.loads(git('show', f'{base}:{name}', cwd=ROOT)) if present else None
    if not before:
        return []
    # Entries before layouts gave machine numbers also listed an identity.
    for entry in before.get(listed, []):
        if isinstance(entry, dict):
            entry.pop('identity', None)
    return validate(before, listed)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py catalog', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    command = parser.add_subparsers(dest='command', required=True)
    checking = command.add_parser('check', help='validate catalog/machines.json and catalog/extensions.json')
    checking.add_argument('--base', help='a git revision to compare with, such as the pull request target')
    checking.add_argument('--allow-control-changes', action='store_true',
                          help="accept releases that change what saved parameter values mean (a catalog maintainer's approval)")
    checking.add_argument('--allow-carried-cpu', action='store_true',
                          help="accept updates that keep their previous cpu measurement (a catalog maintainer's approval)")
    checking.add_argument('--cache', type=Path, default=CACHE, help='fetched machines (default: local/catalog)')
    fetching = command.add_parser('fetch', help='fetch every published machine and extension at its pinned commit')
    testing = command.add_parser('test', help='fetch, then check every published machine and extension with sdk.py test')
    for sub in (fetching, testing):
        sub.add_argument('--cache', type=Path, default=CACHE, help='fetched machines (default: local/catalog)')
        sub.add_argument('--tags', action='store_true', help='also confirm each release tag still names its commit')
        sub.add_argument('--prune', action='store_true',
                         help='then delete fetched folders the catalog no longer pins, such as earlier releases')
    testing.add_argument('--cxx', default='clang++', help='Clang++ with wasm32/wasm-ld, for synth previews')
    testing.add_argument('--host-cxx', default='c++', help="host C++11 compiler for each machine's tests")
    testing.add_argument('--changed-from', metavar='REVISION',
                         help='test only the entries that differ from the catalog at this git revision '
                              '(every entry is still fetched and in the collection)')
    testing.add_argument('--jobs', type=int, default=1, help='folders checked at once (default: 1)')
    testing.add_argument('--no-cpu', action='store_true', help='skip the CPU estimates')
    args = parser.parse_args(argv)
    try:
        catalogs = (('machines', CATALOG, load()), ('extensions', EXTENSION_CATALOG, load_extensions()))
        if args.command == 'check':
            for listed, path, entries in catalogs:
                if args.base:
                    before = base_entries(args.base, listed, path)
                    changes(before, entries, args.allow_carried_cpu)
                    # A moved release is fetched at both commits to compare its controls.
                    if listed == 'machines':
                        for line in release_controls(before, entries, args.cache, args.allow_control_changes):
                            print(line)
                # Against a base, only the entries this change touches; alone, every entry.
                earlier = {e['id']: e for e in before} if args.base else {}
                for entry in entries:
                    if args.base and earlier.get(entry['id']) == entry:
                        continue
                    warning = 'cpu' in entry and cpu_measurement.above_recommended(entry['cpu'])
                    if warning:
                        print(f"WARNING: {entry['id']}: {warning}")
                print(f'PASS: {len(entries)} {listed[:-1]} entr{"y" if len(entries) == 1 else "ies"}; '
                      'IDs are unique.')
            return 0
        found, tested = {}, {}
        for listed, path, entries in catalogs:
            found[listed], tested[listed] = [], []
            earlier = ({e['id']: e for e in base_entries(args.changed_from, listed, path)}
                       if getattr(args, 'changed_from', None) else {})
            for entry in (e for e in entries if e['status'] == 'published'):
                if args.tags:
                    require(tag_commit(entry['repository'], entry['tag']) == entry['commit'],
                            f"{entry['id']}: tag {entry['tag']} no longer names the catalog's commit")
                fetched = fetch(entry, args.cache)
                found[listed].append(fetched.root)
                if not earlier or earlier.get(entry['id']) != entry:
                    tested[listed].append(fetched.root)
                print(f"Fetched {fetched.id} {entry['tag']} ({entry['commit'][:12]}): {fetched.root}")
        # Every published machine and extension, in catalog order: a collection for any build command.
        collection = Path(args.cache) / 'collection.json'
        relative = lambda folders: [{'path': os.path.relpath(f, Path(args.cache).resolve())} for f in folders]
        collection.parent.mkdir(parents=True, exist_ok=True)
        collection.write_text(json.dumps({'schema': 1, 'machines': relative(found['machines']),
                                          'extensions': relative(found['extensions'])}, indent=2) + '\n',
                              encoding='utf-8')
        print(f'Collection: {collection} (pass it to build, compile or kit with --collection)')
        if args.prune:
            for name in prune(args.cache, [f for folders in found.values() for f in folders]):
                print(f'Removed {name}: the catalog no longer pins it')
        if args.command != 'test':
            return 0
        if args.changed_from:
            # For a change to the catalog alone: the other entries passed against this same SDK.
            print(f"Testing {len(tested['machines'])} machine and {len(tested['extensions'])} extension "
                  f'entries that differ from {args.changed_from}')
        if tested['machines'] or tested['extensions']:
            from .machine_test import main as test
            return test([*(f'--machine={folder}' for folder in tested['machines']),
                         *(f'--extension={folder}' for folder in tested['extensions']),
                         '--cxx', args.cxx, '--host-cxx', args.host_cxx, '--jobs', str(args.jobs),
                         *(['--no-cpu'] if args.no_cpu else [])])
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
