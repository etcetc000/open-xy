"""The firmware edits module files need: MTP admits module files in
/projects/openxy beside stock's project rules."""
import copy
import json

from .formats import require, sha
from .pipeline import ROOT, check_patch_boundaries, read

OVERLAY = 'profiles/module-probe-1.2.1.json'
OVERLAY_SHA256 = '97ba661b59a171fe8122142fd43f28ff38b86253ffabeebe2a153c116128b828'
TARGETS = {'sdk_module_mtp_match': 0x8d5c1bb0}


def profile(base):
    """base with the module hooks appended; each replaces one original stock call."""
    from .firmware import absolute_target, decode_branch
    raw = read(ROOT / OVERLAY)
    require(sha(raw) == OVERLAY_SHA256, 'Unreviewed module hooks')
    overlay = json.loads(raw)
    require(overlay['schema'] == 1 and overlay['base'] == base['id'], 'Wrong module profile')
    for hook in overlay['hooks']:
        before = bytes.fromhex(hook['before'])
        target = absolute_target(before) if hook['length'] == 8 else decode_branch(int(hook['address'], 16), before)[0]
        require(target == TARGETS[hook['symbol']], 'A module hook must replace the original stock call: ' + hook['symbol'])
    result = copy.deepcopy(base)
    result['patches'].extend(overlay['hooks'])
    check_patch_boundaries(result, len(base['patches']) + len(overlay['hooks']))
    return result
