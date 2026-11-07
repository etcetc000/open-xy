"""Copy files to an OP-XY in MTP file-transfer mode, or list and read them.

    python sdk.py device push <builds>/mine-001/candidate [--record <dir>]
    python sdk.py device list [content/display]
    python sdk.py device pull content/op1_factory.json --out <file>

`push` mirrors a folder holding os.ldr, content/, or the inspection-only
projects/openxy/collection.oxc onto the device root:
<folder>/os.ldr goes to /os.ldr,
<folder>/content/... to /content/..., creating missing folders. Before writing
it backs up every file it will replace, then writes only changed files, reads
each one back byte for byte, and on any failure or interruption restores the
backed-up files (folders it created stay, empty of new files). It never deletes
anything it did not replace, never writes
firmware to the boot partition and never ejects. The record folder (default
under the private OPXY-SDK folder; always outside every checkout, since backups
hold stock firmware) keeps transfer.json and the backups. Windows goes through
its own MTP driver (tooling/wpd.py, needs comtypes); macOS and Linux speak MTP
over libusb (tooling/usbmtp.py, needs the libusb library).

Pass --no-backup --no-verify together to replace every supplied file without
reading device file contents, saving backups or rolling back failed writes.
The transfer record still identifies completed and interrupted writes.
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
TOP = {'os.ldr', 'content', 'projects'}
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


def source_files(folder, *, allow_attempt=False):
    """({device path parts: bytes}, [skipped junk paths]) for every file below folder."""
    folder = Path(folder)
    require(folder.is_dir(), f'Not a folder: {folder}')
    require(not (folder / 'candidate').is_dir(), f'{folder} holds a build; push its candidate folder: {folder / "candidate"}')
    unexpected = sorted(x.name for x in folder.iterdir() if x.name.lower() not in TOP and not junk(x.name))
    require(not unexpected, f'{folder} is not laid out like the device '
            '(only os.ldr, content/ and projects/openxy/collection.oxc): '
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
    for parts, data in files.items():
        if parts[0].lower() == 'projects':
            require(parts[:2]==('projects','openxy') and len(parts)==3,
                    'Only SDK collection files may be pushed under projects/openxy/')
            from . import collection_probe
            from .pipeline import config
            profile = config()
            name=parts[-1]
            if name=='collection.oxc':
                collection_probe.inspect(data, base=profile['module_base'],
                                         capacity=profile['reserved_bytes'] - profile['table_bytes'],
                                         platform=data[40:72])
            else:
                require(name!='attempt.oxc' or allow_attempt,
                        'Attempt history is managed by install-collection; do not replay a seed or overwrite a failed attempt')
                validate_collection_file(name,data,files)
    activation=('projects','openxy','activate.oxc')
    require(activation not in files or len(files)==1,
            'Push immutable collection files first, then activate.oxc alone in a separate verified transaction')
    distinct_without_case(files)
    return dict(sorted(files.items())), sorted(skipped)


def validate_collection_file(name,data,files):
    import hashlib
    import re
    import struct
    from . import collection_activation as activation,collection_kernel as kernel
    from .pipeline import config
    if name=='kernel.oxc':
        info=kernel.read_information(data)
        require(('os.ldr',) not in files or sha(files[('os.ldr',)])==info['os_sha256'],
                'Kernel information differs from the supplied OS')
    elif name in ('activate.oxc','attempt.oxc'):
        control=activation.read_control(data,platform=data[32:64],abi=data[64:96])
        require(control['phase'] in ((0,1,4) if name=='activate.oxc' else (2,3)), 'Wrong collection control role')
    elif name in ('slot0.oxc','slot1.oxc'):
        require(len(data)>=activation.HEADER_BYTES, 'Truncated collection slot')
        activation.inspect(data,geometry=activation.geometry(config(),kernel.RESIDENT_BYTES),
                           entries=struct.unpack_from('<I',data,36)[0],platform=data[48:80],abi=data[80:112])
    elif re.fullmatch(r'manifest-[0-9a-f]{64}\.oxc',name):
        from .collection_manifest import decode
        decode(data,digest=name[9:-4])
    else:
        match=re.fullmatch(r'(asset|catalog)-([0-9a-f]{64})\.oxc',name)
        require(match is not None,'Unknown SDK collection file: '+name)
        require(data and len(data)<=(activation.MAX_SVG_BYTES if match[1]=='asset' else activation.MAX_CATALOG_BYTES)
                and b'\0' not in data and hashlib.sha256(data).hexdigest()==match[2], 'Invalid content-addressed resource: '+name)


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


def push(device, files, record, *, expected_before=None):
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
        guard_attempt_seed(device,root,files)
        plan(device, root, files, record, report, tasks)
        for parts,_data,_old,before,_entry in tasks:
            if expected_before is not None and parts in expected_before:
                require(before==expected_before[parts],f'/{"/".join(parts)} changed after collection planning')
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
                if expected_before is not None and parts in expected_before and live is not None:
                    require(device.read(live)==expected_before[parts],f'{entry["path"]} changed before activation')
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


def guard_attempt_seed(listings,root,files):
    attempt=files.get(('projects','openxy','attempt.oxc'))
    if attempt is None:return
    from . import collection_activation as activation
    activation.read_control(attempt,platform=attempt[32:64],abi=attempt[64:96])
    parent=listings.resolve(root,('projects','openxy'))
    selector=listings.child(parent,'activate.oxc') if parent is not None else None
    if selector is None:return
    require(not selector.folder and selector.size==160,'Cannot seed history below an invalid selector; disable first')
    control=activation.read_control(listings.read(selector),platform=attempt[32:64],abi=attempt[64:96])
    require(control['phase']==0,'Cannot overwrite attempt history below an active selector; disable first')


def restored(report):
    folders = report['created_folders']
    return 'every file is as it was before this push' + (
        f' (new empty folders may remain: {", ".join(folders)})' if folders else '')


def push_unchecked(device, files, record):
    """Explicit write-only transfer: no device file reads, backups or rollback."""
    check_record(record)
    record = fresh_output(record)
    report = {'files': [], 'created_folders': [], 'complete': False,
              'backup': False, 'verify': False}

    def save():
        (record / 'transfer.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    device = Listings(device)
    touched = False
    try:
        save()
        root = device.device.root()
        require(not any((x.name or '').lower().endswith('.tfw') for x in device.list(root)),
                'A firmware update (.tfw) is waiting in the device root; finish or remove it first')
        require(('projects','openxy','attempt.oxc') not in files,'Attempt history requires the checked collection installer')
        for parts, data in files.items():
            parent = device.resolve(root, parts[:-1])
            old = device.child(parent, parts[-1]) if parent else None
            require(old is None or not old.folder, f'/{"/".join(parts)} is a folder on the device')
            report['files'].append({'path': '/' + '/'.join(parts), 'bytes': len(data),
                                    'sha256': sha(data), 'written': False, 'verified': False})
        save()
        for (parts, data), entry in zip(files.items(), report['files']):
            entry['started'] = True
            save()
            touched = True  # Creation/deletion can take effect before its driver reports an error.
            parent = device.resolve(root, parts[:-1], report['created_folders'])
            live = device.child(parent, parts[-1])
            require(live is None or not live.folder, f'{entry["path"]} became a folder on the device')
            if live is not None:
                device.delete(parent, live)
            device.create_file(parent, live.name if live else parts[-1], data, live)
            entry['written'] = True
            save()
            print('copied ' + entry['path'], flush=True)
        report['complete'] = True
        save()
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        error.device_state = ('transfer incomplete; device files may be partially replaced; no backups or rollback'
                              if touched else 'nothing was written to the device')
        try:
            save()
        except BaseException:
            pass
        raise
    return report


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
    if sys.platform == 'win32':
        from .wpd import Device
    else:
        from .usbmtp import Device
    return Device()


def clear_probe_status(device, record):
    """Back up and remove only SDK probe reports, so a reboot cannot pass on stale files."""
    check_record(record)
    record = fresh_output(record)
    report = dict(files=[], complete=False)
    listings = Listings(device)
    parent = listings.resolve(device.root(), ('projects', 'openxy'))
    pending = []
    names = {role + suffix: formats for role in ('gui', 'audio') for suffix, formats in (
        ('-status.oxc', ('openxy-collection-probe/1',)),
        ('-rpc-status.oxc', ('openxy-collection-rpc-probe/1','openxy-collection-rpc-probe/2')))}
    def save():
        (record / 'clear-status.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    try:
        for name, formats in names.items():
            entry = listings.child(parent, name) if parent is not None else None
            if entry is None:
                continue
            require(not entry.folder and entry.size <= 4096, f'Not an SDK probe report: {name}')
            data = listings.read(entry)
            value = json.loads(data)
            require(value.get('format') in formats and value.get('execution') is False,
                    f'Not an SDK probe report: {name}')
            backup = record / 'before' / name
            backup.parent.mkdir(exist_ok=True)
            backup.write_bytes(data)
            item = dict(path='/projects/openxy/' + name, sha256=sha(data), removed=False)
            report['files'].append(item)
            pending.append((entry, item))
        save()  # Validate and back up every report before deleting any.
        for entry, item in pending:
            listings.delete(parent, entry)
            item['removed'] = True
            save()
        listings.cache.clear()
        require(parent is None or all(listings.child(parent, name) is None
                for name in names), 'Probe reports are still present')
        report['complete'] = True
        save()
        return report
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        save()
        raise


def collection_records(device):
    from . import collection_kernel as kernel,collection_activation as activation
    listings=Listings(device);parent=listings.resolve(device.root(),('projects','openxy'))
    require(parent is not None,'No resident collection kernel information on this device')
    def read_record(name,limit,tolerate=False):
        entry=listings.child(parent,name)
        if entry is None:return None
        if tolerate and (entry.folder or entry.size>limit):return b''
        require(not entry.folder and entry.size<=limit,'Invalid collection record: '+name)
        return listings.read(entry)
    raw=read_record('kernel.oxc',65536)
    require(raw is not None,'Install the verified resident kernel before using RAM collections')
    info=kernel.read_information(raw);identity=dict(platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']))
    selector_raw=read_record('activate.oxc',160,True);attempt_raw=read_record('attempt.oxc',160,True)
    def decode(data):
        if data is None:return None
        try:return activation.read_control(data,**identity)
        except (ValueError,TypeError):return None
    return raw,info,selector_raw,decode(selector_raw),attempt_raw,decode(attempt_raw)


def install_collection(device,bundle,record,verify_only=False,inventory=None,skip_presets=False):
    """Two existing push transactions: inactive bytes first, selector last."""
    from . import collection_bundle,collection_activation as activation
    description,info,image,resources=collection_bundle.read_bundle(bundle)
    require(skip_presets or not description.get('presets'),
            'Regular MTP preset installation is not qualified. Use --skip-presets to preserve existing presets and install only the collection.')
    raw,live,selector_raw,selector,attempt_raw,attempt=collection_records(device)
    require(sha(raw)==description['kernel'] and live==info,'The device is running a different resident kernel')
    require(selector_raw is None or selector is not None,'Corrupt activation record; disable the external collection first')
    require(selector is None or selector['phase'] in (0,1,4),'Invalid selection phase')
    require(attempt is None or attempt['phase'] in (2,3),'Invalid attempt phase')
    active=selector is not None and selector['phase']!=0
    require(not active or attempt is not None,'Active selection has no valid attempt history; disable it before reinstalling')
    generation=max([1]+[r['generation'] for r in (selector,attempt) if r is not None])+1
    require(generation<=0xffffffff,'Collection generation exhausted')
    slot=1-selector['slot'] if active else 0
    identity=dict(platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']))
    selected=activation.control(phase=4 if verify_only else 1,generation=generation,slot=slot,
                                digest=bytes.fromhex(description['digest']),**identity)
    stage={('projects','openxy',name):data for name,data in resources.items()}
    stage[('projects','openxy',f'slot{slot}.oxc')]=image
    if inventory is not None:
        from .collection_manifest import decode,filename
        metadata=decode(inventory,digest=description['digest'],kernel=description['kernel'])
        require([m['id'] for m in metadata['machines']]==description['linked'],'Inventory differs from linked collection')
        stage[('projects','openxy',filename(description['digest']))]=inventory
    if attempt is None:
        # No active selector can observe this seed. It merely provides valid
        # older history for the explicit new generation, never confirmation of it.
        stage[('projects','openxy','attempt.oxc')]=activation.control(phase=3,generation=generation-1,slot=slot,
            digest=bytes.fromhex(description['digest']),**identity)
    check_record(record);record=fresh_output(record)
    expected_attempt=stage.get(('projects','openxy','attempt.oxc'),attempt_raw)
    for label,files in (('stage',stage),('activate',{('projects','openxy','activate.oxc'):selected})):
        folder=record/(label+'-input')
        for parts,data in files.items():
            path=folder.joinpath(*parts);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        checked,_=source_files(folder,allow_attempt=label=='stage' and attempt is None)
        current,_,current_selector,_,current_attempt,_=collection_records(device)
        require(current==raw and current_selector==selector_raw and current_attempt==
                (attempt_raw if label=='stage' else expected_attempt),'Collection records changed during installation; selector left unchanged')
        expected={('projects','openxy','activate.oxc'):selector_raw} if label=='activate' else None
        push(device,checked,record/label,expected_before=expected)
    result=dict(complete=True,generation=generation,slot=slot,digest=description['digest'],
                mode='verify-only' if verify_only else 'armed',applies='next boot',boot_partition_written=False,
                presets_transferred=False)
    try:
        (record/'collection-install.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    except OSError as error:
        error.device_state='collection selected and verified; local summary save failed; see activate/transfer.json'
        raise
    return result


def disable_collection(device,record):
    from . import collection_activation as activation
    _,info,_,selector,_,attempt=collection_records(device)
    generation=max([0]+[r['generation'] for r in (selector,attempt) if r is not None])+1
    # Disabled never executes a slot or reads attempt history. It also provides
    # the recovery path for a corrupt record or an exhausted generation.
    if generation>0xffffffff:generation=1
    data=activation.control(phase=0,generation=generation,slot=0,digest=bytes(32),
        platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']))
    return push(device,{('projects','openxy','activate.oxc'):data},record)


def collection_status(device):
    _,info,selection_raw,selection,attempt_raw,attempt=collection_records(device)
    phases=('disabled','armed','pending','confirmed','verify-only')
    def describe(raw,control):
        if control is None:return {'state':'missing' if raw is None else 'invalid'}
        return dict(state=phases[control['phase']],generation=control['generation'],slot=control['slot'],digest=control['digest'].hex())
    next_boot='embedded'
    if selection is not None and selection['phase'] in (1,4) and attempt is not None and attempt['phase'] in (2,3):
        older=attempt['generation']<selection['generation']
        healthy=(attempt['generation']==selection['generation'] and attempt['phase']==3 and
                 attempt['slot']==selection['slot'] and attempt['digest']==selection['digest'])
        if older or healthy and selection['phase']==1:
            next_boot='verify-only' if selection['phase']==4 else 'external'
    return dict(kernel=info['platform'],embedded=info['embedded'],selection=describe(selection_raw,selection),
                attempt=describe(attempt_raw,attempt),next_boot=next_boot)


def main(argv=None, connect=open_device):
    parser = argparse.ArgumentParser(prog='sdk.py device', description=__doc__.split('\n\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('push', help='Mirror a folder onto the device root')
    p.add_argument('folder', type=Path, help='Folder laid out like the device, such as <build>/candidate')
    p.add_argument('--record', type=Path, help='New folder outside every checkout for the record and backups')
    p.add_argument('--no-backup', action='store_true', help='No backups or rollback; requires --no-verify')
    p.add_argument('--no-verify', action='store_true', help='No device file readback; requires --no-backup')
    kernel_scope=p.add_mutually_exclusive_group()
    kernel_scope.add_argument('--kernel-boot',action='store_true',help='Dev MTP: copy only the resident OS and its protected fallback SVG')
    kernel_scope.add_argument('--kernel-record',action='store_true',help='Normal MTP: copy only the resident kernel information record')
    p = commands.add_parser('list', help='List a device folder')
    p.add_argument('path', nargs='?', default='/')
    p = commands.add_parser('pull', help='Read one device file')
    p.add_argument('path')
    p.add_argument('--out', type=Path, required=True, help='New file outside every checkout')
    p = commands.add_parser('clear-probe-status', help='Back up and remove SDK inspection and RPC reports')
    p.add_argument('--record', type=Path, required=True, help='New folder outside every checkout for report backups')
    p=commands.add_parser('install-collection',help='Install a RAM collection, with activation written last')
    p.add_argument('bundle',type=Path)
    p.add_argument('--verify-only',action='store_true',help='Preflight at next boot without copying or executing code')
    p.add_argument('--skip-presets',action='store_true',help='Keep existing presets; do not transfer bundled factory presets')
    p.add_argument('--record',type=Path,required=True)
    p=commands.add_parser('disable-collection',help='Return to the embedded collection at next boot')
    p.add_argument('--record',type=Path,required=True)
    commands.add_parser('collection-status',help='Read selection/attempt records and the next-boot decision')
    args = parser.parse_args(argv)
    try:
        if args.command == 'push':
            require(args.no_backup == args.no_verify, 'Use --no-backup and --no-verify together')
            files, skipped = source_files(args.folder)  # A bad source fails before the device is touched.
            if args.kernel_boot or args.kernel_record:
                require(not args.no_backup,'Kernel installation requires backups and readback')
                information=('projects','openxy','kernel.oxc')
                require(information in files,'The folder has no resident kernel information')
                wanted={information} if args.kernel_record else {('os.ldr',),('content','display','_openxy_loader_1.svg')}
                require(wanted<=set(files),'Incomplete resident kernel installation files')
                files={parts:data for parts,data in files.items() if parts in wanted}
            for path in skipped:
                print('skipped ' + path, flush=True)
            record = args.record or private_root() / 'transfers' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            check_record(record)  # Before the device is opened.
            with connect() as device:
                try:
                    report = (push_unchecked if args.no_backup else push)(device, files, record)
                finally:
                    if (record / 'transfer.json').exists():
                        print('Transfer record: ' + str(record), flush=True)
            if args.no_backup:
                print(f'COPIED: {len(files)} files; no backups or readback verification. '
                      'Firmware not written to the boot partition.')
            else:
                changed = sum(x['changed'] for x in report['files'])
                print(f'PASS: {len(files)} files on the device ({changed} copied and read back, the rest already matched). '
                      'Firmware not written to the boot partition.')
        elif args.command=='install-collection':
            from .collection_bundle import read_bundle
            read_bundle(args.bundle);check_record(args.record)
            with connect() as device:
                result=install_collection(device,args.bundle,args.record,args.verify_only,skip_presets=args.skip_presets)
            print(json.dumps(result,indent=2));print('Reboot to apply. Transfer records: '+str(args.record))
        elif args.command=='collection-status':
            with connect() as device:result=collection_status(device)
            print(json.dumps(result,indent=2))
        elif args.command=='disable-collection':
            check_record(args.record)
            with connect() as device:disable_collection(device,args.record)
            print('External collection disabled. Reboot to use the embedded machines. Record: '+str(args.record))
        elif args.command == 'clear-probe-status':
            check_record(args.record)
            with connect() as device:
                result = clear_probe_status(device, args.record)
            print(f'PASS: all probe reports absent ({len(result["files"])} backed up and removed). Record: {args.record}')
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
