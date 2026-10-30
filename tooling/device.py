"""Copy files to an OP-XY in MTP file-transfer mode, or list and read them.

    python sdk.py device push <builds>/mine-001/candidate [--record <dir>]
    python sdk.py device list [content/display]
    python sdk.py device pull content/op1_factory.json --out <file>

`push` mirrors a folder holding only os.ldr and content/ onto the device root:
<folder>/os.ldr goes to /os.ldr,
<folder>/content/... to /content/..., creating missing folders. Before writing
it backs up every file it will replace, then writes only changed files, reads
each one back byte for byte, and on any failure or interruption restores the
backed-up files (folders it created stay, empty of new files). It never deletes
anything it did not replace, never writes
firmware to the boot partition and never ejects. The record folder (default
under the private OPXY-SDK folder; always outside every checkout, since backups
hold stock firmware) keeps transfer.json and the backups. Windows only
(tooling/wpd.py); on a Mac use the browser builder's transfer.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path, PurePosixPath
import sys

from .formats import require, sha
from .pipeline import fresh_output
from .release import outside_checkouts

# Explorer and Finder leave these beside copied files; they never belong on the device.
JUNK = {'thumbs.db', 'desktop.ini', '.ds_store'}
# What a build or kit installs; anything else at the top is the wrong folder.
TOP = {'os.ldr', 'content'}
IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003  # A directory junction; cloud and dedup reparse points are ordinary files.


def unreadable(error):
    raise error


def link(path):
    """A symlink or a Windows junction; Path.is_junction needs Python 3.12, this repository supports 3.11."""
    return path.is_symlink() or getattr(os.lstat(path), 'st_reparse_tag', 0) == IO_REPARSE_TAG_MOUNT_POINT


def junk(name):
    return name.lower() in JUNK or name.startswith('._')


def private_root():
    if sys.platform == 'darwin':
        return Path.home() / 'Library/Application Support/OPXY-SDK/private'
    return Path(os.environ.get('LOCALAPPDATA') or Path.home() / '.local/share') / 'OPXY-SDK/private'


def source_files(folder):
    """({device path parts: bytes}, [skipped junk paths]) for every file below folder."""
    folder = Path(folder)
    require(folder.is_dir(), f'Not a folder: {folder}')
    require(not (folder / 'candidate').is_dir(), f'{folder} holds a build; push its candidate folder: {folder / "candidate"}')
    unexpected = sorted(x.name for x in folder.iterdir() if x.name.lower() not in TOP and not junk(x.name))
    require(not unexpected, f'{folder} is not laid out like the device (only os.ldr and content/ at the top): '
            + ', '.join(unexpected))
    files, skipped = {}, []
    for directory, folders, names in os.walk(folder, onerror=unreadable):  # Never push part of a source.
        here = Path(directory)
        for name in sorted(folders + names):
            path = here / name
            # Checked before descending: a junction is not a symlink and could loop the walk.
            require(not link(path), f'Refusing a link: {path}')
            if junk(name):
                skipped.append(path.relative_to(folder).as_posix())
            elif path.is_file():
                files[path.relative_to(folder).parts] = path.read_bytes()
        folders[:] = sorted(x for x in folders if not junk(x))  # Junk folders are skipped with their contents.
    require(files, f'No files below {folder}')
    distinct_without_case(files)
    return dict(sorted(files.items())), sorted(skipped)


def distinct_without_case(paths):
    """A case-sensitive source (WSL, a case-sensitive folder) can hold names the device cannot tell apart."""
    seen = {}
    for parts in paths:
        # Every folder on the way counts too: content/foo and content/FOO/bar.svg collide on the device.
        for depth in range(1, len(parts) + 1):
            key, path = tuple(x.lower() for x in parts[:depth]), parts[:depth]
            require(seen.setdefault(key, path) == path, f'{"/".join(seen[key])} and {"/".join(path)} are the same '
                    'name on the device, which ignores case')


def check_record(record):
    require(outside_checkouts(record), f'Keep the transfer record outside every checkout: backups hold stock firmware ({record})')
    require(not Path(record).exists(), f'{record} already exists; give each transfer a new record folder')


class Listings:
    """Device access with each folder listed once until something in it changes."""

    def __init__(self, device):
        self.device, self.cache = device, {}

    def list(self, parent):
        if parent not in self.cache:
            self.cache[parent] = self.device.list(parent)
        return self.cache[parent]

    def child(self, parent, name):
        # The device's FAT storage ignores case, so matching must too.
        matches = [x for x in self.list(parent) if (x.name or '').lower() == name.lower()]
        require(len(matches) <= 1, f'More than one {name} on the device')
        return matches[0] if matches else None

    def read(self, entry):
        return self.device.read(entry)

    def delete(self, parent, entry):
        try:
            self.device.delete(entry)
        finally:
            self.cache.pop(parent, None)
        require(self.child(parent, entry.name) is None, f'Deleting {entry.name} was not confirmed')

    def create_file(self, parent, name, data, like):
        try:
            self.device.create_file(parent, name, data, like)
        finally:
            self.cache.pop(parent, None)

    def create_folder(self, parent, name):
        try:
            self.device.create_folder(parent, name)
        finally:
            self.cache.pop(parent, None)

    def resolve(self, root, folders, create=None):
        """The id of a folder path; a missing part gives None, or is made and noted in create."""
        parent = root
        for index, name in enumerate(folders):
            path = '/' + '/'.join(folders[:index + 1])
            entry = self.child(parent, name)
            if entry is None:
                if create is None:
                    return None
                try:
                    self.create_folder(parent, name)
                finally:
                    if self.child(parent, name) is not None:
                        create.append(path)  # Noted even if the create errors after taking effect.
                entry = self.child(parent, name)
                require(entry is not None and entry.folder, f'Creating {path} failed')
            require(entry.folder, f'{path} is a file where a folder is needed')
            parent = entry.id
        return parent

    def verify(self, parent, name, data):
        entry = self.child(parent, name)
        require(entry is not None and entry.size == len(data) and self.read(entry) == data,
                f'Readback of {name} differs from the source')


def plan(device, root, files, record, report, tasks):
    """Back up every file about to be replaced; nothing on the device changes here."""
    for parts, data in files.items():
        path = '/' + '/'.join(parts)
        parent = device.resolve(root, parts[:-1])
        old = device.child(parent, parts[-1]) if parent else None
        require(old is None or not old.folder, f'{path} is a folder on the device')
        before = device.read(old) if old else None
        if before is not None and before != data:  # Only files about to be replaced need a backup.
            backup = record.joinpath('before', *parts)
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(before)
        entry = {'path': path, 'bytes': len(data), 'sha256': sha(data),
                 'before_sha256': sha(before) if before is not None else None,
                 'changed': before != data, 'verified': False}
        report['files'].append(entry)
        tasks.append((parts, data, old, before, entry))


def push(device, files, record):
    """Mirror files ({device path parts: bytes}) onto the device; returns the transfer report."""
    check_record(record)
    record = fresh_output(record)
    report = {'files': [], 'created_folders': [], 'complete': False}

    def save():
        (record / 'transfer.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    save()
    device = Listings(device)
    tasks = []
    try:
        root = device.device.root()
        require(not any((x.name or '').lower().endswith('.tfw') for x in device.list(root)),
                'A firmware update (.tfw) is waiting in the device root; finish or remove it first')
        plan(device, root, files, record, report, tasks)
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        error.device_state = 'nothing was written to the device'
        save()
        raise
    save()
    done = []
    try:
        for parts, data, old, before, entry in tasks:
            parent = device.resolve(root, parts[:-1], report['created_folders'])
            if entry['changed']:
                live = device.child(parent, parts[-1])
                unchanged = live is None or (old is not None and live.id == old.id and live.size == len(before))
                require((live is None) == (old is None) and unchanged, f'{entry["path"]} changed on the device after the backup')
                done.append((parts, old, before, entry))
                entry['written'] = True
                save()  # Recorded before the device changes, so an interruption still shows it.
                if live is not None:
                    device.delete(parent, live)
                    entry['deleted'] = True  # Device ids can be reused, so this flag, not an id, says the original is gone.
                # Only the replaced file's own type and name case carry over; a sibling's type may be unrelated.
                device.create_file(parent, live.name if live else parts[-1], data, live)
                print('copied ' + entry['path'], flush=True)
                device.verify(parent, parts[-1], data)
            entry['verified'] = True  # Unchanged files were compared when planned.
            save()
        # Every file was compared byte for byte above; a fresh listing catches anything changed since.
        device.cache.clear()
        for parts, data, _, _, entry in tasks:
            parent = device.resolve(root, parts[:-1])
            final = device.child(parent, parts[-1]) if parent else None
            require(final is not None and final.size == len(data), f'{entry["path"]} changed after it was verified')
    except BaseException as error:  # Ctrl+C mid-write must still restore the backups.
        report['error'] = str(error) or type(error).__name__
        device.cache.clear()
        try:
            for parts, old, before, entry in reversed(done):
                try:
                    parent = device.resolve(root, parts[:-1])
                    current = device.child(parent, parts[-1]) if parent else None
                    # Untouched only if it still holds the original bytes: ids can be reused and a delete
                    # can take effect and then report an error, so neither an id nor a flag is proof.
                    if (current is not None and before is not None and not entry.get('deleted')
                            and current.size == len(before) and device.read(current) == before):
                        entry['rolled_back'] = True
                        continue
                    if current is not None:
                        device.delete(parent, current)
                    if before is not None:
                        device.create_file(parent, old.name, before, old)
                        device.verify(parent, parts[-1], before)
                    entry['rolled_back'] = True
                except BaseException as rollback_error:  # A second Ctrl+C still leaves an accurate record.
                    entry['rollback_error'] = str(rollback_error) or type(rollback_error).__name__
        finally:
            try:
                save()
            except BaseException:
                pass  # A record that can't be written, or a Ctrl+C here, must not hide the restore instructions below.
        failed = [x for _, _, _, x in done if not x.get('rolled_back')]
        if failed:
            steps = []
            restore = [x['path'] for x in failed if x['before_sha256']]
            remove = [x['path'] for x in failed if not x['before_sha256']]
            if restore:
                steps.append(f'copy {", ".join(restore)} back from {record / "before"}')
            if remove:
                steps.append(f'delete {", ".join(remove)}, which are new')
            raise ValueError(f'{report["error"]}; the restore also failed, so the device is left changed: '
                             f'{" and ".join(steps)} before writing the OS') from error
        error.device_state = restored(report)
        if isinstance(error, Exception):
            raise ValueError(f'{report["error"]}; {error.device_state}') from error
        raise
    report['complete'] = True
    try:
        save()  # Outside the try above: failing to write the record must not undo a verified push.
    except OSError as error:
        error.device_state = 'every file was copied and verified, but the record could not be saved'
        raise
    return report


def restored(report):
    folders = report['created_folders']
    return 'every file is as it was before this push' + (
        f' (new empty folders may remain: {", ".join(folders)})' if folders else '')


def entry_at(device, path):
    # Git Bash rewrites a leading / into its install folder (C:/Program Files/Git/...).
    require(not (len(path) > 1 and path[1] == ':'), f'{path} is not a device path; write it without the leading /, '
            'for example content/display')
    device = Listings(device)
    parts = PurePosixPath('/' + path.replace('\\', '/').strip('/')).parts[1:]
    root = device.device.root()
    if not parts:
        return root, None
    parent = device.resolve(root, parts[:-1])
    entry = device.child(parent, parts[-1]) if parent else None
    require(entry is not None, f'Nothing at {path} on the device')
    return entry.id, entry


def open_device():
    from .wpd import Device
    return Device()


def main(argv=None, connect=open_device):
    parser = argparse.ArgumentParser(prog='sdk.py device', description=__doc__.split('\n\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('push', help='Mirror a folder onto the device root')
    p.add_argument('folder', type=Path, help='Folder laid out like the device, such as <build>/candidate')
    p.add_argument('--record', type=Path, help='New folder outside every checkout for the record and backups')
    p = commands.add_parser('list', help='List a device folder')
    p.add_argument('path', nargs='?', default='/')
    p = commands.add_parser('pull', help='Read one device file')
    p.add_argument('path')
    p.add_argument('--out', type=Path, required=True, help='New file outside every checkout')
    args = parser.parse_args(argv)
    try:
        if args.command == 'push':
            files, skipped = source_files(args.folder)  # A bad source fails before the device is touched.
            for path in skipped:
                print('skipped ' + path, flush=True)
            record = args.record or private_root() / 'transfers' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            check_record(record)  # Before the device is opened.
            with connect() as device:
                try:
                    report = push(device, files, record)
                finally:
                    if (record / 'transfer.json').exists():
                        print('Transfer record: ' + str(record), flush=True)
            changed = sum(x['changed'] for x in report['files'])
            print(f'PASS: {len(files)} files on the device ({changed} copied and read back, the rest already matched). '
                  'Firmware not written to the boot partition.')
        elif args.command == 'list':
            with connect() as device:
                folder, entry = entry_at(device, args.path)
                require(entry is None or entry.folder, f'{args.path} is a file')
                for x in sorted(device.list(folder), key=lambda x: (not x.folder, x.name or '')):
                    print(f'{x.name}/' if x.folder else f'{x.name}  {x.size}')
        else:
            require(not args.out.exists(), f'{args.out} already exists')
            require(args.out.parent.is_dir(), f'No folder {args.out.parent} for {args.out.name}')
            require(outside_checkouts(args.out), f'Keep device files outside every checkout: they may be stock ({args.out})')
            with connect() as device:
                _, entry = entry_at(device, args.path)
                require(entry is not None and not entry.folder, f'{args.path} is not a file')
                data = device.read(entry)
            args.out.write_bytes(data)
            print(f'{args.out}: {len(data)} bytes, sha256 {sha(data)}')
    except KeyboardInterrupt as error:
        # push marks an interrupt it restored from; a failed restore raises its own error instead.
        state = getattr(error, 'device_state', None)
        parser.exit(1, 'FAIL: interrupted' + (f'; {state}' if state else '') + '\n')
    except Exception as error:  # Includes the driver's COMError, which is not an OSError.
        state = getattr(error, 'device_state', None)
        parser.exit(1, 'FAIL: ' + str(error) + (f'; {state}' if state else '') + '\n')
    return 0
