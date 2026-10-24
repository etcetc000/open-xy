"""Extension ordering, exclusive control claims and build provenance."""
import copy
import unittest
from unittest import mock
from tooling import extensions, native


class Extensions(unittest.TestCase):
    def test_collection_rejects_conflicting_and_unsafe_declarations(self):
        valid = {'schema': 1, 'extensions': extensions.collection()}
        for mutate in (
            lambda data: data['extensions'].append(copy.deepcopy(data['extensions'][0])),
            lambda data: data['extensions'][0].update(id='../capture'),
            lambda data: data['extensions'][0].update(record_double_tap=False),
            lambda data: data['extensions'][0].update(edit_notes=1),
            lambda data: data['extensions'][0].update(extra=True),
        ):
            data = copy.deepcopy(valid)
            mutate(data)
            with mock.patch.object(extensions, 'document', return_value=data), self.assertRaises(ValueError):
                extensions.collection()
        data = copy.deepcopy(valid)
        data['extensions'].append(dict(id='other', record_double_tap=True, edit_notes=False))
        with mock.patch.object(extensions, 'document', return_value=data), self.assertRaisesRegex(ValueError, 'multiple owners'):
            extensions.collection()

    def test_extension_source_and_selection_are_authenticated_build_inputs(self):
        sources = native.provenance([])
        self.assertIn('profiles/extensions.json', sources)
        self.assertIn('extensions/capture/extension.cpp', sources)
        self.assertIn('extensions/capture/history.hpp', sources)
        self.assertIn(('sdk/extension_capture.o', None, 'extension:capture'), native.plan([]))
        self.assertNotIn('sdk/extension_capture.o', native.object_services([]))

    def test_empty_selection_has_no_extension_objects(self):
        with mock.patch.object(extensions, 'document', return_value={'schema': 1, 'extensions': []}):
            self.assertFalse(any(step.startswith('extension:') for _, _, step in native.plan([])))
            self.assertFalse(any(name.startswith('extensions/') for name in native.provenance([])))
