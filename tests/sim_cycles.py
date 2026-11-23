"""The block_trims and pitch_constants cycle figures docs/cpu-capabilities.md and docs/abi-1.2.1.md quote, printed
from the same BF707 Functional-Sim runs tests/test_track_hooks.py, tests/test_floor_hooks.py and
tests/test_pitch_constants.py check (no cache, no DDR model;
not a device measurement). Needs what those tests need: CCES's simulator (OPXY_TEST_CCES), a stock OS 1.2.1 image
(OPXY_TEST_STOCK_OS) and the pinned Blackfin tools.
    python tests/sim_cycles.py
"""
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent), str(HERE)]

import test_floor_hooks  # noqa: E402
import test_pitch_constants  # noqa: E402
import test_track_hooks  # noqa: E402


def main():
    stock_os = Path(os.environ['OPXY_TEST_STOCK_OS'])
    track = test_track_hooks.run_simulator(stock_os)
    stock, hook = track['named'][0]
    print(f'track mixes, a sounding track with a silent buffer: stock {stock}, sdk_track_mix {hook} cycles')
    tap = track['tap_mean']
    print(f'tap timer end then mixes (mean over random cases): stock {tap[0]}, without profiling {tap[1]} cycles')
    floor = test_floor_hooks.run_simulator(stock_os)
    print(f'LFO pass check (mean of a call): stock {floor["lfo"][2]}, sdk_lfo_pass {floor["lfo"][3]} cycles')
    whole = floor['whole']
    print(f'whole audio loop, mean of the last 16 of 160 blocks (tape idle): stock {whole[3]}, with the tape hooks {whole[4]} cycles '
          f'({whole[3] - whole[4]} saved)')
    pitch = test_pitch_constants.run_simulator(stock_os)
    for name in ('booted', 'random'):
        calls, mismatches, _distinct, stock, edited = pitch[name]
        print(f'note pitch 8d5c0a70 (mean of {calls} calls, {name} tables, {mismatches} mismatches): stock {stock}, '
              f'with pitch_constants {edited} cycles ({stock - edited} saved)')


if __name__ == '__main__':
    main()
