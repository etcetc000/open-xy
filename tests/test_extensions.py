"""Extension ordering, the claim and action registry, event sites and build provenance."""
import copy
import re
import unittest
from unittest import mock
from tooling import extensions, native
from tooling.pipeline import ROOT, config, document


class Extensions(unittest.TestCase):
    def valid(self):
        return document(ROOT / extensions.PROFILE)

    def test_collection_rejects_conflicting_and_unsafe_declarations(self):
        for mutate in (
            lambda data: data['extensions'].append(copy.deepcopy(data['extensions'][0])),
            lambda data: data['extensions'][0].update(id='../capture'),
            lambda data: data['extensions'][0].update(id='runtime'),  # sdk/extension_runtime.o
            lambda data: data['extensions'][0].update(id='delete'),  # a C++ keyword
            lambda data: data['extensions'][0].update(claims=[]),  # edit_notes needs its claim
            lambda data: data['extensions'][0].update(claims=['shift_hold']),  # not in the registry
            lambda data: data['extensions'][0].update(actions=['edit_notes', 'edit_notes']),
            lambda data: data['extensions'][0].update(extra=True),
            lambda data: data['extensions'][0].update(origin='catalog'),
            lambda data: data['extensions'][0].pop('origin'),
            lambda data: data['claims'].update(bad={'label': ''}),
            lambda data: data['actions'].update(other={'requires': 'missing'}),
            lambda data: data.update(schema=1),
        ):
            data = self.valid()
            mutate(data)
            with mock.patch.object(extensions, 'document', return_value=data), self.assertRaises(ValueError):
                extensions.collection()
        data = self.valid()
        data['extensions'].append(dict(id='other', origin='community', claims=['record_double_tap'], actions=[]))
        with mock.patch.object(extensions, 'document', return_value=data), self.assertRaisesRegex(ValueError, 'multiple owners'):
            extensions.collection()

    def test_actions_match_the_runtime_bits_and_claims_are_data(self):
        # An action is SDK code: one registry entry and one runtime constant, in
        # registry order. A claim is data: a registry entry naming its key.
        header = (ROOT / extensions.RUNTIME).read_text(encoding='utf-8')
        claims, actions = extensions.registry()
        declared = re.findall(r'const u32 action_(\w+) = 1u << (\d+);', header)
        self.assertEqual(declared, [(name, str(i)) for i, name in enumerate(actions)])
        self.assertNotRegex(header, r'const u32 claim_')
        self.assertTrue(all(type(c['key']) is int for c in claims.values()))

    def test_every_extension_hook_names_its_events_and_every_event_has_a_site(self):
        profile = config()
        extensions.check_profile(profile)
        self.assertEqual(extensions.events(), ['context', 'recordable_note_on', 'selected_note_off',
                                               'recording_tick', 'input_activity', 'queued_control'])
        hook = lambda p: next(x for x in p['patches'] if x.get('events'))
        for mutate in (
            lambda p: hook(p).pop('events'),
            lambda p: hook(p).update(events=['note_on']),
            lambda p: hook(p).update(events=[]),
            lambda p: next(x for x in p['patches'] if not x.get('service')).update(events=['context']),
            lambda p: [x.update(events=['context']) for x in p['patches'] if x.get('events')],
        ):
            changed = copy.deepcopy(profile)
            mutate(changed)
            with self.assertRaises(ValueError):
                extensions.check_profile(changed)

    def test_site_text_is_one_short_line_and_an_optional_note(self):
        for entry in extensions.metadata():
            self.assertLessEqual(len(entry['description']), extensions.TEXT_LIMITS['description'])
        text = {'name': 'Capture', 'description': 'Play, then double-tap Record.'}
        for bad in (dict(text, description='x' * 141), dict(text, note=''), dict(text, gesture='Record'),
                    {'name': 'Capture'}):
            with mock.patch.object(extensions, 'document', side_effect=lambda path: (
                    self.valid() if path.name == 'extensions.json' else bad)), self.assertRaises(ValueError):
                extensions.metadata()

    def test_extension_source_and_selection_are_authenticated_build_inputs(self):
        sources = native.provenance([])
        self.assertIn('profiles/extensions.json', sources)
        self.assertIn('extensions/capture/extension.cpp', sources)
        self.assertIn('extensions/capture/history.hpp', sources)
        self.assertIn(('sdk/extension_capture.o', None, 'extension:capture'), native.plan([]))
        self.assertNotIn('sdk/extension_capture.o', native.object_services([]))

    def test_empty_selection_has_no_extension_objects(self):
        data = dict(self.valid(), extensions=[])
        with mock.patch.object(extensions, 'document', return_value=data):
            self.assertFalse(any(step.startswith('extension:') for _, _, step in native.plan([])))
            self.assertFalse(any(name.startswith('extensions/') for name in native.provenance([])))
