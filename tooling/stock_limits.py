"""Stock sample-length limits the profile raises, audited site by site.

Stock 1.2.1 caps a loaded file at 882,000 frames (20 s at 44.1 kHz). The stock
sample object stores fewer than 2^20 frames, so the edits raise the cap to
2^20 - 1 = 1,048,575 frames (23.78 s at 44.1 kHz) and nothing higher.

Each edit rewrites only the immediate of one Blackfin+ `Dreg/Preg = imm32`
load, and belongs to the `long_samples` service, a registry capability: an
extension that declares `"capabilities": ["long_samples"]` turns them on, and
without one the build leaves stock's cap unchanged. The recorder's own 882,000 limits (record_buffer.cpp: remaining record
time and the save-time cut) stay stock, so recording still stops at 20 s.
"""
from .formats import require
from .stock_tables import decode

SERVICE = 'long_samples'
STOCK_FRAMES = 882000
RAISED_FRAMES = (1 << 20) - 1

# (address, what reads the constant)
EDITS = (
    (0x8d4fc00e, 'project/preset region parser: start and end clamp'),
    (0x8d50711a, 'region setter: accepts begin < end <= limit'),
    (0x8d508418, 'sample_buffer.cpp loader: frames = MIN(frames, limit)'),
    (0x8d5084dc, 'sample_buffer.cpp loader: the "too long" warning\'s max'),
)


def check_edits(patches):
    """Exactly the audited limit edits, in order, each changing only its immediate."""
    require(len(patches) == len(EDITS), 'Unexpected sample-limit edit count')
    for patch, (site, _) in zip(patches, EDITS):
        require(int(patch['address'], 16) == site and patch['length'] == 8, 'Unexpected sample-limit edit site')
        require(patch.get('service') == SERVICE, 'A sample-limit edit belongs to the long_samples service')
        opcode, value = decode('imm32', bytes.fromhex(patch['before']))
        require(value == STOCK_FRAMES, 'Sample-limit preimage differs from the audited stock value')
        require(decode('imm32', bytes.fromhex(patch['after'])) == (opcode, RAISED_FRAMES),
                'Sample-limit edit must change only the immediate')


def check_profile(profile):
    require(profile['services'].get(SERVICE) == [SERVICE], 'Expected the long_samples service')
    check_edits([p for p in profile['patches'] if p['kind'] == 'limit' and p.get('service') == SERVICE])
