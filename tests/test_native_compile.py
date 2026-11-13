"""The native GUI size policy must never change a machine's DSP compiler mode."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from dataclasses import replace

from tooling import native
from tooling import adapters
from machine_fixtures import fixture


class CompilerModes(unittest.TestCase):
    def test_only_drum_screens_request_pad_tracking(self):
        synth = fixture('probesynth')
        for machine, enabled in ((synth, 0), (replace(synth, kind='drum'), 1)):
            with tempfile.TemporaryDirectory() as folder:
                source = adapters.generate_screen(folder, machine).read_text()
            self.assertIn(f'#define OPXY_SCREEN_DRUM {enabled}\n', source)

    def test_gui_and_audio_commands_differ_only_in_optimization(self):
        commands = []
        with tempfile.TemporaryDirectory() as folder:
            for gui in (False, True):
                with mock.patch.object(native.subprocess, 'run') as compiler, mock.patch.object(native, 'execute'):
                    native.compile_cxx('compiler', 'assembler', 'sdk/platform/blackfin/memory.cpp',
                                       Path(folder) / 'unit.o', native.includes(), gui=gui)
                    commands.append(compiler.call_args.args[0])
        for command, expected in zip(commands, ('-O2', '-Os')):
            self.assertEqual([arg for arg in command if arg.startswith('-O')], [expected])
        self.assertEqual([arg for arg in commands[0] if not arg.startswith('-O')],
                         [arg for arg in commands[1] if not arg.startswith('-O')])

    def test_default_compilation_keeps_speed_optimization(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(native.subprocess, 'run') as compiler, \
                mock.patch.object(native, 'execute'):
            native.compile_cxx('compiler', 'assembler', 'sdk/platform/blackfin/memory.cpp', Path(folder) / 'unit.o')
        self.assertIn('-O2', compiler.call_args.args[0])
        self.assertNotIn('-Os', compiler.call_args.args[0])
