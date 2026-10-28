"""The Python SDK and the browser builder apply the profile's rules identically.

Both read profiles/opxy-1.2.1.json; this public check catches drift in the code
that interprets it: identity rules and hook/startup redirect encodings.
"""
import json
from pathlib import Path
import subprocess
import unittest

from tooling import startup_label
from tooling.firmware import hook_instruction, hooks
from tooling.machines import check_identity
from tooling.pipeline import ROOT, config

BROWSER = """
import {validIdentity, redirect} from %s;
let input = '';
for await (const chunk of process.stdin) input += chunk;
const {rules, identities, redirects} = JSON.parse(input);
const hex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
process.stdout.write(JSON.stringify({
  identities: identities.map(([kind, identity, screen = false]) => validIdentity(rules, kind, identity, screen)),
  redirects: redirects.map(([edit, target]) => hex(redirect(edit, target))),
}));
"""


def browser(request):
    script = BROWSER % json.dumps((ROOT / 'builder/core.mjs').as_uri())
    result = subprocess.run(['node', '--input-type=module', '-e', script], input=json.dumps(request),
                            capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
    return json.loads(result.stdout)


def python_accepts(rules, kind, identity, screen=False):
    try:
        check_identity(rules, kind, identity, screen)
        return True
    except ValueError:
        return False


class ProfileParity(unittest.TestCase):
    def setUp(self):
        self.profile = config()

    def test_identity_rules_agree(self):
        rules = self.profile['identities']
        span = range(-1, max(rules['engines'], rules['pages']) + 2)
        cases = [(kind, {'engine': n, 'page': page}) for kind in ('synth', 'sample') for n in span for page in span]
        cases += [('effect', {'effect': n, 'page': page}) for n in range(-1, 24) for page in (0, 2, 7, 20, 21, 22, 82)]
        cases += [('synth', {'engine': 34}), ('synth', {'engine': 34, 'page': 82, 'effect': 1}),
                  ('synth', {'engine': True, 'page': 82}), ('effect', {'engine': 11, 'page': 21})]
        cases += [('sample', {'engine': n, 'page': page}, True) for n in span for page in span]
        expected = [python_accepts(rules, *case) for case in cases]
        self.assertEqual(browser(dict(rules=rules, identities=cases, redirects=[]))['identities'], expected)
        self.assertTrue(any(expected) and not all(expected))

    def test_redirect_encodings_agree(self):
        low = self.profile['module_base']
        targets = (low, low + 0x1234, low + self.profile['reserved_bytes'] - self.profile['table_bytes'] - 2)
        edits = [p for p in self.profile['patches'] if p['kind'] in ('hook', 'startup')]
        self.assertEqual(len(hooks(self.profile)), len(edits) - 1)
        requests, expected = [], []
        for edit in edits:
            for target in targets:
                requests.append((edit, target))
                expected.append((startup_label.jump(target) if edit['kind'] == 'startup' else
                                 hook_instruction(edit, target)).hex())
        self.assertEqual(browser(dict(rules=self.profile['identities'], identities=[], redirects=requests))['redirects'],
                         expected)


    def test_released_open_xy_os_remains_recoverable(self):
        # The builder rebuilds stock OS from an uploaded open-xy OS by undoing this
        # profile's edits (builder/core.mjs recoverFirmware). Every edit a profile
        # released for this stock OS made must stay among them with the same stock
        # bytes, or devices running that release could no longer be upgraded from
        # their own os.ldr. Add each release's edits to the fixture when it ships.
        profile = config()
        current = {p['file_offset']: p['before'] for p in profile['patches']}
        released = json.loads((ROOT / 'tests/fixtures/released_profiles.json').read_text(encoding='utf-8'))
        self.assertEqual(released['stock_sha256'], profile['stock_sha256'])
        for release, edits in released['releases'].items():
            for offset, before in edits:
                with self.subTest(release=release, offset=offset):
                    self.assertEqual(current.get(offset), before)


if __name__ == '__main__':
    unittest.main()
