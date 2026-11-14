"""Stock content a build must never replace.

A build installs files beside the OP-XY's own content: a machine's screen as
content/display/<id>.svg, its presets in content/presets/<id>/, its samples in
content/samples/<id>/, an aux machine's presets in content/aux_presets/<id>/ and
an extension's artwork in content/display/. The firmware profile's
`stock_content` lists, per content folder, the names stock itself keeps there.
A machine named after one of them would overwrite stock's file or write into
stock's folder, and stock may then fail at boot (a stock screen replaced by a
machine's lacks the elements stock looks up). So such a name is refused, before
anything is built. Names compare case-insensitively, as tooling/device.py assumes
the device's FAT storage does (an assumption, not measured here); the check holds
either way. builder/core.mjs applies the same rule.
"""
from functools import lru_cache
from pathlib import PurePosixPath

from .formats import require


def machine_paths(identity):
    """The content paths a machine ID names, whatever the machine's kind."""
    return (f'content/display/{identity}.svg', f'content/presets/{identity}', f'content/samples/{identity}',
            f'content/aux_presets/{identity}')


@lru_cache(maxsize=None)
def _profile_names():
    from .pipeline import config
    return names(config())


def names(profile):
    """Stock's own names per content folder, keyed lowercased: {'display': {'mixer.svg': 'mixer.svg', ...}, ...}."""
    listed = profile.get('stock_content')
    require(isinstance(listed, dict) and listed and all(
        isinstance(folder, str) and isinstance(entries, list) and all(isinstance(e, str) and e for e in entries)
        for folder, entries in listed.items()), 'The firmware profile lists no stock content names')
    return {folder: {e.lower(): e for e in entries} for folder, entries in listed.items()}


def stock_file(path, profile=None):
    """The stock file or folder (content/<folder>/<name>) a content path would replace or
    write into, or None. `path` is relative to the device root."""
    parts = PurePosixPath(path).parts
    if len(parts) < 3 or parts[0] != 'content':
        return None
    stock = names(profile) if profile is not None else _profile_names()
    name = stock.get(parts[1], {}).get(parts[2].lower())
    return None if name is None else f'content/{parts[1]}/{name}'


RENAME = ("Stock can fail at boot when its own content is replaced or mixed with another's. "
          "Rename it: names stock uses in content/ are refused, in any letter case.")


def check_identity(identity, profile=None):
    """Refuse a machine ID that names a stock content file or folder. The rule is per ID,
    whatever the kind: a machine installs some of these paths, and its ID is held for good."""
    for path in machine_paths(identity):
        stock = stock_file(path, profile)
        require(stock is None, f"Machine {identity}: its ID names the OP-XY's own {stock}. " + RENAME)


def check_output(path, owner, profile=None):
    """Refuse one file a build would install when it lands on a stock name."""
    stock = stock_file(path, profile)
    if stock is not None:
        replaced = PurePosixPath(path).as_posix().lower() == stock.lower()
        require(False, f"{owner} would install {path} "
                       + (f"over the OP-XY's own {stock}. " if replaced else f"into the OP-XY's own folder {stock}. ")
                       + RENAME)
