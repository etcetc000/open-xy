"""The custom firmware's own folder on the user drive, /open-xy. `device push`
hands every file it is given there to check(); each kind of file has its own
place and its own check here, so the transfer tool never changes per feature."""
from .formats import require

FOLDER = 'open-xy'


def check_module(name, data):
    from . import build_module
    stem = name.removesuffix('.opx')
    require(name.endswith('.opx') and build_module.NAME.fullmatch(stem) is not None,
            'Module file names are a lowercase letter, then lowercase letters, digits or _, at most 15, '
            'then .opx: ' + name)
    # A module is known by its name: the file carries the one it was built with.
    require(build_module.inspect(data)['manifest'].get('name') == stem,
            'Module file name differs from the name in its manifest: ' + name)


# Subfolder of /open-xy: the check for each file in it.
KINDS = {'modules': check_module}


def check(parts, data):
    """parts: a device path below the root, ('open-xy', <subfolder>, <name>)."""
    require(len(parts) == 3 and parts[1] in KINDS,
            f'Only {", ".join(FOLDER + "/" + k + "/" for k in KINDS)} files may be pushed under {FOLDER}/: '
            + '/'.join(parts))
    KINDS[parts[1]](parts[2], data)
