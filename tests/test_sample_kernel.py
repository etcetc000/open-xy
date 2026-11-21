"""The sample kernel (opxy/sample_kernel.hpp, sample_kernel.s): linked only for a machine whose own C++
(or one of its tests) includes the header; the reference's words are pinned by golden vectors and by a
model of the header's definition; the device routine is pinned to the code checked against the reference
outside this repository, and its text keeps the ABI it documents."""
import hashlib
import os
import random
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from tooling import native

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / 'sdk/platform/blackfin/sample_kernel.s'
UNITY = 1 << 27
FILL = 0x5a5a5a5a
MASK = 0xffffffff


# A machine's host test calling the routine: one unity-level stereo frame of a mono source.
KERNEL_TEST = '''#include <opxy/sample_kernel.hpp>
int main() {
    const opxy::i16 pcm[2] = {1000, 1000};
    const opxy::u32 position = 0;
    opxy::i32 out[2] = {0, 0};
    opxy::SampleTaps taps{};
    taps.origin = pcm; taps.shift = 16; taps.last = 1; taps.channels = 1;
    taps.level = opxy::sample_unity_level;
    opxy::sample_interpolate(out, &position, 1, taps);
    return out[0] == 1000 * 32768 && out[1] == out[0] ? 0 : 1;
}
'''


# The header's definition, restated: the C reference must give these words.
def level_word(v, level):
    """sign(v) * min((|v| * level) >> 12, 2^31 - 1, or 2^31 when negative)."""
    x = (abs(v) * level) >> 12
    return min(x, 2 ** 31 - 1) if v >= 0 else -min(x, 2 ** 31)


def frame(case, position):
    """One playhead's interpolated value per channel (left, right; mono gives both)."""
    shift, channels, pcm, origin = case['shift'], case['channels'], case['pcm'], case['origin']
    offset = -(position >> shift) if case['reverse'] else position >> shift
    following = 0 if position >= case['last'] else offset + (-1 if case['reverse'] else 1)
    f = ((position << (32 - shift)) & MASK) >> 18
    first, second = origin + offset * channels, origin + following * channels
    assert 0 <= first and 0 <= second, 'a case reads outside its region'
    a, b = pcm[first:first + channels], pcm[second:second + channels]
    v = [(a[k] * (16384 - f) + b[k] * f) >> 14 for k in range(channels)]
    return v if channels == 2 else v * 2


def model(case):
    words = []
    for i, position in enumerate(case['positions']):
        v = frame(case, position)
        if 'weights' in case:
            w, u = case['weights'][i], frame(case, case['to'][i])
            v = [(v[k] * (16384 - w) + u[k] * w) >> 14 for k in range(2)]
        words += [level_word(v[0], case['level']), level_word(v[1], case['level'])]
    return words + [FILL, FILL]


def at(shift, n, f14=0):
    """The position of frame n plus f14 / 16384 of a frame (as far as shift keeps it)."""
    return (n << shift) | (((f14 << 18) & MASK) >> (32 - shift))


def case(shift, frames, pcm, positions, channels=1, reverse=False, level=UNITY, pad=0, to=None, weights=None):
    """A region of `frames` frames in pcm after `pad` extra i16s; origin is its first frame in playback order."""
    origin = pad + ((frames - 1) * channels if reverse else 0)
    value = dict(shift=shift, last=(frames - 1) << shift, reverse=int(reverse), channels=channels, level=level,
                 pcm=list(pcm), origin=origin, positions=list(positions))
    if weights is not None:
        value.update(to=list(to), weights=list(weights))
    return value


MONO = [0, 1000, -1000, 32767, -32768, -1, 1, 12345]
STEREO = [0, -1, 32767, -32768, -32768, 32767, 100, -100, -3, 3, 12345, -12345]
EDGE = [-32768, -32767, -2, -1, 0, 1, 2, 32766, 32767]
LEVELS = [0, 1, 4096, 0x05555555, UNITY - 1, UNITY, UNITY + 1, 0x0fffffff, 0x10000000,
          (4096 << 16) | 16385, (4096 << 16) | 16386, 4097 << 16, 0x2000ffff, 0x7fffffff, MASK]
FRACTIONS = [0, 1, 8191, 8192, 8193, 16383]


def cases():
    """Named cases; each pins a part of the definition."""
    s = 16
    named = {
        'no frames': case(s, 8, MONO, []),
        'one mono frame': case(s, 8, MONO, [at(s, 1)]),
        'one stereo frame': case(s, 6, STEREO, [at(s, 1)], channels=2),
        'mono fractions': case(s, 8, MONO, [at(s, 2, f) for f in FRACTIONS]),
        'stereo fractions': case(s, 6, STEREO, [at(s, 1, f) for f in FRACTIONS], channels=2),
        # A stereo origin off a 4-byte boundary: the device's split loops.
        'unaligned stereo': case(s, 6, [7] + STEREO, [at(s, n, 5000) for n in range(6)], channels=2, pad=1),
        'unaligned stereo reverse': case(s, 6, [7] + STEREO, [at(s, n, 12000) for n in range(6)], channels=2,
                                         reverse=True, pad=1),
        # Arithmetic shifts round negative sums down; the level truncates toward zero.
        'rounding': case(s, 9, EDGE, [at(s, n, f) for n in range(8) for f in (1, 8191, 16383)], level=0x05555555),
        # At and past last the next frame is the region's first.
        'end and wrap': case(s, 8, MONO, [at(s, 6, 9000), at(s, 7), at(s, 7, 1), at(s, 7, 16383)]),
        'reverse and wrap': case(s, 8, MONO, [at(s, 0, 3), at(s, 1, 9000), at(s, 7), at(s, 7, 16383)], reverse=True),
        'stereo reverse and wrap': case(s, 6, STEREO, [at(s, 4, 100), at(s, 5), at(s, 5, 16383)], channels=2,
                                        reverse=True),
        'shift 11': case(11, 8, MONO, [at(11, n, 16383) for n in range(8)]),
        'shift 31': case(31, 2, MONO, [at(31, 0), at(31, 0, 8192), at(31, 1), at(31, 1, 16383)]),
        'a block': case(s, 8, MONO, [at(s, (i * 3) % 8, (i * 517) % 16384) for i in range(64)], level=0x0b504f33),
    }
    for level in LEVELS:
        named['mono level %#x' % level] = case(s, 9, EDGE, [at(s, n) for n in range(9)], level=level)
        named['stereo level %#x' % level] = case(s, 6, STEREO, [at(s, n, 4321) for n in range(6)], channels=2,
                                                 level=level)
    ramp = list(range(0, 16385, 1024)) + [1, 16383]
    named['crossfade, no frames'] = case(s, 8, MONO, [], to=[], weights=[])
    named['crossfade, one frame'] = case(s, 8, MONO, [at(s, 1)], to=[at(s, 3)], weights=[8192])
    named['crossfade ramp, mono'] = case(s, 8, MONO, [at(s, i % 8, 300 * i) for i in range(len(ramp))],
                                         to=[at(s, (i + 3) % 8, 7000) for i in range(len(ramp))], weights=ramp)
    named['crossfade ramp, stereo reverse'] = case(
        s, 6, STEREO, [at(s, i % 6, 50 * i) for i in range(len(ramp))],
        to=[at(s, (5 - i) % 6, 16383) for i in range(len(ramp))], weights=ramp, channels=2, reverse=True)
    named['crossfade ramp, unaligned stereo'] = case(
        s, 6, [7] + STEREO, [at(s, i % 6, 999) for i in range(len(ramp))],
        to=[at(s, (i + 2) % 6, 15000) for i in range(len(ramp))], weights=ramp, channels=2, pad=1)
    for level in (0, 0x05555555, 0x10000000, 0x2000ffff, MASK):
        named['crossfade level %#x' % level] = case(s, 9, EDGE, [at(s, n) for n in range(9)],
                                                    to=[at(s, 8 - n, 8192) for n in range(9)],
                                                    weights=[0, 1, 4096, 8192, 12288, 16383, 16384, 16384, 0],
                                                    level=level)
    return named


def random_cases(count=400, seed=1119):
    """Cases over the whole contract: any shift, both layouts and directions, any level and weight."""
    rng = random.Random(seed)
    values = EDGE + list(range(-40, 41, 7))
    result = []
    for _ in range(count):
        shift = rng.randint(11, 31)
        channels = rng.choice((1, 2))
        frames = rng.randint(1, min(9, 2 ** (32 - shift)))
        pad = rng.choice((0, 1)) if channels == 2 else rng.choice((0, 3))
        pcm = [rng.choice(values) if rng.random() < 0.5 else rng.randint(-32768, 32767)
               for _ in range(pad + frames * channels)]
        n = rng.randint(0, 9)
        positions = [rng.randrange(frames << shift) for _ in range(n)]
        level = rng.choice(LEVELS) if rng.random() < 0.5 else rng.getrandbits(32)
        fade = rng.random() < 0.4
        result.append(case(shift, frames, pcm, positions, channels, rng.random() < 0.5, level, pad,
                           [rng.randrange(frames << shift) for _ in range(n)] if fade else None,
                           [rng.choice((0, 1, 16383, 16384, rng.randint(0, 16384))) for _ in range(n)] if fade else None))
    return result


def encode(value):
    tokens = [len(value)]
    for c in value:
        tokens += [int('weights' in c), c['shift'], c['last'], c['reverse'], c['channels'], c['level'],
                   len(c['pcm']), *c['pcm'], c['origin'], len(c['positions']), *c['positions']]
        if 'weights' in c:
            tokens += [*c['to'], *c['weights']]
    return ' '.join(map(str, tokens)) + '\n'


def run_reference(host_cxx, value):
    """Every case's words from the host build of the kernel (the reference), via tests/fixtures/sample_kernel.cpp."""
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    with tempfile.TemporaryDirectory(prefix='sample-kernel-') as folder:
        binary = Path(folder) / ('kernel' + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([host_cxx, '-std=c++11', '-O2', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                        str(ROOT / 'tests/fixtures/sample_kernel.cpp'), str(ROOT / 'sdk/host/sample_kernel.cpp'),
                        '-o', str(binary)], check=True, capture_output=True, **hidden)
        ran = subprocess.run([str(binary)], input=encode(value), capture_output=True, text=True, check=True, **hidden)
    return [[int(word) for word in line.split()] for line in ran.stdout.splitlines()]


# The reference's words for each named case, two fill words after them: any change
# to the reference's arithmetic changes some of these.
GOLDEN = {
    'no frames': [1515870810, 1515870810],
    'one mono frame': [32768000, 32768000, 1515870810, 1515870810],
    'one stereo frame': [1073709056, -1073741824, 1515870810, 1515870810],
    'mono fractions': [-32768000, -32768000, -32702464, -32702464, 520388608, 520388608, 520454144, 520454144,
        520519680, 520519680, 1073610752, 1073610752, 1515870810, 1515870810],
    'stereo fractions': [1073709056, -1073741824, 1073577984, -1073643520, 98304, -163840, -32768, -32768,
        -163840, 98304, -1073643520, 1073577984, 1515870810, 1515870810],
    'unaligned stereo': [327647232, -327712768, 418349056, -418414592, -745078784, 745013248, 2228224,
        -2260992, 123371520, -123404288, 281051136, -281083904, 1515870810, 1515870810],
    'unaligned stereo reverse': [108167168, -108199936, 2359296, -2392064, -785580032, 785514496, 499089408,
        -499154944, 287277056, -287342592, 296255488, -296321024, 1515870810, 1515870810],
    'rounding': [-715827880, -715827880, -715827880, -715827880, -715827880, -715827880, -715784189,
        -715784189, -357979475, -357979475, -87381, -87381, -43690, -43690, -43690, -43690, -43690, -43690,
        -21845, -21845, -21845, -21845, -21845, -21845, 0, 0, 0, 0, 0, 0, 21845, 21845, 21845, 21845, 21845,
        21845, 65535, 65535, 357870249, 357870249, 715740498, 715740498, 715784189, 715784189, 715784189,
        715784189, 715784189, 715784189, 1515870810, 1515870810],
    'end and wrap': [222199808, 222199808, 404520960, 404520960, 404488192, 404488192, 0, 0, 1515870810,
        1515870810],
    'reverse and wrap': [404422656, 404422656, -32768, -32768, 0, 0, 404488192, 404488192, 1515870810,
        1515870810],
    'stereo reverse and wrap': [1067155456, -1067220992, 0, -32768, 404488192, -404520960, 1515870810,
        1515870810],
    'shift 11': [32735232, 32735232, -32768000, -32768000, 1073152000, 1073152000, -1072726016, -1072726016,
        -557056, -557056, 0, 0, 404291584, 404291584, 196608, 196608, 1515870810, 1515870810],
    'shift 31': [0, 0, 16384000, 16384000, 32768000, 32768000, 0, 0, 1515870810, 1515870810],
    'a block': [0, 0, 1422620822, 1422620822, 36145940, 36145940, 37536169, 37536169, -1326880419, -1326880419,
        481806856, 481806856, 249916743, 249916743, -46340, -46340, 11677919, 11677919, 655956146, 655956146,
        180544341, 180544341, 14133989, 14133989, -943548081, -943548081, 337362115, 337362115, 644927000,
        644927000, -46340, -46340, 23355838, 23355838, -110708529, -110708529, 324942741, 324942741, -9268189,
        -9268189, -560215743, -560215743, 192963715, 192963715, 1039937257, 1039937257, 0, 0, 35080099,
        35080099, -877373205, -877373205, 469341141, 469341141, -32624028, -32624028, -176883405, -176883405,
        48565315, 48565315, 1434947515, 1434947515, 0, 0, 417068, 417068, 1392962614, 1392962614, 41706854,
        41706854, 36655691, 36655691, -1312051315, -1312051315, 476199601, 476199601, 265162915, 265162915,
        -46340, -46340, 12141328, 12141328, 626297938, 626297938, 186105255, 186105255, 13253511, 13253511,
        -928718977, -928718977, 331801201, 331801201, 660219513, 660219513, -46340, -46340, 23819248, 23819248,
        -140366737, -140366737, 330503655, 330503655, -10148668, -10148668, -545386639, -545386639, 187356460,
        187356460, 1055229771, 1055229771, 0, 0, 35543508, 35543508, -907031413, -907031413, 474902055,
        474902055, -33550847, -33550847, -162054301, -162054301, 42958060, 42958060, 1450240028, 1450240028, 0,
        0, 1515870810, 1515870810],
    'mono level 0x0': [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1515870810, 1515870810],
    'stereo level 0x0': [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1515870810, 1515870810],
    'mono level 0x1': [-8, -8, -7, -7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 1515870810, 1515870810],
    'stereo level 0x1': [2, -2, 3, -3, -5, 5, 0, 0, 0, 0, 2, -2, 1515870810, 1515870810],
    'mono level 0x1000': [-32768, -32768, -32767, -32767, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 32766, 32766,
        32767, 32767, 1515870810, 1515870810],
    'stereo level 0x1000': [8641, -8643, 15483, -15485, -24100, 24098, 72, -73, 3253, -3254, 9089, -9090,
        1515870810, 1515870810],
    'mono level 0x5555555': [-715827880, -715827880, -715806034, -715806034, -43690, -43690, -21845, -21845, 0,
        0, 21845, 21845, 43690, 43690, 715784189, 715784189, 715806034, 715806034, 1515870810, 1515870810],
    'stereo level 0x5555555': [188765524, -188809215, 338231294, -338274985, -526472531, 526428840, 1572863,
        -1594709, 71062869, -71084714, 198552233, -198574079, 1515870810, 1515870810],
    'mono level 0x7ffffff': [-1073741816, -1073741816, -1073709048, -1073709048, -65535, -65535, -32767,
        -32767, 0, 0, 32767, 32767, 65535, 65535, 1073676280, 1073676280, 1073709048, 1073709048, 1515870810,
        1515870810],
    'stereo level 0x7ffffff': [283148285, -283213821, 507346940, -507412476, -789708794, 789643258, 2359295,
        -2392063, 106594303, -106627071, 297828349, -297861117, 1515870810, 1515870810],
    'mono level 0x8000000': [-1073741824, -1073741824, -1073709056, -1073709056, -65536, -65536, -32768,
        -32768, 0, 0, 32768, 32768, 65536, 65536, 1073676288, 1073676288, 1073709056, 1073709056, 1515870810,
        1515870810],
    'stereo level 0x8000000': [283148288, -283213824, 507346944, -507412480, -789708800, 789643264, 2359296,
        -2392064, 106594304, -106627072, 297828352, -297861120, 1515870810, 1515870810],
    'mono level 0x8000001': [-1073741832, -1073741832, -1073709063, -1073709063, -65536, -65536, -32768,
        -32768, 0, 0, 32768, 32768, 65536, 65536, 1073676295, 1073676295, 1073709063, 1073709063, 1515870810,
        1515870810],
    'stereo level 0x8000001': [283148290, -283213826, 507346947, -507412483, -789708805, 789643269, 2359296,
        -2392064, 106594304, -106627072, 297828354, -297861122, 1515870810, 1515870810],
    'mono level 0xfffffff': [-2147483640, -2147483640, -2147418104, -2147418104, -131071, -131071, -65535,
        -65535, 0, 0, 65535, 65535, 131071, 131071, 2147352568, 2147352568, 2147418104, 2147418104, 1515870810,
        1515870810],
    'stereo level 0xfffffff': [566296573, -566427645, 1014693884, -1014824956, -1579417594, 1579286522,
        4718591, -4784127, 213188607, -213254143, 595656701, -595722237, 1515870810, 1515870810],
    'mono level 0x10000000': [-2147483648, -2147483648, -2147418112, -2147418112, -131072, -131072, -65536,
        -65536, 0, 0, 65536, 65536, 131072, 131072, 2147352576, 2147352576, 2147418112, 2147418112, 1515870810,
        1515870810],
    'stereo level 0x10000000': [566296576, -566427648, 1014693888, -1014824960, -1579417600, 1579286528,
        4718592, -4784128, 213188608, -213254144, 595656704, -595722240, 1515870810, 1515870810],
    'mono level 0x10004001': [-2147483648, -2147483648, -2147483648, -2147483648, -131080, -131080, -65540,
        -65540, 0, 0, 65540, 65540, 131080, 131080, 2147483647, 2147483647, 2147483647, 2147483647, 1515870810,
        1515870810],
    'stereo level 0x10004001': [566331142, -566462222, 1014755823, -1014886903, -1579514005, 1579382925,
        4718880, -4784420, 213201620, -213267160, 595693062, -595758602, 1515870810, 1515870810],
    'mono level 0x10004002': [-2147483648, -2147483648, -2147483648, -2147483648, -131080, -131080, -65540,
        -65540, 0, 0, 65540, 65540, 131080, 131080, 2147483647, 2147483647, 2147483647, 2147483647, 1515870810,
        1515870810],
    'stereo level 0x10004002': [566331144, -566462224, 1014755827, -1014886907, -1579514011, 1579382931,
        4718880, -4784420, 213201621, -213267161, 595693064, -595758604, 1515870810, 1515870810],
    'mono level 0x10010000': [-2147483648, -2147483648, -2147483648, -2147483648, -131104, -131104, -65552,
        -65552, 0, 0, 65552, 65552, 131104, 131104, 2147483647, 2147483647, 2147483647, 2147483647, 1515870810,
        1515870810],
    'stereo level 0x10010000': [566434832, -566565936, 1014941616, -1015072720, -1579803200, 1579672096,
        4719744, -4785296, 213240656, -213306208, 595802128, -595867680, 1515870810, 1515870810],
    'mono level 0x2000ffff': [-2147483648, -2147483648, -2147483648, -2147483648, -262175, -262175, -131087,
        -131087, 0, 0, 131087, 131087, 262175, 262175, 2147483647, 2147483647, 2147483647, 2147483647,
        1515870810, 1515870810],
    'stereo level 0x2000ffff': [1132731405, -1132993581, 2029635500, -2029897676, -2147483648, 2147483647,
        9438335, -9569423, 426429263, -426560351, 1191458829, -1191589917, 1515870810, 1515870810],
    'mono level 0x7fffffff': [-2147483648, -2147483648, -2147483648, -2147483648, -1048575, -1048575, -524287,
        -524287, 0, 0, 524287, 524287, 1048575, 1048575, 2147483647, 2147483647, 2147483647, 2147483647,
        1515870810, 1515870810],
    'stereo level 0x7fffffff': [2147483647, -2147483648, 2147483647, -2147483648, -2147483648, 2147483647,
        37748735, -38273023, 1705508863, -1706033151, 2147483647, -2147483648, 1515870810, 1515870810],
    'mono level 0xffffffff': [-2147483648, -2147483648, -2147483648, -2147483648, -2097151, -2097151, -1048575,
        -1048575, 0, 0, 1048575, 1048575, 2097151, 2097151, 2147483647, 2147483647, 2147483647, 2147483647,
        1515870810, 1515870810],
    'stereo level 0xffffffff': [2147483647, -2147483648, 2147483647, -2147483648, -2147483648, 2147483647,
        75497471, -76546047, 2147483647, -2147483648, 2147483647, -2147483648, 1515870810, 1515870810],
    'crossfade, no frames': [1515870810, 1515870810],
    'crossfade, one frame': [553222144, 553222144, 1515870810, 1515870810],
    'crossfade ramp, mono': [0, 0, -8880128, -8880128, 6750208, 6750208, 808943616, 808943616, -688455680,
        -688455680, 4325376, 4325376, 29556736, 29556736, 390823936, 390823936, 80478208, 80478208, -336363520,
        -336363520, 63635456, 63635456, 319160320, 319160320, -35717120, -35717120, 11337728, 11337728,
        17104896, 17104896, 430768128, 430768128, 156205056, 156205056, 12288000, 12288000, -32768, -32768,
        1515870810, 1515870810],
    'crossfade ramp, stereo reverse': [404520960, -404520960, -98304, 32768, 131301376, -131366912,
        -1057783808, 1057685504, 796262400, -796327936, 4194304, -4259840, 399835136, -399900672, -32768,
        -65536, 525271040, -525336576, -1047920640, 1047822336, 392364032, -392429568, 4161536, -4227072,
        400785408, -400818176, 32768, -98304, 934019072, -934117376, -1067548672, 1067450368, 3244032,
        -3276800, 20938752, -21037056, 404455424, -404520960, 1515870810, 1515870810],
    'crossfade ramp, unaligned stereo': [65437696, -65503232, 883851264, -883949568, -835780608, 835715072,
        8847360, -8945664, 264142848, -264208384, -17727488, 17661952, 7995392, -8060928, 530350080,
        -530448384, -318898176, 318832640, 20512768, -20611072, 623575040, -623640576, -494829568, 494731264,
        -49446912, 49381376, 176881664, -176947200, 197984256, -198049792, 32178176, -32276480, 983007232,
        -983072768, 379748352, -379813888, -87719936, 87654400, 1515870810, 1515870810],
    'crossfade level 0x0': [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1515870810, 1515870810],
    'crossfade level 0x5555555': [-715827880, -715827880, -715740498, -715740498, 89434794, 89434794, 0, 0, 0,
        0, -21845, -21845, -43690, -43690, -357935785, -357935785, 715806034, 715806034, 1515870810,
        1515870810],
    'crossfade level 0x10000000': [-2147483648, -2147483648, -2147221504, -2147221504, 268304384, 268304384, 0,
        0, 0, 0, -65536, -65536, -131072, -131072, -1073807360, -1073807360, 2147418112, 2147418112,
        1515870810, 1515870810],
    'crossfade level 0x2000ffff': [-2147483648, -2147483648, -2147483648, -2147483648, 536674271, 536674271, 0,
        0, 0, 0, -131087, -131087, -262175, -262175, -2147483648, -2147483648, 2147483647, 2147483647,
        1515870810, 1515870810],
    'crossfade level 0xffffffff': [-2147483648, -2147483648, -2147483648, -2147483648, 2147483647, 2147483647,
        0, 0, 0, 0, -1048575, -1048575, -2097151, -2097151, -2147483648, -2147483648, 2147483647, 2147483647,
        1515870810, 1515870810],
}


def machine(folder, text, name='sample.cpp'):
    Path(folder).mkdir(parents=True, exist_ok=True)
    (Path(folder) / name).write_text(text, encoding='utf-8')
    return SimpleNamespace(root=Path(folder), kind='sample', definition={'resources': {}})


class Selection(unittest.TestCase):
    def test_only_a_machine_including_the_header_links_it(self):
        with tempfile.TemporaryDirectory() as folder:
            uses = machine(Path(folder) / 'a', '#include <opxy/sample_kernel.hpp>\n')
            other = machine(Path(folder) / 'b', '#include <opxy/sample.hpp>\n')
            only_tests = machine(Path(folder) / 'c', '#include <opxy/sample.hpp>\n')
            (Path(folder) / 'c' / 'tests').mkdir()
            (Path(folder) / 'c' / 'tests' / 'k.cpp').write_text('#include <opxy/sample_kernel.hpp>\n', encoding='utf-8')
            def select(machines):
                return native.includes_header(machines, native.UNIT_HEADERS['sample_kernel'])
            self.assertTrue(select([uses]))
            self.assertTrue(select([other, uses]))
            self.assertFalse(select([other]))
            self.assertFalse(select([only_tests]))
            # Its host tests still link the host build.
            self.assertEqual(native.host_units(only_tests), [ROOT / 'sdk/host/sample_kernel.cpp'])
            self.assertEqual(native.host_units(other), [])
            self.assertFalse(select([SimpleNamespace(kind='synth', definition={'resources': {}})]))

    def test_the_plan_links_it_after_divide_only_for_a_machine_including_the_header(self):
        import shutil
        from dataclasses import replace
        from machine_fixtures import FOLDER, fixture
        plain = fixture('probesample')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'probesample'
            shutil.copytree(FOLDER / 'probesample', root)
            source = root / 'sample.cpp'
            source.write_text('#include <opxy/sample_kernel.hpp>\n' + source.read_text(encoding='utf-8'), encoding='utf-8')
            uses = replace(plain, root=root)
            names = lambda machines: [name for name, m, _step in native.plan(machines) if m is None]
            self.assertNotIn('sdk/sample_kernel.o', names([plain]))
            linked = names([uses])
            self.assertEqual(linked[:2], ['sdk/divide.o', 'sdk/sample_kernel.o'])
            self.assertEqual([n for n in linked if n != 'sdk/sample_kernel.o'], names([plain]))

    def test_a_machines_host_tests_link_the_host_kernel(self):
        import shutil
        from dataclasses import replace
        from machine_fixtures import FOLDER, fixture
        from test_sample_services import HOST_CXX
        from tooling import machine_test
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        plain = fixture('probesample')
        self.assertEqual(native.host_units(plain), [])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'probesample'
            shutil.copytree(FOLDER / 'probesample', root)
            source = root / 'sample.cpp'
            source.write_text('#include <opxy/sample_kernel.hpp>\n' + source.read_text(encoding='utf-8'), encoding='utf-8')
            uses = replace(plain, root=root)
            self.assertEqual(native.host_units(uses), [ROOT / 'sdk/host/sample_kernel.cpp'])
            # A test that calls the routine links.
            (root / 'tests').mkdir()
            (root / 'tests/kernel.cpp').write_text(KERNEL_TEST, encoding='utf-8')
            scratch = Path(folder) / 'scratch'
            scratch.mkdir()
            self.assertEqual(machine_test.host_tests(uses, HOST_CXX, scratch), 1)

    def test_a_machine_calling_the_kernel_only_from_a_test_links_its_host_tests(self):
        import shutil
        from dataclasses import replace
        from machine_fixtures import FOLDER, fixture
        from test_sample_services import HOST_CXX
        from tooling import machine_test
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'probesample'
            shutil.copytree(FOLDER / 'probesample', root)
            (root / 'tests').mkdir()
            (root / 'tests/kernel.cpp').write_text(KERNEL_TEST, encoding='utf-8')
            m = replace(fixture('probesample'), root=root)
            # The device build is unchanged: the machine itself does not include the header.
            self.assertFalse(native.includes_header([m], 'sample_kernel'))
            self.assertNotIn('sdk/sample_kernel.o', [name for name, _m, _step in native.plan([m])])
            self.assertEqual(native.host_units(m), [ROOT / 'sdk/host/sample_kernel.cpp'])
            scratch = Path(folder) / 'scratch'
            scratch.mkdir()
            self.assertEqual(machine_test.host_tests(m, HOST_CXX, scratch), 1)

    def test_unit_sources(self):
        self.assertIn('sample_kernel', native.SDK_UNITS)
        self.assertEqual(native.KERNEL_ASM, 'sdk/platform/blackfin/sample_kernel.s')
        # A machine's host build compiles the reference through this file.
        host = (ROOT / 'sdk/host/sample_kernel.cpp').read_text(encoding='utf-8')
        self.assertIn('extern "C" void opxy_sample_interpolate(', host)
        self.assertIn('extern "C" void opxy_sample_crossfade(', host)


class Reference(unittest.TestCase):
    """The reference (sample_interpolate_reference, sample_crossfade_reference) as a machine's host build runs it."""

    @classmethod
    def setUpClass(cls):
        from test_sample_services import HOST_CXX
        if not HOST_CXX:
            raise unittest.SkipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        cls.host_cxx = HOST_CXX

    def test_the_golden_vectors(self):
        named = cases()
        self.assertEqual(sorted(GOLDEN), sorted(named))
        words = run_reference(self.host_cxx, list(named.values()))
        for name, got in zip(named, words):
            with self.subTest(name):
                self.assertEqual(got, GOLDEN[name])

    def test_the_golden_vectors_follow_the_definition(self):
        for name, value in cases().items():
            with self.subTest(name):
                self.assertEqual(model(value), GOLDEN[name])

    def test_the_cases_reach_every_edge(self):
        named = cases()
        self.assertEqual(GOLDEN['no frames'], [FILL, FILL])
        self.assertEqual(GOLDEN['crossfade, no frames'], [FILL, FILL])
        everything = [word for words in GOLDEN.values() for word in words]
        self.assertIn(2 ** 31 - 1, everything)
        self.assertIn(-2 ** 31, everything)
        # Saturated only through the carry into bit 31, and just short of it.
        carry = GOLDEN['mono level %#x' % ((4096 << 16) | 16386)]
        self.assertIn(2 ** 31 - 1, carry)
        self.assertIn(2 ** 31 - 1, GOLDEN['mono level %#x' % ((4096 << 16) | 16385)])
        self.assertEqual(level_word(32766, (4096 << 16) | 16385), 2 ** 31 - 1)
        self.assertEqual((32766 * ((4096 << 16) | 16385)) >> 12, 2 ** 31 - 1)
        # The high product just under 2^28 with a low carry that wraps 32 bits.
        self.assertEqual(level_word(32767, 0x2000ffff), 2 ** 31 - 1)
        self.assertLess(32767 * (0x2000ffff >> 16), 1 << 28)
        # Wraps: a frame at last reads the region's first frame as its next.
        wrap = named['end and wrap']
        self.assertTrue(any(p >= wrap['last'] for p in wrap['positions']))
        self.assertTrue(any(p >= c['last'] for c in named.values() if c['reverse'] for p in c['positions']))
        # Both layouts off a word boundary, both directions, every shift bound.
        self.assertTrue(any(c['channels'] == 2 and c['origin'] % 2 for c in named.values()))
        self.assertEqual({c['shift'] for c in named.values()}, {11, 16, 31})
        self.assertTrue({0, 1, 16383, 16384} <= {w for c in named.values() for w in c.get('weights', [])})

    def test_random_cases_follow_the_definition(self):
        value = random_cases()
        for i, (c, got) in enumerate(zip(value, run_reference(self.host_cxx, value))):
            with self.subTest(i):
                self.assertEqual(got, model(c), c)


# sample_kernel.s's code (comments, blank lines and trailing spaces aside). This is
# the code whose output was compared word for word with the reference on a BF707
# simulator; no test here runs Blackfin code.
KERNEL_CODE_SHA256 = '72107dcc1a63286abafd05da57561c90b232f791cc44bc65224ae3fed9a58943'
REPROVE = '''sample_kernel.s's code changed.

Nothing in this repository runs Blackfin code: its equivalence with
sample_interpolate_reference and sample_crossfade_reference was established on
a BF707 simulator outside the repository, and this digest is what protects it.
Before updating KERNEL_CODE_SHA256, re-prove it and say how in the change:
  1. build an image with a machine that calls sample_interpolate and
     sample_crossfade on the device over at least every case of cases() and
     random_cases() in tests/test_sample_kernel.py (counts 0, 1 and above 1,
     mono and stereo, a stereo origin off a 4-byte boundary, both directions,
     wraps at last, every level in LEVELS, crossfade weights 0 to 16384),
     with two fill words after each output;
  2. run it on a Blackfin simulator or a device;
  3. compare every word, fill words included, with run_reference()'s (the
     host build of the reference); all must match;
  4. check that R4-R7, P3-P5, FP and SP and the L registers are unchanged.
A change to comments only needs none of this: KERNEL_CODE_SHA256 ignores them.'''


def kernel_code(text):
    code = re.sub(r'/\*.*?\*/', '', text.replace('\r\n', '\n'), flags=re.S)
    return '\n'.join(line.rstrip() for line in code.split('\n') if line.strip()) + '\n'


class Pin(unittest.TestCase):
    def test_the_device_code_is_the_code_checked_against_the_reference(self):
        digest = hashlib.sha256(kernel_code(KERNEL.read_text(encoding='utf-8')).encode()).hexdigest()
        self.assertEqual(digest, KERNEL_CODE_SHA256, REPROVE)

    def test_the_pin_ignores_comments_only(self):
        code = kernel_code(KERNEL.read_text(encoding='utf-8'))
        self.assertEqual(kernel_code('/* a\n * note */\n' + code.replace('\n', '  /* x */\n', 1)), code)
        self.assertIn('R0 = W[P0 + 2] (X);', code)
        self.assertNotEqual(kernel_code(code.replace('R0 = W[P0 + 2] (X);', 'R0 = W[P0 + 2] (Z);', 1)), code)

    def test_the_assembled_routine(self):
        from test_divide import blackfin_compiler
        compiler = blackfin_compiler()
        if not compiler:
            self.skipTest('No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
        tools = {name: compiler.with_name(compiler.name.replace('g++', name)) for name in ('as', 'objdump')}
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='sample-kernel-') as folder:
            obj = Path(folder) / 'sample_kernel.o'
            subprocess.run([str(tools['as']), '-mcpu=bf707', '-I', str(ROOT), '-o', str(obj), str(KERNEL)],
                           check=True, capture_output=True, **hidden)
            headers = subprocess.run([str(tools['objdump']), '-h', str(obj)], check=True, capture_output=True,
                                     text=True, **hidden).stdout
            symbols = subprocess.run([str(tools['objdump']), '-t', str(obj)], check=True, capture_output=True,
                                     text=True, **hidden).stdout
        sizes = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'^\s*\d+\s+(\.\w+)\s+([0-9a-f]{8})', headers, re.M)}
        # docs/api.md's 2,872 bytes; nothing outside .text.
        self.assertEqual(sizes.get('.text'), 2872, headers)
        self.assertFalse(sizes.get('.data') or sizes.get('.bss'), headers)
        self.assertRegex(symbols, r'\bg\s+F?\s*\.text\s+[0-9a-f]+\s+_opxy_sample_interpolate\b')
        self.assertRegex(symbols, r'\bg\s+F?\s*\.text\s+[0-9a-f]+\s+_opxy_sample_crossfade\b')


class Routine(unittest.TestCase):
    def setUp(self):
        self.text = KERNEL.read_text(encoding='utf-8')
        self.code = re.sub(r'/\*.*?\*/', '', self.text, flags=re.S)

    def test_callee_saved_registers_are_restored(self):
        self.assertIn('[--SP] = (R7:4, P5:3);', self.code)
        self.assertIn('(R7:4, P5:3) = [SP++];', self.code)
        # Nothing else is pushed, so the fourth argument is at SP + 12 + 28.
        self.assertIn('P3 = [SP + 40];', self.code)
        # Each routine pushes once, and pops what it pushed.
        self.assertEqual(self.code.count('[--SP]'), self.code.count('.global'))
        self.assertEqual(self.code.count('(R7:4, P5:3) = [SP++];'), self.code.count('.global'))

    def test_no_register_outside_the_abi_is_written(self):
        written = set(re.findall(r'^\s*(?:IF !?CC )?([A-Z][A-Z0-9]*)(?:\.[LH])?\s*\+?=(?!=)', self.code, re.M))
        allowed = {'R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7', 'P0', 'P1', 'P2', 'P3', 'P4', 'P5',
                   'I0', 'I1', 'I2', 'I3', 'B0', 'M0', 'M1', 'A0', 'A1', 'CC'}
        self.assertTrue(written <= allowed, written - allowed)
        for register in ('L0', 'L1', 'L2', 'L3', 'ASTAT', 'SYSCFG', 'RETS', 'FP', 'USP'):
            self.assertNotRegex(self.code, r'\b%s\s*=' % register)

    def test_only_unprivileged_code(self):
        self.assertNotRegex(self.code, r'\b(CLI|STI|RAISE|EXCPT|IDLE|SSYNC|CSYNC|FLUSH|CALL)\b')
        self.assertIn('.text', self.code)
        self.assertNotRegex(self.code, r'\.(data|bss)\b')

    def test_a_stereo_origin_off_a_word_boundary_takes_16_bit_loads(self):
        # A 32-bit load from a 2-byte-aligned address faults on the BF707; the
        # origin's bit 1 picks the split loops, which load halves and pack them.
        self.assertRegex(self.code, r'R0 = P5;\s*CC = BITTST \(R0, 1\);')
        self.assertIn('FRAMES 1, 0, 0, 1', self.code)
        self.assertIn('FRAMES 1, 1, 0, 1', self.code)
        self.assertIn('R0 = W[P0 + 2] (X);', self.code)


if __name__ == '__main__':
    unittest.main()
