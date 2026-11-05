"""The firmware edits module files need: MTP admits the /open-xy and
/open-xy/modules folders and module files beside stock's rules, and module
synth artwork and catalog entries reach stock through its own load calls."""
import copy
import json

from .formats import require, sha
from .pipeline import ROOT, check_patch_boundaries, read

OVERLAY = 'profiles/modules-1.2.1.json'
OVERLAY_SHA256 = 'f8465a3e13fdd31681c5b798077d47b5261ca6c55bad8952abd2172d675073da'
TARGETS = {'sdk_module_mtp_folder_match': 0x8d5c1bb0, 'sdk_module_mtp_match': 0x8d5c1bb0,
           'sdk_module_asset_load': 0x8d5f2400, 'sdk_module_catalog_load': 0x8c042490,
           'sdk_module_board_receive': 0x8d5aeb58,
           'sdk_module_preset_dir_open': 0x8c0be54a, 'sdk_module_preset_dir_next': 0x8c0be6a4,
           'sdk_module_preset_dir_close': 0x8c0be7b2, 'sdk_module_preset_load': 0x8c042490}


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
