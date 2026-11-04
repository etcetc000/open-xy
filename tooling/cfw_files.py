"""The custom firmware's own folder on the user drive, /open-xy. `device push`
hands every file it is given there to check(); each kind of file has its own
place and its own check here, so the transfer tool never changes per feature."""
import re

from .formats import require

FOLDER = 'open-xy'


def check_module(name, data):
    from . import build_module
    require(re.fullmatch(r'[\w\-]+\.opx', name, re.ASCII) is not None,
            'Module file names are letters, digits, _ and -, then .opx: ' + name)
    build_module.inspect(data)


# Subfolder of /open-xy: the check for each file in it.
KINDS = {'modules': check_module}


def check(parts, data):
    """parts: a device path below the root, ('open-xy', <subfolder>, <name>)."""
    require(len(parts) == 3 and parts[1] in KINDS,
            f'Only {", ".join(FOLDER + "/" + k + "/" for k in KINDS)} files may be pushed under {FOLDER}/: '
            + '/'.join(parts))
    KINDS[parts[1]](parts[2], data)
