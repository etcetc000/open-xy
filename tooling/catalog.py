"""The catalog: machines published from their own repositories, by ID.

catalog/machines.json lists each machine once, for good:

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
import subprocess
import sys

from .definitions import stock_name
from . import cpu_measurement
from .formats import require
from .machines import machine_at, relative_path
from .pipeline import NO_WINDOW, ROOT

CATALOG = ROOT / 'catalog/machines.json'
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


def official(entry):
    """Whether a catalog entry is one of Open-XY's own machines."""
    return entry['id'] in OFFICIAL


def validate(value):
    """A catalog document's entries, checked against each other."""
    require(isinstance(value, dict) and set(value) == {'schema', 'machines'} and value['schema'] == 1 and
            isinstance(value['machines'], list), 'Unsupported catalog')
    names = {}
    for entry in value['machines']:
        require(isinstance(entry, dict) and isinstance(entry.get('id'), str), 'Catalog entries need an id')
        name = entry['id']
        status = entry.get('status')
        require(status in STATUSES, f'{name}: status is one of {", ".join(STATUSES)}')
        required = COMMON | (RELEASE if status in ('draft', 'published') else set())
        # A retired machine keeps its whole last release, or none of it.
        if status == 'retired' and RELEASE & set(entry):
            required = COMMON | RELEASE
        if 'cpu' in entry:
            require('commit' in entry and status != 'reserved', f'{name}: cpu needs a pinned release')
            cpu_measurement.validate(entry['cpu'], entry.get('kind'))
            required = required | {'cpu'}
        require(set(entry) == required, f'{name}: catalog fields for a {status} machine: '
                + ', '.join(sorted(required)))
        stock_name(name)
        require(name not in names, f'{name}: the ID is already {names.get(name)}')
        names[name] = 'listed in the catalog'
        require(entry['kind'] in ('synth', 'sample', 'effect'), f'{name}: unknown kind')
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
                    f'{name}: path is the machine folder inside the repository, such as "." or "machines/<id>"')
    return value['machines']


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
        if entry['status'] in ('draft', 'published') and entry != previous.get(entry['id']):
            require('cpu' in entry, f"{entry['id']}: new or updated submissions require hardware cpu measurements (docs/profiling.md)")
            cpu_measurement.validate(entry['cpu'], entry['kind'])
            # With a maintainer's approval, an update may carry its previous measurement over
            # unchanged, still naming the commit it measured; otherwise it measures the pinned commit.
            carried = allow_carried_cpu and entry['cpu'] == previous.get(entry['id'], {}).get('cpu')
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
        if entry['status'] != 'published' or old is None or old['commit'] == entry['commit']:
            continue
        previous_machine, next_machine = fetch(old, cache), fetch(entry, cache)
        changed, notes = control_changes(previous_machine.definition['parameters'],
                                         next_machine.definition['parameters'])
        if previous_machine.kind == 'sample':
            previous_mode = previous_machine.definition['resources'].get('singleSample', 0)
            next_mode = next_machine.definition['resources'].get('singleSample', 0)
            if previous_mode != next_mode:
                changed.append(f'sample assignment mode changed: singleSample {previous_mode} -> {next_mode}')
        release = f"{entry['id']} {old['tag']} -> {entry['tag']}"
        report += [f'{release}: {line}' for line in changed + notes]
        refused += [f'{release}: {line}' for line in changed]
    require(allow or not refused, 'saved projects would load different controls:\n  ' + '\n  '.join(refused) +
            '\nKeep each parameter at its index with its id and mapping, or have a catalog maintainer '
            'approve the change with the controls-changed label')
    return report


def load(path=CATALOG):
    return validate(json.loads(Path(path).read_text(encoding='utf-8')))


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
    """One published machine's folder at exactly its catalog commit, checked against its entry."""
    folder = Path(cache) / f"{entry['id']}-{entry['commit'][:12]}"
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
    m = machine_at(folder / entry['path'])
    require(m.id == entry['id'] and m.kind == entry['kind'],
            f"{entry['id']}: the machine at the pinned commit differs from its catalog entry")
    return m


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
    """Whether a machine folder is a published catalog machine (of `entries`, from load())
    at its pinned commit, unchanged."""
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


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py catalog', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    command = parser.add_subparsers(dest='command', required=True)
    checking = command.add_parser('check', help='validate catalog/machines.json')
    checking.add_argument('--base', help='a git revision to compare with, such as the pull request target')
    checking.add_argument('--allow-control-changes', action='store_true',
                          help="accept releases that change what saved parameter values mean (a catalog maintainer's approval)")
    checking.add_argument('--allow-carried-cpu', action='store_true',
                          help="accept updates that keep their previous cpu measurement (a catalog maintainer's approval)")
    checking.add_argument('--cache', type=Path, default=CACHE, help='fetched machines (default: local/catalog)')
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
                if before:
                    # Entries before layouts gave machine numbers also listed an identity.
                    for entry in before.get('machines', []):
                        if isinstance(entry, dict):
                            entry.pop('identity', None)
                before = validate(before) if before else []
                changes(before, entries, args.allow_carried_cpu)
                # A moved release is fetched at both commits to compare its controls.
                for line in release_controls(before, entries, args.cache, args.allow_control_changes):
                    print(line)
            print(f'PASS: {len(entries)} catalog entr{"y" if len(entries) == 1 else "ies"}; '
                  'IDs are unique.')
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
        # Every published machine, in catalog order: a collection for any build command.
        collection = Path(args.cache) / 'collection.json'
        entries = [{'path': os.path.relpath(f, Path(args.cache).resolve())} for f in folders]
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
